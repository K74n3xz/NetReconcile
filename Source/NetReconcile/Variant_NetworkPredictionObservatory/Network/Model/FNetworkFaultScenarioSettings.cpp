#include "FNetworkFaultScenarioSettings.h"

bool FNetworkFaultScenarioSettings::Validate(FText& Error) const
{
	Error = FText::GetEmpty();
	if (Scenario < ENetworkFaultScenario::BurstLoss || Scenario > ENetworkFaultScenario::BandwidthLimit)
	{
		Error = NSLOCTEXT("Observation", "InvalidScenario", "Choose a network fault scenario.");
	}
	else if (Scenario == ENetworkFaultScenario::BurstLoss && BurstPacketCount <= 0)
	{
		Error = NSLOCTEXT("Observation", "InvalidBurstCount", "Burst packet count must be a positive integer.");
	}
	else if ((Scenario == ENetworkFaultScenario::Blackout || Scenario == ENetworkFaultScenario::LatencySpike)
		&& (!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0f))
	{
		Error = NSLOCTEXT("Observation", "InvalidScenarioDuration", "Duration must be a finite positive number of seconds.");
	}
	else if (Scenario == ENetworkFaultScenario::LatencySpike && (!FMath::IsFinite(AdditionalDelayMs) || AdditionalDelayMs <= 0.0f))
	{
		Error = NSLOCTEXT("Observation", "InvalidSpikeDelay", "Additional delay must be a finite positive number of milliseconds.");
	}
	else if (Scenario == ENetworkFaultScenario::BandwidthLimit && (!FMath::IsFinite(BandwidthKbps) || BandwidthKbps <= 0.0f))
	{
		Error = NSLOCTEXT("Observation", "InvalidScenarioBandwidth", "Bandwidth must be a finite positive number of kilobits per second.");
	}
	else if (bRepeat && Scenario != ENetworkFaultScenario::BandwidthLimit)
	{
		if (!FMath::IsFinite(RepeatIntervalSeconds) || RepeatIntervalSeconds <= 0.0f)
		{
			Error = NSLOCTEXT("Observation", "InvalidScenarioInterval", "Repeat interval must be a finite positive number of seconds.");
		}
		else if (Scenario != ENetworkFaultScenario::BurstLoss && DurationSeconds >= RepeatIntervalSeconds)
		{
			Error = NSLOCTEXT("Observation", "OverlappingScenarioWindows", "Duration must be shorter than the repeat interval.");
		}
	}
	return Error.IsEmpty();
}
