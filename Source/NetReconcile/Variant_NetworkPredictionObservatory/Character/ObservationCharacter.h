#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "NetReconcileCharacter.h"
#include "PawnObservationWorldSubsystem.h"
#include "ObservationCharacter.generated.h"

class UMaterialInterface;
class USkeletalMeshComponent;

/** Third person character with native movement observation and local state ghosts. */
UCLASS(Abstract)
class NETRECONCILE_API AObservationCharacter : public ANetReconcileCharacter
{
	GENERATED_BODY()

	friend class FObservationGhostTest;

public:
	AObservationCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	inline USkeletalMeshComponent* GetServerReferenceGhostMesh() const { return ServerReferenceGhostMesh; }
	inline USkeletalMeshComponent* GetLocalStateGhostMesh() const { return LocalStateGhostMesh; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observation|Ghost", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> ServerReferenceGhostMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observation|Ghost", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> LocalStateGhostMesh;

	/** Shared translucent material with GhostColor and GhostOpacity parameters. */
	UPROPERTY(EditDefaultsOnly, Category = "Observation|Ghost")
	TObjectPtr<UMaterialInterface> GhostMaterial;

	TWeakObjectPtr<UPawnObservationWorldSubsystem> CaptureService;
	FPawnObservationSubscription GhostSubscription;
	FDelegateHandle WorldTickStartHandle;
	EObservedPawnRole GhostRole = EObservedPawnRole::Unknown;

	void InitializeGhostMeshes();
	void RefreshGhostSubscription(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void UpdateGhosts(const FPawnObservationSnapshot& Snapshot);
	void UpdateGhostMesh(USkeletalMeshComponent& GhostMesh, const FObservedPose* Pose);
};
