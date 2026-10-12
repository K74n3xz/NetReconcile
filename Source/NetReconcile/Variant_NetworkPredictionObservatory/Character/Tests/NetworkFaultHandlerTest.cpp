#if WITH_DEV_AUTOMATION_TESTS

#include "Variant_NetworkPredictionObservatory/Network/NetworkFaultHandlerComponent.h"

#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

#if DO_ENABLE_NET_TEST

namespace
{
	/** A later protocol handler deliberately adds a bit even to empty packets. */
	class FTestProtocolHandler : public HandlerComponent
	{
	public:
		inline FTestProtocolHandler() : HandlerComponent(TEXT("FaultTestProtocol")) {}
		virtual inline void Initialize() override { SetActive(true); Initialized(); }
		virtual inline bool IsValid() const override { return true; }
		virtual inline int32 GetReservedPacketBits() const override { return 1; }
		virtual inline void Outgoing(FBitWriter& Packet, FOutPacketTraits&) override { ++Calls; Packet.WriteBit(1); }
		int32 Calls = 0;
	};
	struct FCapturedPacket
	{
		TArray<uint8> Data;
		int32 Bits;
		FOutPacketTraits Traits;
	};
	struct FHandlerFixture
	{
		TStrongObjectPtr<USimulatedClientNetConnection> Connection{NewObject<USimulatedClientNetConnection>()};
		TSharedPtr<FNetworkFaultHandlerComponent> Fault;
		TSharedPtr<FTestProtocolHandler> Protocol = MakeShared<FTestProtocolHandler>();
		TArray<FCapturedPacket> Sent;

		explicit inline FHandlerFixture(UE::Handler::Mode Mode = UE::Handler::Mode::Client)
		{
			Connection->SetConnectionState(USOCK_Open);
			Connection->Handler = MakeUnique<PacketHandler>();
			Connection->Handler->InitializeDelegates(FPacketHandlerLowLevelSendTraits::CreateLambda(
				[this](void* Data, int32 Bits, FOutPacketTraits& Traits) { Capture(Data, Bits, Traits); }));
			// Exercise the actual factory and project profile, not a manually substituted component.
			Connection->Handler->Initialize(Mode, 8 * 1024 * 1024, false, nullptr, nullptr, TEXT("GameNetDriver"));
			Fault = StaticCastSharedPtr<FNetworkFaultHandlerComponent>(Connection->Handler->GetComponentByName(FNetworkFaultHandlerComponent::ComponentName()));
			TSharedPtr<HandlerComponent> Tail = Protocol;
			Connection->Handler->AddHandler(Tail, true);
			Connection->Handler->InitializeComponents();
		}
		inline ~FHandlerFixture()
		{
			if (Fault) { Fault->StopScenario(false); }
			// No driver or channels are owned here. Retire the fixture before a later PIE startup performs GC.
			Connection->SetConnectionState(USOCK_Closed);
			Connection->ClientLoginState = EClientLoginState::CleanedUp;
			Connection->Handler.Reset();
		}
		inline void Capture(void* Data, int32 Bits, FOutPacketTraits& Traits)
		{
			Sent.Add({TArray<uint8>(static_cast<uint8*>(Data), FMath::DivideAndRoundUp(Bits, 8)), Bits, Traits});
		}
		inline void Send(uint8 Id, int32 Bits = 9)
		{
			TArray<uint8> Data;
			Data.SetNumZeroed(FMath::DivideAndRoundUp(Bits, 8));
			Data[0] = Id;
			if (Bits > 8) { Data[1] = 1; }
			FOutPacketTraits Traits;
			Traits.bAllowCompression = false;
			Traits.bIsKeepAlive = true;
			Traits.NumAckBits = 3;
			Traits.NumBunchBits = 7;
			const ProcessedPacket Result = Connection->Handler->Outgoing(Data.GetData(), Bits, Traits);
			if (!Result.bError && Result.CountBits > 0) { Capture(Result.Data, Result.CountBits, Traits); }
		}
		inline void Tick(float Seconds) { Connection->Handler->Tick(Seconds); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetworkFaultHandlerTest,
	"NetReconcile.Observation.HandlerScenarios", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNetworkFaultHandlerTest::RunTest(const FString&)
{
	FHandlerFixture Fixture;
	if (!TestNotNull(TEXT("GameNetDriver profile creates the fault component"), Fixture.Fault.Get())) { return false; }
	TestTrue(TEXT("Client component initializes without a handshake"), Fixture.Fault->IsInitialized() && Fixture.Connection->Handler->IsFullyInitialized());
	TestEqual(TEXT("Fault component reserves no protocol bits"), Fixture.Fault->GetReservedPacketBits(), 0);
	Fixture.Send(1);
	TestEqual(TEXT("Default configuration passes packets through"), Fixture.Sent.Num(), 1);
	FText Error;
	FNetworkFaultScenarioSettings Settings;
	Settings.BurstPacketCount = 2;
	TestTrue(TEXT("Start a single burst"), Fixture.Fault->StartScenario(Settings, Fixture.Connection.Get(), Error));
	Fixture.Send(2);
	Fixture.Send(3);
	TestEqual(TEXT("Burst suppresses real sends, including later protocol headers"), Fixture.Sent.Num(), 1);
	TestEqual(TEXT("Consumed packets do not enter the later handler"), Fixture.Protocol->Calls, 1);
	TestEqual(TEXT("Exactly the requested packets are dropped"), Fixture.Fault->GetScenarioState().DroppedPackets, int64(2));
	TestFalse(TEXT("Single burst ends after its last packet"), Fixture.Fault->GetScenarioState().bRunning);
	Fixture.Send(4);
	TestEqual(TEXT("Traffic resumes after the burst"), Fixture.Sent.Num(), 2);
	Settings.bRepeat = true;
	Settings.RepeatIntervalSeconds = 1.0f;
	Fixture.Fault->StartScenario(Settings, Fixture.Connection.Get(), Error);
	Fixture.Send(5);
	Fixture.Send(6);
	Fixture.Send(7);
	TestTrue(TEXT("Completed repeating burst waits for the next cycle"), Fixture.Fault->GetScenarioState().bRunning && !Fixture.Fault->GetScenarioState().bFaultWindowActive);
	Fixture.Tick(3.0f);
	Fixture.Send(8);
	Fixture.Send(9);
	Fixture.Send(10);
	TestEqual(TEXT("Skipped cycles do not accumulate packet drops"), Fixture.Fault->GetScenarioState().DroppedPackets, int64(4));

	Settings = {};
	Settings.Scenario = ENetworkFaultScenario::Blackout;
	Settings.DurationSeconds = 0.5f;
	Fixture.Fault->StartScenario(Settings, Fixture.Connection.Get(), Error);
	const int32 BeforeBlackout = Fixture.Sent.Num();
	Fixture.Send(11);
	Fixture.Tick(0.25f);
	Fixture.Send(12);
	TestEqual(TEXT("Blackout drops all packets in its window"), Fixture.Sent.Num(), BeforeBlackout);
	Fixture.Tick(0.25f);
	Fixture.Send(13);
	TestEqual(TEXT("Blackout recovers at its duration boundary"), Fixture.Sent.Num(), BeforeBlackout + 1);
	Settings.bRepeat = true;
	Settings.RepeatIntervalSeconds = 1.0f;
	Fixture.Fault->StartScenario(Settings, Fixture.Connection.Get(), Error);
	Fixture.Tick(0.5f);
	TestFalse(TEXT("Repeating blackout has a healthy gap"), Fixture.Fault->GetScenarioState().bFaultWindowActive);
	Fixture.Tick(0.5f);
	TestTrue(TEXT("Repeating blackout enters the next window"), Fixture.Fault->GetScenarioState().bFaultWindowActive);

	Settings = {};
	Settings.Scenario = ENetworkFaultScenario::LatencySpike;
	Settings.DurationSeconds = 0.125f;
	Settings.AdditionalDelayMs = 250.0f;
	Fixture.Fault->StartScenario(Settings, Fixture.Connection.Get(), Error);
	const int32 BeforeSpike = Fixture.Sent.Num();
	Fixture.Send(14);
	Fixture.Tick(0.125f);
	Fixture.Send(15);
	TestFalse(TEXT("Single spike ends its fault window automatically"), Fixture.Fault->GetScenarioState().bRunning);
	TestEqual(TEXT("Healthy traffic cannot overtake the spike backlog"), Fixture.Sent.Num(), BeforeSpike);
	TestEqual(TEXT("Both packets are queued in order"), Fixture.Fault->GetScenarioState().PendingPackets, 2);
	Fixture.Tick(0.125f);
	if (!TestEqual(TEXT("Backlog sends at its due time"), Fixture.Sent.Num(), BeforeSpike + 2)) { return false; }
	TestEqual(TEXT("First queued packet retains its identity"), Fixture.Sent[BeforeSpike].Data[0], uint8(14));
	TestEqual(TEXT("Next packet remains behind it"), Fixture.Sent[BeforeSpike + 1].Data[0], uint8(15));
	const FCapturedPacket& Packet = Fixture.Sent[BeforeSpike];
	TestEqual(TEXT("Non-byte-aligned payload retains its exact bit count plus protocol bits"), Packet.Bits, 11);
	TestEqual(TEXT("Payload, protocol and termination bits survive"), uint8(Packet.Data[1] & 7), uint8(7));
	TestTrue(TEXT("Queued packet retains all traits"), !Packet.Traits.bAllowCompression && Packet.Traits.bIsKeepAlive
		&& Packet.Traits.NumAckBits == 3 && Packet.Traits.NumBunchBits == 7);
	TestEqual(TEXT("Drained backlog releases its byte accounting"), Fixture.Fault->GetScenarioState().PendingBytes, int64(0));
	Settings.bRepeat = true;
	Settings.RepeatIntervalSeconds = 0.5f;
	Fixture.Fault->StartScenario(Settings, Fixture.Connection.Get(), Error);
	Fixture.Tick(0.125f);
	Fixture.Send(16);
	TestEqual(TEXT("Spike gap sends new packets normally when there is no backlog"), Fixture.Sent.Last().Data[0], uint8(16));
	Fixture.Tick(0.375f);
	Fixture.Send(17);
	TestEqual(TEXT("Repeated spike buffers the next cycle"), Fixture.Fault->GetScenarioState().PendingPackets, 1);

	Settings = {};
	Settings.Scenario = ENetworkFaultScenario::BandwidthLimit;
	Settings.BandwidthKbps = 1.0f;
	Fixture.Fault->StartScenario(Settings, Fixture.Connection.Get(), Error);
	const int32 BeforeLimit = Fixture.Sent.Num();
	Fixture.Send(18, 1000);
	Fixture.Send(19, 1000);
	Fixture.Tick(0.5f);
	TestEqual(TEXT("Whole packets wait for their serialization budget"), Fixture.Sent.Num(), BeforeLimit);
	Fixture.Tick(0.5f);
	TestEqual(TEXT("One kilobit sends in the first second"), Fixture.Sent.Num(), BeforeLimit + 1);
	Fixture.Tick(1.0f);
	TestEqual(TEXT("Rate limits the next packet to the next second"), Fixture.Sent.Num(), BeforeLimit + 2);
	TestTrue(TEXT("Bandwidth limit remains active after the backlog drains"), Fixture.Fault->GetScenarioState().bRunning);
	Fixture.Send(20);
	Fixture.Send(21);
	const int32 BeforeStop = Fixture.Sent.Num();
	Fixture.Fault->StopScenario(true);
	TestEqual(TEXT("Manual stop immediately sends the ordered backlog"), Fixture.Sent.Num(), BeforeStop + 2);
	TestEqual(TEXT("Manual stop preserves the first backlog packet"), Fixture.Sent[BeforeStop].Data[0], uint8(20));
	TestEqual(TEXT("Manual stop preserves the next backlog packet"), Fixture.Sent[BeforeStop + 1].Data[0], uint8(21));
	Fixture.Fault->StopScenario(true);
	TestEqual(TEXT("Stopping twice does not resend packets"), Fixture.Sent.Num(), BeforeStop + 2);

	Fixture.Fault->StartScenario(Settings, Fixture.Connection.Get(), Error);
	for (int32 Index = 0; Index < 1025; ++Index) { Fixture.Send(22); }
	TestEqual(TEXT("Packet-count bound holds"), Fixture.Fault->GetScenarioState().PendingPackets, 1024);
	TestEqual(TEXT("Queue overflow is observable"), Fixture.Fault->GetScenarioState().OverflowDroppedPackets, int64(1));
	Fixture.Fault->StopScenario(false);
	Fixture.Fault->StartScenario(Settings, Fixture.Connection.Get(), Error);
	for (int32 Index = 0; Index < 17; ++Index) { Fixture.Send(23, 65536 * 8); }
	TestEqual(TEXT("Byte bound holds independently of the packet-count bound"), Fixture.Fault->GetScenarioState().PendingBytes, int64(1024 * 1024));
	TestEqual(TEXT("Byte overflow drops the new packet"), Fixture.Fault->GetScenarioState().OverflowDroppedPackets, int64(1));
	FNetworkFaultScenarioSettings Invalid = Settings;
	Invalid.BandwidthKbps = 0.0f;
	TestFalse(TEXT("Invalid configuration is rejected"), Fixture.Fault->StartScenario(Invalid, Fixture.Connection.Get(), Error));
	TestEqual(TEXT("Rejected configuration does not flush the backlog"), Fixture.Fault->GetScenarioState().PendingPackets, 16);
	TestTrue(TEXT("Rejected configuration leaves the scenario running"), Fixture.Fault->GetScenarioState().bRunning);
	Fixture.Connection->SetConnectionState(USOCK_Closed);
	const int32 BeforeClose = Fixture.Sent.Num();
	Fixture.Tick(10.0f);
	TestEqual(TEXT("Closed connection discards old packets"), Fixture.Sent.Num(), BeforeClose);
	TestEqual(TEXT("Closed connection releases its backlog"), Fixture.Fault->GetScenarioState().PendingPackets, 0);
	TestFalse(TEXT("Closed connection ends the scenario"), Fixture.Fault->GetScenarioState().bRunning);

	FHandlerFixture Server(UE::Handler::Mode::Server);
	if (!TestNotNull(TEXT("Server also loads the profile"), Server.Fault.Get())) { return false; }
	TestFalse(TEXT("Server fault instance stays inactive"), Server.Fault->IsActive());
	TestTrue(TEXT("Inactive server instance supports parallel outgoing connections"), Server.Fault->SupportsParallelConnectionOutgoing());
	TestFalse(TEXT("Server cannot start a client scenario"), Server.Fault->StartScenario(Settings, Server.Connection.Get(), Error));
	Server.Send(24);
	TestEqual(TEXT("Server traffic passes through"), Server.Sent.Num(), 1);

	Invalid = {};
	Invalid.Scenario = ENetworkFaultScenario::LatencySpike;
	Invalid.AdditionalDelayMs = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Non-finite delay is rejected"), Invalid.Validate(Error));
	Invalid.AdditionalDelayMs = 1.0f;
	Invalid.bRepeat = true;
	Invalid.RepeatIntervalSeconds = Invalid.DurationSeconds;
	TestFalse(TEXT("Overlapping repeated windows are rejected"), Invalid.Validate(Error));
	Invalid.Scenario = ENetworkFaultScenario::BurstLoss;
	Invalid.DurationSeconds = -1.0f;
	Invalid.AdditionalDelayMs = -1.0f;
	TestTrue(TEXT("Unused settings do not reject a burst"), Invalid.Validate(Error));
	return true;
}

#endif
#endif
