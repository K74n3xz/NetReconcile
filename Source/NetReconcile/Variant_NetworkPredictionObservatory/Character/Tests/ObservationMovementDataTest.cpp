#if WITH_DEV_AUTOMATION_TESTS
#include "ObservationTestWorld.h"
#include "Variant_NetworkPredictionObservatory/Character/ObservationCharacterMovementComponent.h"
#include "Variant_NetworkPredictionObservatory/Character/ObservationCharacter.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObservationMovementDataTest, "NetReconcile.Observation.MovementData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FObservationMovementDataTest::RunTest(const FString&)
{
	FObservationTestWorld Fixture;
	UClass* CharacterClass = LoadClass<AObservationCharacter>(nullptr, TEXT("/Game/Variant_NetworkPredictionObservatory/Blueprints/BP_ObservationCharacter.BP_ObservationCharacter_C"));
	if (!TestNotNull(TEXT("Existing character Blueprint"), CharacterClass)) { return false; }
	AObservationCharacter* Character = Fixture.World->SpawnActor<AObservationCharacter>(CharacterClass);
	// Type contract (caller guaranteed): the existing observation Blueprint retains AObservationCharacter's custom CMC subobject.
	UObservationCharacterMovementComponent* MovementComponent = CastChecked<UObservationCharacterMovementComponent>(Character->GetCharacterMovement());
	TestFalse(TEXT("Observer does not initialize native prediction data"), MovementComponent->HasPredictionData_Client());
	MovementComponent->CaptureAutonomousObservation();
	TestFalse(TEXT("Read remains non-allocating"), MovementComponent->HasPredictionData_Client());
	TestEqual(TEXT("Base character keeps native CMC"), GetDefault<ANetReconcileCharacter>()->GetCharacterMovement()->GetClass(), UCharacterMovementComponent::StaticClass());
	Fixture.MakeClient();
	Character->SetAutonomousProxy(true); Character->SwapRoles(); MovementComponent->Activate(true);
	// Type contract (caller guaranteed): this custom CMC getter allocates FObservationClientPredictionData.
	auto* ClientData = static_cast<FObservationClientPredictionData*>(MovementComponent->GetPredictionData_Client_Character());
	auto AddMove = [&](float Timestamp, bool bOld, FVector Position)
	{
		ClientData->CurrentTimeStamp = Timestamp;
		FSavedMovePtr Move = ClientData->CreateSavedMove();
		Move->SetMoveFor(Character, 0.02f, FVector(5.0, 6.0, 7.0), *ClientData);
		Character->SetActorLocationAndRotation(Position, FRotator(12.0, 45.0, 18.0));
		MovementComponent->Velocity = FVector(100.0, 20.0, 30.0);
		Move->PostUpdate(Character, FSavedMove_Character::PostUpdate_Record);
		Move->bOldTimeStampBeforeReset = bOld;
		ClientData->SavedMoves.Add(Move);
		return Move;
	};
	const FSavedMovePtr Old = AddMove(239.98f, true, FVector(1.0, 2.0, 3.0));
	const FSavedMovePtr New = AddMove(0.02f, false, FVector(4.0, 5.0, 6.0));
	const FSavedMovePtr Pending = AddMove(0.04f, false, FVector(7.0, 8.0, 9.0));
	ClientData->PendingMove = Pending;
	const FAutonomousPawnObservation Initial = MovementComponent->CaptureAutonomousObservation();
	if (!TestEqual(TEXT("Pending move occurs once"), Initial.UnacknowledgedMoves.Num(), 3)) { return false; }
	TestTrue(TEXT("Identity increases across timestamp reset"), Initial.UnacknowledgedMoves[1].MoveId.MoveSequence > Initial.UnacknowledgedMoves[0].MoveId.MoveSequence);
	TestTrue(TEXT("Native old timestamp marker retained"), Initial.UnacknowledgedMoves[0].bBeforeTimeStampReset);
	TestEqual(TEXT("Crouch is actual end state, not request"), Initial.UnacknowledgedMoves[0].EndState.CrouchStatus, EObservationAvailability::Available);
	TestTrue(TEXT("Prediction rotation retains pitch and roll"), Initial.UnacknowledgedMoves[0].EndState.Pose.Value.Rotation.Equals(FRotator(12.0, 45.0, 18.0)));

	// All three native packed move slots carry the same identity as their respective SavedMove.
	FObservationNetworkMoveDataContainer Send, Receive;
	Send.ClientFillNetworkMoveData(Pending.Get(), New.Get(), Old.Get());
	FBitWriter Writer(16384, true);
	TestTrue(TEXT("Packed move serialization"), Send.Serialize(*MovementComponent, Writer, nullptr));
	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	TestTrue(TEXT("Packed move deserialization"), Receive.Serialize(*MovementComponent, Reader, nullptr));
	for (auto Pair : {TPair<FCharacterNetworkMoveData*, FSavedMovePtr>(Receive.GetNewMoveData(), Pending), {Receive.GetPendingMoveData(), New}, {Receive.GetOldMoveData(), Old}})
	{
		// Type contract (caller guaranteed): Receive owns derived New/Pending/Old slots; AddMove above uses
		// the FObservationSavedMove factory, so both sides of this comparison retain their derived types.
		TestTrue(TEXT("Wire identity matches saved move"), static_cast<FObservationNetworkMoveData*>(Pair.Key)->MoveId.ToObserved() == static_cast<FObservationSavedMove*>(Pair.Value.Get())->MoveId);
	}

	auto ResponseFor = [&](const FSavedMovePtr& Move, int64 Sequence)
	{
		FObservationMoveResponseDataContainer Response;
		Response.ClientAdjustment.bAckGoodMove = true;
		Response.ClientAdjustment.TimeStamp = Move->TimeStamp;
		Response.ServerObservationPayload.bPresent = true;
		// Type contract (caller guaranteed): every ResponseFor caller passes a Move created by AddMove's custom factory.
		Response.ServerObservationPayload.MoveId.FromObserved(static_cast<FObservationSavedMove*>(Move.Get())->MoveId);
		Response.ServerObservationPayload.ServerSampleSequence = Sequence;
		Response.ServerObservationPayload.ServerGameTimeSeconds = 20.0;
		Response.ServerObservationPayload.ZeroOriginPosition = Move->SavedLocation + FVector(10.0, 0.0, 0.0);
		Response.ServerObservationPayload.Velocity = Move->SavedVelocity;
		Response.ServerObservationPayload.Rotation = Move->SavedRotation;
		Response.ServerObservationPayload.PackedMovementMode = MovementComponent->PackNetworkMovementMode();
		Response.ServerObservationPayload.GroundMovementMode = static_cast<uint8>(MovementComponent->GetGroundMovementMode());
		Response.ServerObservationPayload.MovementBasePath = TEXT("/captured/base");
		Response.ServerObservationPayload.MovementBaseBone = TEXT("bone");
		return Response;
	};
	FObservationMoveResponseDataContainer Ack = ResponseFor(Old, 10);
	FBitWriter ResponseWriter(16384, true); Ack.Serialize(*MovementComponent, ResponseWriter, nullptr);
	FObservationMoveResponseDataContainer Decoded;
	FBitReader ResponseReader(ResponseWriter.GetData(), ResponseWriter.GetNumBits());
	TestTrue(TEXT("Response metadata round trip"), Decoded.Serialize(*MovementComponent, ResponseReader, nullptr));
	TestTrue(TEXT("Response identity round trip"), Decoded.ServerObservationPayload.MoveId.ToObserved() == Initial.UnacknowledgedMoves[0].MoveId);
	TestEqual(TEXT("Captured base path survives transport"), Decoded.ServerObservationPayload.MovementBasePath, FString(TEXT("/captured/base")));
	TestEqual(TEXT("Captured base bone survives transport"), Decoded.ServerObservationPayload.MovementBaseBone, FName(TEXT("bone")));
	FObservationResponsePayload CustomState = Decoded.ServerObservationPayload;
	MovementComponent->SetGroundMovementMode(MOVE_NavWalking);
	MovementComponent->SetMovementMode(MOVE_Custom, 17);
	CustomState.PackedMovementMode = MovementComponent->PackNetworkMovementMode();
	CustomState.GroundMovementMode = static_cast<uint8>(MovementComponent->GetGroundMovementMode());
	const FObservedMotion DecodedCustom = CustomState.ToObserved(*MovementComponent);
	TestEqual(TEXT("Packed custom mode retained"), DecodedCustom.CustomMovementMode, uint8(17));
	TestEqual(TEXT("Ground mode is explicit even for custom movement"), DecodedCustom.GroundMovementMode.GetValue(), MOVE_NavWalking);
	MovementComponent->SetMovementMode(MOVE_Walking); MovementComponent->SetGroundMovementMode(MOVE_Walking);
	MovementComponent->ClientHandleMoveResponse(Decoded);
	TestEqual(TEXT("Native ACK prunes old timestamp move"), ClientData->SavedMoves.Num(), 2);
	const FObservedMovePair FirstPair = MovementComponent->GetResponseObservation().LastValidPair;
	TestTrue(TEXT("Native accepted matching response produces pair"), FirstPair.IsAvailable());
	TestEqual(TEXT("Same move position difference"), PawnObservation::Difference(FirstPair).PositionDistanceCm, 10.0);
	TestTrue(TEXT("ACK stores the pre-response end state"), FirstPair.PredictionBeforeResponse.EndState.Pose.Value.ZeroOriginPosition.Equals(FVector(1.0, 2.0, 3.0)));
	MovementComponent->ClientHandleMoveResponse(Decoded);
	TestEqual(TEXT("Duplicate does not increment native counts"), MovementComponent->GetResponseObservation().AcceptedGoodAckCount, int64(1));
	Ack = ResponseFor(New, 11); Ack.ServerObservationPayload.MoveId.MoveSequence += 999;
	MovementComponent->ClientHandleMoveResponse(Ack);
	TestEqual(TEXT("Bad observation ID does not block native ACK"), ClientData->SavedMoves.Num(), 1);
	TestEqual(TEXT("Native accepted count independent of pairing"), MovementComponent->GetResponseObservation().AcceptedGoodAckCount, int64(2));
	TestEqual(TEXT("Wrong ID diagnosed"), MovementComponent->GetResponseObservation().LatestAcceptedResponse.Pairing, EObservedMovePairing::MoveIdMismatch);
	TestEqual(TEXT("Wrong ID retains prior valid pair"), MovementComponent->GetResponseObservation().LastValidPair.ServerSampleSequence, int64(10));
	// Type contract (caller guaranteed): Pending is the live FObservationSavedMove returned by AddMove.
	const FObservedMoveId PendingId = static_cast<FObservationSavedMove*>(Pending.Get())->MoveId;
	Character->SetActorLocation(FVector(42.0, 24.0, 90.0));
	Pending->PostUpdate(Character, FSavedMove_Character::PostUpdate_Replay);
	// Type contract (caller guaranteed): native replay updates the same FObservationSavedMove without changing its type.
	TestTrue(TEXT("Replay retains move identity"), static_cast<FObservationSavedMove*>(Pending.Get())->MoveId == PendingId);
	TestTrue(TEXT("Earlier captures do not change after replay"), Initial.UnacknowledgedMoves[2].EndState.Pose.Value.ZeroOriginPosition.Equals(FVector(7.0, 8.0, 9.0)));
	Old->Clear();
	TestTrue(TEXT("Pool clear cannot mutate paired capture"), FirstPair.PredictionBeforeResponse.MoveId.IsValid());
	Ack = ResponseFor(Pending, 9); MovementComponent->ClientHandleMoveResponse(Ack);
	TestEqual(TEXT("Stale sequence diagnosed without blocking native ACK"), MovementComponent->GetResponseObservation().LatestAcceptedResponse.Pairing, EObservedMovePairing::StaleServerSample);
	TestTrue(TEXT("Available empty native history is explicit"), MovementComponent->CaptureAutonomousObservation().PredictionStatus == EObservationAvailability::Available && ClientData->SavedMoves.IsEmpty());
	const FSavedMovePtr MissingPayload = AddMove(0.06f, false, FVector(20.0, 30.0, 40.0));
	Ack = ResponseFor(MissingPayload, 12);
	Ack.ServerObservationPayload.MovementBasePath = FString::ChrN(1024, TEXT('b'));
	FBitWriter OversizedWriter(16384, true);
	TestTrue(TEXT("Oversized diagnostics preserve native response serialization"), Ack.Serialize(*MovementComponent, OversizedWriter, nullptr));
	TestTrue(TEXT("Oversized payload is omitted rather than overflowing the native RPC"), OversizedWriter.GetNumBits() < 256);
	FObservationMoveResponseDataContainer WithoutPayload;
	FBitReader OversizedReader(OversizedWriter.GetData(), OversizedWriter.GetNumBits());
	TestTrue(TEXT("Native response remains decodable without diagnostics"), WithoutPayload.Serialize(*MovementComponent, OversizedReader, nullptr));
	TestFalse(TEXT("Oversized diagnostics are explicitly absent"), WithoutPayload.ServerObservationPayload.bPresent);
	MovementComponent->ClientHandleMoveResponse(WithoutPayload);
	TestEqual(TEXT("Missing payload still counts the native ACK"), MovementComponent->GetResponseObservation().AcceptedGoodAckCount, int64(4));
	TestEqual(TEXT("Missing payload diagnosed"), MovementComponent->GetResponseObservation().LatestAcceptedResponse.Pairing, EObservedMovePairing::MissingPayload);
	TestEqual(TEXT("Missing payload retains last valid pair"), MovementComponent->GetResponseObservation().LastValidPair.ServerSampleSequence, int64(10));
	const FSavedMovePtr Replayed = AddMove(0.08f, false, FVector(30.0, 40.0, 50.0));
	Character->SetActorLocation(FVector(60.0, 70.0, 80.0));
	Replayed->PostUpdate(Character, FSavedMove_Character::PostUpdate_Replay);
	Ack = ResponseFor(Replayed, 20); MovementComponent->ClientHandleMoveResponse(Ack);
	TestTrue(TEXT("Matching response pairs the latest replayed ending state"), MovementComponent->GetResponseObservation().LastValidPair.PredictionBeforeResponse.EndState.Pose.Value.ZeroOriginPosition.Equals(FVector(60.0, 70.0, 80.0)));
	const FGuid OldSession = PendingId.PredictionSessionId;
	const FGuid Epoch = MovementComponent->GetObservationContextEpoch();
	MovementComponent->ResetPredictionData_Client();
	TestFalse(TEXT("Reset invalidates pair"), MovementComponent->GetResponseObservation().LastValidPair.IsAvailable());
	TestTrue(TEXT("Reset changes observation epoch"), Epoch != MovementComponent->GetObservationContextEpoch());
	TestFalse(TEXT("Read after reset does not allocate"), MovementComponent->HasPredictionData_Client());
	// Type contract (caller guaranteed): after native reset, the same custom CMC recreates FObservationClientPredictionData.
	ClientData = static_cast<FObservationClientPredictionData*>(MovementComponent->GetPredictionData_Client_Character());
	TestTrue(TEXT("Native prediction lifetime has a new session ID"), ClientData->AllocateMoveId().PredictionSessionId != OldSession);
	const FSavedMovePtr Prior = AddMove(0.10f, false, FVector(80.0, 90.0, 100.0));
	const FSavedMovePtr Combined = AddMove(0.12f, false, FVector(90.0, 100.0, 110.0));
	// Type contract (caller guaranteed): Combined comes from AddMove's FObservationSavedMove factory after the reset.
	const FObservedMoveId CombinedId = static_cast<FObservationSavedMove*>(Combined.Get())->MoveId;
	Combined->CombineWith(Prior.Get(), Character, nullptr, Prior->GetRevertedLocation());
	Combined->PostUpdate(Character, FSavedMove_Character::PostUpdate_Record);
	// Type contract (caller guaranteed): native combining keeps the allocated FObservationSavedMove object.
	TestTrue(TEXT("Native combining retains newest move ID"), static_cast<FObservationSavedMove*>(Combined.Get())->MoveId == CombinedId);
	Prior->Clear();
	Prior->SetMoveFor(Character, 0.02f, FVector::ZeroVector, *ClientData);
	// Type contract (caller guaranteed): Prior remains the FObservationSavedMove created by AddMove through clear/reuse.
	TestTrue(TEXT("Pooled reuse receives a fresh monotonically increasing ID"), static_cast<FObservationSavedMove*>(Prior.Get())->MoveId.MoveSequence > CombinedId.MoveSequence);
	return true;
}
#endif
