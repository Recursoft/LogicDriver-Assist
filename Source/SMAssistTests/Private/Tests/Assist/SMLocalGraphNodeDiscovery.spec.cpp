// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"

#include "Helpers/SMTestHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FSMFindNodeTypesSpec, "LogicDriver.Assist.FindNodeTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	FString CreateTransientBlueprint()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return FString();
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), FGuid::NewGuid().ToString());
		Args->SetStringField(TEXT("path"), FAssetHandler::DefaultGamePath());

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("ld.create_blueprint")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return FString();
		}

		FString AssetPath;
		Result.Payload->TryGetStringField(TEXT("asset_path"), AssetPath);
		return AssetPath;
	}

	FString AddState(const FString& InAssetPath, const FString& InStateName)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("state_name"), InStateName);

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("ld.add_state")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return FString();
		}

		FString StateGuid;
		Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
		return StateGuid;
	}

	FString AddConduit(const FString& InAssetPath, const FString& InName)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("state_name"), InName);

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("ld.add_conduit")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return FString();
		}

		FString StateGuid;
		Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
		return StateGuid;
	}

	FString AddTransition(const FString& InAssetPath, const FString& InFromGuid, const FString& InToGuid)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("from_state_guid"), InFromGuid);
		Args->SetStringField(TEXT("to_state_guid"), InToGuid);

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("ld.add_transition")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return FString();
		}

		FString TransitionGuid;
		Result.Payload->TryGetStringField(TEXT("transition_guid"), TransitionGuid);
		return TransitionGuid;
	}

	FSMAssistOperationResult RunFindNodeTypes(const FString& InAssetPath, const FString& InNodeGuid, const FString& InTypeIdFilter = FString())
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("node_guid"), InNodeGuid);
		if (!InTypeIdFilter.IsEmpty())
		{
			Args->SetStringField(TEXT("type_id_filter"), InTypeIdFilter);
		}

		return Subsystem->ExecuteOperation(FName(TEXT("ld.find_node_types")), Args);
	}

	static bool PayloadContainsKind(const TSharedPtr<FJsonObject>& InPayload, const TCHAR* InArrayField, const TCHAR* InKindName)
	{
		const TArray<TSharedPtr<FJsonValue>>* KindsArray = nullptr;
		if (!InPayload->TryGetArrayField(InArrayField, KindsArray))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *KindsArray)
		{
			const TSharedPtr<FJsonObject>* KindObj = nullptr;
			if (!Value->TryGetObject(KindObj))
			{
				continue;
			}
			FString Kind;
			if ((*KindObj)->TryGetStringField(TEXT("kind"), Kind) && Kind == InKindName)
			{
				return true;
			}
		}
		return false;
	}

	static int32 PayloadKindCount(const TSharedPtr<FJsonObject>& InPayload, const TCHAR* InArrayField)
	{
		const TArray<TSharedPtr<FJsonValue>>* KindsArray = nullptr;
		if (!InPayload->TryGetArrayField(InArrayField, KindsArray))
		{
			return 0;
		}
		return KindsArray->Num();
	}
END_DEFINE_SPEC(FSMFindNodeTypesSpec)

void FSMFindNodeTypesSpec::Define()
{
	It("returns state-compatible read kinds in a state graph", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString StateGuid = AddState(AssetPath, TEXT("S1"));
		if (!TestTrue(TEXT("State guid populated"), !StateGuid.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunFindNodeTypes(AssetPath, StateGuid);
		if (!TestTrue(TEXT("Result success"), Result.bSuccess) || !TestTrue(TEXT("Payload valid"), Result.Payload.IsValid()))
		{
			return;
		}

		TestTrue(TEXT("TimeInState present"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("TimeInState")));
		TestTrue(TEXT("HasStateUpdated present"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("HasStateUpdated")));
		TestTrue(TEXT("GetStateInformation present"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("GetStateInformation")));
		TestTrue(TEXT("GetNodeInstance present"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("GetNodeInstance")));

		TestFalse(TEXT("CanEvaluate absent in state graph"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("CanEvaluate")));
		TestFalse(TEXT("CanEvaluateFromEvent absent in state graph"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("CanEvaluateFromEvent")));
		TestFalse(TEXT("GetTransitionInformation absent in state graph"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("GetTransitionInformation")));

		TestEqual(TEXT("no write kinds in state graph"), PayloadKindCount(Result.Payload, TEXT("write_kinds")), 0);

		TestTrue(TEXT("OnStateUpdate event present"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnStateUpdate")));
		TestTrue(TEXT("OnStateEnd event present"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnStateEnd")));
		TestTrue(TEXT("OnInitialized event present in state graph"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnInitialized")));
		TestTrue(TEXT("OnRootStateMachineStart event present in state graph"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnRootStateMachineStart")));
		TestFalse(TEXT("OnTransitionPreEvaluate event absent in state graph"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnTransitionPreEvaluate")));
	});

	It("returns transition-compatible read and write kinds in a transition graph", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString FromGuid = AddState(AssetPath, TEXT("From"));
		const FString ToGuid = AddState(AssetPath, TEXT("To"));
		const FString TransitionGuid = AddTransition(AssetPath, FromGuid, ToGuid);
		if (!TestTrue(TEXT("Transition guid populated"), !TransitionGuid.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunFindNodeTypes(AssetPath, TransitionGuid);
		if (!TestTrue(TEXT("Result success"), Result.bSuccess) || !TestTrue(TEXT("Payload valid"), Result.Payload.IsValid()))
		{
			return;
		}

		TestTrue(TEXT("TimeInState present"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("TimeInState")));
		TestTrue(TEXT("CanEvaluate read present"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("CanEvaluate")));
		TestTrue(TEXT("CanEvaluateFromEvent read present"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("CanEvaluateFromEvent")));
		TestTrue(TEXT("GetTransitionInformation present"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("GetTransitionInformation")));

		TestFalse(TEXT("GetStateInformation absent in transition graph"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("GetStateInformation")));

		TestTrue(TEXT("CanEvaluate write present"), PayloadContainsKind(Result.Payload, TEXT("write_kinds"), TEXT("CanEvaluate")));
		TestTrue(TEXT("CanEvaluateFromEvent write present"), PayloadContainsKind(Result.Payload, TEXT("write_kinds"), TEXT("CanEvaluateFromEvent")));

		TestTrue(TEXT("OnInitialized event present"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnInitialized")));
		TestTrue(TEXT("OnTransitionPreEvaluate event present"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnTransitionPreEvaluate")));
		TestTrue(TEXT("OnTransitionPostEvaluate event present"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnTransitionPostEvaluate")));
		TestTrue(TEXT("OnRootStateMachineStart event present"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnRootStateMachineStart")));
		TestFalse(TEXT("OnStateUpdate event absent in transition graph"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnStateUpdate")));
	});

	It("rejects CanEvaluateFromEvent write in a conduit graph", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString ConduitGuid = AddConduit(AssetPath, TEXT("ConduitDiscover"));
		if (!TestTrue(TEXT("Conduit guid populated"), !ConduitGuid.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunFindNodeTypes(AssetPath, ConduitGuid);
		if (!TestTrue(TEXT("Result success"), Result.bSuccess) || !TestTrue(TEXT("Payload valid"), Result.Payload.IsValid()))
		{
			return;
		}

		TestTrue(TEXT("CanEvaluate write present in conduit"), PayloadContainsKind(Result.Payload, TEXT("write_kinds"), TEXT("CanEvaluate")));
		TestFalse(TEXT("CanEvaluateFromEvent write absent in conduit"), PayloadContainsKind(Result.Payload, TEXT("write_kinds"), TEXT("CanEvaluateFromEvent")));

		// OnInitialized / OnShutdown are conduit-legal and non-singleton, so they are always reported.
		TestTrue(TEXT("OnInitialized event present in conduit"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnInitialized")));
		TestTrue(TEXT("OnShutdown event present in conduit"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnShutdown")));
		// This conduit has no node class, so the root state machine start/stop passthrough nodes are not
		// auto-placed (that only happens on node-class assignment); the singleton stays addable here.
		TestTrue(TEXT("OnRootStateMachineStart event present in conduit"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnRootStateMachineStart")));
		TestFalse(TEXT("OnStateUpdate event absent in conduit"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnStateUpdate")));
		TestFalse(TEXT("OnTransitionPreEvaluate event absent in conduit"), PayloadContainsKind(Result.Payload, TEXT("event_kinds"), TEXT("OnTransitionPreEvaluate")));
	});

	It("type_id_filter narrows results case-insensitively", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString FromGuid = AddState(AssetPath, TEXT("From"));
		const FString ToGuid = AddState(AssetPath, TEXT("To"));
		const FString TransitionGuid = AddTransition(AssetPath, FromGuid, ToGuid);
		if (!TestTrue(TEXT("Transition guid populated"), !TransitionGuid.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunFindNodeTypes(AssetPath, TransitionGuid, TEXT("evaluate"));
		if (!TestTrue(TEXT("Result success"), Result.bSuccess) || !TestTrue(TEXT("Payload valid"), Result.Payload.IsValid()))
		{
			return;
		}

		TestTrue(TEXT("CanEvaluate read survives filter"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("CanEvaluate")));
		TestTrue(TEXT("CanEvaluateFromEvent read survives filter"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("CanEvaluateFromEvent")));
		TestFalse(TEXT("TimeInState filtered out"), PayloadContainsKind(Result.Payload, TEXT("read_kinds"), TEXT("TimeInState")));

		TestTrue(TEXT("CanEvaluate write survives filter"), PayloadContainsKind(Result.Payload, TEXT("write_kinds"), TEXT("CanEvaluate")));
		TestTrue(TEXT("CanEvaluateFromEvent write survives filter"), PayloadContainsKind(Result.Payload, TEXT("write_kinds"), TEXT("CanEvaluateFromEvent")));
	});
}

#endif

#endif
