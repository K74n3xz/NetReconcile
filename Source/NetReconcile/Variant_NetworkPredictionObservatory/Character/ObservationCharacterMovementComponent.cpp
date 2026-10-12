#include "ObservationCharacterMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/BitWriter.h"

void FObservationSavedMove::Clear()
{
	FSavedMove_Character::Clear();
	MoveId = {};
	ObservedEndState = {};
}

void FObservationSavedMove::SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& Accel, FNetworkPredictionData_Client_Character& ClientData)
{
	FSavedMove_Character::SetMoveFor(Character, InDeltaTime, Accel, ClientData);
	// Type contract (caller guaranteed): ClientData is allocated by this CMC's GetPredictionData_Client().
	// Subclasses using these SavedMoves must keep FObservationClientPredictionData as their prediction data type.
	MoveId = static_cast<FObservationClientPredictionData&>(ClientData).AllocateMoveId();
}

void FObservationSavedMove::PostUpdate(ACharacter* Character, EPostUpdateMode Mode)
{
	FSavedMove_Character::PostUpdate(Character, Mode);
	// Canonical coordinates are recorded now; later rebasing, replay or pooled reuse cannot mutate a capture.
	ObservedEndState = PawnObservation::SampleMotion(*Character);
	ObservedEndState.Pose.Value = FObservedPose::Capture(*Character->GetWorld(), SavedLocation, SavedRotation);
	ObservedEndState.Velocity = SavedVelocity;
	ObservedEndState.MovementBase = PawnObservation::CaptureBase(EndMovementBaseInterfaceData, EndBoneName);
}

FObservedSavedMove FObservationSavedMove::CaptureObservation() const
{
	FObservedSavedMove Result;
	Result.MoveId = MoveId;
	Result.NativeTimeStamp = TimeStamp;
	Result.DeltaTimeSeconds = DeltaTime;
	Result.bBeforeTimeStampReset = bOldTimeStampBeforeReset;
	Result.EndState = ObservedEndState;
	Result.Command.Acceleration = Acceleration;
	Result.Command.bJumpRequested = bPressedJump;
	Result.Command.bCrouchRequested = bWantsToCrouch;
	return Result;
}

FObservationClientPredictionData::FObservationClientPredictionData(const UCharacterMovementComponent& MovementComponent)
	: FNetworkPredictionData_Client_Character(MovementComponent), PredictionSessionId(FGuid::NewGuid()) {}

FSavedMovePtr FObservationClientPredictionData::AllocateNewMove() { return MakeShared<FObservationSavedMove>(); }

FObservedMoveId FObservationClientPredictionData::AllocateMoveId()
{
	FObservedMoveId Id;
	Id.PredictionSessionId = PredictionSessionId;
	Id.MoveSequence = ++LastAllocatedMoveSequence;
	return Id;
}

void FObservationNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(Move, Type);
	// Type contract (caller guaranteed): Move comes from FObservationClientPredictionData::AllocateNewMove().
	// A CMC installing this packed Move container must preserve that SavedMove factory.
	MoveId.FromObserved(static_cast<const FObservationSavedMove&>(Move).MoveId);
}

bool FObservationNetworkMoveData::Serialize(UCharacterMovementComponent& MovementComponent, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type)
{
	if (!FCharacterNetworkMoveData::Serialize(MovementComponent, Ar, Map, Type)) { return false; }
	MoveId.Serialize(Ar);
	return !Ar.IsError();
}

FObservationNetworkMoveDataContainer::FObservationNetworkMoveDataContainer()
{
	NewMoveData = &Moves[0]; PendingMoveData = &Moves[1]; OldMoveData = &Moves[2];
}

bool FObservationResponsePayload::IsValid() const
{
	return bPresent && MoveId.ToObserved().IsValid() && ServerSampleSequence > 0 && FMath::IsFinite(ServerGameTimeSeconds)
		&& !ZeroOriginPosition.ContainsNaN() && !Rotation.ContainsNaN() && !Velocity.ContainsNaN();
}

bool FObservationResponsePayload::Serialize(FArchive& Ar, UPackageMap* Map)
{
	Ar.SerializeBits(&bPresent, 1);
	if (!bPresent)
	{
		if (Ar.IsLoading()) { *this = {}; }
		return !Ar.IsError();
	}
	MoveId.Serialize(Ar);
	Ar << ServerSampleSequence;
	Ar << ServerGameTimeSeconds;
	bool bPositionSuccess = true, bVelocitySuccess = true;
	ZeroOriginPosition.NetSerialize(Ar, Map, bPositionSuccess);
	Velocity.NetSerialize(Ar, Map, bVelocitySuccess);
	Ar << Rotation;
	Ar << PackedMovementMode;
	Ar << GroundMovementMode;
	Ar.SerializeBits(&bCrouched, 1);
	Ar << MovementBasePath;
	Ar << MovementBaseOwnerPath;
	FString Bone = Ar.IsSaving() ? MovementBaseBone.ToString() : FString();
	Ar << Bone;
	if (Ar.IsLoading()) { MovementBaseBone = FName(Bone); }
	return bPositionSuccess && bVelocitySuccess && !Ar.IsError();
}

FObservedMotion FObservationResponsePayload::ToObserved(const UCharacterMovementComponent& MovementComponent) const
{
	FObservedMotion State;
	State.Pose.Status = State.VelocityStatus = State.MovementModeStatus = State.CrouchStatus = State.MovementBase.Status = EObservationAvailability::Available;
	State.Pose.Value.ZeroOriginPosition = ZeroOriginPosition;
	State.Pose.Value.Rotation = Rotation;
	State.Velocity = Velocity;
	MovementComponent.UnpackNetworkMovementMode(PackedMovementMode, State.MovementMode, State.CustomMovementMode, State.GroundMovementMode);
	// Type contract (caller guaranteed): GroundMovementMode is a native EMovementMode byte produced by
	// GetGroundMovementMode() on the sender, or an equivalent fixture; callers must preserve that enum domain.
	State.GroundMovementMode = static_cast<EMovementMode>(GroundMovementMode);
	State.bCrouched = bCrouched;
	State.MovementBase.ObjectPath = MovementBasePath;
	State.MovementBase.OwnerPath = MovementBaseOwnerPath;
	State.MovementBase.BoneName = MovementBaseBone;
	return State;
}

void FObservationMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& MovementComponent, const FClientAdjustment& Adjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(MovementComponent, Adjustment);
	// Type contract (caller guaranteed): this container is installed on UObservationCharacterMovementComponent.
	// A subclass replacing native containers must retain the matching CMC/container types and member lifetimes.
	const UObservationCharacterMovementComponent& Component = *CastChecked<UObservationCharacterMovementComponent>(&MovementComponent);
	ServerObservationPayload = Component.PendingResponseNativeTimeStamp == Adjustment.TimeStamp ? Component.PendingServerObservationPayload : FObservationResponsePayload{};
}

bool FObservationMoveResponseDataContainer::Serialize(UCharacterMovementComponent& MovementComponent, FArchive& Ar, UPackageMap* Map)
{
	if (!FCharacterMoveResponseDataContainer::Serialize(MovementComponent, Ar, Map)) { return false; }
	if (Ar.IsLoading()) { return ServerObservationPayload.Serialize(Ar, Map); }
	// Type contract (caller guaranteed): saving uses FBitWriter or a derived archive, as native packed
	// responses (FNetBitWriter) and the FBitWriter fixtures do; arbitrary FArchive writers are unsupported.
	// Diagnostic strings must not overflow the native RPC limit.
	FBitWriter& Writer = static_cast<FBitWriter&>(Ar);
	FBitWriterMark PayloadStart(Writer);
	const bool bSerialized = ServerObservationPayload.Serialize(Ar, Map);
	static const IConsoleVariable* MaxBits = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetPackedMovementMaxBits"));
	if (bSerialized && Writer.GetNumBits() <= MaxBits->GetInt()) { return true; }
	// The payload contains no object references/exports, so only its bits need to be rolled back.
	PayloadStart.Pop(Writer);
	FObservationResponsePayload Omitted;
	return Omitted.Serialize(Ar, Map);
}

UObservationCharacterMovementComponent::UObservationCharacterMovementComponent()
{
	SetNetworkMoveDataContainer(PackedMoveDataContainer);
	SetMoveResponseDataContainer(PackedMoveResponseDataContainer);
}

FNetworkPredictionData_Client* UObservationCharacterMovementComponent::GetPredictionData_Client() const
{
	// Type contract (caller guaranteed): native callers access a mutable, live CMC on the game thread.
	// The const getter follows native lazy initialization; subclasses must keep this prediction data type.
	if (!ClientPredictionData) { const_cast<UObservationCharacterMovementComponent*>(this)->ClientPredictionData = new FObservationClientPredictionData(*this); }
	return ClientPredictionData;
}

void UObservationCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response)
{
	RefreshObservationContext();
	FSavedMovePtr Candidate;
	FObservedSavedMove BeforeResponse;
	if (ClientPredictionData)
	{
		const int32 Index = ClientPredictionData->GetSavedMoveIndex(Response.ClientAdjustment.TimeStamp);
		if (Index != INDEX_NONE)
		{
			Candidate = ClientPredictionData->SavedMoves[Index];
			// Type contract (caller guaranteed): the candidate is an FObservationSavedMove from this CMC's
			// prediction data factory. Copy before native handling can acknowledge, replay or pool the Move.
			if (Candidate) { BeforeResponse = static_cast<const FObservationSavedMove&>(*Candidate).CaptureObservation(); }
		}
	}
	Super::ClientHandleMoveResponse(Response);
	// Only a native accepted response changes counters. A duplicate/ignored ACK cannot create a pair.
	if (!Candidate || !ClientPredictionData || ClientPredictionData->LastAckedMove != Candidate || GetOwnerRole() != ROLE_AutonomousProxy) { return; }
	if (Response.IsCorrection()) { ++AutonomousResponseCache.AcceptedCorrectionCount; }
	else { ++AutonomousResponseCache.AcceptedGoodAckCount; }
	AutonomousResponseCache.NativeResponsesStatus = EObservationAvailability::Available;
	// Type contract (caller guaranteed): Response is the installed FObservationMoveResponseDataContainer,
	// or a fixture of that exact derived type; subclasses replacing the container must preserve this contract.
	const FObservationResponsePayload& Payload = static_cast<const FObservationMoveResponseDataContainer&>(Response).ServerObservationPayload;
	FObservedAcceptedResponse Accepted;
	Accepted.Status = EObservationAvailability::Available;
	Accepted.MoveId = BeforeResponse.MoveId;
	Accepted.NativeTimeStamp = Response.ClientAdjustment.TimeStamp;
	Accepted.Kind = Response.IsCorrection() ? EObservedMoveResponse::Correction : EObservedMoveResponse::GoodAck;
	Accepted.ReceivedAt = PawnObservation::LocalTime(*GetWorld());
	if (!Payload.bPresent) { Accepted.Pairing = EObservedMovePairing::MissingPayload; }
	else if (!Payload.IsValid() || !BeforeResponse.MoveId.IsValid() || !BeforeResponse.EndState.Pose.IsAvailable()) { Accepted.Pairing = EObservedMovePairing::InvalidPayload; }
	else if (Payload.MoveId.ToObserved() != BeforeResponse.MoveId) { Accepted.Pairing = EObservedMovePairing::MoveIdMismatch; }
	else if (Payload.ServerSampleSequence <= AutonomousResponseCache.LastValidPair.ServerSampleSequence) { Accepted.Pairing = EObservedMovePairing::StaleServerSample; }
	else { Accepted.Pairing = EObservedMovePairing::Paired; }
	AutonomousResponseCache.LatestAcceptedResponse = Accepted;
	if (Accepted.Pairing != EObservedMovePairing::Paired) { return; }
	FObservedMovePair Pair;
	Pair.Status = EObservationAvailability::Available;
	Pair.PredictionBeforeResponse = MoveTemp(BeforeResponse);
	Pair.ServerState = Payload.ToObserved(*this);
	Pair.ServerGameTimeSeconds = Payload.ServerGameTimeSeconds;
	Pair.ServerSampleSequence = Payload.ServerSampleSequence;
	Pair.Response = Accepted;
	AutonomousResponseCache.LastValidPair = Pair;
	if (Response.IsCorrection()) { AutonomousResponseCache.LastValidCorrection = MoveTemp(Pair); }
}

void UObservationCharacterMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation)
{
	RefreshObservationContext();
	if (HasValidData() && GetOwnerRole() == ROLE_SimulatedProxy && GetNetMode() == NM_Client)
	{
		// Native decoding has resolved local origin and moving base. Capture before capsule/mesh smoothing.
		SimulatedReferenceCache.LastDecodedReference.Status = EObservationAvailability::Available;
		SimulatedReferenceCache.LastDecodedReference.Value = FObservedPose::Capture(*GetWorld(), NewLocation, NewRotation.Rotator());
		SimulatedReferenceCache.ReferenceReceivedAt = PawnObservation::LocalTime(*GetWorld());
	}
	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
}

void UObservationCharacterMovementComponent::ResetPredictionData_Client()
{
	Super::ResetPredictionData_Client();
	ResetObservationContext();
}

void UObservationCharacterMovementComponent::ResetPredictionData_Server()
{
	Super::ResetPredictionData_Server();
	PendingServerObservationPayload = {};
	// LastServerSampleSequence spans server prediction resets for this component lifetime.
}

const FGuid& UObservationCharacterMovementComponent::GetObservationContextEpoch() const { RefreshObservationContext(); return ObservationContextEpoch; }

FAutonomousPawnObservation UObservationCharacterMovementComponent::CaptureAutonomousObservation() const
{
	RefreshObservationContext();
	FAutonomousPawnObservation Result = AutonomousResponseCache;
	if (GetOwnerRole() != ROLE_AutonomousProxy) { Result.PredictionStatus = Result.NativeResponsesStatus = EObservationAvailability::NotApplicable; return Result; }
	if (!ShouldUsePackedMovementRPCs())
	{
		Result.LatestAcceptedResponse.Status = EObservationAvailability::Unsupported;
		Result.NativeResponsesStatus = EObservationAvailability::Unsupported;
		Result.LatestAcceptedResponse.Pairing = EObservedMovePairing::UnsupportedProtocol;
		Result.LastValidPair = Result.LastValidCorrection = {};
		Result.LastValidPair.Status = Result.LastValidCorrection.Status = EObservationAvailability::Unsupported;
	}
	if (!HasPredictionData_Client()) { return Result; }
	Result.PredictionStatus = EObservationAvailability::Available;
	if (ShouldUsePackedMovementRPCs()) { Result.NativeResponsesStatus = EObservationAvailability::Available; }
	Result.UnacknowledgedMoves.Reserve(ClientPredictionData->SavedMoves.Num());
	// PendingMove is already in SavedMoves. Native order remains authoritative across timestamp resets.
	for (const FSavedMovePtr& Move : ClientPredictionData->SavedMoves)
	{
		// Type contract (caller guaranteed): SavedMoves are owned by this CMC's FObservationClientPredictionData
		// and allocated by its FObservationSavedMove factory, including pending, replayed and pooled Moves.
		if (Move) { Result.UnacknowledgedMoves.Add(static_cast<const FObservationSavedMove&>(*Move).CaptureObservation()); }
	}
	return Result;
}

const FAutonomousPawnObservation& UObservationCharacterMovementComponent::GetResponseObservation() const { RefreshObservationContext(); return AutonomousResponseCache; }

const FSimulatedPawnObservation& UObservationCharacterMovementComponent::GetSimulatedObservation() const { RefreshObservationContext(); return SimulatedReferenceCache; }

void UObservationCharacterMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float InDeltaTime, const FVector& Accel,
	const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	Super::ServerMoveHandleClientError(ClientTimeStamp, InDeltaTime, Accel, RelativeClientLocation, ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
	if (!HasValidData() || !ServerPredictionData || ServerPredictionData->PendingAdjustment.TimeStamp != ClientTimeStamp) { return; }
	PendingResponseNativeTimeStamp = ClientTimeStamp;
	PendingServerObservationPayload = {};
	const FCharacterNetworkMoveData* Move = GetCurrentNetworkMoveData();
	// Legacy RPCs have no observation identity and cannot produce a trustworthy comparison.
	if (!Move) { return; }
	// Type contract (caller guaranteed): the active packed Move points into this CMC's installed
	// FObservationNetworkMoveDataContainer (New/Pending/Old slots); subclasses must retain their derived type.
	const FObservationWireMoveId Id = static_cast<const FObservationNetworkMoveData*>(Move)->MoveId;
	if (!Id.ToObserved().IsValid()) { return; }
	const FObservedMotion State = PawnObservation::SampleMotion(*CharacterOwner);
	PendingServerObservationPayload.bPresent = true;
	PendingServerObservationPayload.MoveId = Id;
	PendingServerObservationPayload.ServerSampleSequence = ++LastServerSampleSequence;
	PendingServerObservationPayload.ServerGameTimeSeconds = GetWorld()->GetTimeSeconds();
	PendingServerObservationPayload.ZeroOriginPosition = State.Pose.Value.ZeroOriginPosition;
	PendingServerObservationPayload.Rotation = State.Pose.Value.Rotation;
	PendingServerObservationPayload.Velocity = State.Velocity;
	PendingServerObservationPayload.PackedMovementMode = PackNetworkMovementMode();
	PendingServerObservationPayload.GroundMovementMode = static_cast<uint8>(GetGroundMovementMode());
	PendingServerObservationPayload.bCrouched = CharacterOwner->IsCrouched();
	PendingServerObservationPayload.MovementBasePath = State.MovementBase.ObjectPath;
	PendingServerObservationPayload.MovementBaseOwnerPath = State.MovementBase.OwnerPath;
	PendingServerObservationPayload.MovementBaseBone = State.MovementBase.BoneName;
}

void UObservationCharacterMovementComponent::RefreshObservationContext() const
{
	AController* Controller = CharacterOwner ? CharacterOwner->GetController() : nullptr;
	const bool bUsesPackedMovementRPCs = ShouldUsePackedMovementRPCs();
	if (!ObservationContextEpoch.IsValid() || CachedLocalRole != GetOwnerRole() || CachedController != Controller || bCachedUsesPackedMovementRPCs != bUsesPackedMovementRPCs)
	{
		ResetObservationContext();
		CachedLocalRole = GetOwnerRole();
		CachedController = Controller;
		bCachedUsesPackedMovementRPCs = bUsesPackedMovementRPCs;
	}
}

void UObservationCharacterMovementComponent::ResetObservationContext() const
{
	ObservationContextEpoch = FGuid::NewGuid();
	AutonomousResponseCache = {};
	SimulatedReferenceCache = {};
}
