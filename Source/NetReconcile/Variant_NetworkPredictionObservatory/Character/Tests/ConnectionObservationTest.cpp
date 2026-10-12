#if WITH_DEV_AUTOMATION_TESTS
#include "ObservationTestWorld.h"
#include "Variant_NetworkPredictionObservatory/Network/ConnectionObservationSubsystem.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionObservationTest, "NetReconcile.Observation.Connection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConnectionObservationTest::RunTest(const FString&)
{
	FObservationTestWorld Fixture;
	UConnectionObservationSubsystem* Service = Fixture.World->GetSubsystem<UConnectionObservationSubsystem>();
	if (!TestNotNull(TEXT("Connection service exists without a Pawn or LocalPlayer"), Service)) { return false; }
	TestEqual(TEXT("No client connection is missing source"), Service->GetConnectionObservation().Status, EObservationAvailability::MissingSource);
	Fixture.MakeClient();
	UNetConnection* Connection = Fixture.Driver->ServerConnection;
	Service->RefreshConfiguration();
	TestEqual(TEXT("Open connection does not fabricate RTT"), Service->GetConnectionObservation().RoundTripTimeMs.Status, EObservationAvailability::AwaitingSample);
	TestEqual(TEXT("Open connection does not fabricate loss"), Service->GetConnectionObservation().OutgoingLossPercent.Status, EObservationAvailability::AwaitingSample);
	TestEqual(TEXT("Native jitter uncertainty is explicit"), Service->GetConnectionObservation().JitterEstimateMs.Quality, EConnectionMetricQuality::NativeEstimateUnknownTiming);
	auto CompleteNativeWindow = [&](int32 RttSamples, float RttSeconds, int32 ReceivedPackets)
	{
		Connection->LagCount = RttSamples;
		Service->BeforeNetFlush(0.016f);
		Connection->AvgLag = RttSeconds;
		Connection->LagCount = 0;
		Connection->InTotalPackets += ReceivedPackets;
		Connection->StatUpdateTime += 1.0;
		Service->AfterWorldTick(Fixture.World, LEVELTICK_PauseTick, 0.016f);
	};
	CompleteNativeWindow(2, 0.125f, 10);
	const FConnectionObservation First = Service->GetConnectionObservation();
	TestEqual(TEXT("Native measured RTT count retained"), First.RoundTripTimeMs.SampleCount, int64(2));
	TestTrue(TEXT("Native RTT is milliseconds"), FMath::IsNearlyEqual(First.RoundTripTimeMs.Value, 125.0, 0.001));
	TestEqual(TEXT("Partial initial loss window waits for alignment"), First.IncomingLossPercent.Status, EObservationAvailability::AwaitingSample);
	CompleteNativeWindow(0, 0.125f, 5);
	const FConnectionObservation Second = Service->GetConnectionObservation();
	TestEqual(TEXT("RTT with no new samples retains original window time"), Second.RoundTripTimeMs.WindowEndRealTimeSeconds, First.RoundTripTimeMs.WindowEndRealTimeSeconds);
	TestEqual(TEXT("Complete incoming loss denominator is known"), Second.IncomingLossPercent.SampleCount, int64(5));
	TestEqual(TEXT("Known zero loss is available"), Second.IncomingLossPercent.Status, EObservationAvailability::Available);
	TestEqual(TEXT("No outgoing acknowledgements means awaiting measured send loss"), Second.OutgoingLossPercent.Status, EObservationAvailability::AwaitingSample);
	APlayerState* Pauser = Fixture.World->SpawnActor<APlayerState>();
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	CompleteNativeWindow(1, 0.250f, 0);
	TestTrue(TEXT("Network sampling continues during world pause"), FMath::IsNearlyEqual(Service->GetConnectionObservation().RoundTripTimeMs.Value, 250.0, 0.001));
	TestEqual(TEXT("Empty loss window retains its actual sample time"), Service->GetConnectionObservation().IncomingLossPercent.WindowEndRealTimeSeconds, Second.IncomingLossPercent.WindowEndRealTimeSeconds);
	Fixture.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Connection->SetConnectionState(USOCK_Closed);
	TestEqual(TEXT("Close invalidates read before a world tick"), Service->GetConnectionObservation().Status, EObservationAvailability::MissingSource);
	Service->RefreshConfiguration();
	Connection->SetConnectionState(USOCK_Open); Service->RefreshConfiguration();
	TestEqual(TEXT("Reopening does not inherit prior metrics"), Service->GetConnectionObservation().RoundTripTimeMs.Status, EObservationAvailability::AwaitingSample);
	CompleteNativeWindow(1, 0.150f, 0);
	FWorldDelegates::OnSeamlessTravelStart.Broadcast(Fixture.World, TEXT("/Game/NextWorld"));
	TestEqual(TEXT("Travel invalidates the outgoing cache"), Service->GetConnectionObservation().Status, EObservationAvailability::MissingSource);
	Service->RefreshConfiguration();
	TestEqual(TEXT("Travel requires fresh metric windows"), Service->GetConnectionObservation().RoundTripTimeMs.Status, EObservationAvailability::AwaitingSample);
	FWorldDelegates::OnWorldBeginTearDown.Broadcast(Fixture.World);
	Service->RefreshConfiguration();
	TestEqual(TEXT("World teardown cannot reacquire its open connection"), Service->GetConnectionObservation().Status, EObservationAvailability::MissingSource);
	return true;
}
#endif
