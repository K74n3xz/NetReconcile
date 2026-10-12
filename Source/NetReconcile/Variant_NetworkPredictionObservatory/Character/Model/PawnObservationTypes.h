#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "PawnObservationTypes.generated.h"

class APawn;
class UWorld;

/** Availability describes the source, independently of its measured value (including zero). */
UENUM(BlueprintType)
enum class EObservationAvailability : uint8
{
	AwaitingSample,
	Available,
	MissingSource,
	Unsupported,
	NotApplicable
};

UENUM(BlueprintType)
enum class EObservedPawnRole : uint8 { Unknown, Authority, AutonomousProxy, SimulatedProxy };

UENUM(BlueprintType)
enum class EObservedMoveResponse : uint8 { GoodAck, Correction };

UENUM(BlueprintType)
enum class EObservedMovePairing : uint8
{
	AwaitingResponse,
	Paired,
	MissingPayload,
	UnsupportedProtocol,
	MoveIdMismatch,
	StaleServerSample,
	InvalidPayload
};

/** Local clocks only. Real time is monotonic FPlatformTime and continues during game pause. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FLocalObservationTime
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") double GameTimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") double RealTimeSeconds = 0.0;
};

/** Canonical position = world-local position + World.OriginLocation, regardless of networking CVars. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedPose
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") FVector ZeroOriginPosition = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FRotator Rotation = FRotator::ZeroRotator;

	static FObservedPose Capture(const UWorld& World, const FVector& LocalPosition, const FRotator& Rotation);

	FVector GetLocalPosition(const UWorld& World) const;
};

USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedPoseSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability Status = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedPose Value;

	inline bool IsAvailable() const { return Status == EObservationAvailability::Available; }
};

/** Captured paths remain readable after the base is destroyed; an empty path means no base. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedMovementBase
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability Status = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") TWeakObjectPtr<UObject> Object;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FString ObjectPath;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FString OwnerPath;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FName BoneName;
};

/** Composition permits a generic Pawn to have a pose/velocity without character movement or crouch. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedMotion
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedPoseSample Pose;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability VelocityStatus = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FVector Velocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability MovementModeStatus = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") TEnumAsByte<EMovementMode> MovementMode = MOVE_None;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") uint8 CustomMovementMode = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") TEnumAsByte<EMovementMode> GroundMovementMode = MOVE_None;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability CrouchStatus = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") bool bCrouched = false;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedMovementBase MovementBase;
};

/** This is the Pawn's last consumed world movement direction, not a replayable command. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedPawnInput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability ConsumedMovementStatus = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FVector LastConsumedWorldMovementDirection = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability JumpRequestStatus = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") bool bJumpRequestedAtSample = false;
};

USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedSmoothing
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability Status = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") ENetworkSmoothingMode Mode = ENetworkSmoothingMode::Disabled;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") bool bComplete = false;
};

USTRUCT(BlueprintType)
struct NETRECONCILE_API FPawnObservationCommon
{
	GENERATED_BODY()

	// Weak pointers retain UObject serial identity without keeping an actor/world alive.
	UPROPERTY(BlueprintReadOnly, Category = "Observation") TWeakObjectPtr<APawn> Pawn;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") TWeakObjectPtr<UWorld> World;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FString PawnPath;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FString PawnName;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FString WorldPath;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") int32 OwningPlayerId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") bool bLocallyControlled = false;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FLocalObservationTime SampleTime;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedMotion Current;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedPawnInput Input;
	/** Mesh pose with the unsmoothed mesh-to-capsule baseline removed. Pose only. */
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedPoseSample VisualCapsulePose;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedSmoothing Smoothing;
};

/** Unique across native timestamp resets and prediction-data lifetimes. Gaps after combining are normal. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedMoveId
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") FGuid PredictionSessionId;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") int64 MoveSequence = 0;

	inline bool operator==(const FObservedMoveId& Other) const { return PredictionSessionId == Other.PredictionSessionId && MoveSequence == Other.MoveSequence; }
	inline bool operator!=(const FObservedMoveId& Other) const { return !(*this == Other); }

	inline bool IsValid() const { return PredictionSessionId.IsValid() && MoveSequence > 0; }
	FString ToString() const;
};

/** Native SavedMove command summary; deliberately not advertised as a full replay command. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedMoveCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") FVector Acceleration = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") bool bJumpRequested = false;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") bool bCrouchRequested = false;
};

USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedSavedMove
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedMoveId MoveId;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") float NativeTimeStamp = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") float DeltaTimeSeconds = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") bool bBeforeTimeStampReset = false;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedMotion EndState;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedMoveCommand Command;
};

USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedAcceptedResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability Status = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedMoveId MoveId;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") float NativeTimeStamp = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservedMoveResponse Kind = EObservedMoveResponse::GoodAck;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FLocalObservationTime ReceivedAt;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservedMovePairing Pairing = EObservedMovePairing::AwaitingResponse;
};

/** Both states refer to PredictionBeforeResponse.MoveId. Prediction is copied immediately before native response handling. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedMovePair
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability Status = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedSavedMove PredictionBeforeResponse;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedMotion ServerState;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") double ServerGameTimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") int64 ServerSampleSequence = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedAcceptedResponse Response;

	inline bool IsAvailable() const { return Status == EObservationAvailability::Available; }
};

USTRUCT(BlueprintType)
struct NETRECONCILE_API FAutonomousPawnObservation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability PredictionStatus = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability NativeResponsesStatus = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") TArray<FObservedSavedMove> UnacknowledgedMoves;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") int64 AcceptedGoodAckCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") int64 AcceptedCorrectionCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedAcceptedResponse LatestAcceptedResponse;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedMovePair LastValidPair;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedMovePair LastValidCorrection;

	double GetUnacknowledgedDurationSeconds() const;
};

/** Decoded native reference has pose only; no server velocity, movement mode or crouch is inferred. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FSimulatedPawnObservation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") FObservedPoseSample LastDecodedReference;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") FLocalObservationTime ReferenceReceivedAt;
};

USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedMotionDifference
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Observation") EObservationAvailability Status = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") double PositionDistanceCm = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") double RotationAngleDegrees = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Observation") double VelocityDistanceCmPerSecond = 0.0;
};

namespace PawnObservation
{
	NETRECONCILE_API EObservedPawnRole GetRole(ENetRole Role);
	NETRECONCILE_API FLocalObservationTime LocalTime(const UWorld& World);
	NETRECONCILE_API FObservedMotionDifference Difference(const FObservedMovePair& Pair);
	NETRECONCILE_API FObservedMovementBase CaptureBase(const struct FMovementBaseInterfaceData& Base, FName Bone);
	NETRECONCILE_API FObservedMotion SampleMotion(const APawn& Pawn);
	NETRECONCILE_API FPawnObservationCommon SampleCommon(const APawn& Pawn);
}
