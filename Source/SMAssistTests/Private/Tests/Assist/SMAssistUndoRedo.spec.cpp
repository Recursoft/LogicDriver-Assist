// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationResult.h"

#include "Helpers/SMTestHelpers.h"

#include "Blueprints/SMBlueprint.h"
#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/Nodes/SMGraphNode_TransitionEdge.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectHash.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

// Behavioral undo/redo coverage for mutating ops: mutate -> undo -> assert restored -> redo ->
// assert reapplied. The hazard under test is a partial transaction (one side of a pin link captured
// without its counterpart), which corrupts the asset on Ctrl+Z. Node pointers survive undo; pin
// objects are reconstructed from the transaction record, so pins are re-resolved after every
// undo/redo instead of held across one.
BEGIN_DEFINE_SPEC(FAssistUndoRedoSpec, "LogicDriver.Assist.UndoRedo",
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

	FString Str(const FSMAssistOperationResult& InResult, const TCHAR* InField)
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
		const FSMAssistOperationResult R = Run(TEXT("sm.create_blueprint"),
			Obj({ { TEXT("name"), FGuid::NewGuid().ToString() }, { TEXT("path"), FAssetHandler::DefaultGamePath() } }));
		return Str(R, TEXT("asset_path"));
	}

	FString AddState(const FString& InAsset, const FString& InName, bool bIsEntry = false)
	{
		const TSharedRef<FJsonObject> Args = Obj({ { TEXT("asset_path"), InAsset }, { TEXT("state_name"), InName } });
		if (bIsEntry)
		{
			Args->SetBoolField(TEXT("is_entry"), true);
		}
		return Str(Run(TEXT("sm.add_state"), Args), TEXT("state_guid"));
	}

	FString AddTransition(const FString& InAsset, const FString& InFrom, const FString& InTo)
	{
		return Str(Run(TEXT("sm.add_transition"),
			Obj({ { TEXT("asset_path"), InAsset }, { TEXT("from_state_guid"), InFrom }, { TEXT("to_state_guid"), InTo } })), TEXT("transition_guid"));
	}

	USMBlueprint* LoadBP(const FString& InAssetPath)
	{
		return Cast<USMBlueprint>(FSoftObjectPath(InAssetPath).TryLoad());
	}

	// Finds a live graph node by guid anywhere in the blueprint's package. Membership in the owner
	// graph's Nodes array filters out trashed copies left behind by remove/undo.
	USMGraphNode_Base* NodeByGuid(const FString& InAssetPath, const FString& InGuidStr)
	{
		USMBlueprint* Blueprint = LoadBP(InAssetPath);
		if (!Blueprint)
		{
			return nullptr;
		}
		// Two-arg form: nested objects are included by default on both the UE 5.7 bool overload and
		// the UE 5.8 EGetObjectsFlags overload, without touching either version's deprecated shape.
		TArray<UObject*> Objects;
		GetObjectsWithPackage(Blueprint->GetPackage(), Objects);
		for (UObject* Object : Objects)
		{
			USMGraphNode_Base* Node = Cast<USMGraphNode_Base>(Object);
			if (Node && IsValid(Node) && Node->NodeGuid.ToString() == InGuidStr)
			{
				const UEdGraph* OwnerGraph = Cast<UEdGraph>(Node->GetOuter());
				if (OwnerGraph && OwnerGraph->Nodes.Contains(Node))
				{
					return Node;
				}
			}
		}
		return nullptr;
	}

	USMGraphNode_StateNodeBase* StateByGuid(const FString& InAssetPath, const FString& InGuidStr)
	{
		return Cast<USMGraphNode_StateNodeBase>(NodeByGuid(InAssetPath, InGuidStr));
	}

	static int32 CountTransitionEdges(const UEdGraph* InGraph)
	{
		int32 Count = 0;
		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			if (Node && Node->IsA<USMGraphNode_TransitionEdge>())
			{
				++Count;
			}
		}
		return Count;
	}

	static UEdGraphNode* FindGraphNodeByGuid(const UEdGraph* InGraph, const FString& InGuidStr)
	{
		for (UEdGraphNode* Node : InGraph->Nodes)
		{
			if (Node && Node->NodeGuid.ToString() == InGuidStr)
			{
				return Node;
			}
		}
		return nullptr;
	}

	static UEdGraphPin* FindPinNamed(UEdGraphNode* InNode, const TCHAR* InName)
	{
		for (UEdGraphPin* Pin : InNode->Pins)
		{
			if (Pin && Pin->PinName.ToString() == InName)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	static UEdGraphPin* FirstInputPin(UEdGraphNode* InNode)
	{
		for (UEdGraphPin* Pin : InNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input)
			{
				return Pin;
			}
		}
		return nullptr;
	}

END_DEFINE_SPEC(FAssistUndoRedoSpec)

void FAssistUndoRedoSpec::Define()
{
	It("add_state captures entry wiring so undo leaves no dangling entry link", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString AGuid = AddState(Asset, TEXT("A"), /*bIsEntry*/true);
		USMGraphNode_StateNodeBase* StateA = StateByGuid(Asset, AGuid);
		if (!TestNotNull(TEXT("entry state created"), StateA))
		{
			return;
		}
		if (!TestEqual(TEXT("entry pin wired to A"), StateA->GetInputPin()->LinkedTo.Num(), 1))
		{
			return;
		}
		USMGraphNode_Base* EntryNode = Cast<USMGraphNode_Base>(StateA->GetInputPin()->LinkedTo[0]->GetOwningNode());
		if (!TestNotNull(TEXT("entry node resolved"), EntryNode))
		{
			return;
		}

		GEditor->UndoTransaction();

		TestNull(TEXT("undo removed the state"), StateByGuid(Asset, AGuid));
		TestEqual(TEXT("entry pin fully unlinked after undo"), EntryNode->GetOutputPin()->LinkedTo.Num(), 0);

		GEditor->RedoTransaction();

		StateA = StateByGuid(Asset, AGuid);
		if (TestNotNull(TEXT("redo restored the state"), StateA))
		{
			TestEqual(TEXT("entry rewired to A on redo"), StateA->GetInputPin()->LinkedTo.Num(), 1);
		}
	});

	It("add_transition records both endpoints so undo restores a symmetric unlinked state", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString AGuid = AddState(Asset, TEXT("A"), /*bIsEntry*/true);
		const FString BGuid = AddState(Asset, TEXT("B"));
		USMGraphNode_StateNodeBase* StateA = StateByGuid(Asset, AGuid);
		USMGraphNode_StateNodeBase* StateB = StateByGuid(Asset, BGuid);
		if (!TestNotNull(TEXT("A resolved"), StateA) || !TestNotNull(TEXT("B resolved"), StateB))
		{
			return;
		}
		UEdGraph* Graph = StateA->GetGraph();
		TestEqual(TEXT("A starts unlinked"), StateA->GetOutputPin()->LinkedTo.Num(), 0);

		const FString TransGuid = AddTransition(Asset, AGuid, BGuid);
		if (!TestFalse(TEXT("transition created"), TransGuid.IsEmpty()))
		{
			return;
		}
		TestEqual(TEXT("A output linked"), StateA->GetOutputPin()->LinkedTo.Num(), 1);
		TestEqual(TEXT("B input linked"), StateB->GetInputPin()->LinkedTo.Num(), 1);
		TestEqual(TEXT("one transition edge in graph"), CountTransitionEdges(Graph), 1);

		GEditor->UndoTransaction();

		TestEqual(TEXT("A output unlinked after undo"), StateA->GetOutputPin()->LinkedTo.Num(), 0);
		TestEqual(TEXT("B input unlinked after undo"), StateB->GetInputPin()->LinkedTo.Num(), 0);
		TestEqual(TEXT("transition edge gone after undo"), CountTransitionEdges(Graph), 0);

		GEditor->RedoTransaction();

		TestEqual(TEXT("A output relinked after redo"), StateA->GetOutputPin()->LinkedTo.Num(), 1);
		TestEqual(TEXT("B input relinked after redo"), StateB->GetInputPin()->LinkedTo.Num(), 1);
		TestEqual(TEXT("transition edge restored after redo"), CountTransitionEdges(Graph), 1);
	});

	It("set_initial_state records both the broken and the created entry link", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString AGuid = AddState(Asset, TEXT("A"), /*bIsEntry*/true);
		const FString BGuid = AddState(Asset, TEXT("B"));
		USMGraphNode_StateNodeBase* StateA = StateByGuid(Asset, AGuid);
		USMGraphNode_StateNodeBase* StateB = StateByGuid(Asset, BGuid);
		if (!TestNotNull(TEXT("A resolved"), StateA) || !TestNotNull(TEXT("B resolved"), StateB)
			|| !TestEqual(TEXT("entry wired to A"), StateA->GetInputPin()->LinkedTo.Num(), 1))
		{
			return;
		}
		USMGraphNode_Base* EntryNode = Cast<USMGraphNode_Base>(StateA->GetInputPin()->LinkedTo[0]->GetOwningNode());
		if (!TestNotNull(TEXT("entry node resolved"), EntryNode))
		{
			return;
		}

		const FSMAssistOperationResult Result = Run(TEXT("sm.set_initial_state"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("state_guid"), BGuid } }));
		if (!TestTrue(TEXT("set_initial_state succeeded"), Result.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("entry now wired to B"), StateB->GetInputPin()->LinkedTo.Num(), 1);
		TestEqual(TEXT("A no longer entry-wired"), StateA->GetInputPin()->LinkedTo.Num(), 0);

		GEditor->UndoTransaction();

		TestEqual(TEXT("entry rewired to A after undo"), StateA->GetInputPin()->LinkedTo.Num(), 1);
		TestEqual(TEXT("B unlinked after undo"), StateB->GetInputPin()->LinkedTo.Num(), 0);
		TestEqual(TEXT("entry pin has exactly one link after undo"), EntryNode->GetOutputPin()->LinkedTo.Num(), 1);

		GEditor->RedoTransaction();

		TestEqual(TEXT("entry rewired to B after redo"), StateB->GetInputPin()->LinkedTo.Num(), 1);
		TestEqual(TEXT("A unlinked after redo"), StateA->GetInputPin()->LinkedTo.Num(), 0);
	});

	It("remove_node undo restores the node and its transition links", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString AGuid = AddState(Asset, TEXT("A"), /*bIsEntry*/true);
		const FString BGuid = AddState(Asset, TEXT("B"));
		const FString TransGuid = AddTransition(Asset, AGuid, BGuid);
		USMGraphNode_StateNodeBase* StateA = StateByGuid(Asset, AGuid);
		if (!TestNotNull(TEXT("A resolved"), StateA) || !TestFalse(TEXT("transition created"), TransGuid.IsEmpty()))
		{
			return;
		}
		UEdGraph* Graph = StateA->GetGraph();
		TestEqual(TEXT("one transition edge before removal"), CountTransitionEdges(Graph), 1);

		const FSMAssistOperationResult Result = Run(TEXT("sm.remove_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), BGuid } }));
		if (!TestTrue(TEXT("remove_node succeeded"), Result.bSuccess))
		{
			return;
		}
		TestNull(TEXT("B removed"), StateByGuid(Asset, BGuid));
		TestEqual(TEXT("transition edge removed with the state"), CountTransitionEdges(Graph), 0);
		TestEqual(TEXT("A output unlinked"), StateA->GetOutputPin()->LinkedTo.Num(), 0);

		GEditor->UndoTransaction();

		USMGraphNode_StateNodeBase* StateB = StateByGuid(Asset, BGuid);
		TestNotNull(TEXT("B restored after undo"), StateB);
		TestEqual(TEXT("transition edge restored after undo"), CountTransitionEdges(Graph), 1);
		TestEqual(TEXT("A output relinked after undo"), StateA->GetOutputPin()->LinkedTo.Num(), 1);
		if (StateB)
		{
			TestEqual(TEXT("B input relinked after undo"), StateB->GetInputPin()->LinkedTo.Num(), 1);
		}

		GEditor->RedoTransaction();

		TestNull(TEXT("B removed again after redo"), StateByGuid(Asset, BGuid));
		TestEqual(TEXT("transition edge gone after redo"), CountTransitionEdges(Graph), 0);
	});

	It("add_local_graph_node undo removes the node and redo restores it", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString AGuid = AddState(Asset, TEXT("A"));
		const FString BGuid = AddState(Asset, TEXT("B"));
		const FString TransGuid = AddTransition(Asset, AGuid, BGuid);
		USMGraphNode_Base* TransNode = NodeByGuid(Asset, TransGuid);
		if (!TestNotNull(TEXT("transition node resolved"), TransNode))
		{
			return;
		}
		UEdGraph* BoundGraph = TransNode->GetBoundGraph();
		if (!TestNotNull(TEXT("bound graph resolved"), BoundGraph))
		{
			return;
		}
		const int32 CountBefore = BoundGraph->Nodes.Num();

		const FSMAssistOperationResult Result = Run(TEXT("sm.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), TransGuid },
				  { TEXT("node_class"), TEXT("K2Node_ExecutionSequence") } }));
		if (!TestTrue(TEXT("node spawned"), Result.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("graph gained one node"), BoundGraph->Nodes.Num(), CountBefore + 1);

		GEditor->UndoTransaction();
		TestEqual(TEXT("undo removed the node"), BoundGraph->Nodes.Num(), CountBefore);

		GEditor->RedoTransaction();
		TestEqual(TEXT("redo restored the node"), BoundGraph->Nodes.Num(), CountBefore + 1);
	});

	It("connect and disconnect local graph pins are undoable without one-sided links", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString AGuid = AddState(Asset, TEXT("A"));
		const FString BGuid = AddState(Asset, TEXT("B"));
		const FString TransGuid = AddTransition(Asset, AGuid, BGuid);
		USMGraphNode_Base* TransNode = NodeByGuid(Asset, TransGuid);
		UEdGraph* BoundGraph = TransNode ? TransNode->GetBoundGraph() : nullptr;
		if (!TestNotNull(TEXT("bound graph resolved"), BoundGraph))
		{
			return;
		}

		const FSMAssistOperationResult Spawn = Run(TEXT("sm.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), TransGuid },
				  { TEXT("node_class"), TEXT("call_function") }, { TEXT("function_name"), TEXT("Greater_DoubleDouble") } }));
		if (!TestTrue(TEXT("call node spawned"), Spawn.bSuccess))
		{
			return;
		}
		const FString CallGuid = Str(Spawn, TEXT("node_guid"));
		UEdGraphNode* CallNode = FindGraphNodeByGuid(BoundGraph, CallGuid);

		// Matched by class name; the result-node class is not exported from SMSystemEditor.
		UEdGraphNode* ResultNode = nullptr;
		for (UEdGraphNode* Node : BoundGraph->Nodes)
		{
			if (Node && Node->GetClass()->GetName() == TEXT("SMGraphK2Node_TransitionResultNode"))
			{
				ResultNode = Node;
				break;
			}
		}
		if (!TestNotNull(TEXT("call node resolved"), CallNode) || !TestNotNull(TEXT("result node resolved"), ResultNode))
		{
			return;
		}
		UEdGraphPin* ResultInput = FirstInputPin(ResultNode);
		if (!TestNotNull(TEXT("result input pin resolved"), ResultInput))
		{
			return;
		}
		const FString ResultPinName = ResultInput->PinName.ToString();

		const FSMAssistOperationResult Connect = Run(TEXT("sm.connect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), TransGuid },
				  { TEXT("from_node_id"), CallGuid }, { TEXT("from_pin"), TEXT("ReturnValue") },
				  { TEXT("to_node_id"), ResultNode->NodeGuid.ToString() }, { TEXT("to_pin"), ResultPinName } }));
		if (!TestTrue(TEXT("connect succeeded"), Connect.bSuccess))
		{
			return;
		}

		auto LinkCount = [&]() -> TPair<int32, int32>
		{
			UEdGraphPin* From = FindPinNamed(CallNode, TEXT("ReturnValue"));
			UEdGraphPin* To = FirstInputPin(ResultNode);
			return TPair<int32, int32>(From ? From->LinkedTo.Num() : -1, To ? To->LinkedTo.Num() : -1);
		};

		TestEqual(TEXT("source linked after connect"), LinkCount().Key, 1);
		TestEqual(TEXT("dest linked after connect"), LinkCount().Value, 1);

		GEditor->UndoTransaction();
		TestEqual(TEXT("source unlinked after undo"), LinkCount().Key, 0);
		TestEqual(TEXT("dest unlinked after undo"), LinkCount().Value, 0);

		GEditor->RedoTransaction();
		TestEqual(TEXT("source relinked after redo"), LinkCount().Key, 1);
		TestEqual(TEXT("dest relinked after redo"), LinkCount().Value, 1);

		const FSMAssistOperationResult Disconnect = Run(TEXT("sm.disconnect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), TransGuid },
				  { TEXT("from_node_id"), CallGuid }, { TEXT("from_pin"), TEXT("ReturnValue") },
				  { TEXT("to_node_id"), ResultNode->NodeGuid.ToString() }, { TEXT("to_pin"), ResultPinName } }));
		if (!TestTrue(TEXT("disconnect succeeded"), Disconnect.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("source unlinked after disconnect"), LinkCount().Key, 0);
		TestEqual(TEXT("dest unlinked after disconnect"), LinkCount().Value, 0);

		GEditor->UndoTransaction();
		TestEqual(TEXT("source relinked after disconnect undo"), LinkCount().Key, 1);
		TestEqual(TEXT("dest relinked after disconnect undo"), LinkCount().Value, 1);
	});

	It("set_local_graph_pin_default is undoable", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString AGuid = AddState(Asset, TEXT("A"));
		const FString BGuid = AddState(Asset, TEXT("B"));
		const FString TransGuid = AddTransition(Asset, AGuid, BGuid);
		USMGraphNode_Base* TransNode = NodeByGuid(Asset, TransGuid);
		UEdGraph* BoundGraph = TransNode ? TransNode->GetBoundGraph() : nullptr;
		if (!TestNotNull(TEXT("bound graph resolved"), BoundGraph))
		{
			return;
		}

		const FSMAssistOperationResult Spawn = Run(TEXT("sm.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), TransGuid },
				  { TEXT("node_class"), TEXT("call_function") }, { TEXT("function_name"), TEXT("Greater_DoubleDouble") } }));
		if (!TestTrue(TEXT("call node spawned"), Spawn.bSuccess))
		{
			return;
		}
		const FString CallGuid = Str(Spawn, TEXT("node_guid"));
		UEdGraphNode* CallNode = FindGraphNodeByGuid(BoundGraph, CallGuid);
		if (!TestNotNull(TEXT("call node resolved"), CallNode))
		{
			return;
		}

		UEdGraphPin* APin = FindPinNamed(CallNode, TEXT("A"));
		if (!TestNotNull(TEXT("input pin A resolved"), APin))
		{
			return;
		}
		const FString OriginalDefault = APin->DefaultValue;

		const FSMAssistOperationResult SetDefault = Run(TEXT("sm.set_local_graph_pin_default"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), TransGuid },
				  { TEXT("node_id"), CallGuid }, { TEXT("pin"), TEXT("A") }, { TEXT("value"), TEXT("5.0") } }));
		if (!TestTrue(TEXT("pin default set"), SetDefault.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("default applied"), FindPinNamed(CallNode, TEXT("A"))->DefaultValue, FString(TEXT("5.0")));

		GEditor->UndoTransaction();
		TestEqual(TEXT("default restored after undo"), FindPinNamed(CallNode, TEXT("A"))->DefaultValue, OriginalDefault);

		GEditor->RedoTransaction();
		TestEqual(TEXT("default reapplied after redo"), FindPinNamed(CallNode, TEXT("A"))->DefaultValue, FString(TEXT("5.0")));
	});

	It("remove_local_graph_node and set_local_graph_node are undoable", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString AGuid = AddState(Asset, TEXT("A"));
		const FString BGuid = AddState(Asset, TEXT("B"));
		const FString TransGuid = AddTransition(Asset, AGuid, BGuid);
		USMGraphNode_Base* TransNode = NodeByGuid(Asset, TransGuid);
		UEdGraph* BoundGraph = TransNode ? TransNode->GetBoundGraph() : nullptr;
		if (!TestNotNull(TEXT("bound graph resolved"), BoundGraph))
		{
			return;
		}

		const FSMAssistOperationResult Spawn = Run(TEXT("sm.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), TransGuid },
				  { TEXT("node_class"), TEXT("K2Node_ExecutionSequence") } }));
		if (!TestTrue(TEXT("node spawned"), Spawn.bSuccess))
		{
			return;
		}
		const FString SeqGuid = Str(Spawn, TEXT("node_guid"));
		UEdGraphNode* SeqNode = FindGraphNodeByGuid(BoundGraph, SeqGuid);
		if (!TestNotNull(TEXT("sequence node resolved"), SeqNode))
		{
			return;
		}

		const FSMAssistOperationResult SetComment = Run(TEXT("sm.set_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), TransGuid },
				  { TEXT("node_id"), SeqGuid }, { TEXT("comment"), TEXT("gate note") } }));
		TestTrue(TEXT("comment set"), SetComment.bSuccess);
		TestEqual(TEXT("comment applied"), SeqNode->NodeComment, FString(TEXT("gate note")));

		GEditor->UndoTransaction();
		TestEqual(TEXT("comment cleared after undo"), SeqNode->NodeComment, FString());

		const int32 CountBefore = BoundGraph->Nodes.Num();
		const FSMAssistOperationResult Remove = Run(TEXT("sm.remove_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), TransGuid }, { TEXT("node_id"), SeqGuid } }));
		TestTrue(TEXT("node removed"), Remove.bSuccess);
		TestEqual(TEXT("graph lost one node"), BoundGraph->Nodes.Num(), CountBefore - 1);

		GEditor->UndoTransaction();
		TestEqual(TEXT("node restored after undo"), BoundGraph->Nodes.Num(), CountBefore);
		TestNotNull(TEXT("sequence node resolvable after undo"), FindGraphNodeByGuid(BoundGraph, SeqGuid));
	});

	It("set_node_property on a graph-node field is undoable", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString AGuid = AddState(Asset, TEXT("A"));
		USMGraphNode_StateNodeBase* StateA = StateByGuid(Asset, AGuid);
		if (!TestNotNull(TEXT("A resolved"), StateA))
		{
			return;
		}

		const FSMAssistOperationResult Result = Run(TEXT("sm.set_node_property"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), AGuid },
				  { TEXT("property_name"), TEXT("NodeComment") }, { TEXT("value"), TEXT("authored note") } }));
		if (!TestTrue(TEXT("property written"), Result.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("comment applied"), StateA->NodeComment, FString(TEXT("authored note")));

		GEditor->UndoTransaction();
		TestEqual(TEXT("comment restored after undo"), StateA->NodeComment, FString());

		GEditor->RedoTransaction();
		TestEqual(TEXT("comment reapplied after redo"), StateA->NodeComment, FString(TEXT("authored note")));
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
