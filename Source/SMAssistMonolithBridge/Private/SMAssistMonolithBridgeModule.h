// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Containers/Set.h"
#include "Delegates/IDelegateInstance.h"
#include "Modules/ModuleInterface.h"
#include "UObject/NameTypes.h"

struct FSMAssistOperationInfo;
class USMAssistSubsystem;

class FSMAssistMonolithBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void BindToAssist();
	void UnbindFromAssist();

	void HandleOperationRegistered(const FSMAssistOperationInfo& InInfo);
	void HandleOperationUnregistered(FName InName);

	static bool SplitOperationName(FName InName, FString& OutNamespace, FString& OutAction);

	USMAssistSubsystem* GetAssistSubsystem() const;

private:
	FDelegateHandle PostEngineInitHandle;

	FDelegateHandle OperationRegisteredHandle;

	FDelegateHandle OperationUnregisteredHandle;

	TSet<FString> BridgedNamespaces;

	bool bBound = false;
};
