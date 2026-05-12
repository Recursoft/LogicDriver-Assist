// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistToolsetModule.h"

#include "LogicDriverToolset.h"

#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

void FSMAssistToolsetModule::StartupModule()
{
	UToolsetRegistry::RegisterToolsetClass(ULogicDriverToolset::StaticClass());
}

void FSMAssistToolsetModule::ShutdownModule()
{
	UToolsetRegistry::UnregisterToolsetClass(ULogicDriverToolset::StaticClass());
}

IMPLEMENT_MODULE(FSMAssistToolsetModule, SMAssistToolset);
