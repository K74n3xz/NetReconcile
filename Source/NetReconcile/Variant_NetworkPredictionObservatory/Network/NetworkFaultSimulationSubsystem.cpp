#include "NetworkFaultSimulationSubsystem.h"
#include "NetworkFaultHandlerComponent.h"

#include "Engine/NetConnection.h"
#include "Engine/World.h"

void UNetworkFaultSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Network time and connection changes continue while actor ticks are paused or disconnected.
	TickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::HandleWorldTickEnd);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::HandleWorldTearDown);
	TravelHandle = FWorldDelegates::OnSeamlessTravelStart.AddUObject(this, &ThisClass::HandleSeamlessTravel);
}

void UNetworkFaultSimulationSubsystem::Deinitialize()
{
	ClearScenario(false);
	FText Error;
	RestoreOriginalSettings(Error);
	FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	FWorldDelegates::OnSeamlessTravelStart.Remove(TravelHandle);
	OnSimulationStateChanged.Clear();
	Super::Deinitialize();
}

void UNetworkFaultSimulationSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	HandleWorldTearDown(&InWorld);
	Super::OnWorldEndPlay(InWorld);
}

bool UNetworkFaultSimulationSubsystem::ApplySettings(const FNetworkFaultSimulationSettings& Settings, FText& Error)
{
	return ApplyPacketSimulationSettings(Settings.ToNative(), Error);
}

bool UNetworkFaultSimulationSubsystem::ApplyPacketSimulationSettings(const FPacketSimulationSettings& Settings, FText& Error)
{
	check(IsInGameThread());
	UNetDriver* Driver = GetClientDriver(Error);
	if (!Driver) { return false; }
#if DO_ENABLE_NET_TEST
	if (bHasOriginalSettings && (ModifiedDriver.Get() != Driver || ModifiedConnection.Get() != Driver->ServerConnection))
	{
		if (!RestoreOriginalSettings(Error)) { return false; }
	}
	FPacketSimulationSettings Validated = Settings;
	Validated.PktLossMinSize = FMath::Max(0, Validated.PktLossMinSize);
	Validated.PktLossMaxSize = FMath::Max(0, Validated.PktLossMaxSize);
	Validated.PktLag = FMath::Max(0, Validated.PktLag);
	Validated.PktLagVariance = FMath::Max(0, Validated.PktLagVariance);
	Validated.PktJitter = FMath::Max(0, Validated.PktJitter);
	Validated.PktFrameDelay = FMath::Max(0, Validated.PktFrameDelay);
	Validated.PktIncomingFrameDelay = FMath::Max(0, Validated.PktIncomingFrameDelay);
	Validated.ValidateSettings();
	if (!bHasOriginalSettings)
	{
		OriginalSettings = Driver->PacketSimulationSettings;
		ModifiedDriver = Driver;
		ModifiedConnection = Driver->ServerConnection;
		bHasOriginalSettings = true;
	}
	Driver->SetPacketSimulationSettings(Validated);
	OnSimulationStateChanged.Broadcast();
	return true;
#else
	return false;
#endif
}

bool UNetworkFaultSimulationSubsystem::ResetSettings(FText& Error)
{
	check(IsInGameThread());
#if !DO_ENABLE_NET_TEST
	GetClientDriver(Error);
	return false;
#else
	if (!RestoreOriginalSettings(Error)) { return false; }
	ClearScenario(true);
	OnSimulationStateChanged.Broadcast();
	return true;
#endif
}

bool UNetworkFaultSimulationSubsystem::TryGetSettings(FNetworkFaultSimulationSettings& Settings, FText& Error) const
{
	Settings = {};
#if DO_ENABLE_NET_TEST
	UNetDriver* Driver = GetClientDriver(Error);
	if (Driver)
	{
		Settings = FNetworkFaultSimulationSettings::FromNative(Driver->PacketSimulationSettings);
		return true;
	}
#else
	GetClientDriver(Error);
#endif
	return false;
}

bool UNetworkFaultSimulationSubsystem::StartScenario(const FNetworkFaultScenarioSettings& Settings, FText& Error)
{
	check(IsInGameThread());
	TSharedPtr<FNetworkFaultHandlerComponent> Component = GetScenarioHandler(Error);
	if (!Component) { return false; }
	UNetConnection* Connection = GetWorld()->GetNetDriver()->ServerConnection.Get();
	if (!Settings.Validate(Error)) { return false; }
	if (ScenarioConnection.Get() != Connection) { ClearScenario(false); }
	if (!Component->StartScenario(Settings, Connection, Error)) { return false; }
	ScenarioConnection = Connection;
	ScenarioHandler = Component;
	LastScenarioState = Component->GetScenarioState();
	OnSimulationStateChanged.Broadcast();
	return true;
}

bool UNetworkFaultSimulationSubsystem::StopScenario(FText& Error)
{
	check(IsInGameThread());
#if !DO_ENABLE_NET_TEST
	GetClientDriver(Error);
	return false;
#else
	Error = FText::GetEmpty();
	ClearScenario(true);
	OnSimulationStateChanged.Broadcast();
	return true;
#endif
}

bool UNetworkFaultSimulationSubsystem::TryGetScenarioSettings(FNetworkFaultScenarioSettings& Settings, FText& Error) const
{
	Settings = {};
	if (TSharedPtr<FNetworkFaultHandlerComponent> Component = GetScenarioHandler(Error))
	{
		Settings = Component->GetSettings();
		return true;
	}
	return false;
}

bool UNetworkFaultSimulationSubsystem::TryGetScenarioState(FNetworkFaultScenarioState& State, FText& Error) const
{
	State = {};
	if (TSharedPtr<FNetworkFaultHandlerComponent> Component = GetScenarioHandler(Error))
	{
		State = Component->GetScenarioState();
		return true;
	}
	return false;
}

bool UNetworkFaultSimulationSubsystem::IsSimulationActive() const
{
	FText Error;
	UNetDriver* Driver = GetClientDriver(Error);
	if (!Driver) { return false; }
	const bool bNativeActive = bHasOriginalSettings && ModifiedDriver.Get() == Driver
		&& ModifiedConnection.Get() == Driver->ServerConnection;
	FNetworkFaultScenarioState State;
	return bNativeActive || (TryGetScenarioState(State, Error) && (State.bRunning || State.PendingPackets > 0));
}

bool UNetworkFaultSimulationSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UNetDriver* UNetworkFaultSimulationSubsystem::GetClientDriver(FText& Error) const
{
	Error = FText::GetEmpty();
#if DO_ENABLE_NET_TEST
	UWorld* World = GetWorld();
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	if (!IsValid(Driver) || World->GetNetMode() != NM_Client || !IsValid(Driver->ServerConnection)
		|| Driver->ServerConnection->GetConnectionState() != USOCK_Open)
	{
		Error = NSLOCTEXT("Observation", "NoClientConnection", "Network faults require an open client game connection.");
		return nullptr;
	}
	if (Driver->bNeverApplyNetworkEmulationSettings)
	{
		Error = NSLOCTEXT("Observation", "EmulationDisabled", "This NetDriver disables network emulation.");
		return nullptr;
	}
	return Driver;
#else
	Error = NSLOCTEXT("Observation", "EmulationUnsupported", "Network fault simulation is unavailable in this build.");
	return nullptr;
#endif
}

TSharedPtr<FNetworkFaultHandlerComponent> UNetworkFaultSimulationSubsystem::GetScenarioHandler(FText& Error) const
{
	UNetDriver* Driver = GetClientDriver(Error);
	if (!Driver) { return nullptr; }
	PacketHandler* Handler = Driver->ServerConnection->Handler.Get();
	TSharedPtr<HandlerComponent> Component = Handler ? Handler->GetComponentByName(FNetworkFaultHandlerComponent::ComponentName()) : nullptr;
	if (!Component || !Component->IsActive() || !Handler->IsFullyInitialized())
	{
		Error = NSLOCTEXT("Observation", "MissingScenarioHandler", "Network fault scenarios require the GameNetDriver packet handler profile. Restart and reconnect after enabling it.");
		return nullptr;
	}
	return StaticCastSharedPtr<FNetworkFaultHandlerComponent>(Component);
}

void UNetworkFaultSimulationSubsystem::ClearScenario(bool bSendQueued)
{
	if (TSharedPtr<FNetworkFaultHandlerComponent> Component = ScenarioHandler.Pin())
	{
		FText Error;
		UNetDriver* Driver = bSendQueued ? GetClientDriver(Error) : nullptr;
		const bool bSameConnection = Driver && ScenarioConnection.Get() == Driver->ServerConnection.Get();
		Component->StopScenario(bSameConnection);
	}
	ScenarioConnection.Reset();
	ScenarioHandler.Reset();
	LastScenarioState = {};
}

bool UNetworkFaultSimulationSubsystem::RestoreOriginalSettings(FText& Error)
{
	Error = FText::GetEmpty();
#if DO_ENABLE_NET_TEST
	if (bHasOriginalSettings)
	{
		if (UNetDriver* Driver = ModifiedDriver.Get())
		{
			if (Driver->bNeverApplyNetworkEmulationSettings)
			{
				Error = NSLOCTEXT("Observation", "RestoreDisabled", "This NetDriver currently prevents restoring its network settings.");
				return false;
			}
			Driver->SetPacketSimulationSettings(OriginalSettings);
		}
	}
#endif
	bHasOriginalSettings = false;
	ModifiedDriver.Reset();
	ModifiedConnection.Reset();
	return true;
}

void UNetworkFaultSimulationSubsystem::HandleWorldTickEnd(UWorld* World, ELevelTick, float)
{
	if (World != GetWorld()) { return; }
	FText Error;
	UNetDriver* Driver = GetClientDriver(Error);
	bool bChanged = false;
	if (bHasOriginalSettings && (!Driver || ModifiedDriver.Get() != Driver || ModifiedConnection.Get() != Driver->ServerConnection))
	{
		bChanged = RestoreOriginalSettings(Error);
	}
	UNetConnection* Connection = Driver ? Driver->ServerConnection.Get() : nullptr;
	if (ScenarioHandler.IsValid())
	{
		if (ScenarioConnection.Get() != Connection)
		{
			ClearScenario(false);
			bChanged = true;
		}
		else if (TSharedPtr<FNetworkFaultHandlerComponent> Component = ScenarioHandler.Pin())
		{
			const FNetworkFaultScenarioState State = Component->GetScenarioState();
			bChanged |= State.bRunning != LastScenarioState.bRunning
				|| State.bFaultWindowActive != LastScenarioState.bFaultWindowActive
				|| (State.PendingPackets == 0) != (LastScenarioState.PendingPackets == 0);
			LastScenarioState = State;
		}
	}
	if (bChanged || AvailableConnection.Get() != Connection)
	{
		AvailableConnection = Connection;
		OnSimulationStateChanged.Broadcast();
	}
}

void UNetworkFaultSimulationSubsystem::HandleWorldTearDown(UWorld* World)
{
	if (World == GetWorld())
	{
		ClearScenario(false);
		FText Error;
		RestoreOriginalSettings(Error);
		OnSimulationStateChanged.Broadcast();
	}
}

void UNetworkFaultSimulationSubsystem::HandleSeamlessTravel(UWorld* World, const FString&)
{
	HandleWorldTearDown(World);
}
