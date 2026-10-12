#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "PawnObservationSnapshot.h"
#include "PawnObservationBlueprintLibrary.generated.h"

UCLASS()
class NETRECONCILE_API UPawnObservationBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Observation")
	static inline bool IsValidSnapshot(const FPawnObservationSnapshot& Snapshot) { return Snapshot.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Observation")
	static inline EObservedPawnRole GetObservedRole(const FPawnObservationSnapshot& Snapshot) { return Snapshot.GetRole(); }

	UFUNCTION(BlueprintCallable, Category = "Observation", meta = (ExpandBoolAsExecs = "ReturnValue"))
	static bool TryGetCommonObservation(const FPawnObservationSnapshot& Snapshot, FPawnObservationCommon& Common);

	UFUNCTION(BlueprintCallable, Category = "Observation", meta = (ExpandBoolAsExecs = "ReturnValue"))
	static bool TryGetAutonomousObservation(const FPawnObservationSnapshot& Snapshot, FAutonomousPawnObservation& Autonomous);

	UFUNCTION(BlueprintCallable, Category = "Observation", meta = (ExpandBoolAsExecs = "ReturnValue"))
	static bool TryGetSimulatedObservation(const FPawnObservationSnapshot& Snapshot, FSimulatedPawnObservation& Simulated);

	UFUNCTION(BlueprintPure, Category = "Observation")
	static inline FObservedMotionDifference GetPairedDifference(const FObservedMovePair& Pair) { return PawnObservation::Difference(Pair); }
};
