// Copyright Recursoft LLC. All Rights Reserved.

#if WITH_MONOLITH

#include "MonolithToolRegistry.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistSubsystem.h"

#include "Dom/JsonObject.h"
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
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS

#endif // WITH_MONOLITH
