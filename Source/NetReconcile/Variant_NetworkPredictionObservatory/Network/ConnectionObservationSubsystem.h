#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "../Character/Model/PawnObservationTypes.h"
#include "Model/FNetworkFaultSimulationSettings.h"
#include "Model/FNetworkFaultScenarioSettings.h"
#include "ConnectionObservationSubsystem.generated.h"

class UNetConnection;
class UNetDriver;

UENUM(BlueprintType)
enum class EConnectionMetricQuality : uint8 { AwaitingMeasurement, CompletedNativeWindow, NativeEstimateUnknownTiming };

/** Metrics keep their original window/time when native networking has no new measurements. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FObservedConnectionMetric
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") EObservationAvailability Status = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") EConnectionMetricQuality Quality = EConnectionMetricQuality::AwaitingMeasurement;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") double Value = 0.0;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") double WindowStartRealTimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") double WindowEndRealTimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") int64 SampleCount = 0;
	inline bool IsAvailable() const { return Status == EObservationAvailability::Available; }
};

/** This world's client game connection, independent of Pawn selection, pause and UI freeze. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FConnectionObservation
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") EObservationAvailability Status = EObservationAvailability::MissingSource;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") TWeakObjectPtr<UNetConnection> Connection;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FString ConnectionPath;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") double ObservedAtRealTimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FObservedConnectionMetric RoundTripTimeMs;
	/** Native EWMA exposes no public initialization/sample time, so that uncertainty is explicit. */
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FObservedConnectionMetric JitterEstimateMs;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FObservedConnectionMetric IncomingLossPercent;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FObservedConnectionMetric OutgoingLossPercent;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") EObservationAvailability PacketSettingsStatus = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FNetworkFaultSimulationSettings PacketSettings;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") EObservationAvailability ScenarioStatus = EObservationAvailability::AwaitingSample;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FNetworkFaultScenarioSettings ScenarioSettings;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FNetworkFaultScenarioState ScenarioState;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") bool bFaultSimulationActive = false;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FText PacketSettingsError;
	UPROPERTY(BlueprintReadOnly, Category="Observation|Connection") FText ScenarioError;
};

UCLASS(BlueprintType)
class NETRECONCILE_API UConnectionObservationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

	friend class FConnectionObservationTest;
	friend class FCharacterObservationPanelTest;

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void OnWorldEndPlay(UWorld& World) override;

	const FConnectionObservation& GetConnectionObservation() const;
	UFUNCTION(BlueprintPure, Category="Observation|Connection") inline FConnectionObservation GetConnectionSnapshot() const { return GetConnectionObservation(); }
	/** Refresh read sources after a fault command; does not fabricate a new metric measurement. */
	void RefreshConfiguration();

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;

private:
	FConnectionObservation Observation;
	TWeakObjectPtr<UNetDriver> ObservedDriver;

	FDelegateHandle PreFlushHandle;
	FDelegateHandle TickEndHandle;
	FDelegateHandle TearDownHandle;
	FDelegateHandle TravelHandle;

	double LastNativeStatsTime = 0.0;
	bool bAlignedStatsWindow = false;
	int32 RttSamplesBeforeFlush = 0;
	uint32 IncomingPacketsAtBoundary = 0;
	uint32 OutgoingPacketsAtBoundary = 0;

	void BeforeNetFlush(float DeltaTime);
	void AfterWorldTick(UWorld* World, ELevelTick Type, float DeltaTime);
	void HandleTearDown(UWorld* World);
	void HandleTravel(UWorld* World, const FString& Map);
	UNetConnection* ResolveConnection() const;
	void Clear();
};
