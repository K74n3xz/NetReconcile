#pragma once

#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"

/** A socket-free game world for source/handle/CMC contracts; network tests use real sockets. */
struct FObservationTestWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UNetDriver* Driver = nullptr;
	inline FObservationTestWorld()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
	}
	inline void MakeClient()
	{
		UClass* DriverClass = LoadClass<UNetDriver>(nullptr, TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
		Driver = NewObject<UNetDriver>(World, DriverClass);
		Driver->SetWorld(World);
		USimulatedClientNetConnection* Connection = NewObject<USimulatedClientNetConnection>(Driver);
		Connection->Driver = Driver;
		Connection->SetConnectionState(USOCK_Open);
		Driver->ServerConnection = Connection;
		World->SetNetDriver(Driver);
	}
	inline void Capture() const
	{
		FWorldDelegates::OnWorldTickStart.Broadcast(World, LEVELTICK_All, 1.0f / 60.0f);
		FWorldDelegates::OnWorldPostActorTick.Broadcast(World, LEVELTICK_All, 1.0f / 60.0f);
	}
	inline ~FObservationTestWorld()
	{
		World->SetNetDriver(nullptr);
		if (Driver)
		{
			Driver->ServerConnection->SetConnectionState(USOCK_Closed);
			Driver->ServerConnection->CleanUp();
			Driver->SetWorld(nullptr);
		}
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
