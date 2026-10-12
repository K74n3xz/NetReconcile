#include "NetworkFaultHandlerComponent.h"

#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"

FNetworkFaultHandlerComponent::FNetworkFaultHandlerComponent() : HandlerComponent(ComponentName())
{
}

FName FNetworkFaultHandlerComponent::ComponentName()
{
	return TEXT("NetworkFaultSimulation");
}

void FNetworkFaultHandlerComponent::Initialize()
{
#if DO_ENABLE_NET_TEST
	SetActive(Handler->Mode == UE::Handler::Mode::Client);
#endif
	Initialized();
}

bool FNetworkFaultHandlerComponent::SupportsParallelConnectionOutgoing() const
{
	// The server instance never changes packets or state in Outgoing.
	return Handler && Handler->Mode == UE::Handler::Mode::Server;
}

void FNetworkFaultHandlerComponent::Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits)
{
	if (!IsActive() || Packet.GetNumBits() == 0) { return; }
	check(IsInGameThread());
	if (!ScenarioState.bRunning && PendingPackets.IsEmpty()) { return; }
	if (!CanSend()) { StopScenario(false); return; }
	UpdateScenario();
	if (ScenarioState.bFaultWindowActive
		&& (Settings.Scenario == ENetworkFaultScenario::BurstLoss || Settings.Scenario == ENetworkFaultScenario::Blackout))
	{
		++ScenarioState.DroppedPackets;
		Packet.SetError();
		if (Settings.Scenario == ENetworkFaultScenario::BurstLoss)
		{
			--BurstPacketsRemaining;
			UpdateScenario();
		}
		return;
	}

	const int64 Bytes = FMath::DivideAndRoundUp(Packet.GetNumBits(), int64(8));
	double SendTime = TimeSeconds;
	if (ScenarioState.bFaultWindowActive && Settings.Scenario == ENetworkFaultScenario::LatencySpike)
	{
		SendTime += double(Settings.AdditionalDelayMs) / 1000.0;
	}
	if (!PendingPackets.IsEmpty()) { SendTime = FMath::Max(SendTime, PendingPackets.Last().SendTime); }
	if (ScenarioState.bRunning && Settings.Scenario == ENetworkFaultScenario::BandwidthLimit)
	{
		SendTime += double(Bytes) * 8.0 / (double(Settings.BandwidthKbps) * 1000.0);
	}
	if (SendTime <= TimeSeconds && PendingPackets.IsEmpty()) { return; }

	if (PendingPackets.Num() >= 1024 || PendingBytes + Bytes > 1024 * 1024)
	{
		++ScenarioState.DroppedPackets;
		++ScenarioState.OverflowDroppedPackets;
	}
	else
	{
		FQueuedPacket& Queued = PendingPackets.AddDefaulted_GetRef();
		Queued.Packet = FBitWriter(Packet.GetNumBits(), true);
		Queued.Packet.SerializeBits(Packet.GetData(), Packet.GetNumBits());
		Queued.Traits = Traits;
		Queued.SendTime = SendTime;
		PendingBytes += Bytes;
	}
	// Stop later handlers from adding headers to a consumed packet. Outgoing errors skip the socket send.
	Packet.SetError();
}

void FNetworkFaultHandlerComponent::Tick(float DeltaTime)
{
	if (!IsActive()) { return; }
	check(IsInGameThread());
	if (FMath::IsFinite(DeltaTime) && DeltaTime > 0.0f) { TimeSeconds += DeltaTime; }
	if (!ScenarioState.bRunning && PendingPackets.IsEmpty()) { return; }
	if (!CanSend()) { StopScenario(false); return; }
	UpdateScenario();
	while (!PendingPackets.IsEmpty() && PendingPackets[0].SendTime <= TimeSeconds)
	{
		FQueuedPacket Queued = MoveTemp(PendingPackets[0]);
		PendingPackets.RemoveAt(0, 1, EAllowShrinking::No);
		PendingBytes -= Queued.Packet.GetNumBytes();
		Handler->SendHandlerPacket(this, Queued.Packet, Queued.Traits);
		if (!CanSend()) { StopScenario(false); break; }
	}
}

bool FNetworkFaultHandlerComponent::StartScenario(const FNetworkFaultScenarioSettings& InSettings, UNetConnection* InConnection, FText& Error)
{
	check(IsInGameThread());
	if (!IsActive() || !Handler->IsFullyInitialized() || !::IsValid(InConnection)
		|| InConnection->GetConnectionState() != USOCK_Open)
	{
		Error = NSLOCTEXT("Observation", "ScenarioConnectionUnavailable", "Scenarios require an initialized client packet handler and an open connection.");
		return false;
	}
	if (!InSettings.Validate(Error)) { return false; }
	StopScenario(true);
	Connection = InConnection;
	Settings = InSettings;
	ScenarioState = {};
	ScenarioState.Scenario = Settings.Scenario;
	ScenarioState.bRunning = true;
	StartedAt = CycleStartedAt = TimeSeconds;
	BurstPacketsRemaining = Settings.BurstPacketCount;
	UpdateScenario();
	return true;
}

void FNetworkFaultHandlerComponent::StopScenario(bool bSendQueued)
{
	check(IsInGameThread());
	ScenarioState.bRunning = false;
	ScenarioState.bFaultWindowActive = false;
	TArray<FQueuedPacket> Backlog = MoveTemp(PendingPackets);
	PendingBytes = 0;
	if (bSendQueued)
	{
		for (FQueuedPacket& Queued : Backlog)
		{
			if (!CanSend()) { break; }
			Handler->SendHandlerPacket(this, Queued.Packet, Queued.Traits);
		}
	}
}

FNetworkFaultScenarioState FNetworkFaultHandlerComponent::GetScenarioState() const
{
	FNetworkFaultScenarioState Result = ScenarioState;
	Result.PendingPackets = PendingPackets.Num();
	Result.PendingBytes = PendingBytes;
	Result.AdditionalDelayMs = ScenarioState.bFaultWindowActive && Settings.Scenario == ENetworkFaultScenario::LatencySpike
		? Settings.AdditionalDelayMs : 0.0f;
	return Result;
}

bool FNetworkFaultHandlerComponent::CanSend() const
{
	return Connection.IsValid() && Connection->GetConnectionState() == USOCK_Open && Handler && Handler->IsFullyInitialized();
}

void FNetworkFaultHandlerComponent::UpdateScenario()
{
	if (!ScenarioState.bRunning) { return; }
	if (Settings.Scenario == ENetworkFaultScenario::BandwidthLimit)
	{
		ScenarioState.bFaultWindowActive = true;
		return;
	}
	if (Settings.bRepeat)
	{
		const double CycleStart = StartedAt + FMath::FloorToDouble((TimeSeconds - StartedAt) / Settings.RepeatIntervalSeconds) * Settings.RepeatIntervalSeconds;
		if (CycleStart > CycleStartedAt)
		{
			CycleStartedAt = CycleStart;
			BurstPacketsRemaining = Settings.BurstPacketCount;
		}
	}
	ScenarioState.bFaultWindowActive = Settings.Scenario == ENetworkFaultScenario::BurstLoss
		? BurstPacketsRemaining > 0 : TimeSeconds - CycleStartedAt < Settings.DurationSeconds;
	if (!Settings.bRepeat && !ScenarioState.bFaultWindowActive) { ScenarioState.bRunning = false; }
}
