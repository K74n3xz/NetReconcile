#include "PawnObservationWorldSubsystem.h"

#include "ObservationCharacterMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

namespace
{
	UObservationCharacterMovementComponent* ObservedCharacterMovementComponent(const APawn& Pawn)
	{
		const ACharacter* Character = Cast<ACharacter>(&Pawn);
		return Character && IsValid(Character->GetCharacterMovement()) ? Cast<UObservationCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	}
}

bool UPawnObservationWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game || Type == EWorldType::PIE; }

void UPawnObservationWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::CaptureWorld);
}

void UPawnObservationWorldSubsystem::Clear()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
	PostActorTickHandle.Reset();
	// Clear first so a callback cannot acquire a frame from the outgoing world.
	TArray<TSharedPtr<FOnPawnObservationCaptured>> Listeners;
	for (const auto& Pair : Entries) { Listeners.Add(Pair.Value.Listeners); }
	Entries.Empty();
	for (const auto& Event : Listeners) { Event->Broadcast(FPawnObservationSnapshot{}); }
}

void UPawnObservationWorldSubsystem::OnWorldEndPlay(UWorld& World) { Clear(); Super::OnWorldEndPlay(World); }
void UPawnObservationWorldSubsystem::Deinitialize() { Clear(); Super::Deinitialize(); }

FPawnObservationSubscription UPawnObservationWorldSubsystem::Subscribe(APawn& Pawn, FOnPawnObservationCaptured::FDelegate Listener)
{
	if (!IsValid(&Pawn) || Pawn.GetWorld() != GetWorld() || !PostActorTickHandle.IsValid()) { return {}; }
	FPawnObservationSubscription Result;
	Result.PawnKey = FObjectKey(&Pawn);
	FEntry& Entry = Entries.FindOrAdd(Result.PawnKey);
	Entry.Pawn = &Pawn;
	Result.Delegate = Entry.Listeners->Add(MoveTemp(Listener));
	return Result;
}

void UPawnObservationWorldSubsystem::Unsubscribe(const FPawnObservationSubscription& Subscription)
{
	if (FEntry* Entry = Entries.Find(Subscription.PawnKey))
	{
		Entry->Listeners->Remove(Subscription.Delegate);
		if (!Entry->Listeners->IsBound()) { Entries.Remove(Subscription.PawnKey); }
	}
}

bool UPawnObservationWorldSubsystem::IsCurrent(const FEntry& Entry, const APawn& Pawn)
{
	const UObservationCharacterMovementComponent* Component = ObservedCharacterMovementComponent(Pawn);
	// Type contract (caller guaranteed): Component is a live UObject resolved from this Pawn on the game thread.
	// The const_cast only constructs a weak serial identity for comparison; no component mutation is performed.
	return Entry.Snapshot.IsValid() && Entry.Pawn == &Pawn && Entry.Role == Pawn.GetLocalRole()
		&& Entry.Controller == Pawn.GetController() && Entry.MovementComponent == TWeakObjectPtr<UObservationCharacterMovementComponent>(const_cast<UObservationCharacterMovementComponent*>(Component))
		&& (!Component || Entry.MovementObservationContextEpoch == Component->GetObservationContextEpoch());
}

FPawnObservationSnapshot UPawnObservationWorldSubsystem::GetLatest(const APawn& Pawn) const
{
	const FEntry* Entry = Entries.Find(FObjectKey(&Pawn));
	return IsValid(&Pawn) && Pawn.GetWorld() == GetWorld() && Entry && IsCurrent(*Entry, Pawn) ? Entry->Snapshot : FPawnObservationSnapshot{};
}

void UPawnObservationWorldSubsystem::CaptureWorld(UWorld* World, ELevelTick Type, float)
{
	if (World != GetWorld() || Type != LEVELTICK_All || World->IsPaused()) { return; }
	struct FPublication { TSharedPtr<FOnPawnObservationCaptured> Event; FPawnObservationSnapshot Snapshot; };
	TArray<FPublication> Publications;
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		FEntry& Entry = It.Value();
		APawn* Pawn = Entry.Pawn.Get();
		if (!Pawn || Pawn->GetWorld() != World)
		{
			Publications.Add({Entry.Listeners, {}});
			It.RemoveCurrent();
			continue;
		}
		UObservationCharacterMovementComponent* Component = ObservedCharacterMovementComponent(*Pawn);
		if (!IsCurrent(Entry, *Pawn)) { Entry.SourceEpoch = FGuid::NewGuid(); }
		Entry.Role = Pawn->GetLocalRole();
		Entry.Controller = Pawn->GetController();
		Entry.MovementComponent = Component;
		Entry.MovementObservationContextEpoch = Component ? Component->GetObservationContextEpoch() : FGuid{};
		FObservedPawnRoleData RoleData;
		switch (Pawn->GetLocalRole())
		{
		case ROLE_Authority: RoleData.Set<FAuthorityPawnObservation>({}); break;
		case ROLE_AutonomousProxy:
		{
			FAutonomousPawnObservation Data;
			if (Component) { Data = Component->CaptureAutonomousObservation(); }
			else
			{
				const ACharacter* Character = Cast<ACharacter>(Pawn);
				Data.PredictionStatus = Data.NativeResponsesStatus = Character
					? (IsValid(Character->GetCharacterMovement()) ? EObservationAvailability::Unsupported : EObservationAvailability::MissingSource)
					: EObservationAvailability::NotApplicable;
				Data.LatestAcceptedResponse.Status = Data.LastValidPair.Status = Data.LastValidCorrection.Status = Data.PredictionStatus;
				Data.LatestAcceptedResponse.Pairing = EObservedMovePairing::UnsupportedProtocol;
			}
			RoleData.Set<FAutonomousPawnObservation>(MoveTemp(Data));
			break;
		}
		case ROLE_SimulatedProxy:
		{
			FSimulatedPawnObservation Data;
			if (Component) { Data = Component->GetSimulatedObservation(); }
			else
			{
				const ACharacter* Character = Cast<ACharacter>(Pawn);
				Data.LastDecodedReference.Status = Character ? (IsValid(Character->GetCharacterMovement()) ? EObservationAvailability::Unsupported : EObservationAvailability::MissingSource) : EObservationAvailability::NotApplicable;
			}
			RoleData.Set<FSimulatedPawnObservation>(MoveTemp(Data));
			break;
		}
		default: break;
		}
		Entry.Snapshot = FPawnObservationSnapshot(FPawnObservationFrame(PawnObservation::SampleCommon(*Pawn), MoveTemp(RoleData), Entry.SourceEpoch, ++PublicationSequence));
		Publications.Add({Entry.Listeners, Entry.Snapshot});
	}
	// Callbacks may select a new Pawn or unsubscribe; map iterators and frames are no longer in use.
	for (const FPublication& Publication : Publications) { Publication.Event->Broadcast(Publication.Snapshot); }
}
