// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistMonolithBridgeModule.h"

#include "Modules/ModuleManager.h"

#if WITH_MONOLITH

#include "MonolithToolRegistry.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistSubsystem.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"

DEFINE_LOG_CATEGORY_STATIC(LogSMAssistMonolithBridge, Log, All);

namespace LD::Assist::MonolithBridge::Private
{
	static FSimpleMulticastDelegate& GetPostEngineInitDelegate()
	{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		return FCoreDelegates::OnPostEngineInit;
#else
		return FCoreDelegates::GetOnPostEngineInit();
#endif
	}

	static FMonolithActionResult ExecuteBridgedOperation(FName InOperationName, const TSharedPtr<FJsonObject>& InParams)
	{
		USMAssistSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
		if (!Subsystem)
		{
			return FMonolithActionResult::Error(TEXT("SMAssist subsystem unavailable."));
		}

		const TSharedRef<FJsonObject> Args = InParams.IsValid() ? InParams.ToSharedRef() : MakeShared<FJsonObject>();
		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(InOperationName, Args);

		if (!Result.bSuccess)
		{
			return FMonolithActionResult::Error(Result.ErrorMessage);
		}

		return FMonolithActionResult::Success(Result.Payload);
	}
}

void FSMAssistMonolithBridgeModule::StartupModule()
{
	if (GEditor)
	{
		BindToAssist();
		return;
	}

	PostEngineInitHandle = LD::Assist::MonolithBridge::Private::GetPostEngineInitDelegate().AddRaw(
		this, &FSMAssistMonolithBridgeModule::BindToAssist);
}

void FSMAssistMonolithBridgeModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		LD::Assist::MonolithBridge::Private::GetPostEngineInitDelegate().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	UnbindFromAssist();

	if (FModuleManager::Get().IsModuleLoaded(TEXT("MonolithCore")))
	{
		FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();
		for (const FString& Namespace : BridgedNamespaces)
		{
			Registry.UnregisterNamespace(Namespace);
		}
	}
	BridgedNamespaces.Empty();
}

void FSMAssistMonolithBridgeModule::BindToAssist()
{
	if (bBound)
	{
		return;
	}

	USMAssistSubsystem* Subsystem = GetAssistSubsystem();
	if (!Subsystem)
	{
		UE_LOG(LogSMAssistMonolithBridge, Warning,
			TEXT("SMAssist subsystem not available at bind time; bridge is inactive."));
		return;
	}

	OperationRegisteredHandle = Subsystem->OnOperationRegistered().AddRaw(
		this, &FSMAssistMonolithBridgeModule::HandleOperationRegistered);
	OperationUnregisteredHandle = Subsystem->OnOperationUnregistered().AddRaw(
		this, &FSMAssistMonolithBridgeModule::HandleOperationUnregistered);

	for (const FSMAssistOperationInfo& Info : Subsystem->GetAllOperationInfos())
	{
		HandleOperationRegistered(Info);
	}

	bBound = true;
}

void FSMAssistMonolithBridgeModule::UnbindFromAssist()
{
	if (!bBound)
	{
		return;
	}

	if (USMAssistSubsystem* Subsystem = GetAssistSubsystem())
	{
		if (OperationRegisteredHandle.IsValid())
		{
			Subsystem->OnOperationRegistered().Remove(OperationRegisteredHandle);
		}
		if (OperationUnregisteredHandle.IsValid())
		{
			Subsystem->OnOperationUnregistered().Remove(OperationUnregisteredHandle);
		}
	}

	OperationRegisteredHandle.Reset();
	OperationUnregisteredHandle.Reset();
	bBound = false;
}

void FSMAssistMonolithBridgeModule::HandleOperationRegistered(const FSMAssistOperationInfo& InInfo)
{
	FString Namespace;
	FString Action;
	if (!SplitOperationName(InInfo.Name, Namespace, Action))
	{
		UE_LOG(LogSMAssistMonolithBridge, Warning,
			TEXT("Skipping operation '%s': name must be 'namespace.action'."), *InInfo.Name.ToString());
		return;
	}

	const FName OperationName = InInfo.Name;
	const FMonolithActionHandler Handler = FMonolithActionHandler::CreateLambda(
		[OperationName](const TSharedPtr<FJsonObject>& InParams)
		{
			return LD::Assist::MonolithBridge::Private::ExecuteBridgedOperation(OperationName, InParams);
		});

	FMonolithToolRegistry::Get().RegisterAction(Namespace, Action, InInfo.Description, Handler, InInfo.InputSchema);
	BridgedNamespaces.Add(Namespace);
}

void FSMAssistMonolithBridgeModule::HandleOperationUnregistered(FName InName)
{
	FString Namespace;
	FString Action;
	if (!SplitOperationName(InName, Namespace, Action))
	{
		return;
	}

	FMonolithToolRegistry::Get().UnregisterNamespace(Namespace);

	USMAssistSubsystem* Subsystem = GetAssistSubsystem();
	if (!Subsystem)
	{
		BridgedNamespaces.Remove(Namespace);
		return;
	}

	bool bHasSurvivors = false;
	for (const FSMAssistOperationInfo& Info : Subsystem->GetAllOperationInfos())
	{
		FString InfoNamespace;
		FString InfoAction;
		if (SplitOperationName(Info.Name, InfoNamespace, InfoAction) && InfoNamespace == Namespace)
		{
			HandleOperationRegistered(Info);
			bHasSurvivors = true;
		}
	}

	if (!bHasSurvivors)
	{
		BridgedNamespaces.Remove(Namespace);
	}
}

bool FSMAssistMonolithBridgeModule::SplitOperationName(FName InName, FString& OutNamespace, FString& OutAction)
{
	const FString NameStr = InName.ToString();
	int32 DotIdx = INDEX_NONE;
	if (!NameStr.FindChar(TEXT('.'), DotIdx))
	{
		return false;
	}

	OutNamespace = NameStr.Left(DotIdx);
	OutAction = NameStr.Mid(DotIdx + 1);
	return !OutNamespace.IsEmpty() && !OutAction.IsEmpty();
}

USMAssistSubsystem* FSMAssistMonolithBridgeModule::GetAssistSubsystem() const
{
	return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
}

#else // WITH_MONOLITH

void FSMAssistMonolithBridgeModule::StartupModule() {}
void FSMAssistMonolithBridgeModule::ShutdownModule() {}
void FSMAssistMonolithBridgeModule::BindToAssist() {}
void FSMAssistMonolithBridgeModule::UnbindFromAssist() {}
void FSMAssistMonolithBridgeModule::HandleOperationRegistered(const FSMAssistOperationInfo&) {}
void FSMAssistMonolithBridgeModule::HandleOperationUnregistered(FName) {}

bool FSMAssistMonolithBridgeModule::SplitOperationName(FName, FString&, FString&) { return false; }

USMAssistSubsystem* FSMAssistMonolithBridgeModule::GetAssistSubsystem() const { return nullptr; }

#endif // WITH_MONOLITH

IMPLEMENT_MODULE(FSMAssistMonolithBridgeModule, SMAssistMonolithBridge)
