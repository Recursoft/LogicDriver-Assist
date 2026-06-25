// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistTestClasses.h"

#include "Helpers/SMTestHelpers.h"
#include "Tests/StructSplit/SMStructSplitTestClasses.h"

#include "Blueprints/SMBlueprint.h"
#include "Blueprints/SMBlueprintGeneratedClass.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/SMPropertyGraph.h"
#include "Properties/SMGraphProperty_Base.h"
#include "SMStateInstance.h"
#include "SMTransitionInstance.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
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

	UBlueprint* CreateTransientBlueprintOfType(
		UClass* InParentClass,
		TSubclassOf<UBlueprint> InBlueprintClass,
		TSubclassOf<UBlueprintGeneratedClass> InGeneratedClass)
	{
		const FString PackageName = FAssetHandler::DefaultGamePath()
			+ FString::Printf(TEXT("BP_Compile_%s"), *FGuid::NewGuid().ToString());
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}

		const FName BPName(*FPackageName::GetShortName(PackageName));
		return FKismetEditorUtilities::CreateBlueprint(
			InParentClass, Package, BPName, BPTYPE_Normal, InBlueprintClass, InGeneratedClass);
	}

	FString AddStateToBlueprint(const FString& InAssetPath, const FString& InStateName, UClass* InStateClass = nullptr, bool bIsEntry = false)
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
		if (bIsEntry)
		{
			Args->SetBoolField(TEXT("is_entry"), true);
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

	FSMAssistOperationResult CompileAsset(const FString& InAssetPath)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return FSMAssistOperationResult::MakeError(TEXT("No subsystem."));
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		return Subsystem->ExecuteOperation(FName(TEXT("sm.compile")), Args);
	}

	void CompileExpectingNoErrors(const FString& InAssetPath, const TCHAR* InContext)
	{
		const FSMAssistOperationResult Result = CompileAsset(InAssetPath);
		TestTrue(FString::Printf(TEXT("Compile succeeds with a %s node"), InContext), Result.bSuccess);

		bool bHasErrors = true;
		if (Result.Payload.IsValid())
		{
			Result.Payload->TryGetBoolField(TEXT("has_errors"), bHasErrors);
		}
		TestFalse(FString::Printf(TEXT("Compile reports no errors with a %s node"), InContext), bHasErrors);
	}

	FString AddTransitionBetween(const FString& InAssetPath, const FString& InFromGuid, const FString& InToGuid)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return FString();
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("from_state_guid"), InFromGuid);
		Args->SetStringField(TEXT("to_state_guid"), InToGuid);

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("sm.add_transition")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return FString();
		}

		FString TransitionGuid;
		Result.Payload->TryGetStringField(TEXT("transition_guid"), TransitionGuid);
		return TransitionGuid;
	}

	TArray<FString> GetRootStateGuids(const FString& InAssetPath)
	{
		TArray<FString> Guids;
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return Guids;
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("sm.get_asset")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return Guids;
		}

		const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
		if (Result.Payload->TryGetArrayField(TEXT("states"), States) && States)
		{
			for (const TSharedPtr<FJsonValue>& Value : *States)
			{
				const TSharedPtr<FJsonObject>* Entry = nullptr;
				if (Value->TryGetObject(Entry) && Entry->IsValid())
				{
					FString Guid;
					if ((*Entry)->TryGetStringField(TEXT("state_guid"), Guid))
					{
						Guids.Add(Guid);
					}
				}
			}
		}
		return Guids;
	}

	FString GetRootStateKind(const FString& InAssetPath, const FString& InStateGuid)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return FString();
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("sm.get_asset")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return FString();
		}

		const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
		if (Result.Payload->TryGetArrayField(TEXT("states"), States) && States)
		{
			for (const TSharedPtr<FJsonValue>& Value : *States)
			{
				const TSharedPtr<FJsonObject>* Entry = nullptr;
				if (Value->TryGetObject(Entry) && Entry->IsValid())
				{
					FString Guid;
					if ((*Entry)->TryGetStringField(TEXT("state_guid"), Guid) && Guid == InStateGuid)
					{
						FString Kind;
						(*Entry)->TryGetStringField(TEXT("kind"), Kind);
						return Kind;
					}
				}
			}
		}
		return FString();
	}

	FString AddConduitToBlueprint(const FString& InAssetPath, const FString& InConduitName)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return FString();
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("state_name"), InConduitName);

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("sm.add_conduit")), Args);
		if (!Result.bSuccess || !Result.Payload.IsValid())
		{
			return FString();
		}

		FString ConduitGuid;
		Result.Payload->TryGetStringField(TEXT("state_guid"), ConduitGuid);
		return ConduitGuid;
	}

	FString AddInlineStateMachine(const FString& InAssetPath, const FString& InStateName)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return FString();
		}

		// sm.add_reference with no reference target mints an inline nested state machine node
		// (a USMGraphNode_StateMachineStateNode that is not yet a reference), the convertible input.
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("state_name"), InStateName);

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
			FName(TEXT("sm.add_reference")), Args);
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

	Describe("sm.add_any_state", [this]()
	{
		// Regression: Any State has no bound graph, so the default USMStateInstance node class must
		// not be forwarded to the graph schema action. A regression fires a non-fatal ensure at the
		// schema-action call site (harness-captured as an error; deduped per call site per session),
		// AND can leave the node structurally unsound. This test exercises the reported use case end
		// to end (Any State -> target transition, then compile) so a broken node also surfaces
		// deterministically through CreateTransitionEdge or the compile gate, independent of the ensure.
		It("Creates an Any State node, wires an outbound transition, and compiles", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			// Entry-wired so the machine has a valid initial state and compile exercises real work.
			const FString TargetGuid = AddStateToBlueprint(AssetPath, TEXT("Phase2"), nullptr, /*bIsEntry*/true);
			if (!TestFalse("Target state created", TargetGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("state_name"), TEXT("AnyHealthThreshold"));
			Args->SetNumberField(TEXT("position_x"), 200.0);
			Args->SetNumberField(TEXT("position_y"), -200.0);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_any_state")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString StateGuid;
			TestTrue("Payload has 'state_guid'",
				Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid));
			TestFalse("State guid is non-empty", StateGuid.IsEmpty());

			TestEqual("Created node reports any_state kind",
				GetRootStateKind(AssetPath, StateGuid), FString(TEXT("any_state")));

			// The reported scenario: a global "from Any State" transition (e.g. Any State -> Phase2).
			const FString TransitionGuid = AddTransitionBetween(AssetPath, StateGuid, TargetGuid);
			TestFalse("Any State outbound transition created", TransitionGuid.IsEmpty());

			CompileExpectingNoErrors(AssetPath, TEXT("Any State"));
		});
	});

	Describe("sm.add_link_state", [this]()
	{
		// Regression: Link State, like Any State, has no bound graph and must not receive the default
		// USMStateInstance node class. Same ensure caveat as add_any_state (both trip the same deduped
		// call site); the compile gate catches a structurally-broken link node independent of it.
		It("Creates a Link State node and compiles", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			// Entry-wired so the machine has a valid initial state and compile exercises real work.
			const FString TargetGuid = AddStateToBlueprint(AssetPath, TEXT("LinkTarget"), nullptr, /*bIsEntry*/true);
			if (!TestFalse("Target state created", TargetGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("link_to_state_name"), TEXT("LinkTarget"));
			Args->SetNumberField(TEXT("position_x"), 200.0);
			Args->SetNumberField(TEXT("position_y"), 200.0);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_link_state")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString StateGuid;
			TestTrue("Payload has 'state_guid'",
				Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid));
			TestFalse("State guid is non-empty", StateGuid.IsEmpty());

			TestEqual("Created node reports link_state kind",
				GetRootStateKind(AssetPath, StateGuid), FString(TEXT("link_state")));

			CompileExpectingNoErrors(AssetPath, TEXT("Link State"));
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

		It("Compiles a state node-class blueprint successfully", [this]()
		{
			UBlueprint* NodeBP = CreateTransientBlueprintOfType(
				USMStateInstance::StaticClass(),
				USMNodeBlueprint::StaticClass(),
				USMNodeBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("State node-class blueprint created", NodeBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), NodeBP->GetPathName());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.compile")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				bool bUpToDate = false;
				TestTrue("Payload has 'up_to_date'",
					Result.Payload->TryGetBoolField(TEXT("up_to_date"), bUpToDate));
				TestTrue("Node blueprint is up to date", bUpToDate);

				bool bHasErrors = true;
				Result.Payload->TryGetBoolField(TEXT("has_errors"), bHasErrors);
				TestFalse("No compile errors", bHasErrors);
			}
		});

		It("Compiles a transition node-class blueprint successfully", [this]()
		{
			UBlueprint* NodeBP = CreateTransientBlueprintOfType(
				USMTransitionInstance::StaticClass(),
				USMNodeBlueprint::StaticClass(),
				USMNodeBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("Transition node-class blueprint created", NodeBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), NodeBP->GetPathName());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.compile")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				bool bUpToDate = false;
				TestTrue("Payload has 'up_to_date'",
					Result.Payload->TryGetBoolField(TEXT("up_to_date"), bUpToDate));
				TestTrue("Node blueprint is up to date", bUpToDate);

				bool bHasErrors = true;
				Result.Payload->TryGetBoolField(TEXT("has_errors"), bHasErrors);
				TestFalse("No compile errors", bHasErrors);
			}
		});

		It("Compiles a non-LD UBlueprint (Actor child) successfully", [this]()
		{
			UBlueprint* ActorBP = CreateTransientBlueprintOfType(
				AActor::StaticClass(),
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("Actor blueprint created", ActorBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), ActorBP->GetPathName());

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.compile")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				bool bUpToDate = false;
				TestTrue("Payload has 'up_to_date'",
					Result.Payload->TryGetBoolField(TEXT("up_to_date"), bUpToDate));
				TestTrue("Actor blueprint is up to date", bUpToDate);

				bool bHasErrors = true;
				Result.Payload->TryGetBoolField(TEXT("has_errors"), bHasErrors);
				TestFalse("No compile errors", bHasErrors);
			}
		});

		It("Fails when asset_path resolves to a non-blueprint asset", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.compile")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions blueprint requirement",
				Result.ErrorMessage.Contains(TEXT("not a blueprint")));
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

		It("Recurses struct properties when 'max_depth' is set", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(
				AssetPath, TEXT("Recursed"), USMAssistSplitTestState::StaticClass());
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetNumberField(TEXT("max_depth"), 2);

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

			TSharedPtr<FJsonObject> OurStructObj;
			for (const TSharedPtr<FJsonValue>& Entry : *Properties)
			{
				const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
				if (!Obj.IsValid())
				{
					continue;
				}
				FString PropName;
				Obj->TryGetStringField(TEXT("name"), PropName);
				if (PropName == TEXT("OurStruct"))
				{
					OurStructObj = Obj;
					break;
				}
			}

			if (!TestTrue("OurStruct property present", OurStructObj.IsValid()))
			{
				return;
			}

			TestTrue("OurStruct retains flat 'value' (additive)", OurStructObj->HasField(TEXT("value")));

			const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
			if (!TestTrue("OurStruct exposes 'members'", OurStructObj->TryGetArrayField(TEXT("members"), Members)))
			{
				return;
			}

			bool bFoundOuterInt = false;
			TSharedPtr<FJsonObject> NestedStructObj;
			for (const TSharedPtr<FJsonValue>& M : *Members)
			{
				const TSharedPtr<FJsonObject> MObj = M->AsObject();
				if (!MObj.IsValid())
				{
					continue;
				}
				FString MName;
				MObj->TryGetStringField(TEXT("name"), MName);
				if (MName == TEXT("OuterInt"))
				{
					bFoundOuterInt = true;
				}
				else if (MName == TEXT("NestedStruct"))
				{
					NestedStructObj = MObj;
				}
			}
			TestTrue("OurStruct.members includes OuterInt", bFoundOuterInt);

			if (TestTrue("OurStruct.members includes NestedStruct", NestedStructObj.IsValid()))
			{
				const TArray<TSharedPtr<FJsonValue>>* InnerMembers = nullptr;
				if (TestTrue("NestedStruct recurses to 'members'",
					NestedStructObj->TryGetArrayField(TEXT("members"), InnerMembers)))
				{
					bool bFoundInnerInt = false;
					bool bFoundInnerFloat = false;
					for (const TSharedPtr<FJsonValue>& IM : *InnerMembers)
					{
						const TSharedPtr<FJsonObject> IMObj = IM->AsObject();
						if (!IMObj.IsValid())
						{
							continue;
						}
						FString IMName;
						IMObj->TryGetStringField(TEXT("name"), IMName);
						bFoundInnerInt |= (IMName == TEXT("InnerInt"));
						bFoundInnerFloat |= (IMName == TEXT("InnerFloat"));
					}
					TestTrue("NestedStruct.members includes InnerInt", bFoundInnerInt);
					TestTrue("NestedStruct.members includes InnerFloat", bFoundInnerFloat);
				}
			}
		});

		It("Does not recurse when 'max_depth' is omitted (back-compat)", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(
				AssetPath, TEXT("Flat"), USMAssistSplitTestState::StaticClass());
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

			if (!TestTrue("Result is success", Result.bSuccess) || !TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
			if (!TestTrue("Payload has 'properties' array",
				Result.Payload->TryGetArrayField(TEXT("properties"), Properties)))
			{
				return;
			}

			for (const TSharedPtr<FJsonValue>& Entry : *Properties)
			{
				const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
				if (!Obj.IsValid())
				{
					continue;
				}
				FString PropName;
				Obj->TryGetStringField(TEXT("name"), PropName);
				if (PropName == TEXT("OurStruct"))
				{
					TestFalse("OurStruct has no 'members' without max_depth", Obj->HasField(TEXT("members")));
				}
			}
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
			AddExpectedError(TEXT("Non-integer array index"), EAutomationExpectedErrorFlags::Contains, 1);
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

		auto GetPropertyGraph = [this](const FString& InAssetPath, const FString& InStateGuid,
			const FString& InVar, const FString& InPath = FString(), bool bIncludePinTree = false)
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), InAssetPath);
			Args->SetStringField(TEXT("node_guid"), InStateGuid);
			Args->SetStringField(TEXT("variable_name"), InVar);
			if (!InPath.IsEmpty())
			{
				Args->SetStringField(TEXT("property_path"), InPath);
			}
			Args->SetBoolField(TEXT("include_pin_tree"), bIncludePinTree);
			return Subsystem->ExecuteOperation(FName(TEXT("sm.get_property_graph")), Args);
		};

		auto SetEditMode = [this](const FString& InAssetPath, const FString& InStateGuid,
			const FString& InVar, bool bEnable, bool bSetEnableField = true)
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), InAssetPath);
			Args->SetStringField(TEXT("node_guid"), InStateGuid);
			Args->SetStringField(TEXT("variable_name"), InVar);
			if (bSetEnableField)
			{
				Args->SetBoolField(TEXT("b_enable"), bEnable);
			}
			return Subsystem->ExecuteOperation(FName(TEXT("sm.set_property_graph_edit_mode")), Args);
		};

		auto LoadResolvedGraph = [](const FSMAssistOperationResult& InResult) -> USMPropertyGraph*
		{
			if (!InResult.bSuccess || !InResult.Payload.IsValid())
			{
				return nullptr;
			}
			FString GraphPath;
			if (!InResult.Payload->TryGetStringField(TEXT("graph_path"), GraphPath))
			{
				return nullptr;
			}
			return Cast<USMPropertyGraph>(FSoftObjectPath(GraphPath).TryLoad());
		};

		It("get_property_graph resolves a top-level text-graph variable and returns handles", [=, this]()
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

			const FSMAssistOperationResult Result = GetPropertyGraph(AssetPath, StateGuid, TEXT("TextGraphValue"));
			if (!TestTrue("get_property_graph succeeds", Result.bSuccess) ||
				!TestTrue("Payload valid", Result.Payload.IsValid()))
			{
				return;
			}

			for (const TCHAR* Field : { TEXT("graph_path"), TEXT("graph_name"), TEXT("graph_guid"),
				TEXT("result_node_name"), TEXT("result_pin_name"), TEXT("element_type") })
			{
				FString Value;
				TestTrue(FString::Printf(TEXT("%s present and non-empty"), Field),
					Result.Payload->TryGetStringField(Field, Value) && !Value.IsEmpty());
			}

			double BucketIndex = 0.0;
			if (TestTrue("bucket_index present", Result.Payload->TryGetNumberField(TEXT("bucket_index"), BucketIndex)))
			{
				TestEqual("bucket_index is INDEX_NONE for a non-bucket leaf", static_cast<int32>(BucketIndex), INDEX_NONE);
			}
			TestFalse("result_pin omitted when include_pin_tree is false", Result.Payload->HasField(TEXT("result_pin")));
		});

		It("get_property_graph includes the result_pin tree when include_pin_tree is set", [=, this]()
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

			const FSMAssistOperationResult Result = GetPropertyGraph(AssetPath, StateGuid,
				TEXT("TextGraphValue"), FString(), /*bIncludePinTree=*/true);
			if (!TestTrue("get_property_graph succeeds", Result.bSuccess) ||
				!TestTrue("Payload valid", Result.Payload.IsValid()))
			{
				return;
			}
			const TSharedPtr<FJsonObject>* ResultPin = nullptr;
			TestTrue("result_pin sub-tree present", Result.Payload->TryGetObjectField(TEXT("result_pin"), ResultPin));
		});

		It("get_property_graph surfaces a non-negative bucket_index for a nested array element", [=, this]()
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

			const FSMAssistOperationResult Result = GetPropertyGraph(AssetPath, StateGuid,
				TEXT("NestedTextGraphStruct"), TEXT("InnerTextStruct.TextArray[0]"));
			if (!TestTrue("get_property_graph succeeds", Result.bSuccess) ||
				!TestTrue("Payload valid", Result.Payload.IsValid()))
			{
				return;
			}
			double BucketIndex = -1.0;
			if (TestTrue("bucket_index present", Result.Payload->TryGetNumberField(TEXT("bucket_index"), BucketIndex)))
			{
				TestEqual("bucket_index is 0 for the first array element", static_cast<int32>(BucketIndex), 0);
			}
		});

		It("get_property_graph rejects a variable that does not exist", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			const FSMAssistOperationResult Result = GetPropertyGraph(AssetPath, StateGuid, TEXT("DoesNotExist"));
			TestFalse("Resolve rejected", Result.bSuccess);
			TestTrue("Error names the missing variable", Result.ErrorMessage.Contains(TEXT("DoesNotExist")));
		});

		It("set_property_graph_edit_mode rejects a missing b_enable arg", [=, this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddStructSplitTestState(AssetPath);
			const FSMAssistOperationResult Result = SetEditMode(AssetPath, StateGuid,
				TEXT("TextGraphValue"), /*bEnable=*/true, /*bSetEnableField=*/false);
			TestFalse("Rejected without b_enable", Result.bSuccess);
			TestTrue("Error names the missing arg", Result.ErrorMessage.Contains(TEXT("b_enable")));
		});

		It("set_property_graph_edit_mode flips a text graph into edit mode and back", [=, this]()
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

			const FSMAssistOperationResult Enable = SetEditMode(AssetPath, StateGuid, TEXT("TextGraphValue"), true);
			TestTrue("Enable succeeds", Enable.bSuccess);
			if (USMPropertyGraph* Graph = LoadResolvedGraph(Enable))
			{
				TestTrue("Graph reports edit mode after enable", Graph->IsGraphBeingUsedToEdit());
			}
			else
			{
				AddError(TEXT("Could not load the resolved graph after enable"));
			}

			const FSMAssistOperationResult Idempotent = SetEditMode(AssetPath, StateGuid, TEXT("TextGraphValue"), true);
			TestTrue("Re-enable is idempotent and succeeds", Idempotent.bSuccess);

			const FSMAssistOperationResult Disable = SetEditMode(AssetPath, StateGuid, TEXT("TextGraphValue"), false);
			TestTrue("Disable succeeds", Disable.bSuccess);
			if (USMPropertyGraph* Graph = LoadResolvedGraph(Disable))
			{
				TestFalse("Graph reports edit mode cleared after disable", Graph->IsGraphBeingUsedToEdit());
			}
		});

		It("set_property_graph_edit_mode maps a read-only variable to an actionable error", [=, this]()
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

			USMPropertyGraph* Graph = LoadResolvedGraph(GetPropertyGraph(AssetPath, StateGuid, TEXT("TextGraphValue")));
			if (!TestNotNull("Resolved text-graph graph", Graph) || !TestNotNull("Result node", Graph->ResultNode.Get()))
			{
				return;
			}
			FSMGraphProperty_Base* PropertyNode = Graph->ResultNode->GetPropertyNode();
			if (!TestNotNull("Property node accessible", PropertyNode))
			{
				return;
			}
			PropertyNode->bReadOnly = true;
			Graph->RefreshProperty(/*bModify=*/false, /*bSetFromPinFirst=*/false);
			if (!TestTrue("Graph picked up read-only flag", Graph->IsVariableReadOnly()))
			{
				return;
			}

			const FSMAssistOperationResult Result = SetEditMode(AssetPath, StateGuid, TEXT("TextGraphValue"), true);
			TestFalse("Toggle rejected on read-only variable", Result.bSuccess);
			TestTrue("Error explains the variable is read-only", Result.ErrorMessage.Contains(TEXT("read-only")));
		});
	});

	Describe("sm.add_sm_variable container types", [this]()
	{
		It("Echoes container_type=Array in the payload for TArray variables", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("variable_name"), TEXT("Tags"));
			Args->SetStringField(TEXT("var_type"), TEXT("name"));
			Args->SetStringField(TEXT("container_type"), TEXT("Array"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_sm_variable")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				FString ContainerType;
				TestTrue("Payload has 'container_type'",
					Result.Payload->TryGetStringField(TEXT("container_type"), ContainerType));
				TestEqual("container_type echoed as Array", ContainerType, FString(TEXT("Array")));
			}
		});

		It("Echoes container_type=Map + key_type in the payload for TMap variables", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("variable_name"), TEXT("Flags"));
			Args->SetStringField(TEXT("var_type"), TEXT("bool"));
			Args->SetStringField(TEXT("container_type"), TEXT("Map"));
			Args->SetStringField(TEXT("key_type"), TEXT("name"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_sm_variable")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				FString ContainerType;
				TestTrue("Payload has 'container_type'",
					Result.Payload->TryGetStringField(TEXT("container_type"), ContainerType));
				TestEqual("container_type echoed as Map", ContainerType, FString(TEXT("Map")));

				FString KeyType;
				TestTrue("Payload has 'key_type'",
					Result.Payload->TryGetStringField(TEXT("key_type"), KeyType));
				TestEqual("key_type echoed as name", KeyType, FString(TEXT("name")));
			}
		});

		It("Adds a TSet variable", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("variable_name"), TEXT("UniqueIds"));
			Args->SetStringField(TEXT("var_type"), TEXT("int"));
			Args->SetStringField(TEXT("container_type"), TEXT("Set"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_sm_variable")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				FString ContainerType;
				TestTrue("Payload has 'container_type'",
					Result.Payload->TryGetStringField(TEXT("container_type"), ContainerType));
				TestEqual("container_type echoed as Set", ContainerType, FString(TEXT("Set")));
			}
		});

		It("Fails with unrecognized container_type", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("variable_name"), TEXT("Bad"));
			Args->SetStringField(TEXT("var_type"), TEXT("int"));
			Args->SetStringField(TEXT("container_type"), TEXT("Bag"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_sm_variable")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions container_type", Result.ErrorMessage.Contains(TEXT("container_type")));
		});

		It("Fails with Map but missing key_type", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("variable_name"), TEXT("NoKey"));
			Args->SetStringField(TEXT("var_type"), TEXT("bool"));
			Args->SetStringField(TEXT("container_type"), TEXT("Map"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_sm_variable")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions key_type", Result.ErrorMessage.Contains(TEXT("key_type")));
		});

		It("Fails with key_type but no Map container", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("variable_name"), TEXT("StrayKey"));
			Args->SetStringField(TEXT("var_type"), TEXT("int"));
			Args->SetStringField(TEXT("container_type"), TEXT("Array"));
			Args->SetStringField(TEXT("key_type"), TEXT("name"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_sm_variable")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions key_type", Result.ErrorMessage.Contains(TEXT("key_type")));
		});
	});

	Describe("sm.add_blueprint_variable container types on a plain Actor blueprint", [this]()
	{
		It("Adds a TMap<FName,bool> with the correct key and value pin types", [this]()
		{
			UBlueprint* ActorBP = CreateTransientBlueprintOfType(
				AActor::StaticClass(),
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("Actor blueprint created", ActorBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), ActorBP->GetPathName());
			Args->SetStringField(TEXT("variable_name"), TEXT("WorldFlags"));
			Args->SetStringField(TEXT("var_type"), TEXT("bool"));
			Args->SetStringField(TEXT("container_type"), TEXT("Map"));
			Args->SetStringField(TEXT("key_type"), TEXT("name"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_blueprint_variable")), Args);
			TestTrue("Result is success", Result.bSuccess);

			const FBPVariableDescription* Var = ActorBP->NewVariables.FindByPredicate(
				[](const FBPVariableDescription& Desc) { return Desc.VarName == FName(TEXT("WorldFlags")); });
			if (TestNotNull("Variable lands in NewVariables", Var))
			{
				TestEqual("Container is Map", Var->VarType.ContainerType, EPinContainerType::Map);
				// Map convention: the outer pin category is the KEY type, PinValueType is the VALUE type.
				TestEqual("Key type is FName", Var->VarType.PinCategory, UEdGraphSchema_K2::PC_Name);
				TestEqual("Value type is bool", Var->VarType.PinValueType.TerminalCategory, UEdGraphSchema_K2::PC_Boolean);
			}
		});

		It("Adds a TArray<int32> for parity", [this]()
		{
			UBlueprint* ActorBP = CreateTransientBlueprintOfType(
				AActor::StaticClass(),
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("Actor blueprint created", ActorBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), ActorBP->GetPathName());
			Args->SetStringField(TEXT("variable_name"), TEXT("Scores"));
			Args->SetStringField(TEXT("var_type"), TEXT("int"));
			Args->SetStringField(TEXT("container_type"), TEXT("Array"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_blueprint_variable")), Args);
			TestTrue("Result is success", Result.bSuccess);

			const FBPVariableDescription* Var = ActorBP->NewVariables.FindByPredicate(
				[](const FBPVariableDescription& Desc) { return Desc.VarName == FName(TEXT("Scores")); });
			if (TestNotNull("Variable lands in NewVariables", Var))
			{
				TestEqual("Container is Array", Var->VarType.ContainerType, EPinContainerType::Array);
				TestEqual("Element type is int", Var->VarType.PinCategory, UEdGraphSchema_K2::PC_Int);
			}
		});

		It("Adds a TSet<FName> for parity", [this]()
		{
			UBlueprint* ActorBP = CreateTransientBlueprintOfType(
				AActor::StaticClass(),
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("Actor blueprint created", ActorBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), ActorBP->GetPathName());
			Args->SetStringField(TEXT("variable_name"), TEXT("VisitedRooms"));
			Args->SetStringField(TEXT("var_type"), TEXT("name"));
			Args->SetStringField(TEXT("container_type"), TEXT("Set"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_blueprint_variable")), Args);
			TestTrue("Result is success", Result.bSuccess);

			const FBPVariableDescription* Var = ActorBP->NewVariables.FindByPredicate(
				[](const FBPVariableDescription& Desc) { return Desc.VarName == FName(TEXT("VisitedRooms")); });
			if (TestNotNull("Variable lands in NewVariables", Var))
			{
				TestEqual("Container is Set", Var->VarType.ContainerType, EPinContainerType::Set);
				TestEqual("Element type is FName", Var->VarType.PinCategory, UEdGraphSchema_K2::PC_Name);
			}
		});
	});

	Describe("sm.add_node_variable container types", [this]()
	{
		It("Adds a plain TArray<FName> variable on a state class", [this]()
		{
			UBlueprint* NodeBP = CreateTransientBlueprintOfType(
				USMStateInstance::StaticClass(),
				USMNodeBlueprint::StaticClass(),
				USMNodeBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("State node-class blueprint created", NodeBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), NodeBP->GetPathName());
			Args->SetStringField(TEXT("variable_name"), TEXT("Tags"));
			Args->SetStringField(TEXT("var_type"), TEXT("name"));
			Args->SetStringField(TEXT("container_type"), TEXT("Array"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_node_variable")), Args);
			TestTrue("Result is success", Result.bSuccess);
			if (TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				FString ContainerType;
				TestTrue("Payload has 'container_type'",
					Result.Payload->TryGetStringField(TEXT("container_type"), ContainerType));
				TestEqual("container_type echoed as Array", ContainerType, FString(TEXT("Array")));
			}
		});

		It("Allows TArray with Direction=Input (Array is graph-exposable)", [this]()
		{
			UBlueprint* NodeBP = CreateTransientBlueprintOfType(
				USMStateInstance::StaticClass(),
				USMNodeBlueprint::StaticClass(),
				USMNodeBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("State node-class blueprint created", NodeBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), NodeBP->GetPathName());
			Args->SetStringField(TEXT("variable_name"), TEXT("InTags"));
			Args->SetStringField(TEXT("var_type"), TEXT("name"));
			Args->SetStringField(TEXT("container_type"), TEXT("Array"));
			Args->SetStringField(TEXT("direction"), TEXT("Input"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_node_variable")), Args);
			TestTrue("Result is success", Result.bSuccess);
		});

		It("Refuses TMap with Direction=Input (Map not graph-exposable)", [this]()
		{
			UBlueprint* NodeBP = CreateTransientBlueprintOfType(
				USMStateInstance::StaticClass(),
				USMNodeBlueprint::StaticClass(),
				USMNodeBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("State node-class blueprint created", NodeBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), NodeBP->GetPathName());
			Args->SetStringField(TEXT("variable_name"), TEXT("InFlags"));
			Args->SetStringField(TEXT("var_type"), TEXT("bool"));
			Args->SetStringField(TEXT("container_type"), TEXT("Map"));
			Args->SetStringField(TEXT("key_type"), TEXT("name"));
			Args->SetStringField(TEXT("direction"), TEXT("Input"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_node_variable")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions graph node exposure",
				Result.ErrorMessage.Contains(TEXT("cannot be exposed on the graph node")));
		});

		It("Allows TMap without Direction (plain variable)", [this]()
		{
			UBlueprint* NodeBP = CreateTransientBlueprintOfType(
				USMStateInstance::StaticClass(),
				USMNodeBlueprint::StaticClass(),
				USMNodeBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("State node-class blueprint created", NodeBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), NodeBP->GetPathName());
			Args->SetStringField(TEXT("variable_name"), TEXT("Flags"));
			Args->SetStringField(TEXT("var_type"), TEXT("bool"));
			Args->SetStringField(TEXT("container_type"), TEXT("Map"));
			Args->SetStringField(TEXT("key_type"), TEXT("name"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_node_variable")), Args);
			TestTrue("Result is success", Result.bSuccess);
		});

		It("Refuses TSet with b_hidden=true", [this]()
		{
			UBlueprint* NodeBP = CreateTransientBlueprintOfType(
				USMStateInstance::StaticClass(),
				USMNodeBlueprint::StaticClass(),
				USMNodeBlueprintGeneratedClass::StaticClass());
			if (!TestNotNull("State node-class blueprint created", NodeBP))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), NodeBP->GetPathName());
			Args->SetStringField(TEXT("variable_name"), TEXT("HiddenSet"));
			Args->SetStringField(TEXT("var_type"), TEXT("name"));
			Args->SetStringField(TEXT("container_type"), TEXT("Set"));
			Args->SetBoolField(TEXT("b_hidden"), true);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.add_node_variable")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions graph node exposure",
				Result.ErrorMessage.Contains(TEXT("cannot be exposed on the graph node")));
		});
	});

	Describe("sm.collapse_to_state_machine", [this]()
	{
		It("Collapses a set of states into a nested state machine and returns the container", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString AGuid = AddStateToBlueprint(AssetPath, TEXT("A"));
			const FString BGuid = AddStateToBlueprint(AssetPath, TEXT("B"));
			const FString CGuid = AddStateToBlueprint(AssetPath, TEXT("C"));
			if (!TestFalse("A guid populated", AGuid.IsEmpty())
				|| !TestFalse("B guid populated", BGuid.IsEmpty())
				|| !TestFalse("C guid populated", CGuid.IsEmpty()))
			{
				return;
			}
			TestFalse("A->B transition added", AddTransitionBetween(AssetPath, AGuid, BGuid).IsEmpty());
			TestFalse("B->C transition added", AddTransitionBetween(AssetPath, BGuid, CGuid).IsEmpty());

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> NodeGuids;
			NodeGuids.Add(MakeShared<FJsonValueString>(BGuid));
			NodeGuids.Add(MakeShared<FJsonValueString>(CGuid));

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetArrayField(TEXT("node_guids"), NodeGuids);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.collapse_to_state_machine")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString ContainerGuid;
			TestTrue("Payload has 'state_guid'",
				Result.Payload->TryGetStringField(TEXT("state_guid"), ContainerGuid));
			TestFalse("Container guid non-empty", ContainerGuid.IsEmpty());

			const TArray<FString> RootGuids = GetRootStateGuids(AssetPath);
			TestTrue("Container is in the root graph", RootGuids.Contains(ContainerGuid));
			TestTrue("Outer state A stayed in the root graph", RootGuids.Contains(AGuid));
			TestFalse("Collapsed state B moved inside the container", RootGuids.Contains(BGuid));
			TestFalse("Collapsed state C moved inside the container", RootGuids.Contains(CGuid));
			TestEqual("Container kind is a nested state machine",
				GetRootStateKind(AssetPath, ContainerGuid), FString(TEXT("state_machine_state")));
		});

		It("Fails when 'node_guids' is missing", [this]()
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
				FName(TEXT("sm.collapse_to_state_machine")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when a node guid cannot be found", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> NodeGuids;
			NodeGuids.Add(MakeShared<FJsonValueString>(FGuid::NewGuid().ToString()));

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetArrayField(TEXT("node_guids"), NodeGuids);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.collapse_to_state_machine")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when 'node_guids' is empty", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TArray<TSharedPtr<FJsonValue>> Empty;
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetArrayField(TEXT("node_guids"), Empty);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.collapse_to_state_machine")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});
	});

	Describe("sm.merge_states", [this]()
	{
		It("Copies a source state's template into the destination stack and leaves the source in place", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString DestGuid = AddStateToBlueprint(AssetPath, TEXT("Dest"));
			const FString SrcGuid = AddStateToBlueprint(AssetPath, TEXT("Src"),
				USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("Dest guid populated", DestGuid.IsEmpty())
				|| !TestFalse("Src guid populated", SrcGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> SourceGuids;
			SourceGuids.Add(MakeShared<FJsonValueString>(SrcGuid));

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("destination_state_guid"), DestGuid);
			Args->SetArrayField(TEXT("source_state_guids"), SourceGuids);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.merge_states")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Merged = nullptr;
			TestTrue("Payload has 'merged_stack_template_guids'",
				Result.Payload->TryGetArrayField(TEXT("merged_stack_template_guids"), Merged));
			if (Merged)
			{
				TestEqual("One template merged", Merged->Num(), 1);
			}

			bool bDestroyEcho = true;
			TestTrue("Payload echoes 'b_destroy_states'",
				Result.Payload->TryGetBoolField(TEXT("b_destroy_states"), bDestroyEcho));
			TestFalse("Copy merge did not destroy sources", bDestroyEcho);

			const TArray<FString> RootGuids = GetRootStateGuids(AssetPath);
			TestTrue("Copy left the destination in place", RootGuids.Contains(DestGuid));
			TestTrue("Copy left the source in place", RootGuids.Contains(SrcGuid));
		});

		It("Destroys the source on a cut merge", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString DestGuid = AddStateToBlueprint(AssetPath, TEXT("Dest"));
			const FString SrcGuid = AddStateToBlueprint(AssetPath, TEXT("Src"),
				USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("Dest guid populated", DestGuid.IsEmpty())
				|| !TestFalse("Src guid populated", SrcGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> SourceGuids;
			SourceGuids.Add(MakeShared<FJsonValueString>(SrcGuid));

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("destination_state_guid"), DestGuid);
			Args->SetArrayField(TEXT("source_state_guids"), SourceGuids);
			Args->SetBoolField(TEXT("b_destroy_states"), true);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.merge_states")), Args);
			TestTrue("Result is success", Result.bSuccess);

			const TArray<FString> RootGuids = GetRootStateGuids(AssetPath);
			TestTrue("Cut left the destination in place", RootGuids.Contains(DestGuid));
			TestFalse("Cut destroyed the source state", RootGuids.Contains(SrcGuid));
		});

		It("Fails when 'destination_state_guid' is missing", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString SrcGuid = AddStateToBlueprint(AssetPath, TEXT("Src"));
			if (!TestFalse("Src guid populated", SrcGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> SourceGuids;
			SourceGuids.Add(MakeShared<FJsonValueString>(SrcGuid));

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetArrayField(TEXT("source_state_guids"), SourceGuids);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.merge_states")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when 'source_state_guids' is missing", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString DestGuid = AddStateToBlueprint(AssetPath, TEXT("Dest"));
			if (!TestFalse("Dest guid populated", DestGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("destination_state_guid"), DestGuid);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.merge_states")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when the destination is not a plain state", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString ConduitGuid = AddConduitToBlueprint(AssetPath, TEXT("Hub"));
			const FString SrcGuid = AddStateToBlueprint(AssetPath, TEXT("Src"),
				USMAssistArrayStateInstance::StaticClass());
			if (!TestFalse("Conduit guid populated", ConduitGuid.IsEmpty())
				|| !TestFalse("Src guid populated", SrcGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> SourceGuids;
			SourceGuids.Add(MakeShared<FJsonValueString>(SrcGuid));

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("destination_state_guid"), ConduitGuid);
			Args->SetArrayField(TEXT("source_state_guids"), SourceGuids);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.merge_states")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when 'source_state_guids' is empty", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString DestGuid = AddStateToBlueprint(AssetPath, TEXT("Dest"));
			if (!TestFalse("Dest guid populated", DestGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TArray<TSharedPtr<FJsonValue>> Empty;
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("destination_state_guid"), DestGuid);
			Args->SetArrayField(TEXT("source_state_guids"), Empty);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.merge_states")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when a source state is not a plain state", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString DestGuid = AddStateToBlueprint(AssetPath, TEXT("Dest"));
			const FString ConduitGuid = AddConduitToBlueprint(AssetPath, TEXT("SrcHub"));
			if (!TestFalse("Dest guid populated", DestGuid.IsEmpty())
				|| !TestFalse("Conduit guid populated", ConduitGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			TArray<TSharedPtr<FJsonValue>> SourceGuids;
			SourceGuids.Add(MakeShared<FJsonValueString>(ConduitGuid));

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("destination_state_guid"), DestGuid);
			Args->SetArrayField(TEXT("source_state_guids"), SourceGuids);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.merge_states")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});
	});

	Describe("sm.replace_node", [this]()
	{
		It("Replaces a state with a conduit", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("ToReplace"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("kind"), TEXT("conduit"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.replace_node")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString NewGuid;
			TestTrue("Payload has 'node_guid'",
				Result.Payload->TryGetStringField(TEXT("node_guid"), NewGuid));
			TestFalse("New node guid non-empty", NewGuid.IsEmpty());

			const TArray<FString> RootGuids = GetRootStateGuids(AssetPath);
			TestTrue("New node is in the root graph", RootGuids.Contains(NewGuid));
			TestEqual("New node is a conduit",
				GetRootStateKind(AssetPath, NewGuid), FString(TEXT("conduit")));
		});

		It("Replaces a state with an inline state machine", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("ToReplace"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("kind"), TEXT("state_machine"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.replace_node")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString NewGuid;
			Result.Payload->TryGetStringField(TEXT("node_guid"), NewGuid);
			TestEqual("New node is a nested state machine",
				GetRootStateKind(AssetPath, NewGuid), FString(TEXT("state_machine_state")));
		});

		It("Fails when 'kind' is missing", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("ToReplace"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.replace_node")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails on an unknown 'kind'", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("ToReplace"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);
			Args->SetStringField(TEXT("kind"), TEXT("banana"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.replace_node")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error names the bad kind", Result.ErrorMessage.Contains(TEXT("banana")));
		});

		It("Fails when the node guid cannot be found", [this]()
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
			Args->SetStringField(TEXT("kind"), TEXT("conduit"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.replace_node")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

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
			Args->SetStringField(TEXT("kind"), TEXT("conduit"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.replace_node")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});
	});

	Describe("sm.convert_to_reference", [this]()
	{
		It("Converts an inline nested state machine into a reference asset and preserves the node guid", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString InlineGuid = AddInlineStateMachine(AssetPath, TEXT("Inline"));
			if (!TestFalse("Inline state machine guid populated", InlineGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), InlineGuid);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.convert_to_reference")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString ReferencePath;
			TestTrue("Payload has 'reference_asset_path'",
				Result.Payload->TryGetStringField(TEXT("reference_asset_path"), ReferencePath));
			TestFalse("Minted reference path non-empty", ReferencePath.IsEmpty());

			FString EchoedGuid;
			TestTrue("Payload echoes 'node_guid'",
				Result.Payload->TryGetStringField(TEXT("node_guid"), EchoedGuid));
			TestEqual("Converted node keeps its guid", EchoedGuid, InlineGuid);
		});

		It("Honors an explicit minted-asset name", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString InlineGuid = AddInlineStateMachine(AssetPath, TEXT("Inline"));
			if (!TestFalse("Inline state machine guid populated", InlineGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const FString ExplicitName = TEXT("REF_AssistConvert_") + FGuid::NewGuid().ToString();
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), InlineGuid);
			Args->SetStringField(TEXT("name"), ExplicitName);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.convert_to_reference")), Args);

			TestTrue("Result is success", Result.bSuccess);
			if (!TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			FString MintedName;
			TestTrue("Payload has 'name'", Result.Payload->TryGetStringField(TEXT("name"), MintedName));
			TestEqual("Minted asset uses the explicit name", MintedName, ExplicitName);
		});

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
				FName(TEXT("sm.convert_to_reference")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when the node is not a state-machine node", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString StateGuid = AddStateToBlueprint(AssetPath, TEXT("PlainState"));
			if (!TestFalse("State guid populated", StateGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), StateGuid);

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.convert_to_reference")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when the node guid cannot be found", [this]()
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
				FName(TEXT("sm.convert_to_reference")), Args);
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Fails when 'parent_class' cannot be loaded", [this]()
		{
			const FString AssetPath = CreateTransientBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}

			const FString InlineGuid = AddInlineStateMachine(AssetPath, TEXT("Inline"));
			if (!TestFalse("Inline state machine guid populated", InlineGuid.IsEmpty()))
			{
				return;
			}

			USMAssistSubsystem* Subsystem = GetSubsystem();

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), AssetPath);
			Args->SetStringField(TEXT("node_guid"), InlineGuid);
			Args->SetStringField(TEXT("parent_class"), TEXT("/Script/SMSystem.ThisClassDoesNotExist"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("sm.convert_to_reference")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error names parent_class", Result.ErrorMessage.Contains(TEXT("parent_class")));
		});
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
