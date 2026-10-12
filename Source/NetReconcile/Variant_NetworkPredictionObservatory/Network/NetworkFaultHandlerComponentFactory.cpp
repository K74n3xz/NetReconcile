#include "NetworkFaultHandlerComponentFactory.h"
#include "NetworkFaultHandlerComponent.h"

TSharedPtr<HandlerComponent> UNetworkFaultHandlerComponentFactory::CreateComponentInstance(FString&)
{
	return MakeShared<FNetworkFaultHandlerComponent>();
}
