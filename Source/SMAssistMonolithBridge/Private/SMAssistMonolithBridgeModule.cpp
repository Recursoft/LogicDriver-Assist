// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistMonolithBridgeModule.h"

#include "Modules/ModuleManager.h"

#if WITH_MONOLITH

#include "MonolithToolRegistry.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistSubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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

	// Monolith's tool registry expects a flat param schema: each top-level key is a
	// parameter name mapping to its definition object, which carries a bool "required"
	// field. SMAssist authors schemas in JSON-Schema form ({ "type": "object",
	// "properties": { ... }, "required": [ ... ] }). Without translation Monolith reads
	// "type"/"properties"/"required" as the parameter names, so every real param is
	// reported "unknown" by FMonolithParamSchema::FindUnknownKeys and discovery is misleading.
	static TSharedPtr<FJsonObject> ConvertSchemaToMonolithFormat(const TSharedPtr<FJsonObject>& InSchema)
	{
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (!InSchema.IsValid() || !InSchema->TryGetObjectField(TEXT("properties"), Properties) || !Properties)
		{
			// Already flat (or no parameters); pass through untouched.
			return InSchema;
		}

		TSet<FString> RequiredNames;
		const TArray<TSharedPtr<FJsonValue>>* RequiredArray = nullptr;
		if (InSchema->TryGetArrayField(TEXT("required"), RequiredArray) && RequiredArray)
		{
			for (const TSharedPtr<FJsonValue>& Value : *RequiredArray)
			{
				FString Name;
				if (Value.IsValid() && Value->TryGetString(Name))
				{
					RequiredNames.Add(Name);
				}
			}
		}

		const TSharedRef<FJsonObject> Flat = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
		{
			const TSharedPtr<FJsonObject>* PropObject = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(PropObject) || !PropObject)
			{
				UE_LOG(LogSMAssistMonolithBridge, Warning,
					TEXT("Dropping schema param '%s': its definition is not a JSON object."), *Pair.Key);
				continue;
			}

			// Leaf values are shared with InSchema (still served verbatim on other transports).
			// Every Monolith schema consumer treats the schema as read-only, so the share is safe.
			const TSharedRef<FJsonObject> ParamDef = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*PropObject)->Values)
			{
				ParamDef->SetField(Field.Key, Field.Value);
			}
			ParamDef->SetBoolField(TEXT("required"), RequiredNames.Contains(Pair.Key));
			Flat->SetObjectField(Pair.Key, ParamDef);
		}

		return Flat;
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
			// Failure payloads carry op diagnostics (e.g. compile errors); Monolith only forwards the
			// error string, so fold the payload in rather than dropping it.
			FString ErrorText = Result.ErrorMessage;
			if (Result.Payload.IsValid() && Result.Payload->Values.Num() > 0)
			{
				FString PayloadText;
				const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadText);
				FJsonSerializer::Serialize(Result.Payload.ToSharedRef(), Writer);
				ErrorText = FString::Printf(TEXT("%s Details: %s"), *ErrorText, *PayloadText);
			}
			return FMonolithActionResult::Error(ErrorText);
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

	FMonolithToolRegistry::Get().RegisterAction(Namespace, Action, InInfo.Description, Handler,
		LD::Assist::MonolithBridge::Private::ConvertSchemaToMonolithFormat(InInfo.InputSchema));
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

	// Monolith's registry has no per-action removal, so the whole namespace is wiped and the
	// survivors re-added. That is only safe for namespaces this bridge owns exclusively; never
	// wipe one that some other system registered into.
	if (!BridgedNamespaces.Contains(Namespace))
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
