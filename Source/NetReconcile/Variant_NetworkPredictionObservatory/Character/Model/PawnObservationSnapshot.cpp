#include "PawnObservationSnapshot.h"
#include "PawnObservationBlueprintLibrary.h"

EObservedPawnRole FPawnObservationFrame::GetRole() const
{
	if (RoleData.IsType<FAuthorityPawnObservation>()) { return EObservedPawnRole::Authority; }
	if (RoleData.IsType<FAutonomousPawnObservation>()) { return EObservedPawnRole::AutonomousProxy; }
	if (RoleData.IsType<FSimulatedPawnObservation>()) { return EObservedPawnRole::SimulatedProxy; }
	return EObservedPawnRole::Unknown;
}

bool UPawnObservationBlueprintLibrary::TryGetCommonObservation(const FPawnObservationSnapshot& Snapshot, FPawnObservationCommon& Common)
{
	Common = Snapshot.Get() ? Snapshot.Get()->GetCommon() : FPawnObservationCommon{};
	return Snapshot.IsValid();
}

bool UPawnObservationBlueprintLibrary::TryGetAutonomousObservation(const FPawnObservationSnapshot& Snapshot, FAutonomousPawnObservation& Autonomous)
{
	const FAutonomousPawnObservation* Data = Snapshot.Get() ? Snapshot.Get()->GetAutonomous() : nullptr;
	Autonomous = Data ? *Data : FAutonomousPawnObservation{};
	return Data != nullptr;
}

bool UPawnObservationBlueprintLibrary::TryGetSimulatedObservation(const FPawnObservationSnapshot& Snapshot, FSimulatedPawnObservation& Simulated)
{
	const FSimulatedPawnObservation* Data = Snapshot.Get() ? Snapshot.Get()->GetSimulated() : nullptr;
	Simulated = Data ? *Data : FSimulatedPawnObservation{};
	return Data != nullptr;
}
