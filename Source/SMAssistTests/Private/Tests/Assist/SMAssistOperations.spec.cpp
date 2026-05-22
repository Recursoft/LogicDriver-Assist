// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistTestClasses.h"

#include "Helpers/SMTestHelpers.h"
#include "Tests/StructSplit/SMStructSplitTestClasses.h"

#include "Blueprints/SMBlueprint.h"
#include "Graph/Nodes/SMGraphNode_Base.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FAssistOperationsSpec, "LogicDriver.Assist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	static FSMAssistOperationResult MakePingResult(const TSharedRef<FJsonObject>& InArgs)
	{
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("echo"), InArgs->GetStringField(TEXT("value")));
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	static FSMAssistOperationInfo MakePingInfo(FName InName)
	{
		FSMAssistOperationInfo Info;
		Info.Name = InName;
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&FAssistOperationsSpec::MakePingResult);
		return Info;
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
			FName(TEXT("sm.create_blueprint")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return FString();
		}

		FString AssetPath;
		Result.Payload->TryGetStringField(TEXT("asset_path"), AssetPath);
		return AssetPath;
	}

	FString AddStateToBlueprint(const FString& InAssetPath, const FString& InStateName, UClass* InStateClass = nullptr)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return FString();
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("state_name"), InStateName);
		if (InStateClass)
		{
			Args->SetStringField(TEXT("state_class"), InStateClass->GetPathName());
		}

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("sm.add_state")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return FString();
		}

		FString StateGuid;
		Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
		return StateGuid;
	}

END_DEFINE_SPEC(FAssistOperationsSpec)

void FAssistOperationsSpec::Define()
{
	It("Registers and unregisters a custom operation", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("test.ping"));
		TestFalse("Operation not registered initially", Subsystem->HasOperation(OpName));

		const bool bRegistered = Subsystem->RegisterOperation(MakePingInfo(OpName));
		TestTrue("Registration succeeded", bRegistered);
		TestTrue("HasOperation reflects registration", Subsystem->HasOperation(OpName));

		const bool bUnregistered = Subsystem->UnregisterOperation(OpName);
		TestTrue("Unregister succeeded", bUnregistered);
		TestFalse("HasOperation reflects removal", Subsystem->HasOperation(OpName));
	});

	It("Rejects duplicate registrations", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("test.duplicate"));
		TestTrue("First registration succeeds", Subsystem->RegisterOperation(MakePingInfo(OpName)));

		AddExpectedError(TEXT("already registered"), EAutomationExpectedErrorFlags::Contains, 1);
		const bool bSecond = Subsystem->RegisterOperation(MakePingInfo(OpName));
		TestFalse("Second registration fails", bSecond);

		Subsystem->UnregisterOperation(OpName);
	});

	It("Rejects empty names and unbound handlers", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const bool bEmpty = Subsystem->RegisterOperation(MakePingInfo(NAME_None));
		TestFalse("Empty name is rejected", bEmpty);

		FSMAssistOperationInfo UnboundInfo;
		UnboundInfo.Name = FName(TEXT("test.unbound"));
		const bool bUnbound = Subsystem->RegisterOperation(MoveTemp(UnboundInfo));
		TestFalse("Unbound handler is rejected", bUnbound);
	});

	It("Returns registered names in sorted order", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpZebra(TEXT("test.zebra"));
		const FName OpApple(TEXT("test.apple"));
		const FName OpMango(TEXT("test.mango"));

		Subsystem->RegisterOperation(MakePingInfo(OpZebra));
		Subsystem->RegisterOperation(MakePingInfo(OpApple));
		Subsystem->RegisterOperation(MakePingInfo(OpMango));

		const TArray<FName> Names = Subsystem->GetRegisteredOperationNames();

		const int32 AppleIdx = Names.IndexOfByKey(OpApple);
		const int32 MangoIdx = Names.IndexOfByKey(OpMango);
		const int32 ZebraIdx = Names.IndexOfByKey(OpZebra);

		TestTrue("Apple present", AppleIdx != INDEX_NONE);
		TestTrue("Mango present", MangoIdx != INDEX_NONE);
		TestTrue("Zebra present", ZebraIdx != INDEX_NONE);
		TestTrue("Apple precedes Mango", AppleIdx < MangoIdx);
		TestTrue("Mango precedes Zebra", MangoIdx < ZebraIdx);

		Subsystem->UnregisterOperation(OpZebra);
		Subsystem->UnregisterOperation(OpApple);
		Subsystem->UnregisterOperation(OpMango);
	});

	It("Returns an error when executing an unknown operation", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("sm.does_not_exist")), Args);

		TestFalse("Result is failure", Result.bSuccess);
		TestTrue("Error message mentions operation name",
			Result.ErrorMessage.Contains(TEXT("sm.does_not_exist")));
	});

	It("Dispatches through a registered handler and returns its payload", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("test.echo"));
		Subsystem->RegisterOperation(MakePingInfo(OpName));

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("value"), TEXT("hello"));

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(OpName, Args);

		TestTrue("Result is success", Result.bSuccess);
		if (TestTrue("Payload populated", Result.Payload.IsValid()))
		{
			FString Echoed;
			TestTrue("Payload has echo field", Result.Payload->TryGetStringField(TEXT("echo"), Echoed));
			TestEqual("Echo matches input", Echoed, FString(TEXT("hello")));
		}

		Subsystem->UnregisterOperation(OpName);
	});

	It("Exposes description and input schema for a registered operation", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FSMAssistOperationInfo* Info = Subsystem->FindOperationInfo(FName(TEXT("sm.create_blueprint")));
		if (!TestNotNull("sm.create_blueprint metadata available", Info))
		{
			return;
		}

		TestFalse("Description is populated", Info->Description.IsEmpty());

		if (!TestTrue("Input schema is valid", Info->InputSchema.IsValid()))
		{
			return;
		}

		FString SchemaType;
		TestTrue("Schema type is 'object'",
			Info->InputSchema->TryGetStringField(TEXT("type"), SchemaType) && SchemaType == TEXT("object"));

		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (TestTrue("Schema has properties", Info->InputSchema->TryGetObjectField(TEXT("properties"), Properties)))
		{
			TestTrue("Schema declares 'name' property", (*Properties)->HasField(TEXT("name")));
		}

		const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
		if (TestTrue("Schema has required array", Info->InputSchema->TryGetArrayField(TEXT("required"), Required)))
		{
			TestTrue("'name' is required", Required->ContainsByPredicate([](const TSharedPtr<FJsonValue>& V)
			{
				return V.IsValid() && V->AsString() == TEXT("name");
			}));
		}
	});

	It("GetAllOperationInfos returns every built-in operation sorted", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const TArray<FSMAssistOperationInfo> Infos = Subsystem->GetAllOperationInfos();
		TestTrue("At least five built-in operations", Infos.Num() >= 5);

		for (int32 Idx = 1; Idx < Infos.Num(); ++Idx)
		{
			TestTrue(
				FString::Printf(TEXT("Sorted at %d (%s < %s)"), Idx,
					*Infos[Idx - 1].Name.ToString(), *Infos[Idx].Name.ToString()),
				Infos[Idx - 1].Name.LexicalLess(Infos[Idx].Name));
		}

		const bool bFoundCreate = Infos.ContainsByPredicate([](const FSMAssistOperationInfo& InInfo)
		{
			return InInfo.Name == FName(TEXT("sm.create_blueprint"));
		});
		TestTrue("sm.create_blueprint is included", bFoundCreate);
	});

	It("Broadcasts OnOperationRegistered when a new op is registered", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("test.delegate_register"));

		int32 CallCount = 0;
		FName CapturedName;
		FDelegateHandle Handle = Subsystem->OnOperationRegistered().AddLambda(
			[&CallCount, &CapturedName](const FSMAssistOperationInfo& InInfo)
			{
				++CallCount;
				CapturedName = InInfo.Name;
			});

		Subsystem->RegisterOperation(MakePingInfo(OpName));

		TestEqual("OnOperationRegistered fired once", CallCount, 1);
		TestEqual("Delegate received the correct name", CapturedName, OpName);

		Subsystem->OnOperationRegistered().Remove(Handle);
		Subsystem->UnregisterOperation(OpName);
	});

	It("Broadcasts OnOperationUnregistered on removal only", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("test.delegate_unregister"));
		Subsystem->RegisterOperation(MakePingInfo(OpName));

		int32 CallCount = 0;
		FName CapturedName;
		FDelegateHandle Handle = Subsystem->OnOperationUnregistered().AddLambda(
			[&CallCount, &CapturedName](FName InName)
			{
				++CallCount;
				CapturedName = InName;
			});

		Subsystem->UnregisterOperation(OpName);
		TestEqual("OnOperationUnregistered fired once", CallCount, 1);
		TestEqual("Delegate received the correct name", CapturedName, OpName);

		Subsystem->UnregisterOperation(OpName);
		TestEqual("Removing a missing op does not fire the delegate", CallCount, 1);

		Subsystem->OnOperationUnregistered().Remove(Handle);
	});

	It("Does not broadcast OnOperationRegistered on a duplicate registration", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("test.delegate_duplicate"));
		Subsystem->RegisterOperation(MakePingInfo(OpName));

		int32 CallCount = 0;
		FDelegateHandle Handle = Subsystem->OnOperationRegistered().AddLambda(
			[&CallCount](const FSMAssistOperationInfo&) { ++CallCount; });

		AddExpectedError(TEXT("already registered"), EAutomationExpectedErrorFlags::Contains, 1);
		const bool bSecond = Subsystem->RegisterOperation(MakePingInfo(OpName));
		TestFalse("Duplicate registration fails", bSecond);
		TestEqual("Delegate not fired for duplicate", CallCount, 0);

		Subsystem->OnOperationRegistered().Remove(Handle);
		Subsystem->UnregisterOperation(OpName);
	});

	Describe("sm.create_blueprint", [this]()
	{
		It("Fails when the 'name' arg is missing", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.create_blueprint")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'name'", Result.ErrorMessage.Contains(TEXT("name")));
		});

		It("Fails when 'name' is empty", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), TEXT(""));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.create_blueprint")), Args);

			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Creates a blueprint and returns its asset path", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const FString AssetName = FGuid::NewGuid().ToString();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), AssetName);
			Args->SetStringField(TEXT("path"), FAssetHandler::DefaultGamePath());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.create_blueprint")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString ReturnedName;
			TestTrue("Payload has 'name'", Result.Payload->TryGetStringField(TEXT("name"), ReturnedName));
			TestEqual("Payload name matches request", ReturnedName, AssetName);

			FString AssetPath;
			TestTrue("Payload has 'asset_path'",
				Result.Payload->TryGetStringField(TEXT("asset_path"), AssetPath));
			TestTrue("Asset path starts with default game path",
				AssetPath.StartsWith(FAssetHandler::DefaultGamePath()));
		});
	});

	Describe("sm.add_state", [this]()
	{
		It("Fails when 'asset_path' is missing", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_state")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'asset_path'", Result.ErrorMessage.Contains(TEXT("asset_path")));
		});

		It("Fails when asset does not exist", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), TEXT("/Game/DoesNotExist.DoesNotExist"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_state")), Args);

			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Adds a state to a blueprint and returns its guid", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("state_name"), TEXT("MyFirstState"));
			Args->SetBoolField(TEXT("is_entry"), true);
			Args->SetNumberField(TEXT("position_x"), 200.0);
			Args->SetNumberField(TEXT("position_y"), 50.0);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_state")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString StateGuid;
			TestTrue("Payload has 'state_guid'",
				Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid));
			TestFalse("State guid is non-empty", StateGuid.IsEmpty());

			FString StateName;
			TestTrue("Payload has 'state_name'",
				Result.Payload->TryGetStringField(TEXT("state_name"), StateName));
			TestEqual("State name matches request", StateName, FString(TEXT("MyFirstState")));
		});
	});

	Describe("sm.add_transition", [this]()
	{
		It("Fails when guids are missing", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), TEXT("/Game/Whatever.Whatever"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_transition")), Args);

			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Connects two states and returns a transition guid", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString FromGuid = AddStateToBlueprint(AssetPath, TEXT("From"));
			const FString ToGuid = AddStateToBlueprint(AssetPath, TEXT("To"));
			if (!TestFalse("From guid populated", FromGuid.IsEmpty())
				|| !TestFalse("To guid populated", ToGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("from_state_guid"), FromGuid);
			Args->SetStringField(TEXT("to_state_guid"), ToGuid);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_transition")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString TransitionGuid;
			TestTrue("Payload has 'transition_guid'",
				Result.Payload->TryGetStringField(TEXT("transition_guid"), TransitionGuid));
			TestFalse("Transition guid non-empty", TransitionGuid.IsEmpty());
		});

		It("Fails when the from-state guid is unknown", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString ToGuid = AddStateToBlueprint(AssetPath, TEXT("Only"));
			if (!TestFalse("To guid populated", ToGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("from_state_guid"), FGuid::NewGuid().ToString());
			Args->SetStringField(TEXT("to_state_guid"), ToGuid);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_transition")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'from'", Result.ErrorMessage.Contains(TEXT("from")));
		});
	});

	Describe("sm.list_assets", [this]()
	{
		It("Lists assets under a path prefix", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("path_prefix"), FAssetHandler::DefaultGamePath());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.list_assets")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
			if (!TestTrue("Payload has 'assets'",
				Result.Payload->TryGetArrayField(TEXT("assets"), Assets)))
			{
				return;
			}

			bool bFound = false;
			for (const TSharedPtr<FJsonValue>& Value : *Assets)
			{
				const TSharedPtr<FJsonObject>* Entry = nullptr;
				if (!Value->TryGetObject(Entry) || !Entry->IsValid())
				{
					continue;
				}

				FString EntryPath;
				if ((*Entry)->TryGetStringField(TEXT("asset_path"), EntryPath)
					&& EntryPath == AssetPath)
				{
					bFound = true;
					break;
				}
			}
			TestTrue("Created blueprint appears in list", bFound);
		});
	});

	Describe("sm.get_asset", [this]()
	{
		It("Returns the asset structure including states and transitions", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString FromGuid = AddStateToBlueprint(AssetPath, TEXT("Alpha"));
			const FString ToGuid = AddStateToBlueprint(AssetPath, TEXT("Beta"));
			if (!TestFalse("From guid populated", FromGuid.IsEmpty())
				|| !TestFalse("To guid populated", ToGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			{
				const TSharedRef<FJsonObject> TransitionArgs = MakeShared<FJsonObject>();
				TransitionArgs->SetStringField(TEXT("asset_path"), AssetPath);
				TransitionArgs->SetStringField(TEXT("from_state_guid"), FromGuid);
				TransitionArgs->SetStringField(TEXT("to_state_guid"), ToGuid);
				const FSMAssistOperationResult TransitionResult = Subsystem->ExecuteOperation(
					FName(TEXT("sm.add_transition")), TransitionArgs);
				TestTrue("Transition added", TransitionResult.bSuccess);
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.get_asset")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
			TestTrue("Payload has 'states'",
				Result.Payload->TryGetArrayField(TEXT("states"), States));
			if (States)
			{
				TestEqual("Two states present", States->Num(), 2);
			}

			const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
			TestTrue("Payload has 'transitions'",
				Result.Payload->TryGetArrayField(TEXT("transitions"), Transitions));
			if (Transitions)
			{
				TestEqual("One transition present", Transitions->Num(), 1);
			}
		});

		It("Fails when the asset does not exist", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), TEXT("/Game/DoesNotExist.DoesNotExist"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.get_asset")), Args);

			TestFalse("Result is failure", Result.bSuccess);
		});
	});

	Describe("sm.remove_node", [this]()
	{
		It("Removes a transition by guid", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString FromGuid = AddStateToBlueprint(AssetPath, TEXT("A"));
			const FString ToGuid = AddStateToBlueprint(AssetPath, TEXT("B"));
			if (!TestFalse("From guid populated", FromGuid.IsEmpty())
				|| !TestFalse("To guid populated", ToGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			FString TransitionGuid;
			{
				const TSharedRef<FJsonObject> TransitionArgs = MakeShared<FJsonObject>();
				TransitionArgs->SetStringField(TEXT("asset_path"), AssetPath);
				TransitionArgs->SetStringField(TEXT("from_state_guid"), FromGuid);
				TransitionArgs->SetStringField(TEXT("to_state_guid"), ToGuid);
				const FSMAssistOperationResult AddResult = Subsystem->ExecuteOperation(
					FName(TEXT("sm.add_transition")), TransitionArgs);
				if (!TestTrue("Transition added", AddResult.bSuccess && AddResult.Payload.IsValid()))
				{
					return;
				}
				AddResult.Payload->TryGetStringField(TEXT("transition_guid"), TransitionGuid);
			}

			const TSharedRef<FJsonObject> RemoveArgs = MakeShared<FJsonObject>();
			RemoveArgs->SetStringField(TEXT("asset_path"), AssetPath);
			RemoveArgs->SetStringField(TEXT("node_guid"), TransitionGuid);

			const FSMAssistOperationResult RemoveResult = Subsystem->ExecuteOperation(
				FName(TEXT("sm.remove_node")), RemoveArgs);
			TestTrue("Remove result is success", RemoveResult.bSuccess);

			const TSharedRef<FJsonObject> GetArgs = MakeShared<FJsonObject>();
			GetArgs->SetStringField(TEXT("asset_path"), AssetPath);
			const FSMAssistOperationResult GetResult = Subsystem->ExecuteOperation(
				FName(TEXT("sm.get_asset")), GetArgs);

			const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
			if (GetResult.Payload.IsValid()
				&& GetResult.Payload->TryGetArrayField(TEXT("transitions"), Transitions))
			{
				TestEqual("No transitions remain", Transitions->Num(), 0);
			}
		});

		It("Fails when the guid cannot be found", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), FGuid::NewGuid().ToString());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.remove_node")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when 'node_guid' is not a valid guid", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), TEXT("not-a-guid"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.remove_node")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions the bad guid", Result.ErrorMessage.Contains(TEXT("not-a-guid")));
		});
	});

	Describe("sm.set_node_property", [this]()
	{
		It("Fails when the property does not exist on the node", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("Solo"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("property_name"), TEXT("DefinitelyNotAProperty"));
			Args->SetStringField(TEXT("value"), TEXT("x"));

			AddExpectedError(TEXT("Could not locate property DefinitelyNotAProperty"),
				EAutomationExpectedErrorFlags::Contains, 1);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error names the property", Result.ErrorMessage.Contains(TEXT("DefinitelyNotAProperty")));
		});

		It("Rejects object values with a clear error", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("Solo"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("property_name"), TEXT("Anything"));
			Args->SetObjectField(TEXT("value"), MakeShared<FJsonObject>());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions value types", Result.ErrorMessage.Contains(TEXT("Value must be")));
		});

		It("Writes an array value and reports element_count", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("Solo"),
				USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> Elements;
			Elements.Add(MakeShared<FJsonValueString>(TEXT("alpha")));
			Elements.Add(MakeShared<FJsonValueString>(TEXT("beta")));
			Elements.Add(MakeShared<FJsonValueString>(TEXT("gamma")));

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("property_name"), TEXT("StringArray"));
			Args->SetArrayField(TEXT("value"), Elements);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			double ElementCount = 0.0;
			TestTrue("Payload has element_count",
				Result.Payload->TryGetNumberField(TEXT("element_count"), ElementCount));
			TestEqual("element_count matches array length", (int32)ElementCount, 3);
		});

		It("Rejects 'array_index' combined with an array value", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("Solo"),
				USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> Elements;
			Elements.Add(MakeShared<FJsonValueString>(TEXT("only")));

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("property_name"), TEXT("StringArray"));
			Args->SetArrayField(TEXT("value"), Elements);
			Args->SetNumberField(TEXT("array_index"), 2);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions array_index",
				Result.ErrorMessage.Contains(TEXT("array_index")));
		});

		It("Supports 'array_action=remove' at the requested index", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("Solo"),
				USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> Elements;
			Elements.Add(MakeShared<FJsonValueString>(TEXT("a")));
			Elements.Add(MakeShared<FJsonValueString>(TEXT("b")));

			const TSharedRef<FJsonObject> SetArgs = MakeShared<FJsonObject>();
			SetArgs->SetStringField(TEXT("asset_path"), AssetPath);
			SetArgs->SetStringField(TEXT("node_guid"), StateGuid);
			SetArgs->SetStringField(TEXT("property_name"), TEXT("StringArray"));
			SetArgs->SetArrayField(TEXT("value"), Elements);
			const FSMAssistOperationResult SetResult = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), SetArgs);
			if (!TestTrue("Seed array succeeded", SetResult.bSuccess))
			{
				return;
			}

			const TSharedRef<FJsonObject> RemoveArgs = MakeShared<FJsonObject>();
			RemoveArgs->SetStringField(TEXT("asset_path"), AssetPath);
			RemoveArgs->SetStringField(TEXT("node_guid"), StateGuid);
			RemoveArgs->SetStringField(TEXT("property_name"), TEXT("StringArray"));
			RemoveArgs->SetStringField(TEXT("array_action"), TEXT("remove"));
			RemoveArgs->SetNumberField(TEXT("array_index"), 0);

			const FSMAssistOperationResult RemoveResult = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), RemoveArgs);
			TestTrue("Remove result is success", RemoveResult.bSuccess);
			if (!TestTrue("Remove payload populated", RemoveResult.Payload.IsValid()))
			{
				return;
			}

			FString Action;
			TestTrue("Payload action is 'remove'",
				RemoveResult.Payload->TryGetStringField(TEXT("array_action"), Action) && Action == TEXT("remove"));
		});

		It("Fails when 'array_action=remove' omits array_index", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("Solo"),
				USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("property_name"), TEXT("StringArray"));
			Args->SetStringField(TEXT("array_action"), TEXT("remove"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions array_index",
				Result.ErrorMessage.Contains(TEXT("array_index")));
		});

		It("Supports 'array_action=clear'", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("Solo"),
				USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("property_name"), TEXT("StringArray"));
			Args->SetStringField(TEXT("array_action"), TEXT("clear"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString Action;
			TestTrue("Payload action is 'clear'",
				Result.Payload->TryGetStringField(TEXT("array_action"), Action) && Action == TEXT("clear"));
		});

		It("Rejects an unknown array_action", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("Solo"),
				USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("property_name"), TEXT("StringArray"));
			Args->SetStringField(TEXT("array_action"), TEXT("wat"));
			Args->SetStringField(TEXT("value"), TEXT("x"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions unknown action",
				Result.ErrorMessage.Contains(TEXT("array_action")));
		});
	});

	Describe("sm.compile", [this]()
	{
		It("Compiles a freshly created blueprint successfully", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			AddStateToBlueprint(AssetPath, TEXT("Only"));

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.compile")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			bool bUpToDate = false;
			TestTrue("Payload has 'up_to_date'",
				Result.Payload->TryGetBoolField(TEXT("up_to_date"), bUpToDate));
			TestTrue("Blueprint is up to date", bUpToDate);

			bool bHasErrors = true;
			Result.Payload->TryGetBoolField(TEXT("has_errors"), bHasErrors);
			TestFalse("No compile errors", bHasErrors);
		});

		It("Fails when asset_path is missing", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.compile")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});
	});

	Describe("sm.rename_state", [this]()
	{
		It("Renames a state node and returns the new name", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("Old"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("state_guid"), StateGuid);
			Args->SetStringField(TEXT("new_name"), TEXT("NewShiny"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.rename_state")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString ReturnedName;
			TestTrue("Payload has 'state_name'",
				Result.Payload->TryGetStringField(TEXT("state_name"), ReturnedName));
			TestEqual("State name matches request", ReturnedName, FString(TEXT("NewShiny")));
		});

		It("Fails when the state guid is unknown", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("state_guid"), FGuid::NewGuid().ToString());
			Args->SetStringField(TEXT("new_name"), TEXT("Anything"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.rename_state")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});
	});

	Describe("sm.set_initial_state", [this]()
	{
		It("Rewires the entry pin to the target state", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString FirstGuid = AddStateToBlueprint(AssetPath, TEXT("First"));
			const FString SecondGuid = AddStateToBlueprint(AssetPath, TEXT("Second"));
			if (!TestFalse("Second state guid populated", SecondGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("state_guid"), SecondGuid);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_initial_state")), Args);
			TestTrue("Result is success", Result.bSuccess);

			const TSharedRef<FJsonObject> CompileArgs = MakeShared<FJsonObject>();
			CompileArgs->SetStringField(TEXT("asset_path"), AssetPath);
			const FSMAssistOperationResult CompileResult = Subsystem->ExecuteOperation(
				FName(TEXT("sm.compile")), CompileArgs);
			TestTrue("Compile after re-wire succeeded", CompileResult.bSuccess);
		});

		It("Fails when the state guid cannot be found", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("state_guid"), FGuid::NewGuid().ToString());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_initial_state")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});
	});

	Describe("sm.add_state_stack", [this]()
	{
		It("Adds a state stack entry and returns its index and template guid", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("WithStack"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("state_guid"), StateGuid);
			Args->SetStringField(TEXT("state_class"),
				USMAssistArrayStateInstance::StaticClass()->GetPathName());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_state_stack")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			double ResolvedIndex = -1.0;
			TestTrue("Payload has stack_index",
				Result.Payload->TryGetNumberField(TEXT("stack_index"), ResolvedIndex));
			TestEqual("Stack index is 0 for first element", (int32)ResolvedIndex, 0);

			FString TemplateGuid;
			TestTrue("Payload has template_guid",
				Result.Payload->TryGetStringField(TEXT("template_guid"), TemplateGuid));
			TestFalse("Template guid is populated", TemplateGuid.IsEmpty());
		});

		It("Targets a stack template when sm.set_node_property passes stack_index", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("StackHost"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> StackArgs = MakeShared<FJsonObject>();
			StackArgs->SetStringField(TEXT("asset_path"), AssetPath);
			StackArgs->SetStringField(TEXT("state_guid"), StateGuid);
			StackArgs->SetStringField(TEXT("state_class"),
				USMAssistArrayStateInstance::StaticClass()->GetPathName());

			const FSMAssistOperationResult StackResult = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_state_stack")), StackArgs);
			if (!TestTrue("Stack add succeeded", StackResult.bSuccess))
			{
				return;
			}

			const TSharedRef<FJsonObject> PropArgs = MakeShared<FJsonObject>();
			PropArgs->SetStringField(TEXT("asset_path"), AssetPath);
			PropArgs->SetStringField(TEXT("node_guid"), StateGuid);
			PropArgs->SetStringField(TEXT("property_name"), TEXT("SingleString"));
			PropArgs->SetStringField(TEXT("value"), TEXT("stacked"));
			PropArgs->SetNumberField(TEXT("stack_index"), 0);

			const FSMAssistOperationResult PropResult = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), PropArgs);
			TestTrue("Set via stack_index succeeded", PropResult.bSuccess);
			if (!TestTrue("Payload populated", PropResult.Payload.IsValid()))
			{
				return;
			}

			double Echoed = -1.0;
			TestTrue("Payload echoes stack_index",
				PropResult.Payload->TryGetNumberField(TEXT("stack_index"), Echoed));
			TestEqual("stack_index echo matches", (int32)Echoed, 0);
		});

		It("Fails when stack_index targets a missing stack template", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("NoStack"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("property_name"), TEXT("SingleString"));
			Args->SetStringField(TEXT("value"), TEXT("won't land"));
			Args->SetNumberField(TEXT("stack_index"), 5);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error names stack_index",
				Result.ErrorMessage.Contains(TEXT("stack template at index")));
		});
	});

	Describe("sm.add_transition_stack", [this]()
	{
		It("Adds a transition stack entry and returns its index and template guid", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString FromGuid = AddStateToBlueprint(AssetPath, TEXT("From"));
			const FString ToGuid = AddStateToBlueprint(AssetPath, TEXT("To"));
			if (!TestFalse("From guid populated", FromGuid.IsEmpty())
				|| !TestFalse("To guid populated", ToGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			FString TransitionGuid;
			{
				const TSharedRef<FJsonObject> TransitionArgs = MakeShared<FJsonObject>();
				TransitionArgs->SetStringField(TEXT("asset_path"), AssetPath);
				TransitionArgs->SetStringField(TEXT("from_state_guid"), FromGuid);
				TransitionArgs->SetStringField(TEXT("to_state_guid"), ToGuid);
				const FSMAssistOperationResult TransitionResult = Subsystem->ExecuteOperation(
					FName(TEXT("sm.add_transition")), TransitionArgs);
				if (!TestTrue("Transition added", TransitionResult.bSuccess)
					|| !TestTrue("Payload populated", TransitionResult.Payload.IsValid()))
				{
					return;
				}
				TestTrue("Payload has 'transition_guid'",
					TransitionResult.Payload->TryGetStringField(TEXT("transition_guid"), TransitionGuid));
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("transition_guid"), TransitionGuid);
			Args->SetStringField(TEXT("transition_class"),
				USMAssistTestTransitionInstance::StaticClass()->GetPathName());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_transition_stack")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			double ResolvedIndex = -1.0;
			TestTrue("Payload has stack_index",
				Result.Payload->TryGetNumberField(TEXT("stack_index"), ResolvedIndex));
			TestEqual("Stack index is 0 for first element", (int32)ResolvedIndex, 0);

			FString TemplateGuid;
			TestTrue("Payload has template_guid",
				Result.Payload->TryGetStringField(TEXT("template_guid"), TemplateGuid));
			TestFalse("Template guid is populated", TemplateGuid.IsEmpty());
		});

		It("Fails when the transition guid is unknown", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("transition_guid"), FGuid::NewGuid().ToString());
			Args->SetStringField(TEXT("transition_class"),
				USMAssistTestTransitionInstance::StaticClass()->GetPathName());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_transition_stack")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Stack entry participates in evaluation; SM compiles and reaches end state", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			// Two states; first is the entry.
			FString StartGuid;
			{
				const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("asset_path"), AssetPath);
				Args->SetStringField(TEXT("state_name"), TEXT("Start"));
				Args->SetBoolField(TEXT("is_entry"), true);
				const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
					FName(TEXT("sm.add_state")), Args);
				if (!TestTrue("Start state added", Result.bSuccess && Result.Payload.IsValid()))
				{
					return;
				}
				Result.Payload->TryGetStringField(TEXT("state_guid"), StartGuid);
			}

			const FString EndGuid = AddStateToBlueprint(AssetPath, TEXT("End"));
			if (!TestFalse("End guid populated", EndGuid.IsEmpty()))
			{
				return;
			}

			// Transition with a true-returning class so the primary half of the AND chain passes.
			FString TransitionGuid;
			{
				const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("asset_path"), AssetPath);
				Args->SetStringField(TEXT("from_state_guid"), StartGuid);
				Args->SetStringField(TEXT("to_state_guid"), EndGuid);
				Args->SetStringField(TEXT("transition_class"),
					USMAssistTestTransitionInstance::StaticClass()->GetPathName());
				const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
					FName(TEXT("sm.add_transition")), Args);
				if (!TestTrue("Transition added", Result.bSuccess && Result.Payload.IsValid()))
				{
					return;
				}
				Result.Payload->TryGetStringField(TEXT("transition_guid"), TransitionGuid);
			}

			// True stack entry; AND chain stays true so the SM transitions.
			{
				const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("asset_path"), AssetPath);
				Args->SetStringField(TEXT("transition_guid"), TransitionGuid);
				Args->SetStringField(TEXT("transition_class"),
					USMAssistTestTransitionInstance::StaticClass()->GetPathName());
				const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
					FName(TEXT("sm.add_transition_stack")), Args);
				if (!TestTrue("Stack entry added", Result.bSuccess))
				{
					return;
				}
			}

			USMBlueprint* Blueprint = Cast<USMBlueprint>(FSoftObjectPath(AssetPath).TryLoad());
			if (!TestNotNull("Blueprint loaded for run", Blueprint))
			{
				return;
			}

			int32 EntryHits = 0;
			int32 UpdateHits = 0;
			int32 EndHits = 0;
			TestHelpers::RunStateMachineToCompletion(this, Blueprint, EntryHits, UpdateHits, EndHits);
		});
	});

	Describe("sm.add_conduit", [this]()
	{
		It("Fails when 'asset_path' is missing", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_conduit")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'asset_path'", Result.ErrorMessage.Contains(TEXT("asset_path")));
		});

		It("Adds a conduit and returns its guid and class", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("state_name"), TEXT("MyConduit"));
			Args->SetBoolField(TEXT("eval_with_transitions"), true);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_conduit")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString StateGuid;
			TestTrue("Payload has 'state_guid'",
				Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid));
			TestFalse("State guid non-empty", StateGuid.IsEmpty());

			FString StateName;
			TestTrue("Payload has 'state_name'",
				Result.Payload->TryGetStringField(TEXT("state_name"), StateName));
			TestEqual("State name matches request", StateName, FString(TEXT("MyConduit")));

			FString StateClass;
			TestTrue("Payload has 'state_class'",
				Result.Payload->TryGetStringField(TEXT("state_class"), StateClass));
			TestTrue("Class path references a conduit node", StateClass.Contains(TEXT("Conduit")));
		});

		It("Rejects a state_class that is not a USMConduitInstance subclass", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("state_class"),
				USMAssistArrayStateInstance::StaticClass()->GetPathName());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_conduit")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions conduit", Result.ErrorMessage.Contains(TEXT("Conduit")));
		});
	});

	Describe("sm.add_reference", [this]()
	{
		It("Adds a reference state with no target when 'reference_asset_path' is omitted", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_reference")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (TestTrue("Payload present", Result.Payload.IsValid()))
			{
				FString StateGuid;
				TestTrue("Payload has state_guid", Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid));
				TestFalse("Payload omits reference_asset_path when target is unset",
					Result.Payload->HasField(TEXT("reference_asset_path")));
			}
		});

		It("Adds a reference node pointing at another SMBlueprint", [this]()
		{
			const FString HostPath = CreateTransientBlueprint();
			const FString ReferencedPath = CreateTransientBlueprint();
			if (!TestFalse("Host blueprint created", HostPath.IsEmpty())
				|| !TestFalse("Referenced blueprint created", ReferencedPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), HostPath);
			Args->SetStringField(TEXT("reference_asset_path"), ReferencedPath);
			Args->SetStringField(TEXT("state_name"), TEXT("RefNode"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_reference")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString StateGuid;
			TestTrue("Payload has 'state_guid'",
				Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid));
			TestFalse("State guid non-empty", StateGuid.IsEmpty());

			FString EchoedRefPath;
			TestTrue("Payload has 'reference_asset_path'",
				Result.Payload->TryGetStringField(TEXT("reference_asset_path"), EchoedRefPath));
			TestTrue("Echo matches referenced blueprint",
				EchoedRefPath.Contains(FPaths::GetBaseFilename(ReferencedPath)));
		});

		It("Rejects a self-reference", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("reference_asset_path"), AssetPath);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_reference")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions self-reference",
				Result.ErrorMessage.Contains(TEXT("reference itself")));
		});
	});

	Describe("sm.get_node_properties", [this]()
	{
		It("Fails when 'node_guid' is missing", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.get_node_properties")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'node_guid'", Result.ErrorMessage.Contains(TEXT("node_guid")));
		});

		It("Returns the editable properties on a state's template", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(
				AssetPath, TEXT("Inspected"), USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.get_node_properties")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
			if (!TestTrue("Payload has 'properties' array",
				Result.Payload->TryGetArrayField(TEXT("properties"), Properties)))
			{
				return;
			}

			bool bFoundSingleString = false;
			bool bFoundStringArray = false;
			for (const TSharedPtr<FJsonValue>& Entry : *Properties)
			{
				const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
				if (!Obj.IsValid())
				{
					continue;
				}

				FString PropName;
				Obj->TryGetStringField(TEXT("name"), PropName);
				if (PropName == TEXT("SingleString"))
				{
					bFoundSingleString = true;
					FString TypeStr;
					Obj->TryGetStringField(TEXT("type"), TypeStr);
					TestEqual("SingleString type is FString", TypeStr, FString(TEXT("FString")));
				}
				else if (PropName == TEXT("StringArray"))
				{
					bFoundStringArray = true;
				}
			}
			TestTrue("Found SingleString", bFoundSingleString);
			TestTrue("Found StringArray", bFoundStringArray);
		});

		It("Fails when 'node_guid' does not resolve", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), FGuid::NewGuid().ToString());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.get_node_properties")), Args);

			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Targets a stack template when 'stack_index' is provided", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("StackHost"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> StackArgs = MakeShared<FJsonObject>();
			StackArgs->SetStringField(TEXT("asset_path"), AssetPath);
			StackArgs->SetStringField(TEXT("state_guid"), StateGuid);
			StackArgs->SetStringField(TEXT("state_class"),
				USMAssistArrayStateInstance::StaticClass()->GetPathName());

			const FSMAssistOperationResult StackResult = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_state_stack")), StackArgs);
			if (!TestTrue("Stack add succeeded", StackResult.bSuccess))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetNumberField(TEXT("stack_index"), 0);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.get_node_properties")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			double Echoed = -1.0;
			TestTrue("Payload echoes stack_index",
				Result.Payload->TryGetNumberField(TEXT("stack_index"), Echoed));
			TestEqual("stack_index echo matches", (int32)Echoed, 0);
		});
	});

	Describe("sm.set_transition_condition", [this]()
	{
		It("Fails when 'transition_guid' is missing", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetBoolField(TEXT("condition"), true);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_transition_condition")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'transition_guid'",
				Result.ErrorMessage.Contains(TEXT("transition_guid")));
		});

		It("Fails when 'condition' is missing", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("transition_guid"), FGuid::NewGuid().ToString());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_transition_condition")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'condition'", Result.ErrorMessage.Contains(TEXT("condition")));
		});

		It("Fails when the node is not a transition edge", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("NotATransition"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("transition_guid"), StateGuid);
			Args->SetBoolField(TEXT("condition"), true);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_transition_condition")), Args);

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'transition edge'",
				Result.ErrorMessage.Contains(TEXT("transition edge")));
		});

		It("Sets a default-class transition to true and reaches the end state", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			FString StartGuid;
			{
				const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("asset_path"), AssetPath);
				Args->SetStringField(TEXT("state_name"), TEXT("Start"));
				Args->SetBoolField(TEXT("is_entry"), true);
				const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
					FName(TEXT("sm.add_state")), Args);
				if (!TestTrue("Start state added", Result.bSuccess && Result.Payload.IsValid()))
				{
					return;
				}
				Result.Payload->TryGetStringField(TEXT("state_guid"), StartGuid);
			}

			const FString EndGuid = AddStateToBlueprint(AssetPath, TEXT("End"));
			if (!TestFalse("End guid populated", EndGuid.IsEmpty()))
			{
				return;
			}

			FString TransitionGuid;
			{
				const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("asset_path"), AssetPath);
				Args->SetStringField(TEXT("from_state_guid"), StartGuid);
				Args->SetStringField(TEXT("to_state_guid"), EndGuid);
				const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
					FName(TEXT("sm.add_transition")), Args);
				if (!TestTrue("Transition added", Result.bSuccess && Result.Payload.IsValid()))
				{
					return;
				}
				Result.Payload->TryGetStringField(TEXT("transition_guid"), TransitionGuid);
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("transition_guid"), TransitionGuid);
			Args->SetBoolField(TEXT("condition"), true);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_transition_condition")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			bool bEchoedCondition = false;
			TestTrue("Payload echoes condition",
				Result.Payload->TryGetBoolField(TEXT("condition"), bEchoedCondition));
			TestTrue("Echoed condition is true", bEchoedCondition);

			USMBlueprint* Blueprint = Cast<USMBlueprint>(FSoftObjectPath(AssetPath).TryLoad());
			if (!TestNotNull("Blueprint loaded for run", Blueprint))
			{
				return;
			}

			int32 EntryHits = 0;
			int32 UpdateHits = 0;
			int32 EndHits = 0;
			TestHelpers::RunStateMachineToCompletion(this, Blueprint, EntryHits, UpdateHits, EndHits);
		});
	});

	Describe("sm.split_pin / sm.recombine_pin", [this]()
	{
		auto AddSplitState = [this](const FString& InAssetPath) -> FString
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!Subsystem)
			{
				return FString();
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), InAssetPath);
			Args->SetStringField(TEXT("state_name"), TEXT("SplitState"));
			Args->SetStringField(TEXT("state_class"), USMAssistSplitTestState::StaticClass()->GetPathName());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_state")), Args);
			if (!Result.bSuccess || !Result.Payload.IsValid())
			{
				return FString();
			}

			FString StateGuid;
			Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
			return StateGuid;
		};

		auto Split = [this](const FString& InAssetPath, const FString& InStateGuid, const FString& InVar, const FString& InPinId = FString())
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), InAssetPath);
			Args->SetStringField(TEXT("node_guid"), InStateGuid);
			Args->SetStringField(TEXT("variable_name"), InVar);
			if (!InPinId.IsEmpty())
			{
				Args->SetStringField(TEXT("pin_id"), InPinId);
			}
			return Subsystem->ExecuteOperation(FName(TEXT("sm.split_pin")), Args);
		};

		auto Recombine = [this](const FString& InAssetPath, const FString& InStateGuid, const FString& InVar, const FString& InPinId = FString())
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), InAssetPath);
			Args->SetStringField(TEXT("node_guid"), InStateGuid);
			Args->SetStringField(TEXT("variable_name"), InVar);
			if (!InPinId.IsEmpty())
			{
				Args->SetStringField(TEXT("pin_id"), InPinId);
			}
			return Subsystem->ExecuteOperation(FName(TEXT("sm.recombine_pin")), Args);
		};

		auto FindSubPinIdByName = [](const TSharedPtr<FJsonObject>& InResultPin, const FString& InEndsWith) -> FString
		{
			if (!InResultPin.IsValid())
			{
				return FString();
			}
			const TArray<TSharedPtr<FJsonValue>>* SubPins = nullptr;
			if (!InResultPin->TryGetArrayField(TEXT("sub_pins"), SubPins))
			{
				return FString();
			}
			for (const TSharedPtr<FJsonValue>& Value : *SubPins)
			{
				const TSharedPtr<FJsonObject>* Entry = nullptr;
				if (!Value->TryGetObject(Entry) || !Entry->IsValid())
				{
					continue;
				}
				FString PinName;
				if ((*Entry)->TryGetStringField(TEXT("pin_name"), PinName)
					&& PinName.EndsWith(InEndsWith))
				{
					FString PinId;
					(*Entry)->TryGetStringField(TEXT("pin_id"), PinId);
					return PinId;
				}
			}
			return FString();
		};

		It("Splits a struct property, then recombines it (top-level round-trip)", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddSplitState(AssetPath);
			if (!TestFalse("State created", StateGuid.IsEmpty()))
			{
				return;
			}

			const FSMAssistOperationResult SplitResult = Split(AssetPath, StateGuid, TEXT("OurStruct"));
			TestTrue("Split succeeds", SplitResult.bSuccess);
			if (!TestTrue("Split payload populated", SplitResult.Payload.IsValid()))
			{
				return;
			}
			bool bApplied = false;
			TestTrue("Split applied=true", SplitResult.Payload->TryGetBoolField(TEXT("applied"), bApplied) && bApplied);
			bool bSplitFlag = false;
			TestTrue("is_split_struct=true after split",
				SplitResult.Payload->TryGetBoolField(TEXT("is_split_struct"), bSplitFlag) && bSplitFlag);

			const FSMAssistOperationResult RecombineResult = Recombine(AssetPath, StateGuid, TEXT("OurStruct"));
			TestTrue("Recombine succeeds", RecombineResult.bSuccess);
			if (!TestTrue("Recombine payload populated", RecombineResult.Payload.IsValid()))
			{
				return;
			}
			bool bRecombineApplied = false;
			TestTrue("Recombine applied=true",
				RecombineResult.Payload->TryGetBoolField(TEXT("applied"), bRecombineApplied) && bRecombineApplied);
			bool bSplitFlagAfterRecombine = true;
			TestTrue("is_split_struct=false after recombine",
				RecombineResult.Payload->TryGetBoolField(TEXT("is_split_struct"), bSplitFlagAfterRecombine)
				&& !bSplitFlagAfterRecombine);

			const FSMAssistOperationResult SplitAgain = Split(AssetPath, StateGuid, TEXT("OurStruct"));
			TestTrue("Re-split succeeds", SplitAgain.bSuccess);
			if (TestTrue("Re-split payload populated", SplitAgain.Payload.IsValid()))
			{
				bool bSplitFlag2 = false;
				TestTrue("is_split_struct=true after re-split",
					SplitAgain.Payload->TryGetBoolField(TEXT("is_split_struct"), bSplitFlag2) && bSplitFlag2);
			}
		});

		It("Splits and recombines a nested sub-pin (OurStruct.NestedStruct)", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddSplitState(AssetPath);
			if (!TestFalse("State created", StateGuid.IsEmpty()))
			{
				return;
			}

			const FSMAssistOperationResult TopSplit = Split(AssetPath, StateGuid, TEXT("OurStruct"));
			if (!TestTrue("Top-level split succeeds", TopSplit.bSuccess) || !TestTrue("Top-level payload", TopSplit.Payload.IsValid()))
			{
				return;
			}

			const TSharedPtr<FJsonObject>* ResultPin = nullptr;
			if (!TestTrue("result_pin present after split", TopSplit.Payload->TryGetObjectField(TEXT("result_pin"), ResultPin)))
			{
				return;
			}

			const FString NestedPinId = FindSubPinIdByName(*ResultPin, TEXT("_NestedStruct"));
			if (!TestFalse("Nested sub-pin id located", NestedPinId.IsEmpty()))
			{
				return;
			}

			const FSMAssistOperationResult SubSplit = Split(AssetPath, StateGuid, TEXT("OurStruct"), NestedPinId);
			TestTrue("Sub-pin split succeeds", SubSplit.bSuccess);

			const FSMAssistOperationResult SubRecombine = Recombine(AssetPath, StateGuid, TEXT("OurStruct"), NestedPinId);
			TestTrue("Sub-pin recombine succeeds", SubRecombine.bSuccess);
			if (TestTrue("Sub-pin recombine payload", SubRecombine.Payload.IsValid()))
			{
				bool bApplied = false;
				TestTrue("Sub-pin recombine applied=true",
					SubRecombine.Payload->TryGetBoolField(TEXT("applied"), bApplied) && bApplied);
			}
		});

		It("Fails when 'asset_path' is missing", [=, this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("node_guid"), FGuid::NewGuid().ToString());
			Args->SetStringField(TEXT("variable_name"), TEXT("OurStruct"));
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(FName(TEXT("sm.split_pin")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'asset_path'", Result.ErrorMessage.Contains(TEXT("asset_path")));
		});

		It("Fails when 'variable_name' is missing", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddSplitState(AssetPath);
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(FName(TEXT("sm.split_pin")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'variable_name'", Result.ErrorMessage.Contains(TEXT("variable_name")));
		});

		It("Fails when the property is not splittable (FText)", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddSplitState(AssetPath);
			const FSMAssistOperationResult Result = Split(AssetPath, StateGuid, TEXT("NonSplittableText"));
			TestFalse("Split is rejected", Result.bSuccess);
			TestTrue("Error mentions CanSplitResultPin",
				Result.ErrorMessage.Contains(TEXT("CanSplitResultPin")));
		});

		It("Fails when the variable is not exposed on the node", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddSplitState(AssetPath);
			const FSMAssistOperationResult Result = Split(AssetPath, StateGuid, TEXT("NoSuchVariable"));
			TestFalse("Split is rejected", Result.bSuccess);
			TestTrue("Error mentions missing property", Result.ErrorMessage.Contains(TEXT("No exposed property")));
		});

		It("Fails when pin_id is invalid", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddSplitState(AssetPath);
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("variable_name"), TEXT("OurStruct"));
			Args->SetStringField(TEXT("pin_id"), TEXT("not-a-guid"));
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(FName(TEXT("sm.split_pin")), Args);
			TestFalse("Split is rejected", Result.bSuccess);
			TestTrue("Error mentions pin_id", Result.ErrorMessage.Contains(TEXT("pin_id")));
		});

		It("Fails recombine when nothing is split", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddSplitState(AssetPath);
			const FSMAssistOperationResult Result = Recombine(AssetPath, StateGuid, TEXT("OurStruct"));
			TestFalse("Recombine is rejected", Result.bSuccess);
			TestTrue("Error mentions not currently split",
				Result.ErrorMessage.Contains(TEXT("not currently split")));
		});

		It("Disambiguates multi-bucket TArray<Struct> buckets by root pin_id", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddSplitState(AssetPath);
			if (!TestFalse("State created", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			auto AddArrayElement = [&]()
			{
				const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("asset_path"), AssetPath);
				Args->SetStringField(TEXT("node_guid"), StateGuid);
				Args->SetStringField(TEXT("property_name"), TEXT("StructArray"));
				Args->SetStringField(TEXT("array_action"), TEXT("add"));
				return Subsystem->ExecuteOperation(FName(TEXT("sm.set_node_property")), Args).bSuccess;
			};
			TestTrue("Add element 0", AddArrayElement());
			TestTrue("Add element 1", AddArrayElement());

			auto QueryBuckets = [&]() -> TSharedPtr<FJsonObject>
			{
				const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("asset_path"), AssetPath);
				Args->SetStringField(TEXT("node_guid"), StateGuid);
				Args->SetStringField(TEXT("variable_name"), TEXT("StructArray"));
				return Subsystem->ExecuteOperation(FName(TEXT("sm.get_property_pins")), Args).Payload;
			};

			{
				const TSharedPtr<FJsonObject> Pins = QueryBuckets();
				if (!TestTrue("Pins payload populated", Pins.IsValid()))
				{
					return;
				}
				int32 Count = 0;
				Pins->TryGetNumberField(TEXT("count"), Count);
				if (!TestEqual("Two buckets present before split", Count, 2))
				{
					return;
				}
			}

			// Split the first bucket via the no-pin_id path (backward-compat single-bucket convention).
			const FSMAssistOperationResult SplitFirst = Split(AssetPath, StateGuid, TEXT("StructArray"));
			TestTrue("Split first bucket succeeds", SplitFirst.bSuccess);

			// After splitting one bucket, exactly one of the two reports is_split_struct=false; grab its root pin_id.
			FString UnsplitRootPinId;
			{
				const TSharedPtr<FJsonObject> Pins = QueryBuckets();
				const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
				if (!TestTrue("Pins array present", Pins->TryGetArrayField(TEXT("properties"), Props)))
				{
					return;
				}
				for (const TSharedPtr<FJsonValue>& Value : *Props)
				{
					const TSharedPtr<FJsonObject>* Entry = nullptr;
					if (!Value->TryGetObject(Entry) || !Entry->IsValid())
					{
						continue;
					}
					bool bSplit = true;
					(*Entry)->TryGetBoolField(TEXT("is_split_struct"), bSplit);
					if (bSplit)
					{
						continue;
					}
					const TSharedPtr<FJsonObject>* ResultPin = nullptr;
					if ((*Entry)->TryGetObjectField(TEXT("result_pin"), ResultPin) && ResultPin->IsValid())
					{
						(*ResultPin)->TryGetStringField(TEXT("pin_id"), UnsplitRootPinId);
					}
					break;
				}
			}
			if (!TestFalse("Resolved unsplit bucket root pin_id", UnsplitRootPinId.IsEmpty()))
			{
				return;
			}

			// Split the second bucket by its root pin_id — the new multi-bucket disambiguation path.
			const FSMAssistOperationResult SplitSecond = Split(AssetPath, StateGuid, TEXT("StructArray"), UnsplitRootPinId);
			TestTrue("Split second bucket by root pin_id succeeds", SplitSecond.bSuccess);

			{
				const TSharedPtr<FJsonObject> Pins = QueryBuckets();
				const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
				Pins->TryGetArrayField(TEXT("properties"), Props);
				int32 SplitCount = 0;
				for (const TSharedPtr<FJsonValue>& Value : *Props)
				{
					const TSharedPtr<FJsonObject>* Entry = nullptr;
					if (!Value->TryGetObject(Entry) || !Entry->IsValid())
					{
						continue;
					}
					bool bSplit = false;
					(*Entry)->TryGetBoolField(TEXT("is_split_struct"), bSplit);
					if (bSplit)
					{
						++SplitCount;
					}
				}
				TestEqual("Both buckets report split=true", SplitCount, 2);
			}

			// Recombine the second bucket by its root pin_id, then the first via the no-pin_id path.
			const FSMAssistOperationResult RecombineSecond = Recombine(AssetPath, StateGuid, TEXT("StructArray"), UnsplitRootPinId);
			TestTrue("Recombine second bucket by root pin_id succeeds", RecombineSecond.bSuccess);

			const FSMAssistOperationResult RecombineFirst = Recombine(AssetPath, StateGuid, TEXT("StructArray"));
			TestTrue("Recombine first bucket succeeds", RecombineFirst.bSuccess);
		});

		It("Reports a clear error when pin_id matches no bucket", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddSplitState(AssetPath);
			if (!TestFalse("State created", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("variable_name"), TEXT("OurStruct"));
			Args->SetStringField(TEXT("pin_id"), FGuid::NewGuid().ToString());
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.split_pin")), Args);
			TestFalse("Split with unknown pin_id is rejected", Result.bSuccess);
			TestTrue("Error mentions 'not found'", Result.ErrorMessage.Contains(TEXT("not found")));
		});
	});

	Describe("sm.set_node_property with property_path", [this]()
	{
		auto AddStructSplitTestState = [this](const FString& InAssetPath) -> FString
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!Subsystem)
			{
				return FString();
			}
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), InAssetPath);
			Args->SetStringField(TEXT("state_name"), TEXT("DeepState"));
			Args->SetStringField(TEXT("state_class"), USMStructSplitTestState::StaticClass()->GetPathName());
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_state")), Args);
			if (!Result.bSuccess || !Result.Payload.IsValid())
			{
				return FString();
			}
			FString StateGuid;
			Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
			return StateGuid;
		};

		auto SplitVar = [this](const FString& InAssetPath, const FString& InStateGuid, const FString& InVar, const FString& InPinId = FString())
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), InAssetPath);
			Args->SetStringField(TEXT("node_guid"), InStateGuid);
			Args->SetStringField(TEXT("variable_name"), InVar);
			if (!InPinId.IsEmpty())
			{
				Args->SetStringField(TEXT("pin_id"), InPinId);
			}
			return Subsystem->ExecuteOperation(FName(TEXT("sm.split_pin")), Args);
		};

		auto SetProperty = [this](const FString& InAssetPath, const FString& InStateGuid,
			const FString& InPropertyName, const FString& InPropertyPath, const FString& InValueJson)
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), InAssetPath);
			Args->SetStringField(TEXT("node_guid"), InStateGuid);
			Args->SetStringField(TEXT("property_name"), InPropertyName);
			if (!InPropertyPath.IsEmpty())
			{
				Args->SetStringField(TEXT("property_path"), InPropertyPath);
			}
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FString::Printf(TEXT("{\"v\":%s}"), *InValueJson));
			TSharedPtr<FJsonObject> Wrapper;
			if (FJsonSerializer::Deserialize(Reader, Wrapper) && Wrapper.IsValid())
			{
				Args->SetField(TEXT("value"), Wrapper->TryGetField(TEXT("v")));
			}
			return Subsystem->ExecuteOperation(FName(TEXT("sm.set_node_property")), Args);
		};

		auto FindSubPinId = [](const TSharedPtr<FJsonObject>& InResultPin, const FString& InEndsWith) -> FString
		{
			if (!InResultPin.IsValid())
			{
				return FString();
			}
			TArray<TSharedPtr<FJsonObject>> Stack = { InResultPin };
			while (Stack.Num() > 0)
			{
				const TSharedPtr<FJsonObject> Top = Stack.Pop();
				FString Name;
				if (Top->TryGetStringField(TEXT("pin_name"), Name) && Name.EndsWith(InEndsWith))
				{
					FString PinId;
					Top->TryGetStringField(TEXT("pin_id"), PinId);
					return PinId;
				}
				const TArray<TSharedPtr<FJsonValue>>* Subs = nullptr;
				if (Top->TryGetArrayField(TEXT("sub_pins"), Subs))
				{
					for (const TSharedPtr<FJsonValue>& V : *Subs)
					{
						const TSharedPtr<FJsonObject>* Obj = nullptr;
						if (V->TryGetObject(Obj) && Obj->IsValid())
						{
							Stack.Add(*Obj);
						}
					}
				}
			}
			return FString();
		};

		// Splits NestedTextGraphStruct and its InnerTextStruct sub-pin so SubPath writes that descend
		// into InnerTextStruct.* satisfy the strict-split contract (every struct parent in the chain
		// must be split before the writer can address sub-pins). SplitVar / FindSubPinId are captured
		// by value because the enclosing Describe lambda exits before the It blocks execute.
		auto EnsureNestedTextGraphSplit = [this, SplitVar, FindSubPinId](const FString& InAssetPath, const FString& InStateGuid) -> bool
		{
			const FSMAssistOperationResult TopSplit = SplitVar(InAssetPath, InStateGuid, TEXT("NestedTextGraphStruct"));
			if (!TestTrue("Top-level split", TopSplit.bSuccess) || !TestTrue("Top-level payload", TopSplit.Payload.IsValid()))
			{
				return false;
			}
			const TSharedPtr<FJsonObject>* ResultPin = nullptr;
			if (!TestTrue("result_pin present", TopSplit.Payload->TryGetObjectField(TEXT("result_pin"), ResultPin)))
			{
				return false;
			}
			const FString InnerPinId = FindSubPinId(*ResultPin, TEXT("_InnerTextStruct"));
			if (!TestFalse("Inner sub-pin id located", InnerPinId.IsEmpty()))
			{
				return false;
			}
			const FSMAssistOperationResult InnerSplit = SplitVar(InAssetPath, InStateGuid,
				TEXT("NestedTextGraphStruct"), InnerPinId);
			return TestTrue("Inner sub-pin split", InnerSplit.bSuccess);
		};

		It("Writes a deeply nested text-graph property addressed by the property itself", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			if (!TestFalse("State created", StateGuid.IsEmpty()))
			{
				return;
			}

			// Split top-level and the InnerTextStruct sub-pin so TextMember lives at the leaf level.
			const FSMAssistOperationResult TopSplit = SplitVar(AssetPath, StateGuid, TEXT("NestedTextGraphStruct"));
			if (!TestTrue("Top-level split", TopSplit.bSuccess) || !TestTrue("Top-level payload", TopSplit.Payload.IsValid()))
			{
				return;
			}
			const TSharedPtr<FJsonObject>* ResultPin = nullptr;
			if (!TestTrue("result_pin present", TopSplit.Payload->TryGetObjectField(TEXT("result_pin"), ResultPin)))
			{
				return;
			}
			const FString InnerPinId = FindSubPinId(*ResultPin, TEXT("_InnerTextStruct"));
			if (!TestFalse("Inner sub-pin id located", InnerPinId.IsEmpty()))
			{
				return;
			}
			const FSMAssistOperationResult InnerSplit = SplitVar(AssetPath, StateGuid, TEXT("NestedTextGraphStruct"), InnerPinId);
			if (!TestTrue("Inner sub-pin split", InnerSplit.bSuccess))
			{
				return;
			}

			// Address the FSMTextGraphProperty by the property itself, NOT by .Result.
			const FSMAssistOperationResult Set = SetProperty(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct.TextMember"), TEXT("\"HELLO!\""));
			TestTrue("Set succeeds", Set.bSuccess);
			if (!Set.bSuccess)
			{
				AddError(FString::Printf(TEXT("Set error: %s"), *Set.ErrorMessage));
				return;
			}

			// Verify the template received the value.
			USMBlueprint* Blueprint = Cast<USMBlueprint>(FSoftObjectPath(AssetPath).TryLoad());
			if (!TestNotNull("Blueprint reloaded", Blueprint))
			{
				return;
			}
			FGuid Guid;
			FGuid::Parse(StateGuid, Guid);
			USMGraphNode_Base* Node = nullptr;
			TArray<UEdGraphNode*> AllNodes;
			FBlueprintEditorUtils::GetAllNodesOfClassEx<USMGraphNode_Base>(Blueprint, AllNodes);
			for (UEdGraphNode* Candidate : AllNodes)
			{
				if (Candidate && Candidate->NodeGuid == Guid)
				{
					Node = Cast<USMGraphNode_Base>(Candidate);
					break;
				}
			}
			if (!TestNotNull("Node located", Node))
			{
				return;
			}
			USMStructSplitTestState* TemplateState = Cast<USMStructSplitTestState>(Node->GetNodeTemplate());
			if (!TestNotNull("Template is USMStructSplitTestState", TemplateState))
			{
				return;
			}
			TestEqual("Template TextMember.Result reflects the path write",
				TemplateState->NestedTextGraphStruct.InnerTextStruct.TextMember.Result.ToString(),
				FString(TEXT("HELLO!")));
		});

		It("Writes a deeply nested scalar leaf addressed by property_path", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			if (!EnsureNestedTextGraphSplit(AssetPath, StateGuid))
			{
				return;
			}

			const FSMAssistOperationResult Set = SetProperty(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct.ScalarValue"), TEXT("42"));
			TestTrue("Set succeeds", Set.bSuccess);

			USMBlueprint* Blueprint = Cast<USMBlueprint>(FSoftObjectPath(AssetPath).TryLoad());
			FGuid Guid;
			FGuid::Parse(StateGuid, Guid);
			USMGraphNode_Base* Node = nullptr;
			TArray<UEdGraphNode*> AllNodes;
			FBlueprintEditorUtils::GetAllNodesOfClassEx<USMGraphNode_Base>(Blueprint, AllNodes);
			for (UEdGraphNode* Candidate : AllNodes)
			{
				if (Candidate && Candidate->NodeGuid == Guid)
				{
					Node = Cast<USMGraphNode_Base>(Candidate);
					break;
				}
			}
			USMStructSplitTestState* TemplateState = Cast<USMStructSplitTestState>(Node->GetNodeTemplate());
			TestEqual("Template InnerTextStruct.ScalarValue reflects path write",
				TemplateState->NestedTextGraphStruct.InnerTextStruct.ScalarValue, 42);
		});

		// Repro for live-demo gap (2026-05-19): one-level-split scalar writes land, but with the
		// inner sub-pin ALSO split (matching the user's flow), the path write returned success while
		// the template stayed at 0. Tests both branches: outer scalar (OuterInt) and inner scalar
		// (InnerTextStruct.ScalarValue) under two-level split.
		It("Writes nested scalar leaves with two-level split (top + InnerTextStruct)", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			if (!TestFalse("State created", StateGuid.IsEmpty()))
			{
				return;
			}

			// Split both the top-level pin and the InnerTextStruct sub-pin (mirrors the live demo).
			const FSMAssistOperationResult TopSplit = SplitVar(AssetPath, StateGuid, TEXT("NestedTextGraphStruct"));
			if (!TestTrue("Top split", TopSplit.bSuccess) || !TestTrue("Top payload", TopSplit.Payload.IsValid()))
			{
				return;
			}
			const TSharedPtr<FJsonObject>* ResultPin = nullptr;
			if (!TestTrue("result_pin present", TopSplit.Payload->TryGetObjectField(TEXT("result_pin"), ResultPin)))
			{
				return;
			}
			const FString InnerPinId = FindSubPinId(*ResultPin, TEXT("_InnerTextStruct"));
			if (!TestFalse("Inner sub-pin id located", InnerPinId.IsEmpty()))
			{
				return;
			}
			const FSMAssistOperationResult InnerSplit = SplitVar(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), InnerPinId);
			if (!TestTrue("Inner sub-pin split", InnerSplit.bSuccess))
			{
				return;
			}

			// Outer-tier scalar.
			const FSMAssistOperationResult SetOuter = SetProperty(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("OuterInt"), TEXT("99"));
			TestTrue("Set OuterInt succeeds", SetOuter.bSuccess);

			// Inner-tier scalar under the further-split sub-pin.
			const FSMAssistOperationResult SetInner = SetProperty(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct.ScalarValue"), TEXT("42"));
			TestTrue("Set InnerTextStruct.ScalarValue succeeds", SetInner.bSuccess);

			USMBlueprint* Blueprint = Cast<USMBlueprint>(FSoftObjectPath(AssetPath).TryLoad());
			if (!TestNotNull("Blueprint reloaded", Blueprint))
			{
				return;
			}
			FGuid Guid;
			FGuid::Parse(StateGuid, Guid);
			USMGraphNode_Base* Node = nullptr;
			TArray<UEdGraphNode*> AllNodes;
			FBlueprintEditorUtils::GetAllNodesOfClassEx<USMGraphNode_Base>(Blueprint, AllNodes);
			for (UEdGraphNode* Candidate : AllNodes)
			{
				if (Candidate && Candidate->NodeGuid == Guid)
				{
					Node = Cast<USMGraphNode_Base>(Candidate);
					break;
				}
			}
			if (!TestNotNull("Node located", Node))
			{
				return;
			}
			USMStructSplitTestState* TemplateState = Cast<USMStructSplitTestState>(Node->GetNodeTemplate());
			if (!TestNotNull("Template is USMStructSplitTestState", TemplateState))
			{
				return;
			}
			TestEqual("Template OuterInt reflects path write under two-level split",
				TemplateState->NestedTextGraphStruct.OuterInt, 99);
			TestEqual("Template InnerTextStruct.ScalarValue reflects path write under two-level split",
				TemplateState->NestedTextGraphStruct.InnerTextStruct.ScalarValue, 42);

			// Mirror the live flow: compile after writing. Construction scripts read pin defaults
			// back into the template, so if the cascade left the leaf sub-pins at "0" the writes
			// get reverted here.
			const TSharedRef<FJsonObject> CompileArgs = MakeShared<FJsonObject>();
			CompileArgs->SetStringField(TEXT("asset_path"), AssetPath);
			GetSubsystem()->ExecuteOperation(FName(TEXT("sm.compile")), CompileArgs);

			TestEqual("Template OuterInt survives compile under two-level split",
				TemplateState->NestedTextGraphStruct.OuterInt, 99);
			TestEqual("Template InnerTextStruct.ScalarValue survives compile under two-level split",
				TemplateState->NestedTextGraphStruct.InnerTextStruct.ScalarValue, 42);
		});

		It("Rejects malformed bracket syntax in property_path", [=, this]()
		{
			AddExpectedError(TEXT("Non-numeric array index"), EAutomationExpectedErrorFlags::Contains, 1);
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			const FSMAssistOperationResult Set = SetProperty(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct[abc].ScalarValue"), TEXT("0"));
			TestFalse("Set rejected", Set.bSuccess);
		});

		It("Rejects a path segment that doesn't exist on the struct", [=, this]()
		{
			AddExpectedError(TEXT("has no matching sub-pin"), EAutomationExpectedErrorFlags::Contains, 1);
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			if (!EnsureNestedTextGraphSplit(AssetPath, StateGuid))
			{
				return;
			}
			const FSMAssistOperationResult Set = SetProperty(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct.NoSuchField"), TEXT("0"));
			TestFalse("Set rejected", Set.bSuccess);
		});

		It("Rejects structural array_action when property_path leaf is a scalar", [=, this]()
		{
			AddExpectedError(TEXT("requires the SubPath leaf to name an array"),
				EAutomationExpectedErrorFlags::Contains, 1);
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			if (!EnsureNestedTextGraphSplit(AssetPath, StateGuid))
			{
				return;
			}
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("property_name"), TEXT("NestedTextGraphStruct"));
			Args->SetStringField(TEXT("property_path"), TEXT("InnerTextStruct.ScalarValue"));
			Args->SetStringField(TEXT("array_action"), TEXT("clear"));
			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.set_node_property")), Args);
			TestFalse("Set rejected", Result.bSuccess);
		});

		auto LoadStateTemplate = [this](const FString& InAssetPath, const FString& InStateGuid) -> USMStructSplitTestState*
		{
			USMBlueprint* Blueprint = Cast<USMBlueprint>(FSoftObjectPath(InAssetPath).TryLoad());
			if (!Blueprint)
			{
				return nullptr;
			}
			FGuid Guid;
			FGuid::Parse(InStateGuid, Guid);
			TArray<UEdGraphNode*> AllNodes;
			FBlueprintEditorUtils::GetAllNodesOfClassEx<USMGraphNode_Base>(Blueprint, AllNodes);
			for (UEdGraphNode* Candidate : AllNodes)
			{
				if (Candidate && Candidate->NodeGuid == Guid)
				{
					if (USMGraphNode_Base* Node = Cast<USMGraphNode_Base>(Candidate))
					{
						return Cast<USMStructSplitTestState>(Node->GetNodeTemplate());
					}
				}
			}
			return nullptr;
		};

		auto MutateArrayAtPath = [this](const FString& InAssetPath, const FString& InStateGuid,
			const FString& InPropertyName, const FString& InPropertyPath, const FString& InAction,
			TOptional<int32> InArrayIndex = TOptional<int32>(), TOptional<int32> InTargetIndex = TOptional<int32>())
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), InAssetPath);
			Args->SetStringField(TEXT("node_guid"), InStateGuid);
			Args->SetStringField(TEXT("property_name"), InPropertyName);
			Args->SetStringField(TEXT("property_path"), InPropertyPath);
			Args->SetStringField(TEXT("array_action"), InAction);
			if (InArrayIndex.IsSet())
			{
				Args->SetNumberField(TEXT("array_index"), InArrayIndex.GetValue());
			}
			if (InTargetIndex.IsSet())
			{
				Args->SetNumberField(TEXT("target_index"), InTargetIndex.GetValue());
			}
			return Subsystem->ExecuteOperation(FName(TEXT("sm.set_node_property")), Args);
		};

		It("Adds elements to a nested array via property_path", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			if (!TestFalse("State created", StateGuid.IsEmpty()))
			{
				return;
			}
			if (!EnsureNestedTextGraphSplit(AssetPath, StateGuid))
			{
				return;
			}

			const FSMAssistOperationResult First = MutateArrayAtPath(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct.TextArray"), TEXT("add"));
			TestTrue("First add succeeds", First.bSuccess);
			const FSMAssistOperationResult Second = MutateArrayAtPath(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct.TextArray"), TEXT("add"));
			TestTrue("Second add succeeds", Second.bSuccess);

			USMStructSplitTestState* TemplateState = LoadStateTemplate(AssetPath, StateGuid);
			if (TestNotNull("Template located", TemplateState))
			{
				TestEqual("Nested TextArray Num is 2 after two adds via MCP",
					TemplateState->NestedTextGraphStruct.InnerTextStruct.TextArray.Num(), 2);
			}
		});

		It("Removes a nested array element via property_path + array_index", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			if (!EnsureNestedTextGraphSplit(AssetPath, StateGuid))
			{
				return;
			}

			MutateArrayAtPath(AssetPath, StateGuid, TEXT("NestedTextGraphStruct"),
				TEXT("InnerTextStruct.TextArray"), TEXT("add"));
			MutateArrayAtPath(AssetPath, StateGuid, TEXT("NestedTextGraphStruct"),
				TEXT("InnerTextStruct.TextArray"), TEXT("add"));

			const FSMAssistOperationResult Remove = MutateArrayAtPath(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct.TextArray"), TEXT("remove"), /*Index=*/0);
			TestTrue("Remove succeeds", Remove.bSuccess);

			USMStructSplitTestState* TemplateState = LoadStateTemplate(AssetPath, StateGuid);
			if (TestNotNull("Template located", TemplateState))
			{
				TestEqual("Nested TextArray shrinks to 1 after remove via MCP",
					TemplateState->NestedTextGraphStruct.InnerTextStruct.TextArray.Num(), 1);
			}
		});

		It("Clears a nested array via property_path", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			if (!EnsureNestedTextGraphSplit(AssetPath, StateGuid))
			{
				return;
			}

			MutateArrayAtPath(AssetPath, StateGuid, TEXT("NestedTextGraphStruct"),
				TEXT("InnerTextStruct.TextArray"), TEXT("add"));
			MutateArrayAtPath(AssetPath, StateGuid, TEXT("NestedTextGraphStruct"),
				TEXT("InnerTextStruct.TextArray"), TEXT("add"));

			const FSMAssistOperationResult Clear = MutateArrayAtPath(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct.TextArray"), TEXT("clear"));
			TestTrue("Clear succeeds", Clear.bSuccess);

			USMStructSplitTestState* TemplateState = LoadStateTemplate(AssetPath, StateGuid);
			if (TestNotNull("Template located", TemplateState))
			{
				TestEqual("Nested TextArray empty after clear via MCP",
					TemplateState->NestedTextGraphStruct.InnerTextStruct.TextArray.Num(), 0);
			}
		});
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
