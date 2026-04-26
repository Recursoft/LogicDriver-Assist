// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistTestClasses.h"

#include "Helpers/SMTestHelpers.h"

#include "Blueprints/SMBlueprint.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Misc/Paths.h"
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
		It("Fails when 'reference_asset_path' is missing", [this]()
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

			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'reference_asset_path'",
				Result.ErrorMessage.Contains(TEXT("reference_asset_path")));
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
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
