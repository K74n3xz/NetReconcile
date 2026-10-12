#pragma once

#include "Subsystems/LocalPlayerSubsystem.h"
#include "PawnObservationWorldSubsystem.h"
#include "LocalPlayerObservationSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPawnObservationSnapshotUpdated, const FPawnObservationSnapshot&, Snapshot);

/** Selects a same-world Pawn, following this local player's possession by default. */
UCLASS(BlueprintType)
class NETRECONCILE_API ULocalPlayerObservationSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="Observation") bool SetObservedPawn(APawn* Pawn);
	UFUNCTION(BlueprintPure, Category="Observation") APawn* GetObservedPawn() const;
	UFUNCTION(BlueprintPure, Category="Observation") bool IsFollowingControlledPawn() const;
	/** Available means a current frame for this target/role/source epoch; source blocks have their own status. */
	UFUNCTION(BlueprintPure, Category="Observation") EObservationAvailability GetObservationTargetStatus() const;
	UFUNCTION(BlueprintPure, Category="Observation") FPawnObservationSnapshot GetPawnObservationSnapshot() const;
	UPROPERTY(BlueprintAssignable, Category="Observation") FOnPawnObservationSnapshotUpdated OnPawnObservationSnapshotUpdated;

private:
	TWeakObjectPtr<APawn> SelectedPawn;
	TWeakObjectPtr<APawn> SubscribedPawn;
	TWeakObjectPtr<UPawnObservationWorldSubsystem> CaptureService;
	FPawnObservationSubscription Subscription;
	UPROPERTY(Transient) FPawnObservationSnapshot Snapshot;

	FDelegateHandle WorldTickStartHandle;

	void HandleWorldTickStart(UWorld* World, ELevelTick Type, float DeltaTime);
	void RefreshTarget();
	void Unsubscribe();
	void HandleCaptured(const FPawnObservationSnapshot& Captured);
};
