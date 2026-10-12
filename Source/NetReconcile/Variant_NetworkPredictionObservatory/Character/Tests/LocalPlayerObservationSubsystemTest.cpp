#if WITH_DEV_AUTOMATION_TESTS
#include "ObservationTestWorld.h"
#include "Variant_NetworkPredictionObservatory/Character/LocalPlayerObservationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/Character/Model/PawnObservationBlueprintLibrary.h"
#include "Components/SceneComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalPlayerObservationSubsystemTest, "NetReconcile.Observation.LocalPlayerSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalPlayerObservationSubsystemTest::RunTest(const FString&)
{
	FObservationTestWorld Fixture;
	UWorld* World = Fixture.World;
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACharacter* Character = World->SpawnActor<ACharacter>();
	APawn* GenericPawn = World->SpawnActor<APawn>();
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = Player;
	Player->PlayerController = Controller;
	Controller->SetAsLocalPlayerController();
	APlayerState* PlayerState = World->SpawnActor<APlayerState>();
	PlayerState->SetPlayerId(42); Controller->SetPlayerState(PlayerState);
	TStrongObjectPtr<ULocalPlayerObservationSubsystem> Selector(NewObject<ULocalPlayerObservationSubsystem>(Player));
	FObjectSubsystemCollection<ULocalPlayerSubsystem> Collection;
	Selector->Initialize(Collection);
	ON_SCOPE_EXIT { Selector->Deinitialize(); };
	UPawnObservationWorldSubsystem* Service = World->GetSubsystem<UPawnObservationWorldSubsystem>();
	if (!TestNotNull(TEXT("World capture service"), Service)) { return false; }
	TestEqual(TEXT("Missing Pawn is explicit"), Selector->GetObservationTargetStatus(), EObservationAvailability::MissingSource);
	Controller->Possess(Character);
	Character->SetActorLocationAndRotation(FVector(150.0, -40.0, 80.0), FRotator(12.0, 37.0, 9.0));
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Custom, 17);
	Character->GetCharacterMovement()->SetGroundMovementMode(MOVE_NavWalking);
	Character->SetIsCrouched(true);
	Character->Jump();
	Fixture.Capture();
	const FPawnObservationSnapshot First = Selector->GetPawnObservationSnapshot();
	if (!TestTrue(TEXT("Possessed Pawn is sampled"), First.IsValid())) { return false; }
	const FPawnObservationCommon& Common = First.Get()->GetCommon();
	TestEqual(TEXT("Weak identity is the captured Pawn"), Common.Pawn.Get(), static_cast<APawn*>(Character));
	TestEqual(TEXT("Owning ID is PlayerState PlayerId"), Common.OwningPlayerId, 42);
	TestEqual(TEXT("Custom movement mode retained"), Common.Current.CustomMovementMode, uint8(17));
	TestEqual(TEXT("Ground movement mode retained"), Common.Current.GroundMovementMode.GetValue(), MOVE_NavWalking);
	TestTrue(TEXT("Full rotation is retained"), Common.Current.Pose.Value.Rotation.Equals(Character->GetActorRotation()));
	TestTrue(TEXT("Actual crouch is known"), Common.Current.CrouchStatus == EObservationAvailability::Available && Common.Current.bCrouched);
	TestTrue(TEXT("Jump is an instantaneous request"), Common.Input.bJumpRequestedAtSample);
	TestEqual(TEXT("No mesh asset is a missing source"), Common.VisualCapsulePose.Status, EObservationAvailability::MissingSource);
	TestNull(TEXT("Authority does not carry autonomous data"), First.Get()->GetAutonomous());
	TestNull(TEXT("Authority does not carry simulated data"), First.Get()->GetSimulated());

	// A second listener gets exactly the same capture, without a second Pawn sample.
	FPawnObservationSnapshot Shared;
	const auto Second = Service->Subscribe(*Character, FOnPawnObservationCaptured::FDelegate::CreateLambda([&](const FPawnObservationSnapshot& Snapshot) { Shared = Snapshot; }));
	Fixture.Capture();
	TestTrue(TEXT("Consumers share the immutable frame"), Shared == Selector->GetPawnObservationSnapshot());
	Service->Unsubscribe(Second);
	FPawnObservationSnapshot BlueprintCopy;
	// Type contract (caller guaranteed): both erased buffers are live FPawnObservationSnapshot instances,
	// matching the UScriptStruct layout and copy traits used by this reflected-copy fixture.
	FPawnObservationSnapshot::StaticStruct()->CopyScriptStruct(&BlueprintCopy, &First);
	TestTrue(TEXT("Reflected copy retains the opaque handle"), BlueprintCopy == First);
	Character->SetActorLocation(FVector(900.0, 20.0, 90.0));
	Fixture.Capture();
	FPawnObservationCommon BlueprintCommon;
	TestTrue(TEXT("BP accessor reads its passed handle"), UPawnObservationBlueprintLibrary::TryGetCommonObservation(BlueprintCopy, BlueprintCommon));
	TestEqual(TEXT("Old handle stays stable after later captures"), BlueprintCommon.Current.Pose.Value.ZeroOriginPosition, Common.Current.Pose.Value.ZeroOriginPosition);
	FAutonomousPawnObservation WrongRole;
	TestFalse(TEXT("BP rejects wrong role"), UPawnObservationBlueprintLibrary::TryGetAutonomousObservation(BlueprintCopy, WrongRole));

	TestTrue(TEXT("Generic Pawn selection supported"), Selector->SetObservedPawn(GenericPawn));
	TestEqual(TEXT("Target changes invalidate immediately"), Selector->GetObservationTargetStatus(), EObservationAvailability::AwaitingSample);
	Fixture.Capture();
	const FPawnObservationCommon& Generic = Selector->GetPawnObservationSnapshot().Get()->GetCommon();
	TestEqual(TEXT("Generic Pawn with no root does not invent a pose"), Generic.Current.Pose.Status, EObservationAvailability::MissingSource);
	TestEqual(TEXT("Generic Pawn has no character movement mode"), Generic.Current.MovementModeStatus, EObservationAvailability::NotApplicable);
	TestEqual(TEXT("Generic Pawn has no jump request"), Generic.Input.JumpRequestStatus, EObservationAvailability::NotApplicable);
	ACharacter* MissingMovementCharacter = World->SpawnActor<ACharacter>();
	MissingMovementCharacter->GetCharacterMovement()->DestroyComponent();
	const FPawnObservationCommon MissingMovement = PawnObservation::SampleCommon(*MissingMovementCharacter);
	TestEqual(TEXT("Destroyed movement component is missing source"), MissingMovement.Current.MovementModeStatus, EObservationAvailability::MissingSource);
	TestEqual(TEXT("Destroyed movement cannot claim velocity"), MissingMovement.Current.VelocityStatus, EObservationAvailability::MissingSource);
	TestEqual(TEXT("Missing movement cannot claim smoothing"), MissingMovement.Smoothing.Status, EObservationAvailability::MissingSource);
	GenericPawn->SetRootComponent(NewObject<USceneComponent>(GenericPawn));
	GenericPawn->GetRootComponent()->RegisterComponent();
	Fixture.Capture();
	TestTrue(TEXT("Zero position is available when root exists"), Selector->GetPawnObservationSnapshot().Get()->GetCommon().Current.Pose.IsAvailable());

	// Reentrant selection must not replace the handle currently being delivered to another consumer.
	FPawnObservationSnapshot Delivered;
	const auto Reentrant = Service->Subscribe(*GenericPawn, FOnPawnObservationCaptured::FDelegate::CreateLambda([&](const FPawnObservationSnapshot& Snapshot)
	{
		Selector->SetObservedPawn(Character);
		Delivered = Snapshot;
	}));
	Fixture.Capture();
	TestEqual(TEXT("Delivered frame survives callback selection"), Delivered.Get()->GetCommon().Pawn.Get(), GenericPawn);
	TestEqual(TEXT("Callback selects the next target"), Selector->GetObservedPawn(), static_cast<APawn*>(Character));
	Service->Unsubscribe(Reentrant);
	Fixture.Capture();
	const FPawnObservationSnapshot BeforePause = Selector->GetPawnObservationSnapshot();
	APlayerState* Pauser = World->SpawnActor<APlayerState>();
	World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	Fixture.Capture();
	TestTrue(TEXT("Pause retains the previous capture"), BeforePause == Selector->GetPawnObservationSnapshot());
	World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Character->Destroy();
	TestFalse(TEXT("Destroyed Pawn invalidates the live query"), Selector->GetPawnObservationSnapshot().IsValid());
	TestTrue(TEXT("Captured path survives destruction"), !First.Get()->GetCommon().PawnPath.IsEmpty());
	TestFalse(TEXT("Snapshot does not keep the Pawn alive"), First.Get()->GetCommon().Pawn.IsValid());

	FObservedPose Canonical = FObservedPose::Capture(*World, FVector(1.0, 2.0, 3.0), FRotator(5.0, 6.0, 7.0));
	World->OriginLocation = FIntVector(100, 200, 300);
	TestEqual(TEXT("Ghost drawing converts canonical coordinates to current origin"), Canonical.GetLocalPosition(*World), FVector(-99.0, -198.0, -297.0));
	World->OriginLocation = FIntVector::ZeroValue;
	return true;
}
#endif
