// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"

#include "Helpers/SMTestHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FSMCaptureLocalGraphSpec, "LogicDriver.Assist.CaptureLocalGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	FSMAssistOperationResult Run(const TCHAR* InOp, const TSharedRef<FJsonObject>& InArgs)
	{
		return GetSubsystem()->ExecuteOperation(FName(InOp), InArgs);
	}

	static TSharedRef<FJsonObject> Obj(std::initializer_list<TPair<FString, FString>> InFields)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Field : InFields)
		{
			Args->SetStringField(Field.Key, Field.Value);
		}
		return Args;
	}

	static FString Str(const FSMAssistOperationResult& InResult, const TCHAR* InField)
	{
		FString Value;
		if (InResult.Payload.IsValid())
		{
			InResult.Payload->TryGetStringField(InField, Value);
		}
		return Value;
	}

	FString CreateBlueprint()
	{
		return Str(Run(TEXT("ld.create_blueprint"),
			Obj({ { TEXT("name"), FGuid::NewGuid().ToString() }, { TEXT("path"), FAssetHandler::DefaultGamePath() } })), TEXT("asset_path"));
	}

	FString AddState(const FString& InAsset, const FString& InName)
	{
		return Str(Run(TEXT("ld.add_state"), Obj({ { TEXT("asset_path"), InAsset }, { TEXT("state_name"), InName } })), TEXT("state_guid"));
	}

	FString AddTransition(const FString& InAsset, const FString& InFrom, const FString& InTo)
	{
		return Str(Run(TEXT("ld.add_transition"),
			Obj({ { TEXT("asset_path"), InAsset }, { TEXT("from_state_guid"), InFrom }, { TEXT("to_state_guid"), InTo } })), TEXT("transition_guid"));
	}

	FSMAssistOperationResult RunCapture(const FString& InAsset, const FString& InNodeGuid, const FString& InPrefix)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetStringField(TEXT("node_guid"), InNodeGuid);
		if (!InPrefix.IsEmpty())
		{
			Args->SetStringField(TEXT("prefix"), InPrefix);
		}
		return Run(TEXT("ld.capture_local_graph"), Args);
	}

	// Capturing a bound graph has two halves: resolving node_guid to the bound graph (the same resolver
	// ld.get_local_graph uses, deterministic and headless-safe) and rendering that graph to a PNG, which
	// opens a real asset-editor GUI. Always prove the resolve via get_local_graph. Only exercise the render
	// half where a real RHI/window platform exists: under -NullRHI the asset editor's deferred
	// RequestSavePersistentLayout ticker fatals in FGenericWindow::GetRestoredDimensions, so opening an
	// editor headless would crash the whole suite. This mirrors the LD Slate specs, which skip on
	// !FApp::CanEverRender(); the render half is covered over MCP against a live editor.
	void VerifyCaptureOnResolvableNode(const FString& InAsset, const FString& InNodeGuid, const FString& InPrefix)
	{
		const FSMAssistOperationResult Resolve = Run(TEXT("ld.get_local_graph"),
			Obj({ { TEXT("asset_path"), InAsset }, { TEXT("node_guid"), InNodeGuid } }));
		if (!TestTrue(TEXT("bound graph resolves via get_local_graph"), Resolve.bSuccess))
		{
			return;
		}

		if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized())
		{
			AddInfo(TEXT("Rendering disabled (-NullRHI); skipping the editor-opening capture. Bound-graph resolution verified above; the render half is exercised over MCP against a live editor."));
			return;
		}

		const FSMAssistOperationResult Capture = RunCapture(InAsset, InNodeGuid, InPrefix);
		if (!TestTrue(FString::Printf(TEXT("capture succeeded (%s)"), *Capture.ErrorMessage), Capture.bSuccess))
		{
			return;
		}
		TestTrue(TEXT("capture payload valid"), Capture.Payload.IsValid());

		const FString Path = Str(Capture, TEXT("path"));
		TestTrue(TEXT("path is non-empty"), !Path.IsEmpty());
		TestTrue(TEXT("path ends with .png"), Path.EndsWith(TEXT(".png")));
		TestTrue(TEXT("path carries the requested prefix"), Path.Contains(InPrefix));

		double Width = 0.0;
		double Height = 0.0;
		double Bytes = 0.0;
		TestTrue(TEXT("width present"), Capture.Payload->TryGetNumberField(TEXT("width"), Width));
		TestTrue(TEXT("height present"), Capture.Payload->TryGetNumberField(TEXT("height"), Height));
		TestTrue(TEXT("bytes present"), Capture.Payload->TryGetNumberField(TEXT("bytes"), Bytes));
		TestTrue(TEXT("width is plausible"), Width > 0.0);
		TestTrue(TEXT("height is plausible"), Height > 0.0);
		TestTrue(TEXT("bytes non-zero"), Bytes > 0.0);
		TestEqual(TEXT("mime is image/png"), Str(Capture, TEXT("mime")), FString(TEXT("image/png")));
		TestTrue(TEXT("PNG file exists on disk"), IFileManager::Get().FileExists(*Path));
	}

END_DEFINE_SPEC(FSMCaptureLocalGraphSpec)

void FSMCaptureLocalGraphSpec::Define()
{
	It("captures a transition's gated bound graph as a PNG", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		const FString From = AddState(Asset, TEXT("Red"));
		const FString To = AddState(Asset, TEXT("Green"));
		const FString Transition = AddTransition(Asset, From, To);
		if (!TestTrue(TEXT("Transition created"), !Transition.IsEmpty()))
		{
			return;
		}

		// Author a gate inside the transition's CanEnterTransition graph so the captured graph carries real
		// logic (a TimeInState read node alongside the transition result node), mirroring the inline-gate
		// scenario this op exists to visualize.
		const FSMAssistOperationResult Gate = Run(TEXT("ld.spawn_local_graph_read_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Transition }, { TEXT("type"), TEXT("TimeInState") } }));
		TestTrue(TEXT("TimeInState gate node spawned into the transition graph"), Gate.bSuccess);

		const FString Prefix = TEXT("cap_transition_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		VerifyCaptureOnResolvableNode(Asset, Transition, Prefix);
	});

	It("captures a state's bound graph as a PNG", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		const FString State = AddState(Asset, TEXT("S1"));
		if (!TestTrue(TEXT("State created"), !State.IsEmpty()))
		{
			return;
		}

		const FString Prefix = TEXT("cap_state_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		VerifyCaptureOnResolvableNode(Asset, State, Prefix);
	});

	It("errors on an unknown node guid", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunCapture(Asset, FGuid::NewGuid().ToString(), FString());
		TestFalse(TEXT("unknown guid fails"), Result.bSuccess);
	});

	It("errors when node_guid is missing", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), Asset);
		const FSMAssistOperationResult Result = Run(TEXT("ld.capture_local_graph"), Args);
		TestFalse(TEXT("missing node_guid fails"), Result.bSuccess);
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
