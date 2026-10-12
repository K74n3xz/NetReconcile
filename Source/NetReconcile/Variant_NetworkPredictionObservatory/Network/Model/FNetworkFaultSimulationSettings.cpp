#include "FNetworkFaultSimulationSettings.h"

#include "Engine/NetDriver.h"

FPacketSimulationSettings FNetworkFaultSimulationSettings::ToNative() const
{
	FPacketSimulationSettings Settings;
	Settings.PktLoss = PktLoss;
	Settings.PktLossMaxSize = PktLossMaxSize;
	Settings.PktLossMinSize = PktLossMinSize;
	Settings.PktOrder = PktOrder;
	Settings.PktDup = PktDup;
	Settings.PktLag = PktLag;
	Settings.PktLagVariance = PktLagVariance;
	Settings.PktLagMin = PktLagMin;
	Settings.PktLagMax = PktLagMax;
	Settings.PktIncomingLagMin = PktIncomingLagMin;
	Settings.PktIncomingLagMax = PktIncomingLagMax;
	Settings.PktIncomingLoss = PktIncomingLoss;
	Settings.PktJitter = PktJitter;
	Settings.PktFrameDelay = PktFrameDelay;
	Settings.PktIncomingFrameDelay = PktIncomingFrameDelay;
	Settings.PktBufferBloatInMS = PktBufferBloatInMS;
	Settings.PktIncomingBufferBloatInMS = PktIncomingBufferBloatInMS;
	return Settings;
}

FNetworkFaultSimulationSettings FNetworkFaultSimulationSettings::FromNative(const FPacketSimulationSettings& Settings)
{
	FNetworkFaultSimulationSettings Result;
	Result.PktLoss = Settings.PktLoss;
	Result.PktLossMaxSize = Settings.PktLossMaxSize;
	Result.PktLossMinSize = Settings.PktLossMinSize;
	Result.PktOrder = Settings.PktOrder;
	Result.PktDup = Settings.PktDup;
	Result.PktLag = Settings.PktLag;
	Result.PktLagVariance = Settings.PktLagVariance;
	Result.PktLagMin = Settings.PktLagMin;
	Result.PktLagMax = Settings.PktLagMax;
	Result.PktIncomingLagMin = Settings.PktIncomingLagMin;
	Result.PktIncomingLagMax = Settings.PktIncomingLagMax;
	Result.PktIncomingLoss = Settings.PktIncomingLoss;
	Result.PktJitter = Settings.PktJitter;
	Result.PktFrameDelay = Settings.PktFrameDelay;
	Result.PktIncomingFrameDelay = Settings.PktIncomingFrameDelay;
	Result.PktBufferBloatInMS = Settings.PktBufferBloatInMS;
	Result.PktIncomingBufferBloatInMS = Settings.PktIncomingBufferBloatInMS;
	return Result;
}

float FNetworkFaultSimulationSettings::GetNominalLatencyMs(const FPacketSimulationSettings& Settings)
{
	// Match CheckOutgoingPacketEmulation's precedence. Tick delays and batching have no fixed millisecond value.
	double Outgoing = 0.0;
	if (Settings.PktFrameDelay == 0 && Settings.PktOrder == 0)
	{
		if (Settings.PktJitter > 0) { Outgoing = double(Settings.PktLagMin) + double(Settings.PktJitter) * 0.5; }
		else if (Settings.PktLag > 0) { Outgoing = Settings.PktLag; }
		else { Outgoing = (double(Settings.PktLagMin) + double(Settings.PktLagMax)) * 0.5; }
	}
	const double Incoming = (double(Settings.PktIncomingLagMin) + double(Settings.PktIncomingLagMax)) * 0.5;
	return static_cast<float>(Outgoing + Incoming);
}
