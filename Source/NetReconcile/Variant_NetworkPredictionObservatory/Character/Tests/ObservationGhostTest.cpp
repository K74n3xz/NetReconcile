#if WITH_DEV_AUTOMATION_TESTS

#include "Variant_NetworkPredictionObservatory/Character/LocalPlayerObservationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/Character/ObservationCharacter.h"
#include "Variant_NetworkPredictionObservatory/Character/ObservationCharacterMovementComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObservationGhostTest,
	"NetReconcile.Observation.Ghosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FObservationGhostTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	UNetDriver* Driver = nullptr;
	ON_SCOPE_EXIT
	{
		World->SetNetDriver(nullptr);
		if (Driver)
		{
			if (Driver->ServerConnection)
			{
				Driver->ServerConnection->SetConnectionState(USOCK_Closed);
				Driver->ServerConnection->CleanUp();
			}
			Driver->SetWorld(nullptr);
		}
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	UClass* CharacterClass = LoadClass<AObservationCharacter>(nullptr,
		TEXT("/Game/Variant_NetworkPredictionObservatory/Blueprints/BP_ObservationCharacter.BP_ObservationCharacter_C"));
	if (!TestNotNull(TEXT("Existing observation Blueprint loads"), CharacterClass)) { return false; }
	AObservationCharacter* Character = World->SpawnActor<AObservationCharacter>(CharacterClass);
	AObservationCharacter* Remote = World->SpawnActor<AObservationCharacter>(CharacterClass);
	if (!TestNotNull(TEXT("Character"), Character) || !TestNotNull(TEXT("Remote character"), Remote)) { return false; }
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	Mesh->SetRelativeLocationAndRotation(FVector(3.0, 7.0, -96.0), FRotator(0.0, -90.0, 0.0));
	Mesh->SetRelativeScale3D(FVector(0.8, 1.1, 0.75));
	Character->CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
	Character->SetActorScale3D(FVector(1.5, 1.25, 0.8));
	Character->DispatchBeginPlay();
	Remote->DispatchBeginPlay();

	USkeletalMeshComponent* ServerGhost = Character->GetServerReferenceGhostMesh();
	USkeletalMeshComponent* LocalGhost = Character->GetLocalStateGhostMesh();
	if (!TestNotNull(TEXT("Server reference ghost"), ServerGhost) || !TestNotNull(TEXT("Local state ghost"), LocalGhost)) { return false; }
	TestEqual(TEXT("Serialized server subobject identifier is preserved"), ServerGhost->GetFName(), FName(TEXT("AuthoritativeGhostMesh")));
	TestEqual(TEXT("Serialized local subobject identifier is preserved"), LocalGhost->GetFName(), FName(TEXT("PredictionGhostMesh")));
	for (USkeletalMeshComponent* Ghost : {ServerGhost, LocalGhost})
	{
		TestFalse(TEXT("Ghost starts hidden"), Ghost->IsVisible());
		TestEqual(TEXT("Ghost reuses the existing skeletal mesh"), Ghost->GetSkeletalMeshAsset(), Mesh->GetSkeletalMeshAsset());
		TestEqual(TEXT("Ghost shares the current bone pose"), Ghost->LeaderPoseComponent.Get(), static_cast<USkinnedMeshComponent*>(Mesh));
		TestNull(TEXT("Ghost has no independent animation"), Ghost->GetAnimInstance());
		TestEqual(TEXT("Ghost has no collision"), Ghost->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Ghost does not simulate physics"), Ghost->IsSimulatingPhysics());
		TestFalse(TEXT("Ghost does not generate overlaps"), Ghost->GetGenerateOverlapEvents());
		TestFalse(TEXT("Ghost does not affect navigation"), Ghost->CanEverAffectNavigation());
		TestFalse(TEXT("Ghost does not cast a shadow"), Ghost->CastShadow);
		TestFalse(TEXT("Ghost is not replicated"), Ghost->GetIsReplicated());
		TestFalse(TEXT("Remote ghosts are visible beyond their owner"), Ghost->bOnlyOwnerSee);
		TestTrue(TEXT("Ghost location is independent of the actor"), Ghost->IsUsingAbsoluteLocation());
		TestTrue(TEXT("Ghost rotation is independent of the actor"), Ghost->IsUsingAbsoluteRotation());
		TestFalse(TEXT("Ghost bounds do not follow the actor"), Ghost->bUseAttachParentBound);
		TestFalse(TEXT("Ghost bounds do not follow the source mesh"), Ghost->bUseBoundsFromLeaderPoseComponent);
	}
	UMaterialInstanceDynamic* ServerMaterial = Cast<UMaterialInstanceDynamic>(ServerGhost->GetMaterial(0));
	UMaterialInstanceDynamic* LocalMaterial = Cast<UMaterialInstanceDynamic>(LocalGhost->GetMaterial(0));
	if (!TestNotNull(TEXT("Server dynamic material"), ServerMaterial) || !TestNotNull(TEXT("Local dynamic material"), LocalMaterial)) { return false; }
	TestTrue(TEXT("Ghosts have separate dynamic materials"), ServerMaterial != LocalMaterial);
	TestEqual(TEXT("Server color is red"), ServerMaterial->K2_GetVectorParameterValue(TEXT("GhostColor")), FLinearColor::Red);
	TestEqual(TEXT("Ghost opacity remains 0.35"), ServerMaterial->K2_GetScalarParameterValue(TEXT("GhostOpacity")), 0.35f);
	TestEqual(TEXT("Ghost material is translucent"), ServerMaterial->GetBlendMode(), BLEND_Translucent);
	for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
	{
		TestEqual(TEXT("All server material slots share the material"), ServerGhost->GetMaterial(Index), static_cast<UMaterialInterface*>(ServerMaterial));
		TestEqual(TEXT("All local material slots share the material"), LocalGhost->GetMaterial(Index), static_cast<UMaterialInterface*>(LocalMaterial));
	}
	auto Sample = [World]() { FWorldDelegates::OnWorldPostActorTick.Broadcast(World, LEVELTICK_All, 1.0f / 60.0f); };
	Sample();
	TestFalse(TEXT("Authority has no redundant server ghost"), ServerGhost->IsVisible());
	TestFalse(TEXT("Authority has no redundant local ghost"), LocalGhost->IsVisible());

	UClass* DriverClass = LoadClass<UNetDriver>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
	if (!TestNotNull(TEXT("Client driver class"), DriverClass)) { return false; }
	Driver = NewObject<UNetDriver>(World, DriverClass);
	Driver->SetWorld(World);
	USimulatedClientNetConnection* Connection = NewObject<USimulatedClientNetConnection>(Driver);
	Connection->Driver = Driver;
	Driver->ServerConnection = Connection;
	World->SetNetDriver(Driver);
	Character->SetAutonomousProxy(true);
	Character->SwapRoles();
	FAutonomousPawnObservation Autonomous;
	Autonomous.PredictionStatus = EObservationAvailability::Available;
	FObservedMovePair& Pair = Autonomous.LastValidPair;
	Pair.Status = EObservationAvailability::Available;
	Pair.ServerState.Pose.Status = EObservationAvailability::Available;
	Pair.ServerState.Pose.Value = FObservedPose::Capture(*World, FVector(250.0, -140.0, 120.0), FRotator(14.0, 37.0, 8.0));
	Pair.PredictionBeforeResponse.EndState.Pose.Status = EObservationAvailability::Available;
	Pair.PredictionBeforeResponse.EndState.Pose.Value = FObservedPose::Capture(*World, FVector(470.0, 110.0, 160.0), FRotator(-11.0, -61.0, 12.0));
	Autonomous.UnacknowledgedMoves.AddDefaulted_GetRef().EndState.Pose.Value.ZeroOriginPosition = FVector(-800.0, 600.0, 300.0);
	auto SnapshotFor = [&]()
	{
		FObservedPawnRoleData Data; Data.Set<FAutonomousPawnObservation>(Autonomous);
		return FPawnObservationSnapshot(FPawnObservationFrame(PawnObservation::SampleCommon(*Character), MoveTemp(Data), FGuid::NewGuid(), 1));
	};
	Mesh->SetRelativeLocationAndRotation(FVector(80.0, -50.0, -30.0), FRotator(0.0, 22.0, 0.0));
	Character->UpdateGhosts(SnapshotFor());
	auto CheckTransform = [&](const USkeletalMeshComponent& Ghost, const FObservedPose& Pose)
	{
		const FTransform Transform(Pose.Rotation, Pose.GetLocalPosition(*World), Character->GetActorScale3D());
		TestTrue(TEXT("Ghost translation removes presentation smoothing"), Ghost.GetComponentLocation().Equals(Transform.TransformPosition(Character->GetBaseTranslationOffset()), 0.01));
		TestTrue(TEXT("Ghost retains full rotation and mesh baseline"), Ghost.GetComponentQuat().Equals(Transform.GetRotation() * Character->GetBaseRotationOffset(), 0.001));
		TestTrue(TEXT("Ghost preserves actor and mesh scale"), Ghost.GetComponentScale().Equals(Character->GetActorScale3D() * Mesh->GetRelativeScale3D(), 0.001));
	};
	TestTrue(TEXT("Autonomous paired server ghost is visible"), ServerGhost->IsVisible());
	TestTrue(TEXT("Autonomous matching pre-response ghost is visible"), LocalGhost->IsVisible());
	TestEqual(TEXT("Autonomous local state is blue"), LocalMaterial->K2_GetVectorParameterValue(TEXT("GhostColor")), FLinearColor::Blue);
	CheckTransform(*ServerGhost, Pair.ServerState.Pose.Value);
	CheckTransform(*LocalGhost, Pair.PredictionBeforeResponse.EndState.Pose.Value);
	World->OriginLocation = FIntVector(100, 200, 300);
	Character->UpdateGhosts(SnapshotFor());
	CheckTransform(*ServerGhost, Pair.ServerState.Pose.Value);
	CheckTransform(*LocalGhost, Pair.PredictionBeforeResponse.EndState.Pose.Value);
	World->OriginLocation = FIntVector::ZeroValue;
	Pair.Status = EObservationAvailability::AwaitingSample;
	Character->UpdateGhosts(SnapshotFor());
	TestFalse(TEXT("Before a valid pair both autonomous ghosts hide even with unacked moves"), ServerGhost->IsVisible() || LocalGhost->IsVisible());

	// Native simulated networking supplies the reference; no local controller or UI is required.
	Remote->SetAutonomousProxy(false);
	Remote->SwapRoles();
	// Type contract (caller guaranteed): Remote uses the existing observation Blueprint with the custom CMC subobject.
	UObservationCharacterMovementComponent* MovementComponent = CastChecked<UObservationCharacterMovementComponent>(Remote->GetCharacterMovement());
	MovementComponent->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
	Sample();
	TestFalse(TEXT("Simulated server ghost waits for a network reference"), Remote->GetServerReferenceGhostMesh()->IsVisible());
	TestTrue(TEXT("Simulated local capsule is observable before a reference"), Remote->GetLocalStateGhostMesh()->IsVisible());
	const FVector ReferencePosition(400.0, 600.0, 200.0);
	const FQuat ReferenceRotation = FRotator(0.0, 42.0, 0.0).Quaternion();
	MovementComponent->SmoothCorrection(Remote->GetActorLocation(), Remote->GetActorQuat(), ReferencePosition, ReferenceRotation);
	Remote->SetActorLocation(ReferencePosition + FVector(25.0, 0.0, 0.0));
	Sample();
	TestTrue(TEXT("Every unowned simulated character updates its server ghost"), Remote->GetServerReferenceGhostMesh()->IsVisible());
	TestTrue(TEXT("Every unowned simulated character updates its local ghost"), Remote->GetLocalStateGhostMesh()->IsVisible());
	TestTrue(TEXT("Server ghost remains at the last reference while local simulation advances"),
		Remote->GetServerReferenceGhostMesh()->GetComponentLocation().Equals(FTransform(ReferenceRotation, ReferencePosition).TransformPosition(Remote->GetBaseTranslationOffset()), 0.01));
	TestTrue(TEXT("Cyan ghost follows the current unsmoothed capsule"),
		Remote->GetLocalStateGhostMesh()->GetComponentLocation().Equals(Remote->GetActorTransform().TransformPosition(Remote->GetBaseTranslationOffset()), 0.01));
	// Type contract (caller guaranteed): ghost initialization installs a live UMaterialInstanceDynamic in every material slot.
	TestEqual(TEXT("Simulated local state is cyan"), CastChecked<UMaterialInstanceDynamic>(Remote->GetLocalStateGhostMesh()->GetMaterial(0))->K2_GetVectorParameterValue(TEXT("GhostColor")), FLinearColor(0.0f, 1.0f, 1.0f));
	TestTrue(TEXT("Native smoothing has initialized client data"), MovementComponent->HasPredictionData_Client());
	const FPawnObservationSnapshot Simulated = World->GetSubsystem<UPawnObservationWorldSubsystem>()->GetLatest(*Remote);
	TestNull(TEXT("Simulated smoothing data cannot expose autonomous history or counters"), Simulated.Get()->GetAutonomous());
	TestNotNull(TEXT("Simulated role carries only its reference payload"), Simulated.Get()->GetSimulated());
	const FVector CanonicalReference = MovementComponent->GetSimulatedObservation().LastDecodedReference.Value.ZeroOriginPosition;
	World->OriginLocation = FIntVector(-100, 50, 0);
	MovementComponent->ApplyWorldOffset(FVector(100.0, -50.0, 0.0), true);
	TestEqual(TEXT("Canonical reference is unchanged by rebasing"), MovementComponent->GetSimulatedObservation().LastDecodedReference.Value.ZeroOriginPosition, CanonicalReference);
	TestEqual(TEXT("Reference draws against current origin"), MovementComponent->GetSimulatedObservation().LastDecodedReference.Value.GetLocalPosition(*World), ReferencePosition + FVector(100.0, -50.0, 0.0));
	World->OriginLocation = FIntVector::ZeroValue;
	MovementComponent->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
	MovementComponent->SmoothCorrection(Remote->GetActorLocation(), Remote->GetActorQuat(), ReferencePosition, ReferenceRotation);
	Sample();
	TestEqual(TEXT("Disabled smoothing remains observable"), PawnObservation::SampleCommon(*Remote).Smoothing.Mode, ENetworkSmoothingMode::Disabled);

	AActor* BaseActor = World->SpawnActor<AActor>();
	UBoxComponent* Base = NewObject<UBoxComponent>(BaseActor);
	BaseActor->SetRootComponent(Base);
	Base->SetMobility(EComponentMobility::Movable);
	Base->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Base->RegisterComponent();
	BaseActor->SetActorLocationAndRotation(FVector(300.0, -200.0, 100.0), FRotator(0.0, 35.0, 0.0));
	FBasedMovementInfo Based;
	Based.MovementBaseInterfaceData = MovementBaseUtility::GetMovementBaseDataFromPhysicsOwner(Base);
	Based.bServerHasBaseComponent = true;
	Based.bRelativeRotation = true;
	Based.Location = FVector(30.0, 20.0, 100.0);
	Based.Rotation = FRotator(0.0, 12.0, 0.0);
	if (!TestTrue(TEXT("Moving-base fixture resolves a relative movement base"), Based.HasRelativeLocation())) { return false; }
	Remote->SetReplicatedBasedMovement(Based);
	Remote->OnRep_ReplicatedBasedMovement();
	TestTrue(TEXT("Native based replication supplies a world-space reference"), MovementComponent->GetSimulatedObservation().LastDecodedReference.Value.GetLocalPosition(*World).Equals(
		Base->GetComponentTransform().TransformPositionNoScale(Based.Location), 0.01));
	TestTrue(TEXT("Native based replication supplies resolved facing"), FMath::IsNearlyEqual(MovementComponent->GetSimulatedObservation().LastDecodedReference.Value.Rotation.Yaw, 47.0f, 0.01f));
	BaseActor->SetActorLocation(BaseActor->GetActorLocation() + FVector(80.0, 0.0, 0.0));
	Remote->OnRep_ReplicatedBasedMovement();
	Sample();
	TestTrue(TEXT("Moving-base updates keep the server reference in local world coordinates"), MovementComponent->GetSimulatedObservation().LastDecodedReference.Value.GetLocalPosition(*World).Equals(
		Base->GetComponentTransform().TransformPositionNoScale(Based.Location), 0.01));

	const FTransform BeforePause = Remote->GetLocalStateGhostMesh()->GetComponentTransform();
	APlayerState* Pauser = World->SpawnActor<APlayerState>();
	World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	Remote->SetActorLocation(ReferencePosition + FVector(100.0, 0.0, 0.0));
	Sample();
	TestTrue(TEXT("World pause preserves the sampled ghost"), Remote->GetLocalStateGhostMesh()->GetComponentTransform().Equals(BeforePause));
	Remote->SwapRoles();
	FWorldDelegates::OnWorldTickStart.Broadcast(World, LEVELTICK_All, 0.0f);
	TestFalse(TEXT("Role change invalidates ghosts even while sampling is paused"), Remote->GetServerReferenceGhostMesh()->IsVisible() || Remote->GetLocalStateGhostMesh()->IsVisible());
	World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Sample();
	TestFalse(TEXT("Becoming authority hides the former simulated server ghost"), Remote->GetServerReferenceGhostMesh()->IsVisible());
	TestFalse(TEXT("Becoming authority hides the former simulated local ghost"), Remote->GetLocalStateGhostMesh()->IsVisible());
	Remote->SwapRoles();
	Sample();
	TestFalse(TEXT("Returning to simulated role requires a fresh reference"), MovementComponent->GetSimulatedObservation().LastDecodedReference.IsAvailable());
	TestTrue(TEXT("Forced client destruction models a server despawn"), Remote->Destroy(true));
	TestFalse(TEXT("EndPlay hides all ghosts"), Remote->GetLocalStateGhostMesh()->IsVisible() || Remote->GetServerReferenceGhostMesh()->IsVisible());
	TestFalse(TEXT("EndPlay removes the frame callback"), Remote->GhostSubscription.IsValid());
	return true;
}

#endif
