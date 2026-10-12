#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Model/PawnObservationTypes.h"
#include "ObservationCharacterMovementComponent.generated.h"

/** Metadata only. Native move simulation, combining, timestamps and replay remain in the base. */
class NETRECONCILE_API FObservationSavedMove : public FSavedMove_Character
{
public:
	FObservedMoveId MoveId;
	FObservedMotion ObservedEndState;

	virtual void Clear() override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& Accel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PostUpdate(ACharacter* Character, EPostUpdateMode Mode) override;
	FObservedSavedMove CaptureObservation() const;
};

class NETRECONCILE_API FObservationClientPredictionData : public FNetworkPredictionData_Client_Character
{
public:
	explicit FObservationClientPredictionData(const UCharacterMovementComponent& MovementComponent);

	virtual FSavedMovePtr AllocateNewMove() override;
	FObservedMoveId AllocateMoveId();

private:
	const FGuid PredictionSessionId;
	int64 LastAllocatedMoveSequence = 0;
};

/** Pure transport data; it is not a reflected UI state or a native timestamp identity. */
struct NETRECONCILE_API FObservationWireMoveId
{
	FGuid PredictionSessionId;
	int64 MoveSequence = 0;

	inline void Serialize(FArchive& Ar) { Ar << PredictionSessionId; Ar << MoveSequence; }
	inline FObservedMoveId ToObserved() const { FObservedMoveId Id; Id.PredictionSessionId = PredictionSessionId; Id.MoveSequence = MoveSequence; return Id; }
	inline void FromObserved(const FObservedMoveId& Id) { PredictionSessionId = Id.PredictionSessionId; MoveSequence = Id.MoveSequence; }
};

struct NETRECONCILE_API FObservationNetworkMoveData : public FCharacterNetworkMoveData
{
	FObservationWireMoveId MoveId;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type) override;
	virtual bool Serialize(UCharacterMovementComponent& MovementComponent, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type) override;
};

struct NETRECONCILE_API FObservationNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FObservationNetworkMoveDataContainer();

private:
	FObservationNetworkMoveData Moves[3];
};

struct NETRECONCILE_API FObservationResponsePayload
{
	bool bPresent = false;
	FObservationWireMoveId MoveId;
	int64 ServerSampleSequence = 0;
	double ServerGameTimeSeconds = 0.0;
	FVector ZeroOriginPosition = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Velocity = FVector::ZeroVector;
	uint8 PackedMovementMode = 0;
	uint8 GroundMovementMode = MOVE_Walking; // Custom packed modes do not encode the native ground mode.
	bool bCrouched = false;
	FString MovementBasePath;
	FString MovementBaseOwnerPath;
	FName MovementBaseBone;

	bool IsValid() const;
	bool Serialize(FArchive& Ar, UPackageMap* Map);
	FObservedMotion ToObserved(const UCharacterMovementComponent& MovementComponent) const;
};

/** Extra metadata on the existing packed response. Both peers must use this format. */
struct NETRECONCILE_API FObservationMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
	FObservationResponsePayload ServerObservationPayload;

	virtual void ServerFillResponseData(const UCharacterMovementComponent& MovementComponent, const FClientAdjustment& Adjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& MovementComponent, FArchive& Ar, UPackageMap* Map) override;
};

UCLASS()
class NETRECONCILE_API UObservationCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend struct FObservationMoveResponseDataContainer;
	friend class FObservationMovementDataTest;

public:
	// Any | Client / Server | Install the packed RPC containers before native networking.
	UObservationCharacterMovementComponent();

	// Autonomous Proxy / Simulated Proxy / Authority (remote Autonomous Proxy) | Client / Listen Server | Allocate prediction or smoothing data.
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	// Autonomous Proxy | Owning Client | Capture before native ACK/correction, then pair only accepted responses.
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response) override;

	// Simulated Proxy | Client | Capture the decoded reference before native capsule/mesh smoothing.
	// Authority (remote Autonomous Proxy) | Listen Server | Preserve native smoothing without capturing a simulated reference.
	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation) override;

	// Any | Client / Server | Native client prediction lifecycle reset, then observation context invalidation.
	virtual void ResetPredictionData_Client() override;
	// Any | Client / Server | Native server prediction lifecycle reset, then pending response invalidation.
	virtual void ResetPredictionData_Server() override;

	/** Observer queries never initialize native prediction data. Role/controller/protocol changes invalidate observer caches. */
	// Any | Client / Server | Query the current observation context identity.
	const FGuid& GetObservationContextEpoch() const;
	// Autonomous Proxy | Owning Client | Copy the response cache and current unacknowledged SavedMoves; other roles are NotApplicable.
	FAutonomousPawnObservation CaptureAutonomousObservation() const;
	// Any | Client / Server | Query cached owning-client responses without allocating prediction data.
	const FAutonomousPawnObservation& GetResponseObservation() const;
	// Any | Client / Server | Query the cached simulated-client decoded reference.
	const FSimulatedPawnObservation& GetSimulatedObservation() const;

protected:
	// Authority | Server | Sample after native Move execution/error checking, before packed response fill.
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float InDeltaTime, const FVector& Accel,
		const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBase,
		FName ClientBaseBoneName, uint8 ClientMovementMode) override;

private:
	// Autonomous Proxy / Authority | Owning Client / Server | Packed Move fill, serialization and decoding.
	FObservationNetworkMoveDataContainer PackedMoveDataContainer;
	// Authority / Autonomous Proxy | Server / Owning Client | Packed response fill, serialization and decoding.
	FObservationMoveResponseDataContainer PackedMoveResponseDataContainer;
	// Authority | Server | Latest sampled Move matching the native pending response timestamp, before response fill.
	FObservationResponsePayload PendingServerObservationPayload;
	float PendingResponseNativeTimeStamp = 0.0f;
	// Authority | Server | Last allocated sample sequence; spans native server prediction resets.
	int64 LastServerSampleSequence = 0;

	// Any | Client / Server | Observation context identity and the keys used to detect context changes.
	mutable FGuid ObservationContextEpoch;
	mutable ENetRole CachedLocalRole = ROLE_None;
	mutable bool bCachedUsesPackedMovementRPCs = false;
	mutable TWeakObjectPtr<AController> CachedController;
	// Autonomous Proxy | Owning Client | Accepted response counts and retained valid pairs after native response handling.
	mutable FAutonomousPawnObservation AutonomousResponseCache;
	// Simulated Proxy | Client | Last decoded reference and receive time captured before smoothing.
	mutable FSimulatedPawnObservation SimulatedReferenceCache;
	// Any | Client / Server | Refresh cached context keys and invalidate observations when they change.
	void RefreshObservationContext() const;
	// Any | Client / Server | Start a new observation epoch and clear role caches without resetting native prediction.
	void ResetObservationContext() const;
};
