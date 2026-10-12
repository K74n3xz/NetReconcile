#include "LocalPlayerObservationSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void ULocalPlayerObservationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::HandleWorldTickStart);
	RefreshTarget();
}

void ULocalPlayerObservationSubsystem::Unsubscribe()
{
	if (CaptureService.IsValid()) { CaptureService->Unsubscribe(Subscription); }
	Subscription = {};
	CaptureService.Reset();
	SubscribedPawn.Reset();
}

void ULocalPlayerObservationSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldTickStart.Remove(WorldTickStartHandle);
	WorldTickStartHandle.Reset();
	Unsubscribe();
	SelectedPawn.Reset();
	Snapshot = {};
	OnPawnObservationSnapshotUpdated.Clear();
	Super::Deinitialize();
}

bool ULocalPlayerObservationSubsystem::SetObservedPawn(APawn* Pawn)
{
	const APlayerController* Controller = GetLocalPlayer()->PlayerController;
	if (Pawn && (!IsValid(Pawn) || !IsValid(Controller) || Controller->GetLocalPlayer() != GetLocalPlayer() || Pawn->GetWorld() != Controller->GetWorld())) { return false; }
	SelectedPawn = Pawn;
	RefreshTarget();
	return true;
}

bool ULocalPlayerObservationSubsystem::IsFollowingControlledPawn() const
{
	const APlayerController* Controller = GetLocalPlayer()->PlayerController;
	return !SelectedPawn.IsValid() || !IsValid(Controller) || Controller->GetLocalPlayer() != GetLocalPlayer() || SelectedPawn->GetWorld() != Controller->GetWorld();
}

APawn* ULocalPlayerObservationSubsystem::GetObservedPawn() const
{
	const APlayerController* Controller = GetLocalPlayer()->PlayerController;
	if (!IsValid(Controller) || Controller->GetLocalPlayer() != GetLocalPlayer()) { return nullptr; }
	if (IsFollowingControlledPawn()) { return Controller->GetPawn(); }
	return SelectedPawn.Get();
}

FPawnObservationSnapshot ULocalPlayerObservationSubsystem::GetPawnObservationSnapshot() const
{
	APawn* Pawn = GetObservedPawn();
	if (!IsValid(Pawn) || Pawn != SubscribedPawn.Get() || !CaptureService.IsValid()) { return {}; }
	return CaptureService->GetLatest(*Pawn);
}

EObservationAvailability ULocalPlayerObservationSubsystem::GetObservationTargetStatus() const
{
	return !IsValid(GetObservedPawn()) ? EObservationAvailability::MissingSource
		: (GetPawnObservationSnapshot().IsValid() ? EObservationAvailability::Available : EObservationAvailability::AwaitingSample);
}

void ULocalPlayerObservationSubsystem::HandleWorldTickStart(UWorld* World, ELevelTick, float)
{
	const APlayerController* Controller = GetLocalPlayer()->PlayerController;
	const UWorld* PlayerWorld = IsValid(Controller) && Controller->GetLocalPlayer() == GetLocalPlayer()
		? Controller->GetWorld() : GetLocalPlayer()->GetWorld();
	if (World == PlayerWorld || (CaptureService.IsValid() && World == CaptureService->GetWorld())) { RefreshTarget(); }
}

void ULocalPlayerObservationSubsystem::RefreshTarget()
{
	if (!WorldTickStartHandle.IsValid()) { return; }
	APawn* Pawn = GetObservedPawn();
	UPawnObservationWorldSubsystem* Service = IsValid(Pawn) && Pawn->GetWorld()->IsGameWorld() ? Pawn->GetWorld()->GetSubsystem<UPawnObservationWorldSubsystem>() : nullptr;
	if (Pawn != SubscribedPawn.Get() || Service != CaptureService.Get() || (Service && !Subscription.IsValid()))
	{
		Unsubscribe();
		if (Service)
		{
			CaptureService = Service;
			SubscribedPawn = Pawn;
			Subscription = Service->Subscribe(*Pawn, FOnPawnObservationCaptured::FDelegate::CreateUObject(this, &ThisClass::HandleCaptured));
		}
	}
	const FPawnObservationSnapshot Current = GetPawnObservationSnapshot();
	if (!(Current == Snapshot)) { HandleCaptured(Current); }
}

void ULocalPlayerObservationSubsystem::HandleCaptured(const FPawnObservationSnapshot& Captured)
{
	if (Captured.IsValid() && Captured.Get()->GetCommon().Pawn != GetObservedPawn()) { return; }
	Snapshot = Captured;
	// The handle survives reentrant selection/possession changes made by a Blueprint callback.
	const FPawnObservationSnapshot Stable = Captured;
	OnPawnObservationSnapshotUpdated.Broadcast(Stable);
}
