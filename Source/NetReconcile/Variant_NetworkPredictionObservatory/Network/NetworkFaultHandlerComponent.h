#pragma once

#include "CoreMinimal.h"
#include "Model/FNetworkFaultScenarioSettings.h"
#include "PacketHandler.h"

class UNetConnection;

/** Zero protocol overhead; outgoing client faults only, with FIFO delivery after this component. */
class NETRECONCILE_API FNetworkFaultHandlerComponent : public HandlerComponent
{
public:
	FNetworkFaultHandlerComponent();

	static FName ComponentName();

	virtual void Initialize() override;

	virtual inline bool IsValid() const override { return true; }
	virtual inline int32 GetReservedPacketBits() const override { return 0; }

	virtual bool SupportsParallelConnectionOutgoing() const override;
	virtual void Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits) override;

	virtual void Tick(float DeltaTime) override;

	bool StartScenario(const FNetworkFaultScenarioSettings& InSettings, UNetConnection* InConnection, FText& Error);
	/** Send the backlog only when the original connection is still open. Lifecycle cleanup passes false. */
	void StopScenario(bool bSendQueued);
	inline FNetworkFaultScenarioSettings GetSettings() const { return Settings; }
	FNetworkFaultScenarioState GetScenarioState() const;

private:
	struct FQueuedPacket
	{
		FBitWriter Packet;
		FOutPacketTraits Traits;
		double SendTime = 0.0;
	};

	FNetworkFaultScenarioSettings Settings;
	FNetworkFaultScenarioState ScenarioState;

	TWeakObjectPtr<UNetConnection> Connection;
	TArray<FQueuedPacket> PendingPackets;

	int64 PendingBytes = 0;
	double TimeSeconds = 0.0;
	double StartedAt = 0.0;
	double CycleStartedAt = 0.0;
	int32 BurstPacketsRemaining = 0;

	bool CanSend() const;
	void UpdateScenario();
};
