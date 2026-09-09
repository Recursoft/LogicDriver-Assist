// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationResult.h"

#include "Helpers/SMTestHelpers.h"

#include "Blueprints/SMBlueprint.h"
#include "Graph/SMGraph.h"
#include "Graph/Nodes/SMGraphNode_RerouteNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineEntryNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "ISMAssetToolsModule.h"
#include "ISMGraphGeneration.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FSMAssistLayoutSpec, "LogicDriver.Assist.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	FSMAssistOperationResult Run(const TCHAR* InOp, const TSharedRef<FJsonObject>& InArgs)
	{
		return GEditor->GetEditorSubsystem<USMAssistSubsystem>()->ExecuteOperation(FName(InOp), InArgs);
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
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), FGuid::NewGuid().ToString());
		Args->SetStringField(TEXT("path"), FAssetHandler::DefaultGamePath());
		return Str(Run(TEXT("ld.create_blueprint"), Args), TEXT("asset_path"));
	}

	// Every state starts stacked on the origin, on top of the entry node and on top of each other, so a
	// pass that fails to move them is never mistaken for one that placed them well.
	FString AddState(const FString& InAsset, const FString& InName, bool bIsEntry = false)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetStringField(TEXT("state_name"), InName);
		Args->SetNumberField(TEXT("position_x"), 0.0);
		Args->SetNumberField(TEXT("position_y"), 0.0);
		if (bIsEntry)
		{
			Args->SetBoolField(TEXT("is_entry"), true);
		}
		return Str(Run(TEXT("ld.add_state"), Args), TEXT("state_guid"));
	}

	FString AddTransition(const FString& InAsset, const FString& InFrom, const FString& InTo)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetStringField(TEXT("from_state_guid"), InFrom);
		Args->SetStringField(TEXT("to_state_guid"), InTo);
		return Str(Run(TEXT("ld.add_transition"), Args), TEXT("transition_guid"));
	}

	FSMAssistOperationResult Layout(const FString& InAsset, bool bApply)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetBoolField(TEXT("apply"), bApply);
		return Run(TEXT("ld.layout_states"), Args);
	}

	FSMAssistOperationResult GetGraphView(const FString& InAsset)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		return Run(TEXT("ld.get_graph_view"), Args);
	}

	static int32 ArrayCount(const FSMAssistOperationResult& InResult, const TCHAR* InField)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!InResult.Payload.IsValid() || !InResult.Payload->TryGetArrayField(InField, Array))
		{
			return INDEX_NONE;
		}
		return Array->Num();
	}

	// The first graphs[] entry, which is the root graph for either scope.
	static const TSharedPtr<FJsonObject>* RootGraphEntry(const FSMAssistOperationResult& InResult)
	{
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (!InResult.Payload.IsValid() || !InResult.Payload->TryGetArrayField(TEXT("graphs"), Graphs) || Graphs->Num() == 0)
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		return (*Graphs)[0]->TryGetObject(Entry) ? Entry : nullptr;
	}

	static int32 PlannedRerouteCount(const FSMAssistOperationResult& InResult)
	{
		const TSharedPtr<FJsonObject>* Entry = RootGraphEntry(InResult);
		const TArray<TSharedPtr<FJsonValue>>* Reroutes = nullptr;
		if (!Entry || !(*Entry)->TryGetArrayField(TEXT("reroutes"), Reroutes))
		{
			return INDEX_NONE;
		}
		return Reroutes->Num();
	}

	static bool ProposedPosition(const FSMAssistOperationResult& InResult, const FString& InNodeGuid, FVector2f& OutPosition)
	{
		const TSharedPtr<FJsonObject>* Entry = RootGraphEntry(InResult);
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Entry || !(*Entry)->TryGetArrayField(TEXT("node_layout"), Nodes))
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Nodes)
		{
			const TSharedPtr<FJsonObject>* NodeEntry = nullptr;
			FString Guid;
			if (!Value->TryGetObject(NodeEntry) || !(*NodeEntry)->TryGetStringField(TEXT("node_guid"), Guid) || Guid != InNodeGuid)
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
			if (!(*NodeEntry)->TryGetArrayField(TEXT("proposed_position"), Position) || Position->Num() != 2)
			{
				return false;
			}
			OutPosition = FVector2f(
				static_cast<float>((*Position)[0]->AsNumber()),
				static_cast<float>((*Position)[1]->AsNumber()));
			return true;
		}
		return false;
	}

	static USMGraph* LoadRootGraph(const FString& InAssetPath)
	{
		USMBlueprint* Blueprint = Cast<USMBlueprint>(StaticLoadObject(USMBlueprint::StaticClass(), nullptr, *InAssetPath));
		const TSharedPtr<ISMGraphGeneration> GraphGeneration = ISMAssetToolsModule::Get().GetGraphGenerationInterface();
		return (Blueprint && GraphGeneration.IsValid()) ? GraphGeneration->GetRootStateMachineGraph(Blueprint) : nullptr;
	}

	static int32 CountRerouteNodes(const FString& InAssetPath)
	{
		USMGraph* Graph = LoadRootGraph(InAssetPath);
		if (!Graph)
		{
			return INDEX_NONE;
		}
		int32 Count = 0;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->IsA<USMGraphNode_RerouteNode>())
			{
				++Count;
			}
		}
		return Count;
	}

	static bool GraphEntryNodePosition(const USMGraph* InGraph, FVector2f& OutPosition)
	{
		USMGraphNode_StateMachineEntryNode* EntryNode = InGraph ? InGraph->GetEntryNode() : nullptr;
		if (!EntryNode)
		{
			return false;
		}
		OutPosition = FVector2f(static_cast<float>(EntryNode->NodePosX), static_cast<float>(EntryNode->NodePosY));
		return true;
	}

	static bool EntryNodePosition(const FString& InAssetPath, FVector2f& OutPosition)
	{
		return GraphEntryNodePosition(LoadRootGraph(InAssetPath), OutPosition);
	}

	// Entry node position of the graph nested inside the state machine node with InContainerGuid.
	static bool NestedEntryNodePosition(const FString& InAssetPath, const FString& InContainerGuid, FVector2f& OutPosition)
	{
		FGuid ContainerGuid;
		USMGraph* RootGraph = LoadRootGraph(InAssetPath);
		if (!RootGraph || !FGuid::Parse(InContainerGuid, ContainerGuid))
		{
			return false;
		}
		for (UEdGraphNode* Node : RootGraph->Nodes)
		{
			const USMGraphNode_StateMachineStateNode* Container = Cast<USMGraphNode_StateMachineStateNode>(Node);
			if (Container && Container->NodeGuid == ContainerGuid)
			{
				return GraphEntryNodePosition(Cast<USMGraph>(Container->GetBoundGraph()), OutPosition);
			}
		}
		return false;
	}

	// ld.layout_states opens the asset editor to measure the graph, and opening one headless fatals in
	// FGenericWindow::GetRestoredDimensions when the deferred RequestSavePersistentLayout ticker fires,
	// about 20 seconds later. A short run can finish before the ticker and look safe, so every test that
	// runs the op is gated the same way SMAssistOperations.spec.cpp gates CanOpenAssetEditor. The
	// overlap checks and the second placement pass also need a painted widget, which -NullRHI never
	// provides.
	bool CanOpenAssetEditor()
	{
		if (FApp::CanEverRender() && FSlateApplication::IsInitialized())
		{
			return true;
		}
		AddInfo(TEXT("Rendering disabled (-NullRHI); skipping the checks that open an asset editor. They are exercised over MCP against a live editor."));
		return false;
	}

	// The graphs[] entry whose graph_path ends with InSuffix, for reading a nested graph's layout.
	static const TSharedPtr<FJsonObject>* GraphEntryEndingWith(const FSMAssistOperationResult& InResult, const FString& InSuffix)
	{
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (!InResult.Payload.IsValid() || !InResult.Payload->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			return nullptr;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Graphs)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			FString GraphPath;
			if (Value->TryGetObject(Entry) && (*Entry)->TryGetStringField(TEXT("graph_path"), GraphPath)
				&& GraphPath.EndsWith(InSuffix))
			{
				return Entry;
			}
		}
		return nullptr;
	}

	// Smallest proposed X across a graphs[] entry's nodes, or a large number when it lists none.
	static float SmallestProposedX(const TSharedPtr<FJsonObject>& InGraphEntry, int32& OutNodeCount)
	{
		OutNodeCount = 0;
		float Smallest = TNumericLimits<float>::Max();
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!InGraphEntry->TryGetArrayField(TEXT("node_layout"), Nodes))
		{
			return Smallest;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Nodes)
		{
			const TSharedPtr<FJsonObject>* NodeEntry = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
			if (!Value->TryGetObject(NodeEntry) || !(*NodeEntry)->TryGetArrayField(TEXT("proposed_position"), Position)
				|| Position->Num() != 2)
			{
				continue;
			}
			++OutNodeCount;
			Smallest = FMath::Min(Smallest, static_cast<float>((*Position)[0]->AsNumber()));
		}
		return Smallest;
	}

END_DEFINE_SPEC(FSMAssistLayoutSpec)

void FSMAssistLayoutSpec::Define()
{
	It("places the first state past the entry node rather than on top of it", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("First"), /*bIsEntry=*/true);
		if (!TestTrue(TEXT("asset and entry state created"), !Asset.IsEmpty() && !First.IsEmpty()))
		{
			return;
		}
		if (!CanOpenAssetEditor())
		{
			return;
		}

		const FSMAssistOperationResult Result = Layout(Asset, /*bApply=*/true);
		if (!TestTrue(FString::Printf(TEXT("layout_states succeeded (%s)"), *Result.ErrorMessage), Result.bSuccess))
		{
			return;
		}

		FVector2f EntryPosition = FVector2f::ZeroVector;
		FVector2f StatePosition = FVector2f::ZeroVector;
		if (!TestTrue(TEXT("entry node and state positions read"),
			EntryNodePosition(Asset, EntryPosition) && ProposedPosition(Result, First, StatePosition)))
		{
			return;
		}

		// The entry node measures under 100 wide, so clearing it by that much puts the state past its
		// right edge without pinning the check to one engine version's widget size.
		TestTrue(FString::Printf(TEXT("the state clears the entry node (state X %.1f, entry X %.1f)"),
			StatePosition.X, EntryPosition.X), StatePosition.X >= EntryPosition.X + 100.0f);
	});

	It("reports a state left sitting on the entry node", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("OnEntry"), /*bIsEntry=*/true);
		if (!TestTrue(TEXT("asset and state created"), !Asset.IsEmpty() && !First.IsEmpty()))
		{
			return;
		}
		if (!CanOpenAssetEditor())
		{
			return;
		}

		// Deliberately not laid out: the state is still at the origin the add put it at, which is where
		// the entry node is. The overlap set includes the entry node, so this pair is reported.
		const FSMAssistOperationResult View = GetGraphView(Asset);
		if (!TestTrue(FString::Printf(TEXT("get_graph_view succeeded (%s)"), *View.ErrorMessage), View.bSuccess))
		{
			return;
		}
		TestTrue(TEXT("the state stacked on the entry node is reported"), ArrayCount(View, TEXT("overlaps")) > 0);
	});

	It("clears every overlap in a chain of states added at one position", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("First"), /*bIsEntry=*/true);
		const FString Second = AddState(Asset, TEXT("Second"));
		const FString Third = AddState(Asset, TEXT("Third"));
		if (!TestTrue(TEXT("states created"), !First.IsEmpty() && !Second.IsEmpty() && !Third.IsEmpty()))
		{
			return;
		}
		AddTransition(Asset, First, Second);
		AddTransition(Asset, Second, Third);
		if (!CanOpenAssetEditor())
		{
			return;
		}

		const FSMAssistOperationResult Result = Layout(Asset, /*bApply=*/true);
		if (!TestTrue(FString::Printf(TEXT("layout_states succeeded (%s)"), *Result.ErrorMessage), Result.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("nothing was left unmeasured"), ArrayCount(Result, TEXT("measurement_warnings")), 0);

		const FSMAssistOperationResult View = GetGraphView(Asset);
		TestEqual(TEXT("no boxes overlap after the layout"), ArrayCount(View, TEXT("overlaps")), 0);
	});

	It("plans a rail for an edge that skips a layer", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("First"), /*bIsEntry=*/true);
		const FString Second = AddState(Asset, TEXT("Second"));
		const FString Third = AddState(Asset, TEXT("Third"));
		if (!TestTrue(TEXT("states created"), !First.IsEmpty() && !Second.IsEmpty() && !Third.IsEmpty()))
		{
			return;
		}
		AddTransition(Asset, First, Second);
		AddTransition(Asset, Second, Third);
		// Spans two layers, so its marker would otherwise be drawn on top of Second.
		AddTransition(Asset, First, Third);
		if (!CanOpenAssetEditor())
		{
			return;
		}

		const FSMAssistOperationResult Result = Layout(Asset, /*bApply=*/false);
		if (!TestTrue(FString::Printf(TEXT("layout_states succeeded (%s)"), *Result.ErrorMessage), Result.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("the skip edge is planned two reroutes and the adjacent edges none"),
			PlannedRerouteCount(Result), 2);
	});

	It("plans a rail for the back-edge of a cycle", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("First"), /*bIsEntry=*/true);
		const FString Second = AddState(Asset, TEXT("Second"));
		const FString Third = AddState(Asset, TEXT("Third"));
		if (!TestTrue(TEXT("states created"), !First.IsEmpty() && !Second.IsEmpty() && !Third.IsEmpty()))
		{
			return;
		}
		AddTransition(Asset, First, Second);
		AddTransition(Asset, Second, Third);
		AddTransition(Asset, Third, First);
		if (!CanOpenAssetEditor())
		{
			return;
		}

		const FSMAssistOperationResult Result = Layout(Asset, /*bApply=*/false);
		if (!TestTrue(FString::Printf(TEXT("layout_states succeeded (%s)"), *Result.ErrorMessage), Result.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("the back-edge is planned two reroutes"), PlannedRerouteCount(Result), 2);
	});

	It("adds a rail once however many times it runs", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("First"), /*bIsEntry=*/true);
		const FString Second = AddState(Asset, TEXT("Second"));
		const FString Third = AddState(Asset, TEXT("Third"));
		if (!TestTrue(TEXT("states created"), !First.IsEmpty() && !Second.IsEmpty() && !Third.IsEmpty()))
		{
			return;
		}
		AddTransition(Asset, First, Second);
		AddTransition(Asset, Second, Third);
		AddTransition(Asset, Third, First);
		if (!CanOpenAssetEditor())
		{
			return;
		}

		if (!TestTrue(TEXT("first layout succeeded"), Layout(Asset, /*bApply=*/true).bSuccess))
		{
			return;
		}
		const int32 AfterFirst = CountRerouteNodes(Asset);
		TestEqual(TEXT("the back-edge rail is built"), AfterFirst, 2);

		if (!TestTrue(TEXT("second layout succeeded"), Layout(Asset, /*bApply=*/true).bSuccess))
		{
			return;
		}
		TestEqual(TEXT("running again repositions the rail rather than building a second one"),
			CountRerouteNodes(Asset), AfterFirst);
	});

	It("leaves edges straight when route_edges is off", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("First"), /*bIsEntry=*/true);
		const FString Second = AddState(Asset, TEXT("Second"));
		const FString Third = AddState(Asset, TEXT("Third"));
		if (!TestTrue(TEXT("states created"), !First.IsEmpty() && !Second.IsEmpty() && !Third.IsEmpty()))
		{
			return;
		}
		AddTransition(Asset, First, Second);
		AddTransition(Asset, Second, Third);
		AddTransition(Asset, Third, First);
		if (!CanOpenAssetEditor())
		{
			return;
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), Asset);
		Args->SetBoolField(TEXT("apply"), true);
		Args->SetBoolField(TEXT("route_edges"), false);
		if (!TestTrue(TEXT("layout succeeded"), Run(TEXT("ld.layout_states"), Args).bSuccess))
		{
			return;
		}
		TestEqual(TEXT("no reroute was created"), CountRerouteNodes(Asset), 0);
	});

	It("honors an explicit origin over the entry node anchor", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("First"), /*bIsEntry=*/true);
		if (!TestTrue(TEXT("asset and state created"), !Asset.IsEmpty() && !First.IsEmpty()))
		{
			return;
		}
		if (!CanOpenAssetEditor())
		{
			return;
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), Asset);
		Args->SetBoolField(TEXT("apply"), false);
		Args->SetNumberField(TEXT("start_x"), 640.0);
		Args->SetNumberField(TEXT("start_y"), 0.0);
		const FSMAssistOperationResult Result = Run(TEXT("ld.layout_states"), Args);
		if (!TestTrue(FString::Printf(TEXT("layout_states succeeded (%s)"), *Result.ErrorMessage), Result.bSuccess))
		{
			return;
		}

		FVector2f StatePosition = FVector2f::ZeroVector;
		if (!TestTrue(TEXT("state position read"), ProposedPosition(Result, First, StatePosition)))
		{
			return;
		}
		// Snapping rounds to the grid, so an exact match would be asserting the grid size instead.
		TestTrue(FString::Printf(TEXT("the explicit origin is used (X %.1f)"), StatePosition.X),
			FMath::Abs(StatePosition.X - 640.0f) <= 16.0f);
	});

	It("anchors a nested graph off its own entry node under scope=all", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString Outer = AddState(Asset, TEXT("Outer"), /*bIsEntry=*/true);
		const FString Inner = AddState(Asset, TEXT("Collapsed"));
		if (!TestTrue(TEXT("states created"), !Outer.IsEmpty() && !Inner.IsEmpty()))
		{
			return;
		}
		AddTransition(Asset, Outer, Inner);

		// Collapsing produces a real nested graph with its own entry node, so the nested layout has its
		// own anchor to clear.
		const TSharedRef<FJsonObject> CollapseArgs = MakeShared<FJsonObject>();
		CollapseArgs->SetStringField(TEXT("asset_path"), Asset);
		TArray<TSharedPtr<FJsonValue>> Guids;
		Guids.Add(MakeShared<FJsonValueString>(Inner));
		CollapseArgs->SetArrayField(TEXT("node_guids"), Guids);
		const FSMAssistOperationResult Collapse = Run(TEXT("ld.collapse_to_state_machine"), CollapseArgs);
		const FString Container = Str(Collapse, TEXT("state_guid"));
		if (!TestTrue(FString::Printf(TEXT("collapse succeeded (%s)"), *Collapse.ErrorMessage), !Container.IsEmpty()))
		{
			return;
		}

		const TSharedRef<FJsonObject> NestedStateArgs = MakeShared<FJsonObject>();
		NestedStateArgs->SetStringField(TEXT("asset_path"), Asset);
		NestedStateArgs->SetStringField(TEXT("state_name"), TEXT("InnerFirst"));
		NestedStateArgs->SetStringField(TEXT("parent_state_guid"), Container);
		NestedStateArgs->SetNumberField(TEXT("position_x"), 0.0);
		NestedStateArgs->SetNumberField(TEXT("position_y"), 0.0);
		NestedStateArgs->SetBoolField(TEXT("is_entry"), true);
		if (!TestTrue(TEXT("nested state created"), Run(TEXT("ld.add_state"), NestedStateArgs).bSuccess))
		{
			return;
		}
		if (!CanOpenAssetEditor())
		{
			return;
		}

		const TSharedRef<FJsonObject> LayoutArgs = MakeShared<FJsonObject>();
		LayoutArgs->SetStringField(TEXT("asset_path"), Asset);
		LayoutArgs->SetBoolField(TEXT("apply"), true);
		LayoutArgs->SetStringField(TEXT("scope"), TEXT("all"));
		const FSMAssistOperationResult Result = Run(TEXT("ld.layout_states"), LayoutArgs);
		if (!TestTrue(FString::Printf(TEXT("layout_states succeeded (%s)"), *Result.ErrorMessage), Result.bSuccess))
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		Result.Payload->TryGetArrayField(TEXT("graphs"), Graphs);
		if (!TestTrue(TEXT("the nested graph was laid out too"), Graphs && Graphs->Num() > 1))
		{
			return;
		}

		// The nested graph is the one that is not the root, which is always the first entry.
		const TSharedPtr<FJsonObject>* NestedEntry = nullptr;
		(*Graphs)[1]->TryGetObject(NestedEntry);
		if (!TestTrue(TEXT("nested graph entry read"), NestedEntry != nullptr))
		{
			return;
		}

		int32 NodeCount = 0;
		const float SmallestX = SmallestProposedX(*NestedEntry, NodeCount);
		FVector2f NestedEntryPosition = FVector2f::ZeroVector;
		if (!TestTrue(TEXT("the nested graph reports its nodes and its entry node position"),
			NodeCount > 0 && NestedEntryNodePosition(Asset, Container, NestedEntryPosition)))
		{
			return;
		}
		// The same clearance as the root graph test: the entry node measures under 100 wide.
		TestTrue(FString::Printf(TEXT("the nested flow starts clear of its entry node (smallest X %.1f, entry X %.1f)"),
			SmallestX, NestedEntryPosition.X),
			SmallestX >= NestedEntryPosition.X + 100.0f);
	});

	It("keeps a reroute the plan does not own and moves it with the states", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("First"), /*bIsEntry=*/true);
		const FString Second = AddState(Asset, TEXT("Second"));
		if (!TestTrue(TEXT("states created"), !First.IsEmpty() && !Second.IsEmpty()))
		{
			return;
		}

		// Adjacent layers, so the router plans no rail for this edge and every reroute on it is the
		// caller's own. It still has to follow the states rather than stay where the layout left it.
		const FString Transition = AddTransition(Asset, First, Second);
		const TSharedRef<FJsonObject> RerouteArgs = MakeShared<FJsonObject>();
		RerouteArgs->SetStringField(TEXT("asset_path"), Asset);
		RerouteArgs->SetStringField(TEXT("transition_guid"), Transition);
		RerouteArgs->SetNumberField(TEXT("position_x"), -4000.0);
		RerouteArgs->SetNumberField(TEXT("position_y"), -4000.0);
		if (!TestTrue(TEXT("reroute created"), Run(TEXT("ld.add_transition_reroute"), RerouteArgs).bSuccess))
		{
			return;
		}
		if (!CanOpenAssetEditor())
		{
			return;
		}

		if (!TestTrue(TEXT("layout succeeded"), Layout(Asset, /*bApply=*/true).bSuccess))
		{
			return;
		}

		TestEqual(TEXT("the reroute is kept, not removed"), CountRerouteNodes(Asset), 1);

		USMGraph* Graph = LoadRootGraph(Asset);
		if (!TestTrue(TEXT("root graph loaded"), Graph != nullptr))
		{
			return;
		}
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->IsA<USMGraphNode_RerouteNode>())
			{
				TestTrue(FString::Printf(TEXT("the reroute was pulled back to the flow (X %d, Y %d)"),
					Node->NodePosX, Node->NodePosY),
					Node->NodePosX > -1000 && Node->NodePosY > -1000);
			}
		}
	});

	It("reports how many placement passes ran", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString First = AddState(Asset, TEXT("First"), /*bIsEntry=*/true);
		if (!TestTrue(TEXT("state created"), !First.IsEmpty()))
		{
			return;
		}
		if (!CanOpenAssetEditor())
		{
			return;
		}

		const FSMAssistOperationResult Result = Layout(Asset, /*bApply=*/true);
		if (!TestTrue(FString::Printf(TEXT("layout_states succeeded (%s)"), *Result.ErrorMessage), Result.bSuccess))
		{
			return;
		}

		double Passes = 0.0;
		TestTrue(TEXT("passes is reported"), Result.Payload->TryGetNumberField(TEXT("passes"), Passes));
		TestEqual(TEXT("one graph with one state needs a single pass"), static_cast<int32>(Passes), 1);
	});

	It("errors when asset_path is missing", [this]()
	{
		TestFalse(TEXT("missing asset_path fails"), Run(TEXT("ld.layout_states"), MakeShared<FJsonObject>()).bSuccess);
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
