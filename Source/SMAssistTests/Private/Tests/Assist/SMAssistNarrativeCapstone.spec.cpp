// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistNarrativeTestClasses.h"

#include "Helpers/SMTestHelpers.h"

#include "Blueprints/SMBlueprint.h"
#include "SMUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

// Narrative capstone regression guard: authors the full "Lighthouse" narrative
// system through ExecuteOperation only, asserts its structure through sm.* read
// ops, and walks it headlessly to the end state. Mirrors the live MCP-authored
// system at /Game/Capstone57 with context-backed fixture twins.
BEGIN_DEFINE_SPEC(FAssistNarrativeCapstoneSpec, "LogicDriver.Assist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	struct FAuthoredNarrative
	{
		FString MainPath;
		FString SubPath;
		TMap<FString, FString> States;
		TMap<FString, FString> Edges;
		bool bSuccess = false;
	};

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	FSMAssistOperationResult Exec(const TCHAR* InOp, const TSharedRef<FJsonObject>& InArgs)
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return FSMAssistOperationResult::MakeError(TEXT("No subsystem."));
		}

		FSMAssistOperationResult Result = Subsystem->ExecuteOperation(FName(InOp), InArgs);
		if (!Result.bSuccess)
		{
			AddError(FString::Printf(TEXT("%s failed: %s"), InOp, *Result.ErrorMessage));
		}
		return Result;
	}

	FString CreateAsset(const FString& InName)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), InName + FGuid::NewGuid().ToString());
		Args->SetStringField(TEXT("path"), FAssetHandler::DefaultGamePath());
		const FSMAssistOperationResult Result = Exec(TEXT("sm.create_blueprint"), Args);
		FString AssetPath;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("asset_path"), AssetPath);
		}
		return AssetPath;
	}

	FString AddState(const FString& InAsset, const FString& InName, UClass* InClass,
		int32 InX, int32 InY, bool bIsEntry = false)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetStringField(TEXT("state_name"), InName);
		Args->SetNumberField(TEXT("position_x"), InX);
		Args->SetNumberField(TEXT("position_y"), InY);
		Args->SetBoolField(TEXT("is_entry"), bIsEntry);
		if (InClass)
		{
			Args->SetStringField(TEXT("state_class"), InClass->GetPathName());
		}
		const FSMAssistOperationResult Result = Exec(TEXT("sm.add_state"), Args);
		FString Guid;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("state_guid"), Guid);
		}
		return Guid;
	}

	FString AddTransition(const FString& InAsset, const FString& InFromGuid,
		const FString& InToGuid, UClass* InClass)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetStringField(TEXT("from_state_guid"), InFromGuid);
		Args->SetStringField(TEXT("to_state_guid"), InToGuid);
		if (InClass)
		{
			Args->SetStringField(TEXT("transition_class"), InClass->GetPathName());
		}
		const FSMAssistOperationResult Result = Exec(TEXT("sm.add_transition"), Args);
		FString Guid;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("transition_guid"), Guid);
		}
		return Guid;
	}

	void SetNodeProperty(const FString& InAsset, const FString& InGuid,
		const FString& InProperty, const TSharedRef<FJsonValue>& InValue)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetStringField(TEXT("node_guid"), InGuid);
		Args->SetStringField(TEXT("property_name"), InProperty);
		Args->SetField(TEXT("value"), InValue);
		Exec(TEXT("sm.set_node_property"), Args);
	}

	TMap<FString, FString> GetNodeProperties(const FString& InAsset, const FString& InGuid)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetStringField(TEXT("node_guid"), InGuid);
		const FSMAssistOperationResult Result = Exec(TEXT("sm.get_node_properties"), Args);

		TMap<FString, FString> Values;
		const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
		if (Result.bSuccess && Result.Payload.IsValid() &&
			Result.Payload->TryGetArrayField(TEXT("properties"), Properties))
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Properties)
			{
				const TSharedPtr<FJsonObject>* EntryObj = nullptr;
				if (Entry->TryGetObject(EntryObj))
				{
					Values.Add((*EntryObj)->GetStringField(TEXT("name")),
						(*EntryObj)->GetStringField(TEXT("value")));
				}
			}
		}
		return Values;
	}

	TSharedPtr<FJsonObject> CompileAsset(const FString& InAsset)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		const FSMAssistOperationResult Result = Exec(TEXT("sm.compile"), Args);
		return Result.bSuccess ? Result.Payload : nullptr;
	}

	FAuthoredNarrative AuthorNarrativeSystem()
	{
		FAuthoredNarrative Out;

		// Sub-conversation: Rumor1 -> Rumor2 -> RumorEnd (terminal end state).
		Out.SubPath = CreateAsset(TEXT("SM_NarrativeRumor_"));
		if (Out.SubPath.IsEmpty())
		{
			return Out;
		}
		const FString Rumor1 = AddState(Out.SubPath, TEXT("Rumor1"),
			USMAssistSailorLineState::StaticClass(), 200, 0, /*bIsEntry*/ true);
		const FString Rumor2 = AddState(Out.SubPath, TEXT("Rumor2"),
			USMAssistKeeperLineState::StaticClass(), 500, 0);
		const FString RumorEnd = AddState(Out.SubPath, TEXT("RumorEnd"), nullptr, 800, 0);
		AddTransition(Out.SubPath, Rumor1, Rumor2, USMAssistAdvanceAfterLineTransition::StaticClass());
		AddTransition(Out.SubPath, Rumor2, RumorEnd, USMAssistAdvanceAfterLineTransition::StaticClass());
		SetNodeProperty(Out.SubPath, Rumor1, TEXT("Line"),
			MakeShared<FJsonValueString>(TEXT("They say the cargo never washed ashore.")));
		SetNodeProperty(Out.SubPath, Rumor2, TEXT("Line"),
			MakeShared<FJsonValueString>(TEXT("The sea keeps what it takes.")));

		// Main conversation.
		Out.MainPath = CreateAsset(TEXT("SM_NarrativeLighthouse_"));
		if (Out.MainPath.IsEmpty())
		{
			return Out;
		}
		auto State = [&](const TCHAR* InName, UClass* InClass, int32 InX, int32 InY, bool bIsEntry = false)
		{
			Out.States.Add(InName, AddState(Out.MainPath, InName, InClass, InX, InY, bIsEntry));
		};
		State(TEXT("Start"), nullptr, 200, 0, /*bIsEntry*/ true);
		State(TEXT("Greet_First"), USMAssistKeeperLineState::StaticClass(), 500, -150);
		State(TEXT("Greet_Return"), USMAssistKeeperLineState::StaticClass(), 500, 150);
		State(TEXT("Hub"), USMAssistKeeperLineState::StaticClass(), 800, 0);
		State(TEXT("StormTale"), USMAssistKeeperLineState::StaticClass(), 1100, -250);
		State(TEXT("SailorIntro"), USMAssistSailorLineState::StaticClass(), 1100, 0);
		State(TEXT("Farewell"), USMAssistKeeperLineState::StaticClass(), 1100, 300);
		State(TEXT("MarkVisit"), USMAssistMarkVisitState::StaticClass(), 1400, -250);

		{
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), Out.MainPath);
			Args->SetStringField(TEXT("state_name"), TEXT("RumorRef"));
			Args->SetStringField(TEXT("reference_asset_path"), Out.SubPath);
			Args->SetBoolField(TEXT("use_intermediate_graph"), true);
			Args->SetNumberField(TEXT("position_x"), 1400);
			Args->SetNumberField(TEXT("position_y"), 0);
			const FSMAssistOperationResult Result = Exec(TEXT("sm.add_reference"), Args);
			FString Guid;
			if (Result.bSuccess && Result.Payload.IsValid())
			{
				Result.Payload->TryGetStringField(TEXT("state_guid"), Guid);
			}
			Out.States.Add(TEXT("RumorRef"), Guid);
		}

		{
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), Out.MainPath);
			Args->SetStringField(TEXT("state_guid"), Out.States[TEXT("Hub")]);
			Args->SetStringField(TEXT("state_class"),
				USMAssistNarrativeDirectorState::StaticClass()->GetPathName());
			Exec(TEXT("sm.add_state_stack"), Args);
		}

		auto Edge = [&](const TCHAR* InFrom, const TCHAR* InTo, UClass* InClass)
		{
			Out.Edges.Add(FString::Printf(TEXT("%s->%s"), InFrom, InTo),
				AddTransition(Out.MainPath, Out.States[InFrom], Out.States[InTo], InClass));
		};
		Edge(TEXT("Start"), TEXT("Greet_First"), USMAssistVisitGateTransition::StaticClass());
		Edge(TEXT("Start"), TEXT("Greet_Return"), USMAssistVisitGateTransition::StaticClass());
		Edge(TEXT("Greet_First"), TEXT("Hub"), USMAssistAdvanceAfterLineTransition::StaticClass());
		Edge(TEXT("Greet_Return"), TEXT("Hub"), USMAssistAdvanceAfterLineTransition::StaticClass());
		Edge(TEXT("Hub"), TEXT("StormTale"), USMAssistChoiceGateTransition::StaticClass());
		Edge(TEXT("Hub"), TEXT("SailorIntro"), USMAssistChoiceGateTransition::StaticClass());
		Edge(TEXT("Hub"), TEXT("Farewell"), USMAssistChoiceGateTransition::StaticClass());
		Edge(TEXT("StormTale"), TEXT("MarkVisit"), USMAssistAdvanceAfterLineTransition::StaticClass());
		Edge(TEXT("SailorIntro"), TEXT("RumorRef"), USMAssistAdvanceAfterLineTransition::StaticClass());
		Edge(TEXT("RumorRef"), TEXT("MarkVisit"), USMAssistSubDoneGateTransition::StaticClass());
		Edge(TEXT("MarkVisit"), TEXT("Start"), nullptr);

		SetNodeProperty(Out.MainPath, Out.Edges[TEXT("Start->Greet_First")],
			TEXT("bRequireReturning"), MakeShared<FJsonValueBoolean>(false));
		SetNodeProperty(Out.MainPath, Out.Edges[TEXT("Start->Greet_Return")],
			TEXT("bRequireReturning"), MakeShared<FJsonValueBoolean>(true));
		SetNodeProperty(Out.MainPath, Out.Edges[TEXT("Hub->StormTale")],
			TEXT("RequiredChoice"), MakeShared<FJsonValueNumber>(0));
		SetNodeProperty(Out.MainPath, Out.Edges[TEXT("Hub->SailorIntro")],
			TEXT("RequiredChoice"), MakeShared<FJsonValueNumber>(1));
		SetNodeProperty(Out.MainPath, Out.Edges[TEXT("Hub->Farewell")],
			TEXT("RequiredChoice"), MakeShared<FJsonValueNumber>(2));

		{
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), Out.MainPath);
			Args->SetStringField(TEXT("transition_guid"), Out.Edges[TEXT("MarkVisit->Start")]);
			Args->SetBoolField(TEXT("condition"), true);
			Exec(TEXT("sm.set_transition_condition"), Args);
		}
		{
			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), Out.MainPath);
			Args->SetStringField(TEXT("transition_guid"), Out.Edges[TEXT("MarkVisit->Start")]);
			Args->SetNumberField(TEXT("position_x"), 800);
			Args->SetNumberField(TEXT("position_y"), -450);
			Exec(TEXT("sm.add_transition_reroute"), Args);
		}

		const TMap<FString, FString> Lines = {
			{ TEXT("Greet_First"), TEXT("Welcome in out of the wind, stranger.") },
			{ TEXT("Greet_Return"), TEXT("Back again, are you?") },
			{ TEXT("Hub"), TEXT("What would you know of this place?") },
			{ TEXT("StormTale"), TEXT("The storm took the relief boat.") },
			{ TEXT("SailorIntro"), TEXT("I was on the water that night.") },
			{ TEXT("Farewell"), TEXT("Then go with the light at your back.") },
		};
		for (const TPair<FString, FString>& Pair : Lines)
		{
			SetNodeProperty(Out.MainPath, Out.States[Pair.Key], TEXT("Line"),
				MakeShared<FJsonValueString>(Pair.Value));
		}

		Out.bSuccess = !HasAnyErrors();
		return Out;
	}

END_DEFINE_SPEC(FAssistNarrativeCapstoneSpec)

void FAssistNarrativeCapstoneSpec::Define()
{
	Describe("Narrative capstone", [this]()
	{
		It("Authors the full system via sm.* and verifies structure and compile", [this]()
		{
			const FAuthoredNarrative Authored = AuthorNarrativeSystem();
			if (!TestTrue("Authoring succeeded", Authored.bSuccess))
			{
				return;
			}

			const TSharedPtr<FJsonObject> SubCompile = CompileAsset(Authored.SubPath);
			const TSharedPtr<FJsonObject> MainCompile = CompileAsset(Authored.MainPath);
			if (!TestTrue("Both assets compiled", SubCompile.IsValid() && MainCompile.IsValid()))
			{
				return;
			}
			TestFalse("Sub compile has no errors", SubCompile->GetBoolField(TEXT("has_errors")));
			TestFalse("Sub compile has no warnings", SubCompile->GetBoolField(TEXT("has_warnings")));
			TestFalse("Main compile has no errors", MainCompile->GetBoolField(TEXT("has_errors")));
			TestFalse("Main compile has no warnings", MainCompile->GetBoolField(TEXT("has_warnings")));

			const TSharedRef<FJsonObject> AssetArgs = MakeShared<FJsonObject>();
			AssetArgs->SetStringField(TEXT("asset_path"), Authored.MainPath);
			const FSMAssistOperationResult AssetResult = Exec(TEXT("sm.get_asset"), AssetArgs);
			if (!TestTrue("get_asset succeeded", AssetResult.bSuccess && AssetResult.Payload.IsValid()))
			{
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
			AssetResult.Payload->TryGetArrayField(TEXT("transitions"), Transitions);
			if (!TestNotNull("transitions array present", Transitions))
			{
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* EntryGuids = nullptr;
			AssetResult.Payload->TryGetArrayField(TEXT("entry_state_guids"), EntryGuids);
			if (TestTrue("one entry state", EntryGuids && EntryGuids->Num() == 1))
			{
				TestEqual("entry is Start", (*EntryGuids)[0]->AsString(), Authored.States[TEXT("Start")]);
			}

			int32 HubFanOut = 0;
			int32 LoopBackCount = 0;
			int32 FarewellOut = 0;
			bool bHubFanOutAllChoiceGates = true;
			for (const TSharedPtr<FJsonValue>& Entry : *Transitions)
			{
				const TSharedPtr<FJsonObject>* EntryObj = nullptr;
				if (!Entry->TryGetObject(EntryObj))
				{
					continue;
				}
				const FString From = (*EntryObj)->GetStringField(TEXT("from_state_guid"));
				const FString To = (*EntryObj)->GetStringField(TEXT("to_state_guid"));
				if (From == Authored.States[TEXT("Hub")])
				{
					++HubFanOut;
					FString TransitionClass;
					(*EntryObj)->TryGetStringField(TEXT("transition_class"), TransitionClass);
					bHubFanOutAllChoiceGates &= TransitionClass.Contains(TEXT("SMAssistChoiceGateTransition"));
				}
				if (From == Authored.States[TEXT("MarkVisit")] && To == Authored.States[TEXT("Start")])
				{
					++LoopBackCount;
				}
				if (From == Authored.States[TEXT("Farewell")])
				{
					++FarewellOut;
				}
			}
			TestEqual("hub fan-out is 3", HubFanOut, 3);
			TestTrue("hub fan-out all choice gates", bHubFanOutAllChoiceGates);
			TestTrue("loop-back MarkVisit->Start exists", LoopBackCount >= 1);
			TestEqual("Farewell is terminal", FarewellOut, 0);

			const TMap<FString, FString> HubProps =
				GetNodeProperties(Authored.MainPath, Authored.States[TEXT("Hub")]);
			TestEqual("Hub speaker is Keeper", HubProps.FindRef(TEXT("SpeakerTag")), TEXT("Keeper"));
			TestTrue("Hub line text present",
				HubProps.FindRef(TEXT("Line")).Contains(TEXT("What would you know")));

			const TMap<FString, FString> SailorProps =
				GetNodeProperties(Authored.MainPath, Authored.States[TEXT("SailorIntro")]);
			TestEqual("SailorIntro speaker is Sailor", SailorProps.FindRef(TEXT("SpeakerTag")), TEXT("Sailor"));

			const TMap<FString, FString> StormGate =
				GetNodeProperties(Authored.MainPath, Authored.Edges[TEXT("Hub->StormTale")]);
			TestEqual("StormTale gate RequiredChoice 0", StormGate.FindRef(TEXT("RequiredChoice")), TEXT("0"));
			const TMap<FString, FString> FarewellGate =
				GetNodeProperties(Authored.MainPath, Authored.Edges[TEXT("Hub->Farewell")]);
			TestEqual("Farewell gate RequiredChoice 2", FarewellGate.FindRef(TEXT("RequiredChoice")), TEXT("2"));
			const TMap<FString, FString> ReturnGate =
				GetNodeProperties(Authored.MainPath, Authored.Edges[TEXT("Start->Greet_Return")]);
			TestEqual("Greet_Return gate requires returning",
				ReturnGate.FindRef(TEXT("bRequireReturning")), TEXT("True"));
		});

		It("Walks the authored system headlessly to the end state", [this]()
		{
			const FAuthoredNarrative Authored = AuthorNarrativeSystem();
			if (!TestTrue("Authoring succeeded", Authored.bSuccess))
			{
				return;
			}
			CompileAsset(Authored.SubPath);
			CompileAsset(Authored.MainPath);
			if (HasAnyErrors())
			{
				return;
			}

			USMBlueprint* Blueprint = Cast<USMBlueprint>(
				FSoftObjectPath(Authored.MainPath).TryLoad());
			if (!TestNotNull("Main blueprint loaded", Blueprint))
			{
				return;
			}

			USMAssistNarrativeContext* Context = NewObject<USMAssistNarrativeContext>(GetTransientPackage());
			USMInstance* Instance = USMBlueprintUtils::CreateStateMachineInstance(
				TSubclassOf<USMInstance>(Blueprint->GeneratedClass.Get()), Context);
			if (!TestNotNull("Instance created", Instance))
			{
				return;
			}

			Instance->Start();
			for (int32 Idx = 0; Idx < 40 && !Instance->IsInEndState(); ++Idx)
			{
				Instance->Update(1.0f);
			}

			TestTrue("Reached the end state", Instance->IsInEndState());
			const FSMState_Base* ActiveState = Instance->GetSingleActiveState();
			if (TestNotNull("Single active state", ActiveState))
			{
				TestEqual("Final state is Farewell", ActiveState->GetNodeName(), TEXT("Farewell"));
			}

			const FString ExpectedWalk = TEXT(
				",Greet_First,Hub,StormTale,MarkVisit"
				",Greet_Return,Hub,SailorIntro,Rumor1,Rumor2,MarkVisit"
				",Greet_Return,Hub,Farewell");
			TestEqual("Breadcrumb walk exact", Context->Breadcrumbs, ExpectedWalk);
			TestEqual("Two full loop passes", Context->VisitCount, 2);
			TestEqual("Final scripted choice", Context->ChoiceIndex, 2);

			Instance->Stop();
		});
	});
}

#endif

#endif
