#include "ConnectionObservationSubsystem.h"

#include "NetworkFaultSimulationSubsystem.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"

namespace
{
	void RecordMetric(FObservedConnectionMetric& Metric, double Value, int64 Samples, double Start, double End)
	{
		if (Samples <= 0) { return; }
		Metric.Status = EObservationAvailability::Available;
		Metric.Quality = EConnectionMetricQuality::CompletedNativeWindow;
		Metric.Value = Value;
		Metric.SampleCount = Samples;
		Metric.WindowStartRealTimeSeconds = Start;
		Metric.WindowEndRealTimeSeconds = End;
	}
	uint32 IncomingTotal(const UNetConnection& Connection) { return uint32(Connection.InTotalPackets) + uint32(Connection.InTotalPacketsLost); }
}

bool UConnectionObservationSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game || Type == EWorldType::PIE; }

void UConnectionObservationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UNetworkFaultSimulationSubsystem>();
	PreFlushHandle = GetWorld()->OnPreTickFlush().AddUObject(this, &ThisClass::BeforeNetFlush);
	TickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::AfterWorldTick);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::HandleTearDown);
	TravelHandle = FWorldDelegates::OnSeamlessTravelStart.AddUObject(this, &ThisClass::HandleTravel);
	RefreshConfiguration();
}

void UConnectionObservationSubsystem::Clear()
{
	Observation = {};
	ObservedDriver.Reset();
	LastNativeStatsTime = 0.0;
	bAlignedStatsWindow = false;
	RttSamplesBeforeFlush = 0;
	IncomingPacketsAtBoundary = OutgoingPacketsAtBoundary = 0;
}

void UConnectionObservationSubsystem::OnWorldEndPlay(UWorld& World)
{
	HandleTearDown(&World);
	Super::OnWorldEndPlay(World);
}

void UConnectionObservationSubsystem::HandleTearDown(UWorld* World)
{
	if (World != GetWorld()) { return; }
	GetWorld()->OnPreTickFlush().Remove(PreFlushHandle);
	FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
	PreFlushHandle.Reset(); TickEndHandle.Reset();
	Clear();
}

void UConnectionObservationSubsystem::HandleTravel(UWorld* World, const FString&)
{
	if (World == GetWorld()) { Clear(); }
}

void UConnectionObservationSubsystem::Deinitialize()
{
	GetWorld()->OnPreTickFlush().Remove(PreFlushHandle);
	FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	FWorldDelegates::OnSeamlessTravelStart.Remove(TravelHandle);
	PreFlushHandle.Reset(); TickEndHandle.Reset();
	Clear();
	Super::Deinitialize();
}

UNetConnection* UConnectionObservationSubsystem::ResolveConnection() const
{
	const UNetDriver* Driver = GetWorld()->GetNetDriver();
	UNetConnection* Connection = Driver ? Driver->ServerConnection.Get() : nullptr;
	return PreFlushHandle.IsValid() && GetWorld()->GetNetMode() == NM_Client && Connection && Connection->GetConnectionState() == USOCK_Open ? Connection : nullptr;
}

const FConnectionObservation& UConnectionObservationSubsystem::GetConnectionObservation() const
{
	static const FConnectionObservation Missing;
	return Observation.Connection.IsValid() && Observation.Connection.Get() == ResolveConnection() && ObservedDriver == GetWorld()->GetNetDriver() ? Observation : Missing;
}

void UConnectionObservationSubsystem::RefreshConfiguration()
{
	UNetConnection* Connection = ResolveConnection();
	UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (Observation.Connection.Get() != Connection || ObservedDriver != Driver)
	{
		Clear();
		if (Connection)
		{
			Observation.Connection = Connection;
			Observation.ConnectionPath = Connection->GetPathName();
			ObservedDriver = Driver;
			LastNativeStatsTime = Connection->StatUpdateTime;
		}
	}
	if (!Connection) { return; }
	Observation.Status = EObservationAvailability::Available;
	Observation.ObservedAtRealTimeSeconds = FPlatformTime::Seconds();
	Observation.JitterEstimateMs.Status = EObservationAvailability::Available;
	Observation.JitterEstimateMs.Quality = EConnectionMetricQuality::NativeEstimateUnknownTiming;
	Observation.JitterEstimateMs.Value = Connection->GetAverageJitterInMS();
	UNetworkFaultSimulationSubsystem* Faults = GetWorld()->GetSubsystem<UNetworkFaultSimulationSubsystem>();
	Observation.PacketSettingsStatus = Faults->TryGetSettings(Observation.PacketSettings, Observation.PacketSettingsError)
		? EObservationAvailability::Available : EObservationAvailability::Unsupported;
	const bool bScenarioAvailable = Faults->TryGetScenarioState(Observation.ScenarioState, Observation.ScenarioError)
		&& Faults->TryGetScenarioSettings(Observation.ScenarioSettings, Observation.ScenarioError);
	Observation.ScenarioStatus = bScenarioAvailable ? EObservationAvailability::Available : EObservationAvailability::Unsupported;
	Observation.bFaultSimulationActive = Faults->IsSimulationActive();
}

void UConnectionObservationSubsystem::BeforeNetFlush(float)
{
	RefreshConfiguration();
	const UNetConnection* Connection = ResolveConnection();
	RttSamplesBeforeFlush = Connection ? Connection->LagCount : 0;
}

void UConnectionObservationSubsystem::AfterWorldTick(UWorld* World, ELevelTick, float)
{
	if (World != GetWorld()) { return; }
	RefreshConfiguration();
	const UNetConnection* Connection = ResolveConnection();
	if (!Connection || Connection->StatUpdateTime == LastNativeStatsTime) { return; }
	// The first boundary establishes a complete loss denominator. Joining a partial native window is not a measurement.
	const double End = Connection->StatUpdateTime;
	RecordMetric(Observation.RoundTripTimeMs, Connection->AvgLag * 1000.0, RttSamplesBeforeFlush, LastNativeStatsTime, End);
	const uint32 Incoming = IncomingTotal(*Connection);
	const uint32 Outgoing = Connection->GetOutTotalNotifiedPackets();
	if (bAlignedStatsWindow)
	{
		RecordMetric(Observation.IncomingLossPercent, Connection->GetInLossPercentage().GetLossPercentage() * 100.0,
			uint32(Incoming - IncomingPacketsAtBoundary), LastNativeStatsTime, End);
		RecordMetric(Observation.OutgoingLossPercent, Connection->GetOutLossPercentage().GetLossPercentage() * 100.0,
			uint32(Outgoing - OutgoingPacketsAtBoundary), LastNativeStatsTime, End);
	}
	bAlignedStatsWindow = true;
	IncomingPacketsAtBoundary = Incoming;
	OutgoingPacketsAtBoundary = Outgoing;
	LastNativeStatsTime = End;
}
