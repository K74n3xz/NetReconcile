#pragma once

#include "CoreMinimal.h"
#include "FNetworkFaultScenarioSettings.generated.h"

UENUM(BlueprintType)
enum class ENetworkFaultScenario : uint8
{
	None,
	BurstLoss,
	Blackout,
	LatencySpike,
	BandwidthLimit
};

/** One outgoing client scenario, independent of the native packet simulation settings. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FNetworkFaultScenarioSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ENetworkFaultScenario Scenario = ENetworkFaultScenario::BurstLoss;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRepeat = false;
	/** Seconds between cycle starts; unused by continuous bandwidth limiting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.001"))
	float RepeatIntervalSeconds = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 BurstPacketCount = 10;
	/** Duration of a blackout or latency spike. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.001"))
	float DurationSeconds = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.001"))
	float AdditionalDelayMs = 250.0f;
	/** Kilobits per second (1000 bits); excludes subsequent handler and IP/UDP overhead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.001"))
	float BandwidthKbps = 32.0f;

	bool Validate(FText& Error) const;
};

/** Counters belong to the last started scenario and reset on the next successful start. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FNetworkFaultScenarioState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	ENetworkFaultScenario Scenario = ENetworkFaultScenario::None;
	UPROPERTY(BlueprintReadOnly)
	bool bRunning = false;
	UPROPERTY(BlueprintReadOnly)
	bool bFaultWindowActive = false;
	/** Configured spike delay during an active window; queue waiting time is separate. */
	UPROPERTY(BlueprintReadOnly)
	float AdditionalDelayMs = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	int32 PendingPackets = 0;
	UPROPERTY(BlueprintReadOnly)
	int64 PendingBytes = 0;
	/** Includes packets dropped because the bounded queue was full. */
	UPROPERTY(BlueprintReadOnly)
	int64 DroppedPackets = 0;
	UPROPERTY(BlueprintReadOnly)
	int64 OverflowDroppedPackets = 0;
};
