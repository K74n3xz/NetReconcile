#include "PawnObservationTypes.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/MovementBaseInterface.h"

FObservedPose FObservedPose::Capture(const UWorld& World, const FVector& LocalPosition, const FRotator& InRotation)
{
	FObservedPose Pose;
	Pose.ZeroOriginPosition = LocalPosition + FVector(World.OriginLocation);
	Pose.Rotation = InRotation;
	return Pose;
}

FVector FObservedPose::GetLocalPosition(const UWorld& World) const
{
	return ZeroOriginPosition - FVector(World.OriginLocation);
}

FString FObservedMoveId::ToString() const
{
	return IsValid() ? FString::Printf(TEXT("%s:%lld"), *PredictionSessionId.ToString(EGuidFormats::Digits), MoveSequence) : TEXT("unavailable");
}

double FAutonomousPawnObservation::GetUnacknowledgedDurationSeconds() const
{
	double Duration = 0.0;
	for (const FObservedSavedMove& Move : UnacknowledgedMoves) { Duration += Move.DeltaTimeSeconds; }
	return Duration;
}

EObservedPawnRole PawnObservation::GetRole(ENetRole Role)
{
	switch (Role)
	{
	case ROLE_Authority: return EObservedPawnRole::Authority;
	case ROLE_AutonomousProxy: return EObservedPawnRole::AutonomousProxy;
	case ROLE_SimulatedProxy: return EObservedPawnRole::SimulatedProxy;
	default: return EObservedPawnRole::Unknown;
	}
}

FLocalObservationTime PawnObservation::LocalTime(const UWorld& World)
{
	FLocalObservationTime Time;
	Time.GameTimeSeconds = World.GetTimeSeconds();
	Time.RealTimeSeconds = FPlatformTime::Seconds();
	return Time;
}

FObservedMotionDifference PawnObservation::Difference(const FObservedMovePair& Pair)
{
	FObservedMotionDifference Result;
	const FObservedMotion& Prediction = Pair.PredictionBeforeResponse.EndState;
	if (!Pair.IsAvailable() || !Prediction.Pose.IsAvailable() || !Pair.ServerState.Pose.IsAvailable()
		|| Prediction.VelocityStatus != EObservationAvailability::Available || Pair.ServerState.VelocityStatus != EObservationAvailability::Available)
	{
		Result.Status = Pair.IsAvailable() ? EObservationAvailability::MissingSource : Pair.Status;
		return Result;
	}
	Result.Status = EObservationAvailability::Available;
	Result.PositionDistanceCm = FVector::Distance(Prediction.Pose.Value.ZeroOriginPosition, Pair.ServerState.Pose.Value.ZeroOriginPosition);
	Result.RotationAngleDegrees = FMath::RadiansToDegrees(Prediction.Pose.Value.Rotation.Quaternion().AngularDistance(Pair.ServerState.Pose.Value.Rotation.Quaternion()));
	Result.VelocityDistanceCmPerSecond = FVector::Distance(Prediction.Velocity, Pair.ServerState.Velocity);
	return Result;
}

FObservedMovementBase PawnObservation::CaptureBase(const FMovementBaseInterfaceData& Base, FName Bone)
{
	FObservedMovementBase Result;
	Result.Status = EObservationAvailability::Available;
	Result.Object = Base.GetMovementBaseObject();
	Result.ObjectPath = GetPathNameSafe(Base.GetMovementBaseObject());
	Result.OwnerPath = GetPathNameSafe(Base.GetMovementBaseObjectOwner());
	if (!Base.GetMovementBaseObject()) { Result.ObjectPath.Empty(); Result.OwnerPath.Empty(); }
	Result.BoneName = Bone;
	return Result;
}

FObservedMotion PawnObservation::SampleMotion(const APawn& Pawn)
{
	FObservedMotion State;
	const UWorld* World = Pawn.GetWorld();
	if (!World) { return State; }
	State.Pose.Status = IsValid(Pawn.GetRootComponent()) ? EObservationAvailability::Available : EObservationAvailability::MissingSource;
	if (State.Pose.IsAvailable()) { State.Pose.Value = FObservedPose::Capture(*World, Pawn.GetActorLocation(), Pawn.GetActorRotation()); }
	State.VelocityStatus = State.Pose.Status;
	State.Velocity = Pawn.GetVelocity();
	State.MovementModeStatus = State.CrouchStatus = State.MovementBase.Status = EObservationAvailability::NotApplicable;
	if (const ACharacter* Character = Cast<ACharacter>(&Pawn))
	{
		State.CrouchStatus = EObservationAvailability::Available;
		State.bCrouched = Character->IsCrouched();
		const UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement();
		State.MovementModeStatus = State.MovementBase.Status = IsValid(MovementComponent) ? EObservationAvailability::Available : EObservationAvailability::MissingSource;
		if (IsValid(MovementComponent))
		{
			State.VelocityStatus = EObservationAvailability::Available;
			State.Velocity = MovementComponent->Velocity;
			State.MovementMode = MovementComponent->MovementMode;
			State.CustomMovementMode = MovementComponent->CustomMovementMode;
			State.GroundMovementMode = MovementComponent->GetGroundMovementMode();
			State.MovementBase = CaptureBase(Character->GetBasedMovement().MovementBaseInterfaceData, Character->GetBasedMovement().BoneName);
		}
		else { State.VelocityStatus = EObservationAvailability::MissingSource; }
	}
	return State;
}

FPawnObservationCommon PawnObservation::SampleCommon(const APawn& Pawn)
{
	FPawnObservationCommon Result;
	const UWorld* World = Pawn.GetWorld();
	if (!IsValid(&Pawn) || !World) { return Result; }
	// Type contract (caller guaranteed): Pawn and World are live UObjects on the game thread.
	// These casts only record weak serial identities; sampling does not mutate either object or extend its lifetime.
	Result.Pawn = const_cast<APawn*>(&Pawn);
	Result.World = const_cast<UWorld*>(World);
	Result.PawnPath = Pawn.GetPathName();
	Result.PawnName = Pawn.GetName();
	Result.WorldPath = World->GetPathName();
	Result.SampleTime = LocalTime(*World);
	Result.bLocallyControlled = Pawn.IsLocallyControlled();
	if (const APlayerState* Player = Pawn.GetPlayerState<APlayerState>(); IsValid(Player)) { Result.OwningPlayerId = Player->GetPlayerId(); }
	Result.Current = SampleMotion(Pawn);
	Result.Input.ConsumedMovementStatus = Pawn.IsLocallyControlled() ? EObservationAvailability::Available : EObservationAvailability::NotApplicable;
	Result.Input.LastConsumedWorldMovementDirection = Pawn.GetLastMovementInputVector();
	Result.Input.JumpRequestStatus = Result.VisualCapsulePose.Status = Result.Smoothing.Status = EObservationAvailability::NotApplicable;
	if (const ACharacter* Character = Cast<ACharacter>(&Pawn))
	{
		Result.Input.JumpRequestStatus = Pawn.IsLocallyControlled() ? EObservationAvailability::Available : EObservationAvailability::NotApplicable;
		Result.Input.bJumpRequestedAtSample = Character->bPressedJump;
		const UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement();
		Result.Smoothing.Status = IsValid(MovementComponent) ? EObservationAvailability::Available : EObservationAvailability::MissingSource;
		if (IsValid(MovementComponent))
		{
			Result.Smoothing.Mode = MovementComponent->NetworkSmoothingMode;
			Result.Smoothing.bComplete = MovementComponent->bNetworkSmoothingComplete;
		}
		const USkeletalMeshComponent* Mesh = Character->GetMesh();
		Result.VisualCapsulePose.Status = IsValid(Mesh) && Mesh->GetSkeletalMeshAsset() ? EObservationAvailability::Available : EObservationAvailability::MissingSource;
		if (Result.VisualCapsulePose.IsAvailable())
		{
			const FQuat VisualRotation = Mesh->GetComponentQuat() * Character->GetBaseRotationOffset().Inverse();
			const FVector VisualPosition = Mesh->GetComponentLocation() - VisualRotation.RotateVector(Character->GetActorScale3D() * Character->GetBaseTranslationOffset());
			Result.VisualCapsulePose.Value = FObservedPose::Capture(*World, VisualPosition, VisualRotation.Rotator());
		}
	}
	return Result;
}
