#include "ObservationCharacter.h"
#include "ObservationCharacterMovementComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

AObservationCharacter::AObservationCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UObservationCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Keep serialized default-subobject identifiers used by the existing character Blueprint.
	ServerReferenceGhostMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AuthoritativeGhostMesh"));
	LocalStateGhostMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PredictionGhostMesh"));
	for (USkeletalMeshComponent* GhostMesh : {ServerReferenceGhostMesh.Get(), LocalStateGhostMesh.Get()})
	{
		GhostMesh->SetupAttachment(GetRootComponent());
		GhostMesh->SetAbsolute(true, true, true);
		GhostMesh->SetVisibility(false);
		GhostMesh->SetOnlyOwnerSee(false);
		GhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GhostMesh->SetGenerateOverlapEvents(false);
		GhostMesh->SetCanEverAffectNavigation(false);
		GhostMesh->SetCastShadow(false);
		GhostMesh->bUseAttachParentBound = false;
		GhostMesh->bUseBoundsFromLeaderPoseComponent = false;
	}
}

void AObservationCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
	{
		GhostRole = EObservedPawnRole::Unknown;
		InitializeGhostMeshes();
		WorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::RefreshGhostSubscription);
		RefreshGhostSubscription(GetWorld(), LEVELTICK_All, 0.0f);
	}
}

void AObservationCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CaptureService.IsValid()) { CaptureService->Unsubscribe(GhostSubscription); }
	FWorldDelegates::OnWorldTickStart.Remove(WorldTickStartHandle);
	WorldTickStartHandle.Reset();
	GhostSubscription = {};
	CaptureService.Reset();
	UpdateGhostMesh(*ServerReferenceGhostMesh, nullptr);
	UpdateGhostMesh(*LocalStateGhostMesh, nullptr);
	Super::EndPlay(EndPlayReason);
}

void AObservationCharacter::InitializeGhostMeshes()
{
	if (!GetMesh()->GetSkeletalMeshAsset() || !GhostMaterial) { return; }
	auto InitializeMesh = [this](USkeletalMeshComponent& GhostMesh, const FLinearColor& Color)
	{
		GhostMesh.SetSkeletalMeshAsset(GetMesh()->GetSkeletalMeshAsset());
		GhostMesh.SetLeaderPoseComponent(GetMesh());
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(GhostMaterial, this);
		Material->SetVectorParameterValue(TEXT("GhostColor"), Color);
		for (int32 Index = 0; Index < GhostMesh.GetNumMaterials(); ++Index) { GhostMesh.SetMaterial(Index, Material); }
	};
	InitializeMesh(*ServerReferenceGhostMesh, FLinearColor::Red);
	InitializeMesh(*LocalStateGhostMesh, FLinearColor::Blue);
}

void AObservationCharacter::RefreshGhostSubscription(UWorld* World, ELevelTick, float)
{
	if (World != GetWorld()) { return; }
	UPawnObservationWorldSubsystem* Service = World->GetSubsystem<UPawnObservationWorldSubsystem>();
	if (Service == CaptureService.Get() && GhostSubscription.IsValid())
	{
		// Role/controller/prediction changes invalidate the source even while Pawn sampling is paused.
		if (!Service->GetLatest(*this).IsValid()) { UpdateGhosts({}); }
		return;
	}
	// Actors retained by seamless travel must subscribe to the new world's collector too.
	if (CaptureService.IsValid()) { CaptureService->Unsubscribe(GhostSubscription); }
	UpdateGhosts({});
	CaptureService = Service;
	GhostSubscription = Service ? Service->Subscribe(*this, FOnPawnObservationCaptured::FDelegate::CreateUObject(this, &ThisClass::UpdateGhosts)) : FPawnObservationSubscription{};
}

void AObservationCharacter::UpdateGhostMesh(USkeletalMeshComponent& GhostMesh, const FObservedPose* Pose)
{
	const bool bVisible = Pose && GhostMesh.GetSkeletalMeshAsset() && GhostMaterial;
	GhostMesh.SetVisibility(bVisible);
	if (bVisible)
	{
		// Mesh smoothing is presentation only; use the unsmoothed mesh-to-capsule baseline.
		const FTransform MeshOffset(GetBaseRotationOffset(), GetBaseTranslationOffset(), GetMesh()->GetRelativeScale3D());
		const FTransform StateTransform(Pose->Rotation, Pose->GetLocalPosition(*GetWorld()), GetActorScale3D());
		GhostMesh.SetWorldTransform(MeshOffset * StateTransform);
	}
}

void AObservationCharacter::UpdateGhosts(const FPawnObservationSnapshot& Snapshot)
{
	const FPawnObservationFrame* Frame = Snapshot.Get();
	const EObservedPawnRole CapturedRole = Snapshot.GetRole();
	const bool bSimulated = CapturedRole == EObservedPawnRole::SimulatedProxy;
	const bool bValid = Frame && Frame->GetCommon().Pawn == this && CapturedRole == PawnObservation::GetRole(GetLocalRole());
	if (GhostRole != CapturedRole)
	{
		GhostRole = CapturedRole;
		if (auto* Material = Cast<UMaterialInstanceDynamic>(LocalStateGhostMesh->GetMaterial(0)))
		{
			Material->SetVectorParameterValue(TEXT("GhostColor"), bSimulated ? FLinearColor(0.0f, 1.0f, 1.0f) : FLinearColor::Blue);
		}
	}
	const FObservedPose* Server = nullptr;
	const FObservedPose* Local = nullptr;
	if (bValid && Frame->GetAutonomous())
	{
		const FObservedMovePair& Pair = Frame->GetAutonomous()->LastValidPair;
		if (Pair.IsAvailable() && Pair.ServerState.Pose.IsAvailable() && Pair.PredictionBeforeResponse.EndState.Pose.IsAvailable())
		{
			Server = &Pair.ServerState.Pose.Value;
			Local = &Pair.PredictionBeforeResponse.EndState.Pose.Value;
		}
	}
	else if (bValid && bSimulated)
	{
		const FObservedPoseSample& Reference = Frame->GetSimulated()->LastDecodedReference;
		Server = Reference.IsAvailable() ? &Reference.Value : nullptr;
		Local = Frame->GetCommon().Current.Pose.IsAvailable() ? &Frame->GetCommon().Current.Pose.Value : nullptr;
	}
	UpdateGhostMesh(*ServerReferenceGhostMesh, Server);
	UpdateGhostMesh(*LocalStateGhostMesh, Local);
}
