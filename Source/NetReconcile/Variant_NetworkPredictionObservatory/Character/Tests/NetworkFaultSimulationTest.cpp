#if WITH_DEV_AUTOMATION_TESTS

#include "Variant_NetworkPredictionObservatory/Network/NetworkFaultSimulationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/Network/ConnectionObservationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/Network/NetworkFaultHandlerComponent.h"
#include "Variant_NetworkPredictionObservatory/Character/LocalPlayerObservationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/Character/ObservationCharacter.h"
#include "Variant_NetworkPredictionObservatory/ObservationPlayerController.h"
#include "Variant_NetworkPredictionObservatory/UI/CharacterObservationPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if DO_ENABLE_NET_TEST

namespace
{
	struct FNetworkFaultTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		UNetDriver* Driver = nullptr;
		USimulatedClientNetConnection* Connection = nullptr;
		int32 SentPackets = 0;

		inline FNetworkFaultTestWorld()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			UClass* DriverClass = LoadClass<UNetDriver>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
			if (!DriverClass) { return; }
			Driver = NewObject<UNetDriver>(World, DriverClass);
			Driver->SetWorld(World);
			// This fixture has no socket. Keep the world's native tick gates, but skip network I/O.
			World->OnTickDispatch().RemoveAll(Driver);
			World->OnPostTickDispatch().RemoveAll(Driver);
			World->OnTickFlush().RemoveAll(Driver);
			World->OnPostTickFlush().RemoveAll(Driver);
			Connection = NewObject<USimulatedClientNetConnection>(Driver);
			Connection->Driver = Driver;
			Connection->SetConnectionState(USOCK_Open);
			Connection->Handler = MakeUnique<PacketHandler>();
			Connection->Handler->InitializeDelegates(FPacketHandlerLowLevelSendTraits::CreateLambda([this](void*, int32, FOutPacketTraits&) { ++SentPackets; }));
			Connection->Handler->Initialize(UE::Handler::Mode::Client, 65536, false, nullptr, nullptr, TEXT("GameNetDriver"));
			Connection->Handler->InitializeComponents();
			Driver->ServerConnection = Connection;
			World->SetNetDriver(Driver);
		}
		inline ~FNetworkFaultTestWorld()
		{
			FWorldDelegates::OnWorldBeginTearDown.Broadcast(World);
			World->SetNetDriver(nullptr);
			if (Driver) { Driver->ServerConnection = nullptr; Driver->SetWorld(nullptr); }
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		inline void Poll() const
		{
			FWorldDelegates::OnWorldTickStart.Broadcast(World, LEVELTICK_All, 1.0f / 60.0f);
			FWorldDelegates::OnWorldPostActorTick.Broadcast(World, LEVELTICK_All, 1.0f / 60.0f);
			FWorldDelegates::OnWorldTickEnd.Broadcast(World, LEVELTICK_All, 1.0f / 60.0f);
		}
		inline void QueuePacket() const
		{
			uint8 Data[] = {1, 1};
			FOutPacketTraits Traits;
			Connection->Handler->Outgoing(Data, 9, Traits);
		}
	};
	bool EqualSettings(const FPacketSimulationSettings& Left, const FPacketSimulationSettings& Right)
	{
		// Type contract (caller guaranteed): both erased buffers are live FPacketSimulationSettings instances
		// matching this UScriptStruct; neither pointer refers to the Blueprint wrapper.
		return FPacketSimulationSettings::StaticStruct()->CompareScriptStruct(&Left, &Right, 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetworkFaultSimulationTest,
	"NetReconcile.Observation.NetworkFaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNetworkFaultSimulationTest::RunTest(const FString&)
{
	FNetworkFaultTestWorld Fixture;
	if (!TestNotNull(TEXT("Client test Driver"), Fixture.Driver)) { return false; }
	UNetworkFaultSimulationSubsystem* Faults = Fixture.World->GetSubsystem<UNetworkFaultSimulationSubsystem>();
	if (!TestNotNull(TEXT("World fault subsystem"), Faults)) { return false; }
	TestEqual(TEXT("Fixture is a client world"), Fixture.World->GetNetMode(), NM_Client);
	FPacketSimulationSettings Baseline;
	Baseline.PktLag = 23;
	Baseline.PktIncomingLoss = 7;
	Baseline.PktIncomingBufferBloatInMS = 11;
	Fixture.Driver->SetPacketSimulationSettings(Baseline);
	FNetworkFaultSimulationSettings ReadBack;
	FText Error;
	TestTrue(TEXT("Initial Driver settings can be read"), Faults->TryGetSettings(ReadBack, Error));
	TestTrue(TEXT("Initial config is read without being overwritten"), EqualSettings(ReadBack.ToNative(), Baseline));
	TestFalse(TEXT("Initial config is not owned by this subsystem"), Faults->IsSimulationActive());

	FPacketSimulationSettings EveryField;
	int32 FieldCount = 0;
	for (TFieldIterator<FIntProperty> Field(FPacketSimulationSettings::StaticStruct()); Field; ++Field)
	{
		// Type contract (caller guaranteed): Field is an FIntProperty from FPacketSimulationSettings and
		// EveryField is a live instance of that exact reflected container layout.
		Field->SetPropertyValue_InContainer(&EveryField, ++FieldCount);
	}
	TestEqual(TEXT("All 17 native fields are covered"), FieldCount, 17);
	EveryField.ValidateSettings();
	const FNetworkFaultSimulationSettings Wrapped = FNetworkFaultSimulationSettings::FromNative(EveryField);
	TestTrue(TEXT("Blueprint conversion preserves every native field"), EqualSettings(EveryField, Wrapped.ToNative()));
	TestTrue(TEXT("Blueprint API applies every field"), Faults->ApplySettings(Wrapped, Error));
	TestTrue(TEXT("Driver received every parameter"), EqualSettings(EveryField, Fixture.Driver->PacketSimulationSettings));
	TestTrue(TEXT("Connection received every parameter"), EqualSettings(EveryField, Fixture.Connection->PacketSimulationSettings));
	TestTrue(TEXT("Application is active"), Faults->IsSimulationActive());

	FPacketSimulationSettings Invalid;
	for (TFieldIterator<FIntProperty> Field(FPacketSimulationSettings::StaticStruct()); Field; ++Field)
	{
		// Type contract (caller guaranteed): Invalid has the FPacketSimulationSettings layout used by Field's iterator.
		Field->SetPropertyValue_InContainer(&Invalid, -10);
	}
	Invalid.PktLoss = 130;
	Invalid.PktDup = 160;
	Invalid.PktOrder = 3;
	Invalid.PktIncomingLoss = 200;
	Invalid.PktLagMin = 70;
	Invalid.PktLagMax = 10;
	Invalid.PktIncomingLagMin = 90;
	Invalid.PktIncomingLagMax = 20;
	TestTrue(TEXT("Native API validates values"), Faults->ApplyPacketSimulationSettings(Invalid, Error));
	const FPacketSimulationSettings& Normalized = Fixture.Driver->PacketSimulationSettings;
	TestEqual(TEXT("Outgoing loss is bounded"), Normalized.PktLoss, 100);
	TestEqual(TEXT("Duplication is bounded"), Normalized.PktDup, 100);
	TestEqual(TEXT("Reordering is bounded"), Normalized.PktOrder, 1);
	TestEqual(TEXT("Incoming loss is bounded"), Normalized.PktIncomingLoss, 100);
	TestEqual(TEXT("Outgoing range is ordered"), Normalized.PktLagMax, 70);
	TestEqual(TEXT("Incoming range is ordered"), Normalized.PktIncomingLagMax, 90);
	for (TFieldIterator<FIntProperty> Field(FPacketSimulationSettings::StaticStruct()); Field; ++Field)
	{
		// Type contract (caller guaranteed): Normalized is native FPacketSimulationSettings, matching Field's owner.
		TestTrue(*FString::Printf(TEXT("%s is nonnegative"), *Field->GetName()), Field->GetPropertyValue_InContainer(&Normalized) >= 0);
	}
	TestTrue(TEXT("Readback reports normalization"), Faults->TryGetSettings(ReadBack, Error));
	TestTrue(TEXT("Readback equals Driver state"), EqualSettings(ReadBack.ToNative(), Normalized));
	TestTrue(TEXT("Reset succeeds"), Faults->ResetSettings(Error));
	TestTrue(TEXT("Repeated applies retained the first baseline"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	TestTrue(TEXT("Reset reaches the connection"), EqualSettings(Baseline, Fixture.Connection->PacketSimulationSettings));
	TestFalse(TEXT("Reset releases ownership"), Faults->IsSimulationActive());
	TestTrue(TEXT("Repeated reset is a no-op"), Faults->ResetSettings(Error));
	FNetworkFaultScenarioSettings Scenario;
	Scenario.Scenario = ENetworkFaultScenario::LatencySpike;
	FNetworkFaultScenarioState ScenarioState;
	FNetworkFaultScenarioSettings ScenarioReadBack;
	TestTrue(TEXT("Scenario can run without owning base settings"), Faults->StartScenario(Scenario, Error));
	TestTrue(TEXT("Scenario settings are readable"), Faults->TryGetScenarioSettings(ScenarioReadBack, Error));
	TestEqual(TEXT("Scenario readback preserves its configured delay"), ScenarioReadBack.AdditionalDelayMs, Scenario.AdditionalDelayMs);
	TestTrue(TEXT("Read-only scenario calls preserve native settings"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	TestTrue(TEXT("Scenario counts as active simulation"), Faults->IsSimulationActive());
	Fixture.QueuePacket();
	TestTrue(TEXT("Live scenario reports its backlog"), Faults->TryGetScenarioState(ScenarioState, Error) && ScenarioState.PendingPackets == 1);
	FNetworkFaultScenarioSettings InvalidScenario = Scenario;
	InvalidScenario.AdditionalDelayMs = -1.0f;
	TestFalse(TEXT("Invalid scenario cannot replace the live one"), Faults->StartScenario(InvalidScenario, Error));
	TestTrue(TEXT("Invalid request retains the backlog"), Faults->TryGetScenarioState(ScenarioState, Error) && ScenarioState.PendingPackets == 1);
	TestEqual(TEXT("Invalid request does not send queued data"), Fixture.SentPackets, 0);
	TestTrue(TEXT("Base settings can be combined with a scenario"), Faults->ApplySettings(Wrapped, Error));
	TestTrue(TEXT("Applying base settings keeps the scenario running"), Faults->TryGetScenarioState(ScenarioState, Error) && ScenarioState.bRunning);
	TestTrue(TEXT("Restore original stops both kinds of simulation"), Faults->ResetSettings(Error));
	TestFalse(TEXT("Combined reset releases active simulation"), Faults->IsSimulationActive());
	TestEqual(TEXT("Manual reset flushes the backlog"), Fixture.SentPackets, 1);
	TestTrue(TEXT("Combined reset restores the original baseline"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));

	FPacketSimulationSettings Latency;
	Latency.PktLag = 150;
	Latency.PktIncomingLagMin = 40;
	Latency.PktIncomingLagMax = 60;
	TestEqual(TEXT("Nominal latency sums send and receive"), FNetworkFaultSimulationSettings::GetNominalLatencyMs(Latency), 200.0f);
	Latency.PktJitter = 80;
	Latency.PktLagMin = 20;
	TestEqual(TEXT("Jitter takes precedence over fixed lag"), FNetworkFaultSimulationSettings::GetNominalLatencyMs(Latency), 110.0f);
	Latency.PktOrder = 1;
	TestEqual(TEXT("Reordering suppresses outgoing nominal milliseconds"), FNetworkFaultSimulationSettings::GetNominalLatencyMs(Latency), 50.0f);

	Faults->ApplyPacketSimulationSettings(Latency, Error);
	Faults->StartScenario(Scenario, Error);
	Fixture.QueuePacket();
	Fixture.Connection->SetConnectionState(USOCK_Closed);
	int32 ActorTickEvents = 0;
	const FDelegateHandle ActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda(
		[&](UWorld* World, ELevelTick, float) { if (World == Fixture.World) { ++ActorTickEvents; } });
	ON_SCOPE_EXIT { FWorldDelegates::OnWorldPostActorTick.Remove(ActorTickHandle); };
	AddExpectedError(TEXT("UEngine::BroadcastNetworkFailure: FailureType = ConnectionLost"), EAutomationExpectedErrorFlags::Contains, 1);
	Fixture.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
	TestEqual(TEXT("A closed connection suppresses the real actor tick callback"), ActorTickEvents, 0);
	TestFalse(TEXT("Closed connection is unavailable"), Faults->TryGetSettings(ReadBack, Error));
	TestFalse(TEXT("Closed connection releases session configuration"), Faults->IsSimulationActive());
	TestTrue(TEXT("Disconnect restores the original Driver"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	TestFalse(TEXT("Offline configuration is rejected"), Faults->ApplySettings(Wrapped, Error));
	TestFalse(TEXT("Rejection explains why"), Error.IsEmpty());
	TestEqual(TEXT("Disconnect discards rather than flushes queued packets"), Fixture.SentPackets, 1);
	Fixture.Connection->SetConnectionState(USOCK_Open);
	Fixture.Poll();
	TestTrue(TEXT("Reconnect reads its original configuration"), Faults->TryGetSettings(ReadBack, Error));
	TestTrue(TEXT("Offline request was not queued"), EqualSettings(Baseline, ReadBack.ToNative()));
	TestTrue(TEXT("Reconnect has no old scenario backlog"), Faults->TryGetScenarioState(ScenarioState, Error) && !ScenarioState.bRunning && ScenarioState.PendingPackets == 0);
	Fixture.Driver->bNeverApplyNetworkEmulationSettings = true;
	TestFalse(TEXT("Driver policy disables application"), Faults->ApplySettings(Wrapped, Error));
	Fixture.Driver->bNeverApplyNetworkEmulationSettings = false;
	Faults->ApplySettings(Wrapped, Error);
	Faults->StartScenario(Scenario, Error);
	Fixture.QueuePacket();
	USimulatedClientNetConnection* Replacement = NewObject<USimulatedClientNetConnection>(Fixture.Driver);
	Replacement->Driver = Fixture.Driver;
	Replacement->SetConnectionState(USOCK_Open);
	Fixture.Driver->ServerConnection = Replacement;
	APlayerState* Pauser = Fixture.World->SpawnActor<APlayerState>();
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	ActorTickEvents = 0;
	Fixture.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
	TestEqual(TEXT("A paused world suppresses the real actor tick callback"), ActorTickEvents, 0);
	TestFalse(TEXT("Replacing a connection ends the original session"), Faults->IsSimulationActive());
	TestTrue(TEXT("Replacing a connection restores Driver settings"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	TestTrue(TEXT("A new connection receives the baseline"), EqualSettings(Baseline, Replacement->PacketSimulationSettings));
	TestFalse(TEXT("A connection without the configured handler rejects scenarios"), Faults->StartScenario(Scenario, Error));
	TestFalse(TEXT("Missing handler has an explanatory error"), Error.IsEmpty());
	TestTrue(TEXT("Native simulation remains usable without the enhanced handler"), Faults->ApplySettings(Wrapped, Error));
	Faults->ResetSettings(Error);
	TestEqual(TEXT("Replacing a connection discards its old backlog"), Fixture.SentPackets, 1);
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Fixture.Driver->ServerConnection = Fixture.Connection;
	Fixture.Driver->OnPacketSimulationSettingsChanged();
	TestTrue(TEXT("Original connection has no stale packets after replacement"), Faults->TryGetScenarioState(ScenarioState, Error) && ScenarioState.PendingPackets == 0 && !ScenarioState.bRunning);

	Faults->ApplySettings(Wrapped, Error);
	Faults->StartScenario(Scenario, Error);
	Fixture.QueuePacket();
	FWorldDelegates::OnSeamlessTravelStart.Broadcast(Fixture.World, TEXT("NextMap"));
	TestTrue(TEXT("Travel restores the baseline before Driver reuse"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	TestFalse(TEXT("Travel releases the session"), Faults->IsSimulationActive());
	TestTrue(TEXT("Travel discards the enhanced backlog"), Faults->TryGetScenarioState(ScenarioState, Error) && ScenarioState.PendingPackets == 0);
	TestEqual(TEXT("Travel does not flush old-world data"), Fixture.SentPackets, 1);
	Faults->ApplySettings(Wrapped, Error);
	Faults->StartScenario(Scenario, Error);
	Fixture.QueuePacket();
	FWorldDelegates::OnWorldBeginTearDown.Broadcast(Fixture.World);
	TestTrue(TEXT("World teardown restores settings"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	TestTrue(TEXT("World teardown ends the enhanced scenario"), Faults->TryGetScenarioState(ScenarioState, Error) && !ScenarioState.bRunning && ScenarioState.PendingPackets == 0);
	TestEqual(TEXT("World teardown discards queued data"), Fixture.SentPackets, 1);

	Fixture.Driver->ServerConnection = nullptr;
	TestFalse(TEXT("A server has no client configuration entry"), Faults->ApplySettings(Wrapped, Error));
	Fixture.Driver->ServerConnection = Fixture.Connection;
	Faults->ApplySettings(Wrapped, Error);
	Fixture.World->SetNetDriver(nullptr);
	Fixture.Poll();
	TestFalse(TEXT("Removing the game Driver ends the session"), Faults->IsSimulationActive());
	TestTrue(TEXT("The old Driver is restored before release"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	Fixture.World->SetNetDriver(Fixture.Driver);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterObservationPanelTest,
	"NetReconcile.Observation.Panel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCharacterObservationPanelTest::RunTest(const FString&)
{
	FNetworkFaultTestWorld Fixture;
	if (!TestNotNull(TEXT("UI test Driver"), Fixture.Driver)) { return false; }
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	Fixture.World->SetGameInstance(GameInstance);
	UClass* ControllerClass = LoadClass<AObservationPlayerController>(nullptr,
		TEXT("/Game/Variant_NetworkPredictionObservatory/Blueprints/BP_ObservationPlayerController.BP_ObservationPlayerController_C"));
	UClass* PanelClass = UCharacterObservationPanel::StaticClass();
	if (!TestNotNull(TEXT("Observation Controller Blueprint parent"), ControllerClass)
		|| !TestNotNull(TEXT("Native observation panel"), PanelClass)) { return false; }
	AObservationPlayerController* Controller = Fixture.World->SpawnActor<AObservationPlayerController>(ControllerClass);
	ACharacter* Character = Fixture.World->SpawnActor<ACharacter>();
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	Controller->Player = LocalPlayer;
	Controller->SetAsLocalPlayerController();
	LocalPlayer->PlayerController = Controller;
	Controller->Possess(Character);
	Character->SetAutonomousProxy(true);
	Character->SwapRoles();
	LocalPlayer->PlayerAdded(nullptr, 0);
	ON_SCOPE_EXIT { LocalPlayer->PlayerRemoved(); };
	ULocalPlayerObservationSubsystem* Observation = LocalPlayer->GetSubsystem<ULocalPlayerObservationSubsystem>();
	if (!TestNotNull(TEXT("Local observation subsystem"), Observation)) { return false; }
	Fixture.Poll();
	TStrongObjectPtr<UCharacterObservationPanel> Panel(CreateWidget<UCharacterObservationPanel>(Controller, PanelClass));
	if (!TestNotNull(TEXT("Observation panel"), Panel.Get())) { return false; }
	const TSharedRef<SWidget> SlatePanel = Panel->TakeWidget();
	ON_SCOPE_EXIT { Panel->NativeDestruct(); };
	Controller->ObservationPanel = Panel.Get();
	ON_SCOPE_EXIT { Controller->ObservationPanel = nullptr; };
	TestEqual(TEXT("All native fields have editable inputs"), Panel->FaultInputs.Num(), 17);
	TestTrue(TEXT("Real local-player observation binding"), Panel->ObservationSubsystem.Get() == Observation);
	TestTrue(TEXT("Real World fault binding"), Panel->NetworkFaultSubsystem.Get() == Fixture.World->GetSubsystem<UNetworkFaultSimulationSubsystem>());

	auto Value = [&](const TCHAR* Name)
	{
		UTextBlock* Text = Cast<UTextBlock>(Panel->GetWidgetFromName(Name));
		TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Text);
		return Text ? Text->GetText().ToString() : FString();
	};
	FPawnObservationCommon Common = PawnObservation::SampleCommon(*Character);
	Common.Current.Pose.Value.ZeroOriginPosition = FVector(8.0, 9.0, 10.0);
	FAutonomousPawnObservation Auto;
	Auto.PredictionStatus = EObservationAvailability::Available;
	Auto.NativeResponsesStatus = EObservationAvailability::Available;
	Auto.AcceptedGoodAckCount = 7; Auto.AcceptedCorrectionCount = 2;
	FObservedSavedMove OldPrediction;
	OldPrediction.MoveId.PredictionSessionId = FGuid::NewGuid(); OldPrediction.MoveId.MoveSequence = 1;
	OldPrediction.EndState = Common.Current;
	OldPrediction.EndState.Pose.Value.ZeroOriginPosition = FVector(999.0, 0.0, 0.0);
	OldPrediction.NativeTimeStamp = 99.0f; OldPrediction.DeltaTimeSeconds = 0.05f; OldPrediction.bBeforeTimeStampReset = true;
	FObservedSavedMove LatestPrediction = OldPrediction;
	LatestPrediction.MoveId.MoveSequence = 2; LatestPrediction.NativeTimeStamp = 0.2f; LatestPrediction.DeltaTimeSeconds = 0.02f; LatestPrediction.bBeforeTimeStampReset = false;
	LatestPrediction.EndState.Pose.Value.ZeroOriginPosition = FVector(888.0, 0.0, 0.0);
	Auto.UnacknowledgedMoves = {OldPrediction, LatestPrediction};
	Auto.LastValidPair.Status = EObservationAvailability::Available;
	Auto.LastValidPair.PredictionBeforeResponse = OldPrediction;
	Auto.LastValidPair.PredictionBeforeResponse.EndState.Pose.Value.ZeroOriginPosition = FVector(42.0, 0.0, 0.0);
	Auto.LastValidPair.ServerState = Common.Current;
	Auto.LastValidPair.ServerState.Pose.Value.ZeroOriginPosition = FVector(123.0, 456.0, 789.0);
	Auto.LastValidPair.Response.Kind = EObservedMoveResponse::Correction;
	Auto.LastValidCorrection = Auto.LastValidPair;
	const FGuid Epoch = FGuid::NewGuid();
	int64 Sequence = 0;
	auto Publish = [&](FObservedPawnRoleData Data)
	{
		Observation->OnPawnObservationSnapshotUpdated.Broadcast(FPawnObservationSnapshot(FPawnObservationFrame(Common, MoveTemp(Data), Epoch, ++Sequence)));
		Panel->RefreshDisplay();
	};
	auto PublishAuto = [&]() { FObservedPawnRoleData Data; Data.Set<FAutonomousPawnObservation>(Auto); Publish(MoveTemp(Data)); };
	auto MeasureRtt = [&](float Seconds)
	{
		Fixture.Connection->LagCount = 1;
		Fixture.World->GetSubsystem<UConnectionObservationSubsystem>()->BeforeNetFlush(0.016f);
		Fixture.Connection->AvgLag = Seconds; Fixture.Connection->LagCount = 0;
		Fixture.Connection->StatUpdateTime += 1.0;
		FWorldDelegates::OnWorldTickEnd.Broadcast(Fixture.World, LEVELTICK_All, 0.016f);
	};
	MeasureRtt(0.125f);
	PublishAuto();
	TestEqual(TEXT("Authority row reads paired server state"), Value(TEXT("Server_PositionX")), FString(TEXT("123.00")));
	TestEqual(TEXT("Prediction reads matching pre-response state, not newest unacked"), Value(TEXT("Prediction_PositionX")), FString(TEXT("42.00")));
	TestEqual(TEXT("Unacked duration summed across reset"), Value(TEXT("UnackedDuration")), FString(TEXT("70.00")));
	TestEqual(TEXT("Native good ACK count shown"), Value(TEXT("GoodACKs")), FString(TEXT("7")));
	TestEqual(TEXT("Native correction count shown"), Value(TEXT("Corrections")), FString(TEXT("2")));
	TestTrue(TEXT("Last correction diagnostic is retained"), Value(TEXT("LastCorrection")).Contains(OldPrediction.MoveId.ToString()));
	TestNull(TEXT("Unused synthetic frame fields removed"), Panel->GetWidgetFromName(TEXT("ROW_LocalFrame")));
	Panel->SetExpanded(true); Panel->SelectPage(2);
	TestTrue(TEXT("Old timestamp marker displayed"), Panel->MoveTexts[0]->GetText().ToString().Contains(TEXT("BEFORE RESET")));
	TestTrue(TEXT("Saved move rows carry their identity"), Panel->MoveTexts[1]->GetText().ToString().Contains(LatestPrediction.MoveId.ToString()));
	Panel->HandleFreeze();
	Common.Current.Pose.Value.ZeroOriginPosition.X = 88.0;
	Auto.UnacknowledgedMoves.Reset();
	MeasureRtt(0.250f); PublishAuto();
	TestEqual(TEXT("Freeze retains current state"), Value(TEXT("Current_PositionX")), FString(TEXT("8.00")));
	TestEqual(TEXT("Freeze retains SavedMoves"), Panel->MoveRows[1]->GetVisibility(), ESlateVisibility::Visible);
	TestTrue(TEXT("Frozen Pawn view retains live connection measurements"), Value(TEXT("HUD_RTT")).Contains(TEXT("250.0")));
	Panel->HandleFreeze();
	TestEqual(TEXT("Resume displays newest current state"), Value(TEXT("Current_PositionX")), FString(TEXT("88.00")));
	TestEqual(TEXT("Available empty moves collapse rows"), Panel->MoveRows[1]->GetVisibility(), ESlateVisibility::Collapsed);
	Auto.LastValidPair.Response.Kind = EObservedMoveResponse::GoodAck;
	PublishAuto();
	TestTrue(TEXT("Later good ACK does not erase last correction"), Value(TEXT("LastCorrection")).Contains(OldPrediction.MoveId.ToString()));
	Auto.LastValidPair = {}; PublishAuto();
	TestEqual(TEXT("Absent pair clears server values"), Value(TEXT("Server_PositionX")), FString(TEXT("\u2014")));
	TestEqual(TEXT("Absent pair clears matching prediction values"), Value(TEXT("Prediction_PositionX")), FString(TEXT("\u2014")));
	Panel->HandleFreeze();
	Common.Current.Pose.Value.ZeroOriginPosition.X = 99.0; PublishAuto();
	Panel->SetExpanded(false);
	TestFalse(TEXT("Closing releases freeze"), Panel->bFrozen);
	TestEqual(TEXT("Closing displays newest state"), Value(TEXT("Current_PositionX")), FString(TEXT("99.00")));

	FSimulatedPawnObservation Sim;
	Sim.LastDecodedReference.Status = EObservationAvailability::Available;
	Sim.LastDecodedReference.Value.ZeroOriginPosition = FVector(456.0, 234.0, 100.0);
	Sim.LastDecodedReference.Value.Rotation = FRotator(5.0, 18.0, 6.0);
	Sim.ReferenceReceivedAt.GameTimeSeconds = 3.0; Sim.ReferenceReceivedAt.RealTimeSeconds = 30.0;
	Common.SampleTime.GameTimeSeconds = 3.5; Common.SampleTime.RealTimeSeconds = 30.5;
	Common.VisualCapsulePose.Status = EObservationAvailability::Available;
	Common.VisualCapsulePose.Value.ZeroOriginPosition = FVector(12.0, 34.0, 56.0);
	Common.Smoothing.Status = EObservationAvailability::Available; Common.Smoothing.Mode = ENetworkSmoothingMode::Exponential;
	Panel->SetExpanded(true); Panel->SelectPage(2); Panel->HandleFreeze();
	FObservedPawnRoleData SimRole; SimRole.Set<FSimulatedPawnObservation>(Sim); Publish(MoveTemp(SimRole));
	TestFalse(TEXT("Role transition releases frozen snapshot"), Panel->bFrozen);
	TestEqual(TEXT("Simulated role selects State"), Panel->Pages->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Simulated reference position displayed"), Value(TEXT("Server_PositionX")), FString(TEXT("456.00")));
	TestEqual(TEXT("Reference receive game clock displayed"), Value(TEXT("Server_Time")), FString(TEXT("3.00")));
	TestEqual(TEXT("Reference age uses monotonic local clock"), Value(TEXT("ReferenceAge")), FString(TEXT("500.00")));
	TestEqual(TEXT("Visual capsule pose shown separately"), Value(TEXT("Render_PositionX")), FString(TEXT("12.00")));
	TestEqual(TEXT("Simulated role hides Moves"), Panel->TabButtons[2]->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Simulated role hides prediction card"), Panel->Cards[1]->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Simulated role hides ACK stats"), Panel->PredictionStats->GetVisibility(), ESlateVisibility::Collapsed);
	Panel->UpdateLayout(FVector2D(640.0, 480.0));
	TestEqual(TEXT("Resize does not expose reference velocity"), Panel->Values.FindChecked(TEXT("Server_Velocity"))->GetParent()->GetVisibility(), ESlateVisibility::Collapsed);
	FObservedPawnRoleData Authority; Authority.Set<FAuthorityPawnObservation>({}); Publish(MoveTemp(Authority));
	TestEqual(TEXT("Authority uses current state card"), Panel->Cards[2]->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("Authority hides redundant server card"), Panel->Cards[0]->GetVisibility(), ESlateVisibility::Collapsed);
	PublishAuto();
	TestEqual(TEXT("Autonomous restores Moves"), Panel->TabButtons[2]->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("Autonomous restores server velocity"), Panel->Values.FindChecked(TEXT("Server_Velocity"))->GetParent()->GetVisibility(), ESlateVisibility::Visible);

	UClass* RemoteClass = LoadClass<AObservationCharacter>(nullptr,
		TEXT("/Game/Variant_NetworkPredictionObservatory/Blueprints/BP_ObservationCharacter.BP_ObservationCharacter_C"));
	if (!TestNotNull(TEXT("Selectable remote Blueprint"), RemoteClass)) { return false; }
	AObservationCharacter* Remote = Fixture.World->SpawnActor<AObservationCharacter>(RemoteClass);
	Remote->SetAutonomousProxy(false);
	Remote->SwapRoles();
	Panel->RefreshTargets();
	const int32 RemoteIndex = Panel->TargetPawns.IndexOfByPredicate([Remote](const auto& Pawn) { return Pawn.Get() == Remote; });
	Panel->TargetSelector->SetSelectedIndex(RemoteIndex);
	Fixture.Poll();
	TestEqual(TEXT("The real target selector samples the remote Pawn"), Observation->GetObservedPawn(), static_cast<APawn*>(Remote));
	Panel->HandleFreeze();
	Controller->SetPawn(Remote);
	APlayerState* SelectionPauser = Fixture.World->SpawnActor<APlayerState>();
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(SelectionPauser);
	Panel->TargetSelector->SetSelectedIndex(0);
	TestTrue(TEXT("Changing follow mode on the same paused Pawn reuses its valid sample"), Panel->bDisplayingSnapshot);
	TestFalse(TEXT("Changing follow mode releases the frozen sample"), Panel->bFrozen);
	TestTrue(TEXT("The Self option resumes following while paused"), Observation->IsFollowingControlledPawn());
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Controller->SetPawn(Character);
	Remote->Destroy(true);
	Fixture.Poll();
	Panel->SetExpanded(false);

	UNetworkFaultSimulationSubsystem* Faults = Fixture.World->GetSubsystem<UNetworkFaultSimulationSubsystem>();
	const FPacketSimulationSettings Baseline = Fixture.Driver->PacketSimulationSettings;
	FPacketSimulationSettings ExternalSettings = Baseline;
	ExternalSettings.PktLag = 80;
	FText FaultError;
	TestTrue(TEXT("C++ settings apply while the panel is bound"), Faults->ApplyPacketSimulationSettings(ExternalSettings, FaultError));
	TestEqual(TEXT("Untouched inputs follow C++ settings"), Panel->FaultInputs.FindChecked(TEXT("PktLag"))->GetText().ToString(), FString(TEXT("80")));
	Panel->FaultInputs.FindChecked(TEXT("PktLag"))->SetText(FText::FromString(TEXT("150")));
	// UMG's SetText does not broadcast the user-edit event; simulate that event as well.
	Panel->FaultInputs.FindChecked(TEXT("PktLag"))->OnTextChanged.Broadcast(FText::FromString(TEXT("150")));
	ExternalSettings.PktLag = 90;
	TestTrue(TEXT("External changes can arrive during editing"), Faults->ApplyPacketSimulationSettings(ExternalSettings, FaultError));
	TestEqual(TEXT("External updates preserve an unsubmitted draft"), Panel->FaultInputs.FindChecked(TEXT("PktLag"))->GetText().ToString(), FString(TEXT("150")));
	Fixture.Poll();
	Panel->RefreshDisplay();
	TestEqual(TEXT("Snapshot updates preserve the draft"), Panel->FaultInputs.FindChecked(TEXT("PktLag"))->GetText().ToString(), FString(TEXT("150")));
	TestEqual(TEXT("Applied value remains visible beside the draft"), Value(TEXT("PktLag_Applied")), FString(TEXT("90")));
	TestTrue(TEXT("Dirty draft is visibly marked"), Value(TEXT("BaseDraft")).Contains(TEXT("UNAPPLIED")));
	Panel->RevertButton->OnClicked.Broadcast();
	TestEqual(TEXT("Revert reads current applied settings"), Panel->FaultInputs.FindChecked(TEXT("PktLag"))->GetText().ToString(), FString(TEXT("90")));
	Panel->FaultInputs.FindChecked(TEXT("PktLag"))->SetText(FText::FromString(TEXT("150")));
	Panel->FaultInputs.FindChecked(TEXT("PktLag"))->OnTextChanged.Broadcast(FText::FromString(TEXT("150")));
	TestTrue(TEXT("Editing does not apply settings"), EqualSettings(ExternalSettings, Fixture.Driver->PacketSimulationSettings));
	Panel->ApplyButton->OnClicked.Broadcast();
	TestEqual(TEXT("Apply button updates the real Driver"), Fixture.Driver->PacketSimulationSettings.PktLag, 150);
	TestTrue(TEXT("Applied UI configuration is active"), Faults->IsSimulationActive());
	for (const TCHAR* Invalid : {TEXT(""), TEXT("-1"), TEXT("1.5"), TEXT("100ms"), TEXT("2147483648")})
	{
		Panel->FaultInputs.FindChecked(TEXT("PktLag"))->SetText(FText::FromString(Invalid));
		Panel->ApplyButton->OnClicked.Broadcast();
		TestEqual(TEXT("Invalid input does not mutate settings"), Fixture.Driver->PacketSimulationSettings.PktLag, 150);
		TestFalse(TEXT("Invalid input has a visible explanation"), Panel->FaultMessage->GetText().IsEmpty());
	}
	Panel->ResetButton->OnClicked.Broadcast();
	TestTrue(TEXT("Reset button restores the baseline"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	TestEqual(TEXT("Reset reads actual restored parameters"), Panel->FaultInputs.FindChecked(TEXT("PktLag"))->GetText().ToString(), FString::FromInt(Baseline.PktLag));
	TestFalse(TEXT("Reset leaves no active override"), Faults->IsSimulationActive());
	TestEqual(TEXT("Scenario selector exposes the four outgoing scenarios"), Panel->ScenarioSelector->GetOptionCount(), 4);
	for (FName Counter : {FName(TEXT("GoodACKs")), FName(TEXT("Corrections"))})
	{
		// Type contract (caller guaranteed): AddValue builds the heading UTextBlock as child zero of the value row.
		const UTextBlock* Heading = CastChecked<UTextBlock>(Panel->Values.FindChecked(Counter)->GetParent()->GetChildAt(0));
		const FString ExpectedHeading = Counter == TEXT("GoodACKs") ? TEXT("Accepted good ACKs") : TEXT("Accepted corrections");
		TestEqual(TEXT("Read-only counter labels describe accepted native responses"), Heading->GetText().ToString(), ExpectedHeading);
	}
	Panel->ScenarioSelector->SetSelectedIndex(2);
	TestEqual(TEXT("Spike exposes its duration"), Panel->ScenarioInputs.FindChecked(TEXT("Duration"))->GetParent()->GetParent()->GetParent()->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("Spike hides burst-only parameters"), Panel->ScenarioInputs.FindChecked(TEXT("BurstPacketCount"))->GetParent()->GetParent()->GetParent()->GetVisibility(), ESlateVisibility::Collapsed);
	Panel->ScenarioInputs.FindChecked(TEXT("AdditionalDelay"))->SetText(FText::FromString(TEXT("100")));
	Panel->ScenarioStartButton->OnClicked.Broadcast();
	FNetworkFaultScenarioState ScenarioState;
	FNetworkFaultScenarioSettings ScenarioSettings;
	TestTrue(TEXT("Scenario button starts the real handler"), Faults->TryGetScenarioState(ScenarioState, FaultError) && ScenarioState.bRunning);
	TestTrue(TEXT("Scenario starts independently of base settings"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	Panel->ScenarioInputs.FindChecked(TEXT("AdditionalDelay"))->SetText(FText::FromString(TEXT("150")));
	Panel->ScenarioInputs.FindChecked(TEXT("AdditionalDelay"))->OnTextChanged.Broadcast(FText::FromString(TEXT("150")));
	ScenarioSettings.Scenario = ENetworkFaultScenario::LatencySpike;
	ScenarioSettings.AdditionalDelayMs = 50.0f;
	Faults->StartScenario(ScenarioSettings, FaultError);
	TestEqual(TEXT("External scenario changes preserve an unsubmitted draft"), Panel->ScenarioInputs.FindChecked(TEXT("AdditionalDelay"))->GetText().ToString(), FString(TEXT("150")));
	Panel->RefreshScenarioState();
	TestTrue(TEXT("Scenario status reports the live spike separately"), Panel->ScenarioStatus->GetText().ToString().Contains(TEXT("50.0 ms")));
	for (const TCHAR* Invalid : {TEXT(""), TEXT("-1"), TEXT("nan"), TEXT("inf"), TEXT("100ms")})
	{
		Panel->ScenarioInputs.FindChecked(TEXT("AdditionalDelay"))->SetText(FText::FromString(Invalid));
		Panel->ScenarioStartButton->OnClicked.Broadcast();
		Faults->TryGetScenarioSettings(ScenarioSettings, FaultError);
		TestEqual(TEXT("Invalid scenario input preserves the running configuration"), ScenarioSettings.AdditionalDelayMs, 50.0f);
		TestFalse(TEXT("Invalid scenario input has an explanation"), Panel->ScenarioMessage->GetText().IsEmpty());
	}
	Panel->ScenarioStopButton->OnClicked.Broadcast();
	Faults->TryGetScenarioState(ScenarioState, FaultError);
	TestFalse(TEXT("Stop button ends the scenario"), ScenarioState.bRunning);
	TestTrue(TEXT("Stop leaves base settings alone"), EqualSettings(Baseline, Fixture.Driver->PacketSimulationSettings));
	Panel->ScenarioSelector->SetSelectedIndex(0);
	Panel->ScenarioRepeat->SetIsChecked(true);
	Panel->HandleScenarioRepeatChanged(true);
	Panel->ScenarioInputs.FindChecked(TEXT("BurstPacketCount"))->SetText(FText::FromString(TEXT("3")));
	Panel->ScenarioStartButton->OnClicked.Broadcast();
	Faults->TryGetScenarioSettings(ScenarioSettings, FaultError);
	TestTrue(TEXT("UI supports repeated bursts"), ScenarioSettings.bRepeat && ScenarioSettings.BurstPacketCount == 3);
	Panel->ResetButton->OnClicked.Broadcast();
	TestFalse(TEXT("Restore original also stops an enhanced scenario"), Faults->IsSimulationActive());
	Panel->SetExpanded(true);
	TestEqual(TEXT("Expanded details initially pass pointer input through"), Panel->DetailSize->GetVisibility(), ESlateVisibility::HitTestInvisible);
	Panel->SetExpanded(false);
	TestEqual(TEXT("Playing collapses details"), Panel->DetailSize->GetVisibility(), ESlateVisibility::Collapsed);
	Controller->ToggleObservationPanelExpansion();
	TestTrue(TEXT("F2 expands details without interrupting gameplay"), Controller->IsObservationPanelExpanded() && !Controller->bShowMouseCursor
		&& !Controller->IsObservationUIInputEnabled() && !Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored());
	Panel->SelectPage(1);
	Panel->FaultInputs.FindChecked(TEXT("PktLag"))->SetText(FText::FromString(TEXT("123")));
	Panel->HandleFreeze();
	Controller->SetIgnoreMoveInput(true);
	Controller->SetObservationUIInput(true);
	Controller->SetObservationUIInput(true);
	TestTrue(TEXT("Editing shows the cursor and blocks character input"), Controller->IsObservationUIInputEnabled()
		&& Controller->bShowMouseCursor && Controller->IsMoveInputIgnored() && Controller->IsLookInputIgnored());
	TestEqual(TEXT("Editing allows child controls to receive pointer input"), Panel->DetailSize->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	Controller->SetObservationUIInput(false);
	Controller->SetObservationUIInput(false);
	TestTrue(TEXT("Leaving editing retains the expanded frozen snapshot and draft"), Controller->IsObservationPanelExpanded() && Panel->bFrozen
		&& Panel->FaultInputs.FindChecked(TEXT("PktLag"))->GetText().ToString() == TEXT("123") && Panel->Pages->GetActiveWidgetIndex() == 1);
	TestTrue(TEXT("Leaving editing balances only the inspector's own movement block"), Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored());
	Controller->SetIgnoreMoveInput(false);
	Controller->SetObservationUIInput(true);
	Controller->ToggleObservationPanelExpansion();
	TestFalse(TEXT("F2 returns to gameplay"), Controller->IsObservationPanelExpanded() || Controller->bShowMouseCursor
		|| Controller->IsObservationUIInputEnabled() || Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored() || Panel->bFrozen);

	APlayerState* Pauser = Fixture.World->SpawnActor<APlayerState>();
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	Controller->ToggleObservationPanelExpansion();
	Controller->SetObservationUIInput(true);
	FSlateApplication::Get().OnApplicationActivationStateChanged().Broadcast(false);
	TestFalse(TEXT("Application deactivation releases UI input even without a widget tick"), Controller->IsObservationUIInputEnabled() || Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored());
	Controller->SetObservationUIInput(true);
	Panel->NativeTick(FGeometry(), 0.0f);
	TestFalse(TEXT("Paused-world Slate tick releases UI input without an owning viewport focus"), Controller->IsObservationUIInputEnabled() || Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored());
	Controller->ToggleObservationPanelExpansion();
	const FString Frozen = Value(TEXT("Current_PositionX"));
	Character->SetActorLocation(FVector(900.0, 800.0, 700.0));
	Fixture.Poll();
	Panel->RefreshDisplay();
	TestEqual(TEXT("Paused observation values freeze"), Value(TEXT("Current_PositionX")), Frozen);
	FNetworkFaultScenarioSettings PausedScenario;
	PausedScenario.Scenario = ENetworkFaultScenario::Blackout;
	PausedScenario.DurationSeconds = 0.1f;
	TestTrue(TEXT("A scenario can start in a paused world"), Faults->StartScenario(PausedScenario, FaultError));
	TestTrue(TEXT("The bound panel receives the initial active event"), Panel->ScenarioStatus->GetText().ToString().Contains(TEXT("Active")));
	const auto PausedHandler = StaticCastSharedPtr<FNetworkFaultHandlerComponent>(
		Fixture.Connection->Handler->GetComponentByName(FNetworkFaultHandlerComponent::ComponentName()));
	// Advance the isolated handler's network clock; the real world tick must publish its transition.
	PausedHandler->Tick(0.2f);
	TestTrue(TEXT("The off-screen panel does not poll the completed scenario"), Panel->ScenarioStatus->GetText().ToString().Contains(TEXT("Active")));
	Fixture.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
	TestTrue(TEXT("A real paused tick publishes the scenario's stopped event"), Panel->ScenarioStatus->GetText().ToString().Contains(TEXT("Stopped")));
	Faults->ResetSettings(FaultError);
	MeasureRtt(0.350f);
	Panel->RefreshDisplay();
	TestTrue(TEXT("Connection metrics update while world sampling is paused"), Value(TEXT("HUD_RTT")).Contains(TEXT("350.0")));
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Panel->SetExpanded(true);
	Panel->HandleFreeze();
	TestTrue(TEXT("Snapshot can be frozen independently of world pause"), Panel->bFrozen);
	Controller->UnPossess();
	Fixture.Poll();
	Panel->RefreshDisplay();
	TestEqual(TEXT("Invalid target clears presentation"), Value(TEXT("Current_PositionX")), FString(TEXT("\u2014")));
	TestFalse(TEXT("Losing a target releases its frozen snapshot"), Panel->bFrozen);
	Controller->Possess(Character);
	Fixture.Poll();
	Panel->RefreshDisplay();
	TestEqual(TEXT("A new target snapshot restores values"), Value(TEXT("Current_PositionX")), FString(TEXT("900.00")));
	TUniquePtr<PacketHandler> ConfiguredHandler = MoveTemp(Fixture.Connection->Handler);
	Panel->HandleFaultStateChanged();
	TestTrue(TEXT("Missing enhanced handler leaves native Apply enabled"), Panel->ApplyButton->GetIsEnabled());
	TestFalse(TEXT("Missing enhanced handler disables scenario start"), Panel->ScenarioStartButton->GetIsEnabled());
	TestTrue(TEXT("Missing enhanced handler is explained in the panel"), Panel->ScenarioStatus->GetText().ToString().Contains(TEXT("packet handler profile")));
	Fixture.Connection->Handler = MoveTemp(ConfiguredHandler);
	return true;
}

#endif
#endif
