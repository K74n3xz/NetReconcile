#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "PawnObservationTypes.h"
#include "PawnObservationSnapshot.generated.h"

struct FAuthorityPawnObservation {};
struct FUnclassifiedPawnObservation {};

using FObservedPawnRoleData = TVariant<FUnclassifiedPawnObservation, FAuthorityPawnObservation, FAutonomousPawnObservation, FSimulatedPawnObservation>;

/** An immutable capture. Roles are mutually exclusive; authority uses Common.Current. */
class NETRECONCILE_API FPawnObservationFrame
{
public:
	inline FPawnObservationFrame(FPawnObservationCommon InCommon, FObservedPawnRoleData InRoleData, FGuid InSourceEpoch, int64 InPublicationSequence)
		: Common(MoveTemp(InCommon)), RoleData(MoveTemp(InRoleData)), SourceEpoch(InSourceEpoch), PublicationSequence(InPublicationSequence) {}

	inline const FPawnObservationCommon& GetCommon() const { return Common; }
	inline const FAutonomousPawnObservation* GetAutonomous() const { return RoleData.TryGet<FAutonomousPawnObservation>(); }
	inline const FSimulatedPawnObservation* GetSimulated() const { return RoleData.TryGet<FSimulatedPawnObservation>(); }
	EObservedPawnRole GetRole() const;
	inline const FGuid& GetSourceEpoch() const { return SourceEpoch; }
	inline int64 GetPublicationSequence() const { return PublicationSequence; }

private:
	FPawnObservationCommon Common;
	FObservedPawnRoleData RoleData;
	FGuid SourceEpoch;
	int64 PublicationSequence = 0; // Publication count, not a simulation frame or move number.
};

/** Opaque Blueprint handle. Copies retain the same immutable frame, including during freeze. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FPawnObservationSnapshot
{
	GENERATED_BODY()

	inline FPawnObservationSnapshot() = default;
	explicit inline FPawnObservationSnapshot(FPawnObservationFrame Frame) : Data(MakeShared<const FPawnObservationFrame>(MoveTemp(Frame))) {}

	inline bool operator==(const FPawnObservationSnapshot& Other) const { return Data == Other.Data; }

	inline const FPawnObservationFrame* Get() const { return Data.Get(); }
	inline bool IsValid() const { return Data.IsValid(); }
	inline EObservedPawnRole GetRole() const { return Data ? Data->GetRole() : EObservedPawnRole::Unknown; }

private:
	TSharedPtr<const FPawnObservationFrame> Data;
};

template<> struct TStructOpsTypeTraits<FPawnObservationSnapshot> : TStructOpsTypeTraitsBase2<FPawnObservationSnapshot>
{
	enum { WithCopy = true, WithIdenticalViaEquality = true };
};
