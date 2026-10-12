#pragma once

#include "CoreMinimal.h"
#include "FNetworkFaultSimulationSettings.generated.h"

struct FPacketSimulationSettings;

/** Blueprint-accessible counterpart of the engine's packet simulation settings. Units and precedence are unchanged. */
USTRUCT(BlueprintType)
struct NETRECONCILE_API FNetworkFaultSimulationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0", ClampMax = "100"))
	int32 PktLoss = 0;
	/** Bytes; zero means no upper packet-size limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0"))
	int32 PktLossMaxSize = 0;
	/** Bytes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0"))
	int32 PktLossMinSize = 0;
	/** Zero disables reordering; one enables it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0", ClampMax = "1"))
	int32 PktOrder = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0", ClampMax = "100"))
	int32 PktDup = 0;
	/** Milliseconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0"))
	int32 PktLag = 0;
	/** Milliseconds, applied around the selected outgoing latency. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0"))
	int32 PktLagVariance = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0"))
	int32 PktLagMin = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0"))
	int32 PktLagMax = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incoming", meta = (ClampMin = "0"))
	int32 PktIncomingLagMin = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incoming", meta = (ClampMin = "0"))
	int32 PktIncomingLagMax = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incoming", meta = (ClampMin = "0", ClampMax = "100"))
	int32 PktIncomingLoss = 0;
	/** Milliseconds; alternates outgoing latency between PktLagMin and PktLagMin + PktJitter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0"))
	int32 PktJitter = 0;
	/** Network ticks, not milliseconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0"))
	int32 PktFrameDelay = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incoming", meta = (ClampMin = "0"))
	int32 PktIncomingFrameDelay = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outgoing", meta = (ClampMin = "0"))
	int32 PktBufferBloatInMS = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incoming", meta = (ClampMin = "0"))
	int32 PktIncomingBufferBloatInMS = 0;

	FPacketSimulationSettings ToNative() const;
	static FNetworkFaultSimulationSettings FromNative(const FPacketSimulationSettings& Settings);
	/** Nominal added outgoing + incoming milliseconds; excludes tick delays and buffer bloat. */
	static float GetNominalLatencyMs(const FPacketSimulationSettings& Settings);
};
