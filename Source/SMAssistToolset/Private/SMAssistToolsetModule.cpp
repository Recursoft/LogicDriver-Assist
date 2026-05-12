// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistToolsetModule.h"

#include "Modules/ModuleManager.h"

#if WITH_TOOLSET_REGISTRY
#include "LogicDriverToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#endif

void FSMAssistToolsetModule::StartupModule()
{
#if WITH_TOOLSET_REGISTRY
	UToolsetRegistry::RegisterToolsetClass(ULogicDriverToolset::StaticClass());
#endif
}

void FSMAssistToolsetModule::ShutdownModule()
{
#if WITH_TOOLSET_REGISTRY
	UToolsetRegistry::UnregisterToolsetClass(ULogicDriverToolset::StaticClass());
#endif
}

IMPLEMENT_MODULE(FSMAssistToolsetModule, SMAssistToolset)
