#pragma once

#include "CoreMinimal.h"
#include "HandlerComponentFactory.h"
#include "NetworkFaultHandlerComponentFactory.generated.h"

/** Loaded by the GameNetDriver PacketHandler profile before connection initialization. */
UCLASS()
class UNetworkFaultHandlerComponentFactory : public UHandlerComponentFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<HandlerComponent> CreateComponentInstance(FString& Options) override;
};
