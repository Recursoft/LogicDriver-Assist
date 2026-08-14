// Copyright Recursoft LLC. All Rights Reserved.

#if WITH_MONOLITH

#include "MonolithParamSchema.h"
#include "MonolithToolRegistry.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistSubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FSMAssistMonolithBridgeSpec, "LogicDriver.Assist.MonolithBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	static FSMAssistOperationResult EchoHandler(const TSharedRef<FJsonObject>& InArgs)
	{
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		FString Echoed;
		InArgs->TryGetStringField(TEXT("value"), Echoed);
		Payload->SetStringField(TEXT("echo"), Echoed);
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	static FSMAssistOperationInfo MakeInfo(FName InName)
	{
		FSMAssistOperationInfo Info;
		Info.Name = InName;
		Info.Description = TEXT("Bridge spec fixture.");
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&FSMAssistMonolithBridgeSpec::EchoHandler);
		return Info;
	}

	// SMAssist authors schemas in JSON-Schema form: { "type": "object", "properties": { ... },
	// "required": [ ... ] }. This fixture mirrors that shape so the bridge's conversion to
	// Monolith's flat format can be exercised.
	static FSMAssistOperationInfo MakeInfoWithJsonSchema(FName InName)
	{
		const TSharedRef<FJsonObject> AssetPathProp = MakeShared<FJsonObject>();
		AssetPathProp->SetStringField(TEXT("type"), TEXT("string"));
		AssetPathProp->SetStringField(TEXT("description"), TEXT("Asset path."));

		const TSharedRef<FJsonObject> StateNameProp = MakeShared<FJsonObject>();
		StateNameProp->SetStringField(TEXT("type"), TEXT("string"));
		StateNameProp->SetStringField(TEXT("description"), TEXT("State name."));

		const TSharedRef<FJsonObject> PositionXProp = MakeShared<FJsonObject>();
		PositionXProp->SetStringField(TEXT("type"), TEXT("number"));
		PositionXProp->SetStringField(TEXT("description"), TEXT("X position."));

		const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetObjectField(TEXT("asset_path"), AssetPathProp);
		Properties->SetObjectField(TEXT("state_name"), StateNameProp);
		Properties->SetObjectField(TEXT("position_x"), PositionXProp);

		const TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), Properties);
		TArray<TSharedPtr<FJsonValue>> Required;
		Required.Add(MakeShared<FJsonValueString>(TEXT("asset_path")));
		Schema->SetArrayField(TEXT("required"), Required);

		FSMAssistOperationInfo Info = MakeInfo(InName);
		Info.InputSchema = Schema;
		return Info;
	}

	static TSharedPtr<FJsonObject> FindActionSchema(const FString& InNamespace, const FString& InAction)
	{
		for (const FMonolithActionInfo& Action : FMonolithToolRegistry::Get().GetActions(InNamespace))
		{
			if (Action.Action == InAction)
			{
				return Action.ParamSchema;
			}
		}
		return nullptr;
	}

	static bool FindAction(const FString& InNamespace, const FString& InAction, FMonolithActionInfo& OutInfo)
	{
		for (const FMonolithActionInfo& Action : FMonolithToolRegistry::Get().GetActions(InNamespace))
		{
			if (Action.Action == InAction)
			{
				OutInfo = Action;
				return true;
			}
		}
		return false;
	}

	static FSMAssistOperationInfo MakeInfoWithImpact(FName InName, ESMAssistOperationImpact InImpact)
	{
		FSMAssistOperationInfo Info = MakeInfo(InName);
		Info.Impact = InImpact;
		return Info;
	}

END_DEFINE_SPEC(FSMAssistMonolithBridgeSpec)

void FSMAssistMonolithBridgeSpec::Define()
{
	It("Mirrors every built-in SMAssist operation into Monolith's registry", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();
		for (const FSMAssistOperationInfo& Info : Subsystem->GetAllOperationInfos())
		{
			const FString NameStr = Info.Name.ToString();
			int32 DotIdx = INDEX_NONE;
			if (!NameStr.FindChar(TEXT('.'), DotIdx))
			{
				continue;
			}

			const FString Namespace = NameStr.Left(DotIdx);
			const FString Action = NameStr.Mid(DotIdx + 1);

			TestTrue(
				FString::Printf(TEXT("%s.%s present in Monolith"), *Namespace, *Action),
				Registry.HasAction(Namespace, Action));
		}
	});

	It("Forwards each operation's impact onto its Monolith action hints", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		for (const FSMAssistOperationInfo& Info : Subsystem->GetAllOperationInfos())
		{
			const FString NameStr = Info.Name.ToString();
			int32 DotIdx = INDEX_NONE;
			if (!NameStr.FindChar(TEXT('.'), DotIdx))
			{
				continue;
			}

			FMonolithActionInfo Mirrored;
			if (!FindAction(NameStr.Left(DotIdx), NameStr.Mid(DotIdx + 1), Mirrored))
			{
				AddError(FString::Printf(TEXT("'%s' is missing from Monolith's registry."), *NameStr));
				continue;
			}

			TestEqual(FString::Printf(TEXT("%s readOnlyHint"), *NameStr),
				Mirrored.bReadOnlyHint, Info.Impact == ESMAssistOperationImpact::ReadOnly);
			TestEqual(FString::Printf(TEXT("%s destructiveHint"), *NameStr),
				Mirrored.bDestructiveHint, Info.Impact == ESMAssistOperationImpact::Destructive);
		}
	});

	It("Maps each impact onto the matching pair of Monolith hints", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const TArray<TPair<ESMAssistOperationImpact, FString>> Cases =
		{
			{ ESMAssistOperationImpact::ReadOnly, TEXT("peek") },
			{ ESMAssistOperationImpact::Destructive, TEXT("wipe") }
		};

		for (const TPair<ESMAssistOperationImpact, FString>& Case : Cases)
		{
			const FName OpName(*FString::Printf(TEXT("bridgespecimpact.%s"), *Case.Value));
			Subsystem->RegisterOperation(MakeInfoWithImpact(OpName, Case.Key));

			FMonolithActionInfo Mirrored;
			if (TestTrue(FString::Printf(TEXT("%s is registered"), *Case.Value),
				FindAction(TEXT("bridgespecimpact"), Case.Value, Mirrored)))
			{
				TestEqual(FString::Printf(TEXT("%s readOnlyHint"), *Case.Value),
					Mirrored.bReadOnlyHint, Case.Key == ESMAssistOperationImpact::ReadOnly);
				TestEqual(FString::Printf(TEXT("%s destructiveHint"), *Case.Value),
					Mirrored.bDestructiveHint, Case.Key == ESMAssistOperationImpact::Destructive);
			}

			Subsystem->UnregisterOperation(OpName);
		}
	});

	It("Forwards a new SMAssist registration to Monolith synchronously", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("bridgespec.echo"));
		Subsystem->RegisterOperation(MakeInfo(OpName));

		FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();
		TestTrue("Bridged action is registered",
			Registry.HasAction(TEXT("bridgespec"), TEXT("echo")));

		const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("value"), TEXT("hello-bridge"));

		const FMonolithActionResult Result = Registry.ExecuteAction(
			TEXT("bridgespec"), TEXT("echo"), Params);
		TestTrue("Execution succeeded", Result.bSuccess);

		if (TestTrue("Result payload populated", Result.Result.IsValid()))
		{
			FString Echoed;
			TestTrue("Payload has echo field", Result.Result->TryGetStringField(TEXT("echo"), Echoed));
			TestEqual("Echo roundtripped", Echoed, FString(TEXT("hello-bridge")));
		}

		Subsystem->UnregisterOperation(OpName);
	});

	It("Removes the action from Monolith when the SMAssist op is unregistered", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("bridgespec.remove_me"));
		Subsystem->RegisterOperation(MakeInfo(OpName));

		FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();
		TestTrue("Action present after register",
			Registry.HasAction(TEXT("bridgespec"), TEXT("remove_me")));

		Subsystem->UnregisterOperation(OpName);

		TestFalse("Action gone after unregister",
			Registry.HasAction(TEXT("bridgespec"), TEXT("remove_me")));
	});

	It("Preserves sibling actions when one op in the namespace is unregistered", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpA(TEXT("bridgespec.sibling_a"));
		const FName OpB(TEXT("bridgespec.sibling_b"));

		Subsystem->RegisterOperation(MakeInfo(OpA));
		Subsystem->RegisterOperation(MakeInfo(OpB));

		FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

		Subsystem->UnregisterOperation(OpA);

		TestFalse("sibling_a is gone",
			Registry.HasAction(TEXT("bridgespec"), TEXT("sibling_a")));
		TestTrue("sibling_b survives",
			Registry.HasAction(TEXT("bridgespec"), TEXT("sibling_b")));

		Subsystem->UnregisterOperation(OpB);
	});

	It("Skips operations whose name has no namespace separator", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("bridgespec_nodot"));
		AddExpectedError(TEXT("namespace.action"), EAutomationExpectedErrorFlags::Contains, 1);
		Subsystem->RegisterOperation(MakeInfo(OpName));

		const TArray<FString> Namespaces = FMonolithToolRegistry::Get().GetNamespaces();
		TestFalse("No namespace was created",
			Namespaces.Contains(TEXT("bridgespec_nodot")));

		Subsystem->UnregisterOperation(OpName);
	});

	It("Translates JSON-Schema input into Monolith's flat param schema", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const FName OpName(TEXT("bridgespec.schema_shape"));
		Subsystem->RegisterOperation(MakeInfoWithJsonSchema(OpName));

		const TSharedPtr<FJsonObject> Stored = FindActionSchema(TEXT("bridgespec"), TEXT("schema_shape"));
		if (TestTrue("Bridged action carries a schema", Stored.IsValid()))
		{
			// Flat form: real param names are top-level keys; the JSON-Schema wrapper keys are gone.
			TestTrue("state_name is a top-level schema key", Stored->HasField(TEXT("state_name")));
			TestTrue("position_x is a top-level schema key", Stored->HasField(TEXT("position_x")));
			TestFalse("No leftover 'properties' wrapper", Stored->HasField(TEXT("properties")));

			bool bRequired = false;
			const TSharedPtr<FJsonObject>* AssetPathDef = nullptr;
			if (TestTrue("asset_path param present", Stored->TryGetObjectField(TEXT("asset_path"), AssetPathDef) && AssetPathDef))
			{
				(*AssetPathDef)->TryGetBoolField(TEXT("required"), bRequired);
				TestTrue("asset_path marked required", bRequired);
			}

			bool bOptionalRequired = true;
			const TSharedPtr<FJsonObject>* StateNameDef = nullptr;
			if (TestTrue("state_name param present", Stored->TryGetObjectField(TEXT("state_name"), StateNameDef) && StateNameDef))
			{
				TestTrue("state_name carries a 'required' field",
					(*StateNameDef)->TryGetBoolField(TEXT("required"), bOptionalRequired));
				TestFalse("state_name (not in required[]) marked optional", bOptionalRequired);
			}

			// The reported bug: real params logged as Unknown because validation read the wrapper keys.
			const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("asset_path"), TEXT("/Game/Foo.Foo"));
			Params->SetStringField(TEXT("state_name"), TEXT("Bar"));
			Params->SetNumberField(TEXT("position_x"), 100.0);

			const TArray<FString> Unknown = FMonolithParamSchema::FindUnknownKeys(Stored, Params);
			TestEqual("No params reported unknown", Unknown.Num(), 0);
		}

		Subsystem->UnregisterOperation(OpName);
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS

#endif // WITH_MONOLITH
