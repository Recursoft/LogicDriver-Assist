// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"

class FOutputDevice;

class FSMAssistModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void HandleExecCommand(const TArray<FString>& InArgs, FOutputDevice& InAr);
	void HandleListCommand(const TArray<FString>& InArgs, FOutputDevice& InAr);

private:
	TUniquePtr<FAutoConsoleCommandWithArgsAndOutputDevice> ExecCommand;
	TUniquePtr<FAutoConsoleCommandWithArgsAndOutputDevice> ListCommand;
};
