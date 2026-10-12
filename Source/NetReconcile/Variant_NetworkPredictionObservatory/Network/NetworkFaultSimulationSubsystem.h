#pragma once

#include "CoreMinimal.h"
#include "Model/FNetworkFaultSimulationSettings.h"
#include "Model/FNetworkFaultScenarioSettings.h"
#include "Engine/NetDriver.h"
#include "Subsystems/WorldSubsystem.h"
#include "NetworkFaultSimulationSubsystem.generated.h"

class FNetworkFaultHandlerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNetworkSimulationStateChanged);

/** Controls only this world's connected client game NetDriver. Local players sharing it share the simulation. */
UCLASS(BlueprintType)
class NETRECONCILE_API UNetworkFaultSimulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

	/** Replaces the complete configuration. The first successful application captures the reset baseline. */
	UFUNCTION(BlueprintCallable, Category = "Observation|Network Faults")
	bool ApplySettings(const FNetworkFaultSimulationSettings& Settings, FText& Error);

	bool ApplyPacketSimulationSettings(const FPacketSimulationSettings& Settings, FText& Error);

	/** Restores the native baseline and stops the scenario, sending its backlog on the current open connection. */
	UFUNCTION(BlueprintCallable, Category = "Observation|Network Faults")
	bool ResetSettings(FText& Error);

	/** Reads the current Driver, including settings applied by PIE, config, or console commands. */
	UFUNCTION(BlueprintPure, Category = "Observation|Network Faults")
	bool TryGetSettings(FNetworkFaultSimulationSettings& Settings, FText& Error) const;

	/** Starts one outgoing client scenario without changing the native configuration. */
	UFUNCTION(BlueprintCallable, Category = "Observation|Network Faults")
	bool StartScenario(const FNetworkFaultScenarioSettings& Settings, FText& Error);
	/** Sends the backlog in order; does not change native settings. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category = "Observation|Network Faults")
	bool StopScenario(FText& Error);
	UFUNCTION(BlueprintPure, Category = "Observation|Network Faults")
	bool TryGetScenarioSettings(FNetworkFaultScenarioSettings& Settings, FText& Error) const;
	UFUNCTION(BlueprintPure, Category = "Observation|Network Faults")
	bool TryGetScenarioState(FNetworkFaultScenarioState& State, FText& Error) const;

	UFUNCTION(BlueprintPure, Category = "Observation|Network Faults")
	bool IsSimulationActive() const;

	UPROPERTY(BlueprintAssignable, Category = "Observation|Network Faults")
	FOnNetworkSimulationStateChanged OnSimulationStateChanged;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	TWeakObjectPtr<UNetConnection> AvailableConnection;
	TWeakObjectPtr<UNetDriver> ModifiedDriver;
	TWeakObjectPtr<UNetConnection> ModifiedConnection;
	UNetDriver* GetClientDriver(FText& Error) const;

	TWeakObjectPtr<UNetConnection> ScenarioConnection;
	TWeakPtr<FNetworkFaultHandlerComponent> ScenarioHandler;
	FNetworkFaultScenarioState LastScenarioState;
	TSharedPtr<FNetworkFaultHandlerComponent> GetScenarioHandler(FText& Error) const;
	void ClearScenario(bool bSendQueued);

	bool bHasOriginalSettings = false;
	FPacketSimulationSettings OriginalSettings;
	bool RestoreOriginalSettings(FText& Error);

	FDelegateHandle TickEndHandle;
	FDelegateHandle TearDownHandle;
	FDelegateHandle TravelHandle;
	void HandleWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void HandleWorldTearDown(UWorld* World);
	void HandleSeamlessTravel(UWorld* World, const FString& Map);
};
