#if WITH_DEV_AUTOMATION_TESTS

#include "Variant_NetworkPredictionObservatory/Character/LocalPlayerObservationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/Character/ObservationCharacter.h"
#include "Variant_NetworkPredictionObservatory/Character/ObservationCharacterMovementComponent.h"
#include "Variant_NetworkPredictionObservatory/Network/NetworkFaultSimulationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/Network/ConnectionObservationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/ObservationPlayerController.h"
#include "Variant_NetworkPredictionObservatory/UI/CharacterObservationPanel.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#if DO_ENABLE_NET_TEST

/** Run in a game client connected to a server using the observation character. */
class FObservationNetworkCommand : public IAutomationLatentCommand
{
public:
	explicit inline FObservationNetworkCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual inline ~FObservationNetworkCommand() override
	{
		if (NetworkFaultSubsystem.IsValid()) { FText Error; NetworkFaultSubsystem->ResetSettings(Error); }
	}

	virtual inline bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - StartedAt > 90.0)
		{
			Test->AddError(FString::Printf(TEXT("Timed out waiting for network observation (stage %d)"), Stage));
			return true;
		}
		AObservationCharacter* Character = nullptr;
		ULocalPlayerObservationSubsystem* Subsystem = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld() || World->GetNetMode() != NM_Client) { continue; }
			APlayerController* Controller = World->GetFirstPlayerController();
			ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
			Character = Controller ? Cast<AObservationCharacter>(Controller->GetPawn()) : nullptr;
			Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<ULocalPlayerObservationSubsystem>() : nullptr;
			if (Character && Subsystem) { break; }
		}
		if (!Character || !Subsystem || !Subsystem->GetPawnObservationSnapshot().IsValid()) { return false; }
		UObservationCharacterMovementComponent* MovementComponent = Cast<UObservationCharacterMovementComponent>(Character->GetCharacterMovement());
		if (!Test->TestNotNull(TEXT("Network character uses the observation component"), MovementComponent)) { return true; }
		if (!NetDriver.IsValid())
		{
			UNetDriver* Driver = Character->GetWorld()->GetNetDriver();
			NetDriver = Driver;
			OriginalPacketSettings = Driver->PacketSimulationSettings;
			FPacketSimulationSettings LaggedSettings = OriginalPacketSettings;
			LaggedSettings.PktLag = 150;
			LaggedSettings.PktIncomingLagMin = 50;
			LaggedSettings.PktIncomingLagMax = 50;
			NetworkFaultSubsystem = Character->GetWorld()->GetSubsystem<UNetworkFaultSimulationSubsystem>();
			FText Error;
			if (!Test->TestTrue(TEXT("Runtime fault API applies bidirectional latency"),
				NetworkFaultSubsystem.IsValid() && NetworkFaultSubsystem->ApplyPacketSimulationSettings(LaggedSettings, Error))) { return true; }
			ConnectedSettings = Driver->PacketSimulationSettings;
		}
		// Input is consumed by the regular character tick; the test never constructs network moves.
		Character->AddMovementInput(FVector::ForwardVector, 0.25f);
		const FPawnObservationSnapshot SnapshotHandle = Subsystem->GetPawnObservationSnapshot();
		if (!SnapshotHandle.Get()->GetAutonomous()) { return false; }
		const FAutonomousPawnObservation& Snapshot = *SnapshotHandle.Get()->GetAutonomous();
		const FPawnObservationCommon& Common = SnapshotHandle.Get()->GetCommon();
		const FConnectionObservation& Network = Character->GetWorld()->GetSubsystem<UConnectionObservationSubsystem>()->GetConnectionObservation();
		if (Stage == 6) { return VerifyHandlerScenario(*Character, *MovementComponent, Snapshot, Now); }
		if (Stage == 0)
		{
			if (MovementComponent->GetResponseObservation().AcceptedGoodAckCount == 0 || !Snapshot.LastValidPair.IsAvailable() || Snapshot.UnacknowledgedMoves.IsEmpty()
				|| !Network.RoundTripTimeMs.IsAvailable() || FNetworkFaultSimulationSettings::GetNominalLatencyMs(Network.PacketSettings.ToNative()) != 200.0f) { return false; }
			Test->TestEqual(TEXT("Client retains target validity"), SnapshotHandle.GetRole(), EObservedPawnRole::AutonomousProxy);
			Test->TestTrue(TEXT("Good ACK carries its move timestamp"), Snapshot.LastValidPair.PredictionBeforeResponse.NativeTimeStamp > 0.0f);
			Test->TestTrue(TEXT("Network metrics are available"), (Network.Status == EObservationAvailability::Available));
			Test->TestTrue(TEXT("Autonomous client exposes initialized prediction data"), (Snapshot.PredictionStatus == EObservationAvailability::Available));
			Test->TestTrue(TEXT("Autonomous client exposes accepted response counts"), (Snapshot.PredictionStatus == EObservationAvailability::Available));
			Test->TestEqual(TEXT("Snapshot reports accepted good ACK count"), Snapshot.AcceptedGoodAckCount, MovementComponent->GetResponseObservation().AcceptedGoodAckCount);
			Test->TestEqual(TEXT("Snapshot reports configured nominal latency"), FNetworkFaultSimulationSettings::GetNominalLatencyMs(Network.PacketSettings.ToNative()), 200.0f);
			AObservationPlayerController* ObservationController = Cast<AObservationPlayerController>(Character->GetController());
			if (!Test->TestNotNull(TEXT("Observation controller is active"), ObservationController)
				|| !Test->TestNotNull(TEXT("Panel is created for the real client"), ObservationController->GetObservationPanel())) { return true; }
			if (!FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
			{
				const FGeometry Screen = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(ObservationController);
				const FGeometry Panel = ObservationController->GetObservationPanel()->GetCachedGeometry();
				const FVector2D TopLeft = Screen.AbsoluteToLocal(Panel.GetAbsolutePosition());
				const FVector2D BottomRight = Screen.AbsoluteToLocal(Panel.LocalToAbsolute(Panel.GetLocalSize()));
				Test->TestTrue(TEXT("Rendered panel has visible area"), Panel.GetLocalSize().X > 0.0 && Panel.GetLocalSize().Y > 0.0);
				Test->TestTrue(TEXT("Panel stays within the player's viewport"), TopLeft.X >= -1.0 && TopLeft.Y >= -1.0
					&& BottomRight.X <= Screen.GetLocalSize().X + 1.0 && BottomRight.Y <= Screen.GetLocalSize().Y + 1.0);
			}
			ObservationController->ToggleObservationPanelExpansion();
			Test->TestTrue(TEXT("F2 expands details while preserving gameplay input"), ObservationController->IsObservationPanelExpanded()
				&& !ObservationController->IsObservationUIInputEnabled() && !ObservationController->bShowMouseCursor
				&& !ObservationController->IsMoveInputIgnored() && !ObservationController->IsLookInputIgnored());
			ObservationController->ToggleObservationPanelExpansion();
			Test->TestFalse(TEXT("F2 action restores character input"), ObservationController->IsMoveInputIgnored() || ObservationController->IsLookInputIgnored());
			CheckNativeHistory(*MovementComponent, Snapshot);
			CheckGhosts(*Character, Snapshot);
			BeforeCorrection = SnapshotHandle;
			CorrectionsBefore = MovementComponent->GetResponseObservation().AcceptedCorrectionCount;
			Character->SetActorLocation(Character->GetActorLocation() + FVector(0.0, 300.0, 0.0),
				false, nullptr, ETeleportType::TeleportPhysics);
			Stage = 1;
			return false;
		}
		if (Stage == 1)
		{
			if (MovementComponent->GetResponseObservation().AcceptedCorrectionCount <= CorrectionsBefore || Snapshot.UnacknowledgedMoves.IsEmpty()) { return false; }
			// The first world frame after the correction has replayed the remaining SavedMoves.
			CheckNativeHistory(*MovementComponent, Snapshot);
			CheckGhosts(*Character, Snapshot);
			Test->TestTrue(TEXT("Real native correction carries a valid same-move pair"), Snapshot.LastValidCorrection.IsAvailable()
				&& Snapshot.LastValidCorrection.Response.Kind == EObservedMoveResponse::Correction);
			Test->TestTrue(TEXT("Correction captures the deliberately divergent prediction before adjustment"), PawnObservation::Difference(Snapshot.LastValidCorrection).PositionDistanceCm > 200.0);
			Test->TestTrue(TEXT("Correction advances actual authoritative state"),
				Snapshot.LastValidPair.ServerSampleSequence > BeforeCorrection.Get()->GetAutonomous()->LastValidPair.ServerSampleSequence);
			Test->TestTrue(TEXT("Native replay removes the deliberate client position error"),
				FMath::Abs(Snapshot.UnacknowledgedMoves.Last().EndState.Pose.Value.ZeroOriginPosition.Y - Snapshot.LastValidPair.ServerState.Pose.Value.ZeroOriginPosition.Y) < 100.0);
			Test->TestTrue(TEXT("Presentation follows the corrected client simulation"),
				Common.Current.Pose.Value.GetLocalPosition(*Character->GetWorld()).Equals(Character->GetActorLocation(), 0.01));
			Test->TestTrue(TEXT("Prediction remains available after native correction/replay"),
				Snapshot.UnacknowledgedMoves.Num() > 0);
			CorrectionSequence = Snapshot.LastValidPair.ServerSampleSequence;
			CorrectedAt = Now;
			if (FParse::Param(FCommandLine::Get(), TEXT("ObservationGhostScreenshot")))
			{
				// Type contract (caller guaranteed): the observation map's game mode uses AObservationPlayerController for this owning client.
				CastChecked<AObservationPlayerController>(Character->GetController())->ToggleObservationPanelExpansion();
				bScreenshotPanelExpanded = true;
				ScreenshotPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
					/ FString::Printf(TEXT("ObservationGhostCorrection-%s.png"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
				FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
			}
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			if (bScreenshotPanelExpanded && IFileManager::Get().FileExists(*ScreenshotPath))
			{
				// Type contract (caller guaranteed): the observation map's game mode uses AObservationPlayerController for this owning client.
				CastChecked<AObservationPlayerController>(Character->GetController())->ToggleObservationPanelExpansion();
				bScreenshotPanelExpanded = false;
			}
			if (Now - CorrectedAt < 1.0 || Snapshot.LastValidPair.ServerSampleSequence <= CorrectionSequence) { return false; }
			if (!ScreenshotPath.IsEmpty() && !IFileManager::Get().FileExists(*ScreenshotPath)) { return false; }
			CheckNativeHistory(*MovementComponent, Snapshot);
			CheckGhosts(*Character, Snapshot);
			Test->TestTrue(TEXT("A prior snapshot remains a stable value copy"),
				BeforeCorrection.Get()->GetAutonomous()->LastValidPair.ServerSampleSequence < Snapshot.LastValidPair.ServerSampleSequence);
			Test->TestTrue(TEXT("Later responses retain the last valid correction diagnostic"), Snapshot.LastValidCorrection.IsAvailable()
				&& Snapshot.LastValidCorrection.Response.Kind == EObservedMoveResponse::Correction);
			FPacketSimulationSettings Blocked = ConnectedSettings;
			Blocked.PktIncomingLoss = 100;
			FText Error;
			if (!Test->TestTrue(TEXT("Runtime API applies complete incoming loss"), NetworkFaultSubsystem->ApplyPacketSimulationSettings(Blocked, Error))) { return true; }
			LossStartedAt = Now;
			Stage = 3;
			return false;
		}
		if (Stage == 3)
		{
			// Drain packets that were queued before the blackout before asserting that ACKs stop.
			if (Now - LossStartedAt < 0.3) { return false; }
			BlockedSequence = Snapshot.LastValidPair.ServerSampleSequence;
			BlockedGoodMoves = MovementComponent->GetResponseObservation().AcceptedGoodAckCount;
			LossStartedAt = Now;
			Stage = 4;
			return false;
		}
		if (Stage == 4)
		{
			if (Now - LossStartedAt < 0.5) { return false; }
			Test->TestEqual(TEXT("100% incoming loss stops server state updates"), Snapshot.LastValidPair.ServerSampleSequence, BlockedSequence);
			Test->TestEqual(TEXT("100% incoming loss stops good ACKs"), MovementComponent->GetResponseObservation().AcceptedGoodAckCount, BlockedGoodMoves);
			FText Error;
			if (!Test->TestTrue(TEXT("Runtime reset restores the original network configuration"), NetworkFaultSubsystem->ResetSettings(Error))) { return true; }
			// Type contract (caller guaranteed): both erased buffers are live native FPacketSimulationSettings,
			// matching the compared UScriptStruct layout.
			Test->TestTrue(TEXT("Driver baseline survives multiple applications"),
				FPacketSimulationSettings::StaticStruct()->CompareScriptStruct(&OriginalPacketSettings, &NetDriver->PacketSimulationSettings, 0));
			Stage = 5;
			return false;
		}
		if (Snapshot.LastValidPair.ServerSampleSequence <= BlockedSequence || MovementComponent->GetResponseObservation().AcceptedGoodAckCount <= BlockedGoodMoves) { return false; }
		CheckNativeHistory(*MovementComponent, Snapshot);
		CheckGhosts(*Character, Snapshot);
		Test->TestFalse(TEXT("Restored connection has no session override"), NetworkFaultSubsystem->IsSimulationActive());
		Test->TestTrue(TEXT("ACKs and prediction resume after packet loss"), MovementComponent->GetResponseObservation().AcceptedGoodAckCount > BlockedGoodMoves);
		Test->AddInfo(FString::Printf(TEXT("Received %lld good ACKs and %lld corrections; %d unacknowledged moves remain"),
			MovementComponent->GetResponseObservation().AcceptedGoodAckCount, MovementComponent->GetResponseObservation().AcceptedCorrectionCount, Snapshot.UnacknowledgedMoves.Num()));
		if (!ScreenshotPath.IsEmpty()) { Test->AddInfo(FString::Printf(TEXT("Ghost correction screenshot: %s"), *ScreenshotPath)); }
		Stage = 6;
		return false;
	}

private:
	inline bool VerifyHandlerScenario(AObservationCharacter& Character, UObservationCharacterMovementComponent& MovementComponent,
		const FAutonomousPawnObservation& Snapshot, double Now)
	{
		const ENetworkFaultScenario Scenarios[] = {ENetworkFaultScenario::BurstLoss, ENetworkFaultScenario::Blackout,
			ENetworkFaultScenario::LatencySpike, ENetworkFaultScenario::BandwidthLimit};
		if (ScenarioIndex >= UE_ARRAY_COUNT(Scenarios)) { return true; }
		FText Error;
		if (ScenarioStage == 0)
		{
			FNetworkFaultScenarioSettings Settings;
			Settings.Scenario = Scenarios[ScenarioIndex];
			Settings.BurstPacketCount = 3;
			Settings.DurationSeconds = 0.75f;
			Settings.BandwidthKbps = 1.0f;
			if (!Test->TestTrue(TEXT("Real client starts its configured HandlerComponent scenario"), NetworkFaultSubsystem->StartScenario(Settings, Error)))
			{
				Test->AddError(Error.ToString());
				return true;
			}
			ScenarioStartedAt = Now;
			bObservedScenarioEffect = false;
			bObservedPredictionBacklog = false;
			ScenarioStage = 1;
			return false;
		}
		if (ScenarioStage == 1)
		{
			FNetworkFaultScenarioState State;
			if (!Test->TestTrue(TEXT("Real client exposes live handler state"), NetworkFaultSubsystem->TryGetScenarioState(State, Error))) { return true; }
			bObservedScenarioEffect |= State.DroppedPackets > 0 || State.PendingPackets > 0;
			bObservedPredictionBacklog |= bObservedScenarioEffect && !Snapshot.UnacknowledgedMoves.IsEmpty()
				&& Snapshot.UnacknowledgedMoves.Last().MoveId.MoveSequence > Snapshot.LastValidPair.PredictionBeforeResponse.MoveId.MoveSequence;
			if (Now - ScenarioStartedAt < 1.0) { return false; }
			Test->TestTrue(TEXT("Scenario affects real outgoing packets"), bObservedScenarioEffect);
			Test->TestTrue(TEXT("Scenario retains unacknowledged client prediction"), bObservedPredictionBacklog);
			CheckNativeHistory(MovementComponent, Snapshot);
			CheckGhosts(Character, Snapshot);
			GoodMovesBeforeScenarioStop = MovementComponent.GetResponseObservation().AcceptedGoodAckCount;
			if (!Test->TestTrue(TEXT("Real scenario stops and sends its backlog"), NetworkFaultSubsystem->StopScenario(Error))) { return true; }
			NetworkFaultSubsystem->TryGetScenarioState(State, Error);
			Test->TestTrue(TEXT("Stop clears runtime scenario state and its backlog"), !State.bRunning && State.PendingPackets == 0 && State.PendingBytes == 0);
			// Type contract (caller guaranteed): OriginalPacketSettings and the driver's settings share this native struct layout.
			Test->TestTrue(TEXT("Enhanced scenario leaves native baseline untouched"),
				FPacketSimulationSettings::StaticStruct()->CompareScriptStruct(&OriginalPacketSettings, &NetDriver->PacketSimulationSettings, 0));
			ScenarioStage = 2;
			return false;
		}
		if (MovementComponent.GetResponseObservation().AcceptedGoodAckCount <= GoodMovesBeforeScenarioStop) { return false; }
		CheckNativeHistory(MovementComponent, Snapshot);
		CheckGhosts(Character, Snapshot);
		Test->TestFalse(TEXT("Recovered scenario leaves no active override"), NetworkFaultSubsystem->IsSimulationActive());
		Test->AddInfo(FString::Printf(TEXT("Handler scenario %s affected traffic, preserved native history and ghosts, then resumed ACKs."),
			*StaticEnum<ENetworkFaultScenario>()->GetNameStringByValue(static_cast<int64>(Scenarios[ScenarioIndex]))));
		++ScenarioIndex;
		ScenarioStage = 0;
		return false;
	}

	inline void CheckGhosts(const AObservationCharacter& Character, const FAutonomousPawnObservation& Snapshot)
	{
		if (!Snapshot.LastValidPair.IsAvailable()) { return; }
		auto CheckGhost = [&](const USkeletalMeshComponent& Ghost, const FObservedPose& Pose)
		{
			Test->TestTrue(TEXT("Available paired state has a visible ghost"), Ghost.IsVisible());
			const FTransform Transform(Pose.Rotation, Pose.GetLocalPosition(*Character.GetWorld()), Character.GetActorScale3D());
			Test->TestTrue(TEXT("Network ghost follows paired state position"), Ghost.GetComponentLocation().Equals(Transform.TransformPosition(Character.GetBaseTranslationOffset()), 0.01));
			Test->TestTrue(TEXT("Network ghost follows full paired rotation"), Ghost.GetComponentQuat().Equals(Transform.GetRotation() * Character.GetBaseRotationOffset(), 0.001));
		};
		CheckGhost(*Character.GetServerReferenceGhostMesh(), Snapshot.LastValidPair.ServerState.Pose.Value);
		CheckGhost(*Character.GetLocalStateGhostMesh(), Snapshot.LastValidPair.PredictionBeforeResponse.EndState.Pose.Value);
	}

	inline void CheckNativeHistory(const UObservationCharacterMovementComponent& MovementComponent, const FAutonomousPawnObservation& Snapshot)
	{
		const FNetworkPredictionData_Client_Character* ClientData = MovementComponent.GetPredictionData_Client_Character();
		if (!Test->TestEqual(TEXT("Snapshot history has exactly the native SavedMoves"), Snapshot.UnacknowledgedMoves.Num(), ClientData->SavedMoves.Num())) { return; }
		for (int32 Index = 0; Index < Snapshot.UnacknowledgedMoves.Num(); ++Index)
		{
			const FSavedMove_Character& Move = *ClientData->SavedMoves[Index];
			const FObservedSavedMove& State = Snapshot.UnacknowledgedMoves[Index];
			Test->TestEqual(TEXT("Native timestamp order preserved"), State.NativeTimeStamp, Move.TimeStamp);
			Test->TestTrue(TEXT("Copied end position follows native replay"), State.EndState.Pose.Value.GetLocalPosition(*MovementComponent.GetWorld()).Equals(Move.SavedLocation, 0.01));
			Test->TestTrue(TEXT("Copied ending velocity follows native buffer"), State.EndState.Velocity.Equals(Move.SavedVelocity, 0.01));
			// Type contract (caller guaranteed): the custom CMC's SavedMoves come from FObservationClientPredictionData's
			// FObservationSavedMove factory; native buffering/replay does not replace their derived type.
			Test->TestTrue(TEXT("Packed identity matches native metadata"), State.MoveId == static_cast<const FObservationSavedMove&>(Move).MoveId);
		}
		// Type contract (caller guaranteed): LastAckedMove is retained from that same custom SavedMove factory.
		Test->TestTrue(TEXT("Pair identity matches last native acknowledgement"), ClientData->LastAckedMove.IsValid()
			&& static_cast<const FObservationSavedMove&>(*ClientData->LastAckedMove).MoveId == Snapshot.LastValidPair.PredictionBeforeResponse.MoveId);
	}

	FAutomationTestBase* Test;
	double StartedAt = FPlatformTime::Seconds();
	double CorrectedAt = 0.0;
	int32 Stage = 0;
	int64 CorrectionsBefore = 0;
	int64 CorrectionSequence = 0;
	FPawnObservationSnapshot BeforeCorrection;
	FString ScreenshotPath;
	bool bScreenshotPanelExpanded = false;
	TWeakObjectPtr<UNetDriver> NetDriver;
	FPacketSimulationSettings OriginalPacketSettings;
	FPacketSimulationSettings ConnectedSettings;
	TWeakObjectPtr<UNetworkFaultSimulationSubsystem> NetworkFaultSubsystem;
	double LossStartedAt = 0.0;
	int64 BlockedSequence = 0;
	int64 BlockedGoodMoves = 0;
	int32 ScenarioIndex = 0;
	int32 ScenarioStage = 0;
	double ScenarioStartedAt = 0.0;
	int64 GoodMovesBeforeScenarioStop = 0;
	bool bObservedScenarioEffect = false;
	bool bObservedPredictionBacklog = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObservationNetworkTest,
	"NetReconcile.Observation.Network",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FObservationNetworkTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FObservationNetworkCommand>(this));
	return true;
}

#endif

#endif
