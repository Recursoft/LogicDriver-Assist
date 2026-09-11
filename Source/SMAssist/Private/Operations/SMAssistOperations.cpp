// Copyright Recursoft LLC. All Rights Reserved.

#include "Operations/SMAssistOperations.h"

#include "Operations/SMAssistGraphView.h"
#include "Operations/SMAssistLocalGraphNodeDiscovery.h"
#include "Operations/SMAssistOpKeys.h"
#include "Layout/SMAssistLayout.h"
#include "SMAssistLog.h"
#include "Utilities/SMAssistUtils.h"

#include "ISMAssetManager.h"
#include "ISMAssetToolsModule.h"
#include "ISMGraphGeneration.h"
#include "Blueprints/SMBlueprint.h"
#include "Blueprints/SMBlueprintGeneratedClass.h"
#include "ExposedFunctions/SMExposedFunctions.h"
#include "Graph/Nodes/SMGraphNode_AnyStateNode.h"
#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/Nodes/SMGraphNode_ConduitNode.h"
#include "Graph/Nodes/SMGraphNode_LinkStateNode.h"
#include "Graph/Nodes/SMGraphNode_RerouteNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineEntryNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/Nodes/SMGraphNode_TransitionEdge.h"
#include "Graph/Nodes/SMGraphK2Node_Base.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/RootNodes/SMGraphK2Node_ConduitResultNode.h"
#include "Graph/Nodes/RootNodes/SMGraphK2Node_TransitionResultNode.h"
#include "Graph/SMConduitGraph.h"
#include "Graph/SMGraph.h"
#include "Graph/SMPropertyGraph.h"
#include "Graph/SMStateGraph.h"
#include "Graph/SMTransitionGraph.h"
#include "NodeStack/NodeStackContainer.h"
#include "Properties/SMEditorPropertyUtils.h"
#include "Properties/SMGraphProperty_Base.h"
#include "SMConduitInstance.h"
#include "SMInstance.h"
#include "SMNodeInstance.h"
#include "SMStateInstance.h"
#include "SMStateMachineComponent.h"
#include "SMStateMachineInstance.h"
#include "SMTransitionInstance.h"

#include "Algo/Reverse.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BlueprintEditor.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Variable.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Components/ActorComponent.h"
#include "EdGraphNode_Comment.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "Engine/SimpleConstructionScript.h"
#include "ScopedTransaction.h"
#include "GraphEditor.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "SNodePanel.h"
#include "UObject/UnrealType.h"

namespace LD::Assist::Private
{
	static ISMGraphGeneration* GetGraphGeneration(FString& OutError)
	{
		ISMAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<ISMAssetToolsModule>(
			LOGICDRIVER_ASSET_TOOLS_MODULE_NAME);

		const TSharedPtr<ISMGraphGeneration> GraphGen = AssetTools.GetGraphGenerationInterface();
		if (!GraphGen.IsValid())
		{
			OutError = TEXT("Graph generation interface unavailable.");
			return nullptr;
		}
		return GraphGen.Get();
	}

	// One state machine graph within an asset, paired with the container state node that owns it. The
	// root graph has no container, so its ParentStateGuid is invalid and its GraphPath is its own graph
	// name; nested entries append each graph name below it, reading "RootGraph/NestedGraph".
	struct FStateMachineGraphEntry
	{
		USMGraph* Graph = nullptr;
		FGuid ParentStateGuid;
		FString GraphPath;
	};

	// Gather every USMGraph reachable through nested state machine state nodes, root first and nested
	// graphs in encounter order. LD graph nodes do not override UEdGraphNode::GetSubGraphs(), so a
	// container exposes its nested graph only through GetBoundGraph(); the same-package check keeps the
	// walk inside this asset so a state machine reference is never followed into another blueprint.
	// Mirrors the descent in Utils::FindNodeByGuid so enumeration and guid addressing agree on scope.
	static void CollectStateMachineGraphs(USMGraph* InRoot, TArray<FStateMachineGraphEntry>& OutGraphs)
	{
		if (!InRoot)
		{
			return;
		}

		const UPackage* OwningPackage = InRoot->GetOutermost();

		TSet<const USMGraph*> Visited;
		Visited.Add(InRoot);

		FStateMachineGraphEntry RootEntry;
		RootEntry.Graph = InRoot;
		RootEntry.GraphPath = InRoot->GetName();
		OutGraphs.Add(MoveTemp(RootEntry));

		// Index-based walk over OutGraphs: appending while iterating yields breadth-first order without
		// a second worklist, and every appended entry already carries the path its children extend.
		for (int32 EntryIdx = 0; EntryIdx < OutGraphs.Num(); ++EntryIdx)
		{
			USMGraph* Graph = OutGraphs[EntryIdx].Graph;
			const FString ParentPath = OutGraphs[EntryIdx].GraphPath;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				USMGraphNode_StateMachineStateNode* ContainerNode = Cast<USMGraphNode_StateMachineStateNode>(Node);
				if (!ContainerNode)
				{
					continue;
				}
				USMGraph* NestedGraph = Cast<USMGraph>(ContainerNode->GetBoundGraph());
				if (!NestedGraph
					|| NestedGraph->GetOutermost() != OwningPackage
					|| Visited.Contains(NestedGraph))
				{
					continue;
				}
				Visited.Add(NestedGraph);

				FStateMachineGraphEntry Entry;
				Entry.Graph = NestedGraph;
				Entry.ParentStateGuid = ContainerNode->NodeGuid;
				Entry.GraphPath = FString::Printf(TEXT("%s/%s"), *ParentPath, *NestedGraph->GetName());
				OutGraphs.Add(MoveTemp(Entry));
			}
		}
	}

	// Path label for one graph within its asset, in the same form ld.layout_states reports, so a caller
	// can join graph_path across ops. Empty when the graph is not reachable from the root.
	static FString FindGraphPathLabel(USMGraph* InRoot, const USMGraph* InTarget)
	{
		if (!InRoot || !InTarget)
		{
			return FString();
		}
		TArray<FStateMachineGraphEntry> Graphs;
		CollectStateMachineGraphs(InRoot, Graphs);
		for (const FStateMachineGraphEntry& Entry : Graphs)
		{
			if (Entry.Graph == InTarget)
			{
				return Entry.GraphPath;
			}
		}
		return FString();
	}

	// Parse the shared 'scope' arg used by the ops that can walk nested state machines. Absent or
	// 'root' keeps the op on the root graph; 'all' recurses.
	static bool TryParseGraphScope(const TSharedRef<FJsonObject>& InArgs, bool& bOutScopeAll, FString& OutError)
	{
		bOutScopeAll = false;

		FString ScopeStr;
		if (!InArgs->TryGetStringField(Args::Scope, ScopeStr) || ScopeStr.IsEmpty())
		{
			return true;
		}
		if (ScopeStr.Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			bOutScopeAll = true;
			return true;
		}
		if (ScopeStr.Equals(TEXT("root"), ESearchCase::IgnoreCase))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Unknown scope '%s'. Use 'root' or 'all'."), *ScopeStr);
		return false;
	}

	// 'node_guid' is accepted alongside 'state_guid' because that is the field name ld.get_graph_view
	// uses for every node, so a caller reading a graph does not have to rename it. OutArgName reports
	// which of the two was read, so a later message about the value names the arg the caller sent.
	static bool TryGetStateGuidArg(const TSharedRef<FJsonObject>& InArgs, FString& OutGuidStr,
		const TCHAR*& OutArgName, FString& OutError)
	{
		if (InArgs->TryGetStringField(Args::StateGuid, OutGuidStr) && !OutGuidStr.IsEmpty())
		{
			OutArgName = Args::StateGuid;
			return true;
		}
		if (InArgs->TryGetStringField(Args::NodeGuid, OutGuidStr) && !OutGuidStr.IsEmpty())
		{
			OutArgName = Args::NodeGuid;
			return true;
		}
		OutArgName = Args::StateGuid;
		OutError = TEXT("Missing required arg 'state_guid' (or 'node_guid', which is accepted for it).");
		return false;
	}

	// Resolve the optional 'parent_state_guid' arg to the nested state machine graph an op should act
	// on. An absent guid leaves OutGraph null, which every caller treats as the blueprint root graph.
	static bool ResolveTargetStateMachineGraph(const TSharedRef<FJsonObject>& InArgs, USMBlueprint* InBlueprint,
		USMGraph*& OutGraph, FString& OutError)
	{
		OutGraph = nullptr;

		FString ParentGuidStr;
		if (!InArgs->TryGetStringField(Args::ParentStateGuid, ParentGuidStr) || ParentGuidStr.IsEmpty())
		{
			return true;
		}

		FGuid ParentGuid;
		if (!FGuid::Parse(ParentGuidStr, ParentGuid))
		{
			OutError = FString::Printf(TEXT("Invalid 'parent_state_guid' '%s'."), *ParentGuidStr);
			return false;
		}

		USMGraphNode_Base* ParentNode = LD::Assist::Utils::FindNodeByGuid(InBlueprint, ParentGuid);
		if (!ParentNode)
		{
			OutError = FString::Printf(TEXT("Could not find node with guid '%s' for 'parent_state_guid'."), *ParentGuidStr);
			return false;
		}

		USMGraphNode_StateMachineStateNode* ContainerNode = Cast<USMGraphNode_StateMachineStateNode>(ParentNode);
		if (!ContainerNode)
		{
			OutError = FString::Printf(
				TEXT("Node '%s' is not a nested state machine (kind 'state_machine_state'), so it owns no state machine graph to target. 'parent_state_guid' must name a state machine state node."),
				*ParentGuidStr);
			return false;
		}

		// A reference node delegates its states to another blueprint; its bound graph is either absent
		// or an intermediate K2 graph, neither of which accepts state nodes.
		USMGraph* NestedGraph = Cast<USMGraph>(ContainerNode->GetBoundGraph());
		if (!NestedGraph)
		{
			OutError = FString::Printf(
				TEXT("Nested state machine '%s' owns no state machine graph. State machine references delegate their states to the referenced blueprint; edit that asset instead."),
				*ContainerNode->GetStateName());
			return false;
		}

		if (NestedGraph->GetOutermost() != InBlueprint->GetOutermost())
		{
			OutError = FString::Printf(
				TEXT("Nested state machine '%s' resolves to a graph in another asset. Edit that asset directly."),
				*ContainerNode->GetStateName());
			return false;
		}

		OutGraph = NestedGraph;
		return true;
	}

	// FindStateNodeByGuid resolves Any State (output pin only), Link State (input pin only), and nodes in
	// nested graphs, but core transition/entry APIs assume two-pin states in a single graph and assert or
	// null-deref otherwise. Reject unusable endpoints with a specific message before reaching core.
	static bool ValidateTransitionEndpoints(const USMGraphNode_StateNodeBase* InFromState,
		const USMGraphNode_StateNodeBase* InToState, FString& OutError)
	{
		// A reroute node carries both pins and would pass every check below. Connecting from one runs
		// BreakAllOutgoingReroutedConnections, which severs the downstream chain and destroys each severed
		// transition's condition graph. ld.layout_states creates reroutes on every back-edge and
		// ld.get_graph_view returns their guids beside state guids, so a caller reaches this by accident.
		if (InFromState->IsA<USMGraphNode_RerouteNode>())
		{
			OutError = FString::Printf(
				TEXT("'from' state '%s' is a transition reroute node and cannot be a transition endpoint. A reroute is a waypoint on a transition that already exists; add one with ld.add_transition_reroute."),
				*InFromState->GetStateName());
			return false;
		}
		if (InToState->IsA<USMGraphNode_RerouteNode>())
		{
			OutError = FString::Printf(
				TEXT("'to' state '%s' is a transition reroute node and cannot be a transition endpoint. A reroute is a waypoint on a transition that already exists; add one with ld.add_transition_reroute."),
				*InToState->GetStateName());
			return false;
		}
		if (InFromState->IsA<USMGraphNode_LinkStateNode>())
		{
			OutError = FString::Printf(
				TEXT("'from' state '%s' is a Link State and cannot have outgoing transitions."),
				*InFromState->GetStateName());
			return false;
		}
		if (!InFromState->GetOutputPin())
		{
			OutError = FString::Printf(
				TEXT("'from' state '%s' has no output pin and cannot have outgoing transitions."),
				*InFromState->GetStateName());
			return false;
		}
		if (InToState->IsA<USMGraphNode_AnyStateNode>())
		{
			OutError = FString::Printf(
				TEXT("'to' state '%s' is an Any State and cannot receive transitions."),
				*InToState->GetStateName());
			return false;
		}
		if (!InToState->GetInputPin())
		{
			OutError = FString::Printf(
				TEXT("'to' state '%s' has no input pin and cannot receive transitions."),
				*InToState->GetStateName());
			return false;
		}
		if (InFromState->GetGraph() != InToState->GetGraph())
		{
			OutError = FString::Printf(
				TEXT("'from' state '%s' and 'to' state '%s' are in different graphs. Transitions must connect states within the same state machine graph."),
				*InFromState->GetStateName(), *InToState->GetStateName());
			return false;
		}
		return true;
	}

	static bool ValidateInitialStateNode(const USMGraphNode_StateNodeBase* InStateNode, FString& OutError)
	{
		if (InStateNode->IsA<USMGraphNode_AnyStateNode>())
		{
			OutError = FString::Printf(
				TEXT("State '%s' is an Any State and cannot be the initial state."),
				*InStateNode->GetStateName());
			return false;
		}
		if (!InStateNode->GetInputPin())
		{
			OutError = FString::Printf(
				TEXT("State '%s' has no input pin and cannot be the initial state."),
				*InStateNode->GetStateName());
			return false;
		}
		return true;
	}

	// The graph-node property write path goes through raw reflection (PropertyUtils::SetPropertyValue), which
	// can silently corrupt identity or structural state. Refuse the node's identity guid, object-typed fields
	// (e.g. BoundGraph), and containers (e.g. GraphPropertyGraphs, StateStack); scalar positional, comment,
	// and similar value fields stay writable.
	static bool IsWritableGraphNodeProperty(const FProperty* InProperty)
	{
		if (!InProperty)
		{
			return false;
		}
		if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UEdGraphNode, NodeGuid))
		{
			return false;
		}
		if (CastField<FObjectPropertyBase>(InProperty))
		{
			return false;
		}
		return CastField<FArrayProperty>(InProperty) == nullptr
			&& CastField<FMapProperty>(InProperty) == nullptr
			&& CastField<FSetProperty>(InProperty) == nullptr;
	}

	// SetPropertyValue returns void and swallows import failures, so a malformed value (e.g. "banana" for an
	// int position) silently no-ops while the op still reports success. Confirm the value parses first,
	// mirroring the writer's parser chain (PropertyValueFromString_Direct, then generic ImportText).
	static bool GraphNodePropertyValueParses(const FProperty* InProperty, const FString& InValue, UObject* InOwner)
	{
		// The engine's integer text import can accept garbage depending on process state; see
		// LD::Assist::Utils::IntegerPropertyTextParses.
		if (!LD::Assist::Utils::IntegerPropertyTextParses(InProperty, InValue))
		{
			return false;
		}
		void* Temp = FMemory::Malloc(InProperty->GetSize(), InProperty->GetMinAlignment());
		InProperty->InitializeValue(Temp);
		const bool bParsed =
			FBlueprintEditorUtils::PropertyValueFromString_Direct(InProperty, InValue, static_cast<uint8*>(Temp), InOwner)
			|| InProperty->ImportText_Direct(*InValue, Temp, InOwner, PPF_SerializedAsImportText, nullptr) != nullptr;
		InProperty->DestroyValue(Temp);
		FMemory::Free(Temp);
		return bParsed;
	}
}

FSMAssistOperationResult LD::Assist::CreateBlueprint(const TSharedRef<FJsonObject>& InArgs)
{
	FString Name;
	if (!InArgs->TryGetStringField(Args::Name, Name) || Name.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'name'."));
	}

	FString Path;
	InArgs->TryGetStringField(Args::Path, Path);

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(Name, TEXT("name"), LengthError)
		|| !LD::Assist::Utils::IsWithinNameLength(Path, TEXT("path"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	ISMAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<ISMAssetToolsModule>(
		LOGICDRIVER_ASSET_TOOLS_MODULE_NAME);

	const TSharedPtr<ISMAssetManager> AssetManager = AssetToolsModule.GetAssetManagerInterface();
	if (!AssetManager.IsValid())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Asset manager interface unavailable."));
	}

	ISMAssetManager::FCreateStateMachineBlueprintArgs CreateArgs;
	CreateArgs.Name = *Name;
	CreateArgs.Path = Path;

	USMBlueprint* NewBlueprint = AssetManager->CreateStateMachineBlueprint(CreateArgs);
	if (!NewBlueprint)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create state machine blueprint."));
	}

	LDASSIST_LOG_INFO(TEXT("Created state machine blueprint %s."), *NewBlueprint->GetPathName());

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, NewBlueprint->GetPathName());
	Payload->SetStringField(Args::Name, NewBlueprint->GetName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	USMGraph* TargetGraph = nullptr;
	{
		FString TargetGraphError;
		if (!LD::Assist::Private::ResolveTargetStateMachineGraph(InArgs, Blueprint, TargetGraph, TargetGraphError))
		{
			return FSMAssistOperationResult::MakeError(TargetGraphError);
		}
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;
	CreateArgs.GraphOwner = TargetGraph;

	FString StateName;
	if (InArgs->TryGetStringField(Args::StateName, StateName))
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(StateName, TEXT("state_name"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}
		CreateArgs.StateName = StateName;
	}

	bool bIsEntry = false;
	if (InArgs->TryGetBoolField(Args::IsEntry, bIsEntry))
	{
		CreateArgs.bIsEntryState = bIsEntry;
	}

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	FString StateClassPath;
	if (InArgs->TryGetStringField(Args::StateClass, StateClassPath) && !StateClassPath.IsEmpty())
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(StateClassPath, TEXT("state_class"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}
		UClass* StateClass = LoadClass<USMStateInstance_Base>(nullptr, *StateClassPath);
		if (!StateClass)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not load 'state_class' '%s'."), *StateClassPath));
		}
		CreateArgs.StateInstanceClass = StateClass;
	}

	// Core's schema action transacts the node add, but entry wiring and naming run after that inner
	// transaction closes; undoing the partial record would dangle the entry pin. One op-level scope
	// captures the whole chain (the inner transaction merges into it).
	USMGraphNode_StateNodeBase* StateNode = nullptr;
	{
		FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistAddState", "Add State (Assist)"));
		StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
		if (!StateNode)
		{
			Transaction.Cancel();
		}
	}
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create state node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddTransition(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString FromGuidStr;
	if (!InArgs->TryGetStringField(Args::FromStateGuid, FromGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'from_state_guid'."));
	}

	FString ToGuidStr;
	if (!InArgs->TryGetStringField(Args::ToStateGuid, ToGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'to_state_guid'."));
	}

	FGuid FromGuid;
	if (!FGuid::Parse(FromGuidStr, FromGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'from_state_guid' '%s'."), *FromGuidStr));
	}

	FGuid ToGuid;
	if (!FGuid::Parse(ToGuidStr, ToGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'to_state_guid' '%s'."), *ToGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_StateNodeBase* FromState = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, FromGuid);
	if (!FromState)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find 'from' state with guid '%s'."), *FromGuidStr));
	}

	USMGraphNode_StateNodeBase* ToState = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, ToGuid);
	if (!ToState)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find 'to' state with guid '%s'."), *ToGuidStr));
	}

	FString EndpointError;
	if (!LD::Assist::Private::ValidateTransitionEndpoints(FromState, ToState, EndpointError))
	{
		return FSMAssistOperationResult::MakeError(EndpointError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateTransitionEdgeArgs CreateArgs;
	CreateArgs.FromStateNode = FromState;
	CreateArgs.ToStateNode = ToState;

	FString TransitionClassPath;
	if (InArgs->TryGetStringField(Args::TransitionClass, TransitionClassPath) && !TransitionClassPath.IsEmpty())
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(TransitionClassPath, TEXT("transition_class"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}
		UClass* TransitionClass = LoadClass<USMTransitionInstance>(nullptr, *TransitionClassPath);
		if (!TransitionClass)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not load 'transition_class' '%s'."), *TransitionClassPath));
		}
		CreateArgs.TransitionInstanceClass = TransitionClass;
	}

	// Core CreateTransitionEdge wires both endpoints with no transaction of its own; without an
	// op-level scope the engine's Modify calls record nothing and the connection is invisible to undo.
	USMGraphNode_TransitionEdge* TransitionEdge = nullptr;
	{
		FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistAddTransition", "Add Transition (Assist)"));
		TransitionEdge = GraphGen->CreateTransitionEdge(Blueprint, CreateArgs);
		if (!TransitionEdge)
		{
			Transaction.Cancel();
		}
	}
	if (!TransitionEdge)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create transition edge."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::TransitionGuid, TransitionEdge->NodeGuid.ToString());
	Payload->SetStringField(Args::FromStateGuid, FromState->NodeGuid.ToString());
	Payload->SetStringField(Args::ToStateGuid, ToState->NodeGuid.ToString());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddTransitionReroute(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraph* TargetGraph = nullptr;
	{
		FString TargetGraphError;
		if (!LD::Assist::Private::ResolveTargetStateMachineGraph(InArgs, Blueprint, TargetGraph, TargetGraphError))
		{
			return FSMAssistOperationResult::MakeError(TargetGraphError);
		}
	}

	ISMGraphGeneration::FCreateTransitionRerouteArgs RerouteArgs;
	RerouteArgs.GraphOwner = TargetGraph;

	// Optional inline-insert target: when transition_guid is supplied, the reroute is spliced
	// into that transition's outgoing pin chain. When omitted, the reroute is created standalone.
	FString TransitionGuidStr;
	if (InArgs->TryGetStringField(Args::TransitionGuid, TransitionGuidStr) && !TransitionGuidStr.IsEmpty())
	{
		FGuid TransitionGuid;
		if (!FGuid::Parse(TransitionGuidStr, TransitionGuid))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Invalid 'transition_guid' '%s'."), *TransitionGuidStr));
		}

		USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, TransitionGuid);
		USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node);
		if (!TransitionEdge)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Node '%s' is not a transition edge."), *TransitionGuidStr));
		}

		RerouteArgs.TransitionEdge = TransitionEdge;
	}

	double PositionX = 0.0;
	double PositionY = 0.0;
	InArgs->TryGetNumberField(Args::PositionX, PositionX);
	InArgs->TryGetNumberField(Args::PositionY, PositionY);
	RerouteArgs.NodePosition = FVector2D(PositionX, PositionY);

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	USMGraphNode_RerouteNode* Reroute = GraphGen->CreateTransitionReroute(Blueprint, RerouteArgs);
	if (!Reroute)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create transition reroute."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::RerouteGuid, Reroute->NodeGuid.ToString());
	if (RerouteArgs.TransitionEdge)
	{
		Payload->SetStringField(Args::TransitionGuid, RerouteArgs.TransitionEdge->NodeGuid.ToString());
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::ListAssets(const TSharedRef<FJsonObject>& InArgs)
{
	FString PathPrefix;
	InArgs->TryGetStringField(Args::PathPrefix, PathPrefix);

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(PathPrefix, TEXT("path_prefix"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry"));
	const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(USMBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	if (!PathPrefix.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathPrefix));
	}

	TArray<FAssetData> AssetData;
	AssetRegistry.GetAssets(Filter, AssetData);

	TArray<TSharedPtr<FJsonValue>> AssetArray;
	AssetArray.Reserve(AssetData.Num());

	for (const FAssetData& Data : AssetData)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(Args::AssetPath, Data.GetObjectPathString());
		Entry->SetStringField(Args::Name, Data.AssetName.ToString());

		const FString ParentClassTag = Data.GetTagValueRef<FString>(FBlueprintTags::ParentClassPath);
		if (!ParentClassTag.IsEmpty())
		{
			Entry->SetStringField(Args::ParentClass, ParentClassTag);
		}

		AssetArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(Args::Assets, AssetArray);
	Payload->SetNumberField(Args::Count, AssetArray.Num());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::GetAsset(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraph* RootGraph = LD::Assist::Utils::GetRootStateMachineGraph(Blueprint);
	if (!RootGraph)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Blueprint has no root state machine graph."));
	}

	bool bScopeAll = false;
	{
		FString ScopeError;
		if (!LD::Assist::Private::TryParseGraphScope(InArgs, bScopeAll, ScopeError))
		{
			return FSMAssistOperationResult::MakeError(ScopeError);
		}
	}

	TArray<LD::Assist::Private::FStateMachineGraphEntry> GraphsToScan;
	if (bScopeAll)
	{
		LD::Assist::Private::CollectStateMachineGraphs(RootGraph, GraphsToScan);
	}
	else
	{
		LD::Assist::Private::FStateMachineGraphEntry RootEntry;
		RootEntry.Graph = RootGraph;
		RootEntry.GraphPath = RootGraph->GetName();
		GraphsToScan.Add(MoveTemp(RootEntry));
	}

	// Top-level entry_state_guids stays the root graph's, preserving its documented meaning. A nested
	// graph's own entry states are reported by their is_entry flag, which is evaluated per graph.
	TSet<FGuid> EntryStateGuids;
	TArray<TSharedPtr<FJsonValue>> States;
	TArray<TSharedPtr<FJsonValue>> Transitions;

	for (const LD::Assist::Private::FStateMachineGraphEntry& GraphEntry : GraphsToScan)
	{
		USMGraph* Graph = GraphEntry.Graph;

		// Under scope='root' no per-node fields are added, so existing callers parse the same entries.
		auto StampGraphLocation = [bScopeAll, &GraphEntry](const TSharedRef<FJsonObject>& InTarget)
		{
			if (!bScopeAll)
			{
				return;
			}
			InTarget->SetStringField(Args::GraphPath, GraphEntry.GraphPath);
			if (GraphEntry.ParentStateGuid.IsValid())
			{
				InTarget->SetStringField(Args::ParentStateGuid, GraphEntry.ParentStateGuid.ToString());
			}
		};

		TSet<FGuid> GraphEntryStateGuids;
		if (const USMGraphNode_StateMachineEntryNode* EntryNode = Graph->GetEntryNode())
		{
			if (const UEdGraphPin* EntryOutputPin = EntryNode->GetOutputPin())
			{
				for (const UEdGraphPin* LinkedPin : EntryOutputPin->LinkedTo)
				{
					if (LinkedPin)
					{
						if (const USMGraphNode_StateNodeBase* LinkedState = Cast<USMGraphNode_StateNodeBase>(LinkedPin->GetOwningNode()))
						{
							GraphEntryStateGuids.Add(LinkedState->NodeGuid);
						}
					}
				}
			}
		}
		if (Graph == RootGraph)
		{
			EntryStateGuids = GraphEntryStateGuids;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || Node->IsA<USMGraphNode_StateMachineEntryNode>())
			{
				continue;
			}

			if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Node))
			{
				const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
				Entry->SetStringField(Args::StateName, StateNode->GetStateName());
				if (const UClass* NodeClass = StateNode->GetNodeClass())
				{
					Entry->SetStringField(Args::StateClass, NodeClass->GetPathName());
				}
				Entry->SetNumberField(Args::PositionX, StateNode->NodePosX);
				Entry->SetNumberField(Args::PositionY, StateNode->NodePosY);
				Entry->SetBoolField(Args::IsEntry, GraphEntryStateGuids.Contains(StateNode->NodeGuid));

				const TCHAR* Kind = TEXT("state");
				if (StateNode->IsA<USMGraphNode_AnyStateNode>())
				{
					Kind = TEXT("any_state");
				}
				else if (const USMGraphNode_LinkStateNode* LinkNode = Cast<USMGraphNode_LinkStateNode>(StateNode))
				{
					Kind = TEXT("link_state");
					if (const USMGraphNode_StateNodeBase* LinkedState = LinkNode->GetLinkedState())
					{
						Entry->SetStringField(Args::LinkedStateGuid, LinkedState->NodeGuid.ToString());
						Entry->SetStringField(Args::LinkToStateName, LinkedState->GetStateName());
					}
				}
				else if (StateNode->IsA<USMGraphNode_RerouteNode>())
				{
					Kind = TEXT("reroute");
				}
				else if (StateNode->IsA<USMGraphNode_StateMachineStateNode>())
				{
					Kind = TEXT("state_machine_state");
				}
				else if (StateNode->IsA<USMGraphNode_ConduitNode>())
				{
					Kind = TEXT("conduit");
				}
				Entry->SetStringField(Args::Kind, Kind);
				StampGraphLocation(Entry);

				States.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			if (const USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node))
			{
				const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(Args::TransitionGuid, TransitionEdge->NodeGuid.ToString());
				if (const USMGraphNode_StateNodeBase* FromState = TransitionEdge->GetFromState())
				{
					Entry->SetStringField(Args::FromStateGuid, FromState->NodeGuid.ToString());
				}
				if (const USMGraphNode_StateNodeBase* ToState = TransitionEdge->GetToState())
				{
					Entry->SetStringField(Args::ToStateGuid, ToState->NodeGuid.ToString());
				}
				const UClass* TransitionNodeClass = TransitionEdge->GetNodeClass();
				if (TransitionNodeClass)
				{
					Entry->SetStringField(Args::TransitionClass, TransitionNodeClass->GetPathName());
				}

				// Condition axis ("what must be true"), readable without a get_local_graph. The trigger axis below is
				// independent. Reuse the plugin's own classifier so the constant/inline call matches compilation and
				// handles a result node buried in a nested graph.
				FString Gate;
				if (TransitionNodeClass && TransitionNodeClass != USMTransitionInstance::StaticClass())
				{
					Gate = TEXT("class");
				}
				else if (const USMTransitionGraph* TransitionGraph = TransitionEdge->GetTransitionGraph())
				{
					switch (TransitionGraph->GetConditionalEvaluationType())
					{
					case ESMConditionalEvaluationType::SM_AlwaysTrue:
						Gate = TEXT("constant:true");
						break;
					case ESMConditionalEvaluationType::SM_AlwaysFalse:
						Gate = TEXT("constant:false");
						break;
					default:
						Gate = TEXT("inline");
						break;
					}
				}
				if (!Gate.IsEmpty())
				{
					Entry->SetStringField(Args::Gate, Gate);
				}

				// Trigger axis, independent of the gate: polled each tick, fired by an auto-bound event, or both. The
				// event component needs an actual binding, since the permission flag defaults true.
				if (const USMTransitionInstance* TransitionInstance = Cast<USMTransitionInstance>(TransitionEdge->GetNodeTemplate()))
				{
					bool bTick = TransitionInstance->GetCanEvaluate();
					// A from-state that disables tick transition evaluation suppresses polling of its outgoing edges,
					// leaving them event-only. Mirrors FSMState_Base::CanEvaluateTransitionsOnTick.
					if (const USMGraphNode_StateNodeBase* FromState = TransitionEdge->GetFromState())
					{
						if (const USMStateInstance_Base* FromInstance = Cast<USMStateInstance_Base>(FromState->GetNodeTemplate()))
						{
							bTick = bTick && !FromInstance->GetDisableTickTransitionEvaluation();
						}
					}
					const bool bEvent = TransitionEdge->DelegatePropertyName != NAME_None && TransitionInstance->GetCanEvaluateFromEvent();
					const TCHAR* Evaluation = bTick
						? (bEvent ? TEXT("tick+event") : TEXT("tick"))
						: (bEvent ? TEXT("event") : TEXT("none"));
					Entry->SetStringField(Args::Evaluation, Evaluation);
				}

				// Auto-bound event binding, mirroring configure_transition_event's fields so a caller can verify it
				// landed without a get_local_graph.
				if (TransitionEdge->DelegatePropertyName != NAME_None)
				{
					const TCHAR* OwnerInstance = TEXT("Context");
					switch (TransitionEdge->DelegateOwnerInstance.GetValue())
					{
					case SMDO_This:
						OwnerInstance = TEXT("This");
						break;
					case SMDO_PreviousState:
						OwnerInstance = TEXT("PreviousState");
						break;
					default:
						break;
					}

					const TSharedRef<FJsonObject> EventObject = MakeShared<FJsonObject>();
					EventObject->SetStringField(Args::DelegatePropertyName, TransitionEdge->DelegatePropertyName.ToString());
					EventObject->SetStringField(Args::DelegateOwnerInstance, OwnerInstance);
					if (const UClass* OwnerClass = TransitionEdge->DelegateOwnerClass)
					{
						EventObject->SetStringField(Args::DelegateOwnerClass, OwnerClass->GetPathName());
					}
					EventObject->SetBoolField(Args::EventTriggersTargetedUpdate, TransitionEdge->bEventTriggersTargetedUpdate != 0);
					EventObject->SetBoolField(Args::EventTriggersFullUpdate, TransitionEdge->bEventTriggersFullUpdate != 0);
					Entry->SetObjectField(Args::Event, EventObject);
				}

				StampGraphLocation(Entry);
				Transitions.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> EntryGuidArray;
	for (const FGuid& EntryGuid : EntryStateGuids)
	{
		EntryGuidArray.Add(MakeShared<FJsonValueString>(EntryGuid.ToString()));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::Name, Blueprint->GetName());
	if (const UClass* ParentClass = Blueprint->ParentClass)
	{
		Payload->SetStringField(Args::ParentClass, ParentClass->GetPathName());
	}
	Payload->SetStringField(Args::Scope, bScopeAll ? TEXT("all") : TEXT("root"));
	Payload->SetArrayField(Args::EntryStateGuids, EntryGuidArray);
	Payload->SetArrayField(Args::States, States);
	Payload->SetArrayField(Args::Transitions, Transitions);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::RemoveNode(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	// Core RemoveNode Modifies the graph, the node, and link counterparts but opens no transaction;
	// the op-level scope makes those records effective so undo restores the node and its links.
	bool bRemoved = false;
	{
		FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistRemoveNode", "Remove Node (Assist)"));
		bRemoved = GraphGen->RemoveNode(Node);
		if (!bRemoved)
		{
			Transaction.Cancel();
		}
	}
	if (!bRemoved)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to remove node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static bool IsPropertyHiddenOnInstanceTemplate(const FProperty* InProperty, const USMNodeInstance* InTemplate)
	{
		if (!InProperty || !InTemplate)
		{
			return false;
		}
		if (InProperty->HasMetaData(TEXT("InstancedTemplate")))
		{
			return true;
		}
		if (InTemplate->GetTemplateGuid().IsValid() && InProperty->HasMetaData(TEXT("NodeBaseOnly")))
		{
			return true;
		}
		return false;
	}

	// Reflection drops the _DEPRECATED suffix, so a lookup by wire name resolves properties that
	// get_node_properties never reports. Their only remaining reader is the load-time migration that
	// folds them into their replacements, which has already run by the time a caller can write one.
	static FSMAssistOperationResult MakeDeprecatedPropertyError(const FString& InPropertyName)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Property '%s' is deprecated and writing it changes nothing. Call ld.get_node_properties to list the properties this node supports."),
			*InPropertyName));
	}

	static bool JsonScalarToDefaultString(const TSharedPtr<FJsonValue>& InValue, FString& OutString, FString& OutError)
	{
		if (!InValue.IsValid())
		{
			OutError = TEXT("Missing value.");
			return false;
		}

		switch (InValue->Type)
		{
			case EJson::String:
				OutString = InValue->AsString();
				return true;
			case EJson::Number:
			{
				// Emit integer-valued numbers without a trailing ".0". UEdGraphSchema_K2::TrySetDefaultValue
				// on integer sub-pins rejects float-formatted strings, and integer ImportText accepts
				// both forms, so this format is universally safe across pin types.
				const double Number = InValue->AsNumber();
				const double Truncated = FMath::TruncToDouble(Number);
				if (Number == Truncated && FMath::Abs(Number) < static_cast<double>(TNumericLimits<int64>::Max()))
				{
					OutString = FString::Printf(TEXT("%lld"), static_cast<int64>(Truncated));
				}
				else
				{
					OutString = FString::SanitizeFloat(Number);
				}
				return true;
			}
			case EJson::Boolean:
				OutString = InValue->AsBool() ? TEXT("true") : TEXT("false");
				return true;
			case EJson::Null:
				OutString.Reset();
				return true;
			default:
				OutError = TEXT("Value must be a string, number, boolean, or null.");
				return false;
		}
	}

	static bool JsonValueToDefaultStrings(const TSharedPtr<FJsonValue>& InValue, TArray<FString>& OutStrings, bool& bOutIsArray, FString& OutError)
	{
		bOutIsArray = false;
		OutStrings.Reset();

		if (!InValue.IsValid())
		{
			OutError = TEXT("Missing 'value'.");
			return false;
		}

		if (InValue->Type == EJson::Array)
		{
			bOutIsArray = true;
			const TArray<TSharedPtr<FJsonValue>>& Array = InValue->AsArray();
			OutStrings.Reserve(Array.Num());
			for (int32 Idx = 0; Idx < Array.Num(); ++Idx)
			{
				FString Element;
				FString ElementError;
				if (!JsonScalarToDefaultString(Array[Idx], Element, ElementError))
				{
					OutError = FString::Printf(TEXT("'value[%d]': %s"), Idx, *ElementError);
					return false;
				}
				OutStrings.Add(MoveTemp(Element));
			}
			return true;
		}

		FString Scalar;
		if (!JsonScalarToDefaultString(InValue, Scalar, OutError))
		{
			return false;
		}
		OutStrings.Add(MoveTemp(Scalar));
		return true;
	}
}

FSMAssistOperationResult LD::Assist::SetNodeProperty(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FString PropertyName;
	if (!InArgs->TryGetStringField(Args::PropertyName, PropertyName) || PropertyName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'property_name'."));
	}

	FString PropertyNameLengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(PropertyName, TEXT("property_name"), PropertyNameLengthError))
	{
		return FSMAssistOperationResult::MakeError(PropertyNameLengthError);
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	USMNodeInstance* TargetTemplate = nullptr;
	int32 StackIndex = INDEX_NONE;
	const bool bStackIndexProvided = InArgs->TryGetNumberField(Args::StackIndex, StackIndex) && StackIndex >= 0;
	if (bStackIndexProvided)
	{
		TargetTemplate = Node->GetTemplateFromIndex(StackIndex);
		if (!TargetTemplate)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("No stack template at index %d on node '%s'."), StackIndex, *NodeGuidStr));
		}
	}

	FString ArrayAction;
	InArgs->TryGetStringField(Args::ArrayAction, ArrayAction);
	const FString NormalizedAction = ArrayAction.ToLower();

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	Payload->SetStringField(Args::PropertyName, PropertyName);
	if (bStackIndexProvided)
	{
		Payload->SetNumberField(Args::StackIndex, StackIndex);
	}

	// Target UEdGraphNode fields (NodePosX/NodePosY/NodeComment, etc.) when the caller didn't pin a stack template.
	// Skip deprecated graph-node fields. UE reflection strips the _DEPRECATED suffix during name lookup, so a
	// template-side property name (e.g. bDisableTickTransitionEvaluation) could otherwise match a deprecated
	// graph-node mirror and silently write to the wrong object.
	if (!bStackIndexProvided)
	{
		FProperty* GraphNodeProperty = FindFProperty<FProperty>(Node->GetClass(), *PropertyName);
		if (GraphNodeProperty && GraphNodeProperty->HasAnyPropertyFlags(CPF_Deprecated))
		{
			GraphNodeProperty = nullptr;
		}
		if (GraphNodeProperty && !LD::Assist::Private::IsWritableGraphNodeProperty(GraphNodeProperty))
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(
				TEXT("'%s' resolves to an internal graph-node field (node identity, an object reference, or a structural container) and cannot be written via set_node_property."),
				*PropertyName));
		}
		if (GraphNodeProperty)
		{
			if (!NormalizedAction.IsEmpty() && NormalizedAction != TEXT("set"))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("'%s' resolves to graph-node property '%s'; 'array_action=%s' is only supported on node-template properties (template array_action values: 'set', 'remove', 'clear', 'add', 'insert', 'duplicate', 'move')."),
						*PropertyName, *PropertyName, *ArrayAction));
			}

			TArray<FString> ValueStrings;
			bool bValueIsArray = false;
			FString ValueError;
			const TSharedPtr<FJsonValue> ValueField = InArgs->TryGetField(Args::Value);
			if (!LD::Assist::Private::JsonValueToDefaultStrings(ValueField, ValueStrings, bValueIsArray, ValueError))
			{
				return FSMAssistOperationResult::MakeError(ValueError);
			}

			int32 StartIndex = 0;
			const bool bHasExplicitIndex = InArgs->TryGetNumberField(Args::ArrayIndex, StartIndex);
			if (bValueIsArray && bHasExplicitIndex)
			{
				return FSMAssistOperationResult::MakeError(
					TEXT("'array_index' cannot be combined with an array 'value'; elements are written starting at index 0."));
			}

			if (StartIndex < 0)
			{
				return FSMAssistOperationResult::MakeError(TEXT("'array_index' must be non-negative."));
			}

			// With containers rejected above, the index addresses a static (C-array) slot; an out-of-range value
			// asserts inside ContainerPtrToValuePtr. Graph-node properties are almost always scalar (ArrayDim 1),
			// so this rejects e.g. array_index 1 on NodeComment before it can crash the editor.
			const int32 HighestWriteIndex = bValueIsArray ? (ValueStrings.Num() - 1) : StartIndex;
			if (HighestWriteIndex >= GraphNodeProperty->ArrayDim)
			{
				return FSMAssistOperationResult::MakeError(FString::Printf(
					TEXT("Index %d is out of range for graph-node property '%s' (fixed size %d); structural array actions are supported on node-template properties only."),
					HighestWriteIndex, *PropertyName, GraphNodeProperty->ArrayDim));
			}

			for (const FString& CandidateValue : ValueStrings)
			{
				if (!LD::Assist::Private::GraphNodePropertyValueParses(GraphNodeProperty, CandidateValue, Node))
				{
					return FSMAssistOperationResult::MakeError(FString::Printf(
						TEXT("Could not parse value '%s' as %s for graph-node property '%s'."),
						*CandidateValue, *GraphNodeProperty->GetCPPType(), *PropertyName));
				}
			}

			// Without a transaction the Modify below records nothing; scope it so graph-node field
			// writes (position, comment) participate in undo.
			const FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistSetNodeProperty", "Set Node Property (Assist)"));
			Node->Modify();
			for (int32 Idx = 0; Idx < ValueStrings.Num(); ++Idx)
			{
				const int32 WriteIndex = bValueIsArray ? Idx : StartIndex;
				LD::Editor::PropertyUtils::SetPropertyValue(GraphNodeProperty, ValueStrings[Idx], Node, WriteIndex);
			}

			FPropertyChangedEvent PropertyChangedEvent(GraphNodeProperty, EPropertyChangeType::ValueSet);
			Node->PostEditChangeProperty(PropertyChangedEvent);

			if (UEdGraph* OwnerGraph = Node->GetGraph())
			{
				OwnerGraph->NotifyGraphChanged();
			}
			Node->GetPackage()->MarkPackageDirty();

			if (bValueIsArray)
			{
				Payload->SetNumberField(Args::ElementCount, ValueStrings.Num());
			}
			else if (bHasExplicitIndex)
			{
				Payload->SetNumberField(Args::ArrayIndex, StartIndex);
			}
			return FSMAssistOperationResult::MakeSuccess(Payload);
		}
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	USMNodeInstance* ResolvedTemplate = TargetTemplate ? TargetTemplate : Node->GetNodeTemplate();
	if (ResolvedTemplate)
	{
		if (const FProperty* TemplateProperty = ResolvedTemplate->GetClass()->FindPropertyByName(*PropertyName))
		{
			if (TemplateProperty->HasAnyPropertyFlags(CPF_Deprecated))
			{
				return LD::Assist::Private::MakeDeprecatedPropertyError(PropertyName);
			}

			if (LD::Assist::Private::IsPropertyHiddenOnInstanceTemplate(TemplateProperty, ResolvedTemplate))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Property '%s' is base-class-only and cannot be written on an instance template. Edit the class default (on the owning USMNodeInstance subclass) instead."),
						*PropertyName));
			}
		}
	}

	ISMGraphGeneration::FSetNodePropertyArgs PropertyArgs;
	PropertyArgs.PropertyName = *PropertyName;
	PropertyArgs.NodeInstance = TargetTemplate;

	FString PropertyPath;
	InArgs->TryGetStringField(Args::PropertyPath, PropertyPath);
	PropertyArgs.SubPath = PropertyPath;

	const bool bIsStructuralAction =
		NormalizedAction == TEXT("add") ||
		NormalizedAction == TEXT("insert") ||
		NormalizedAction == TEXT("duplicate") ||
		NormalizedAction == TEXT("move") ||
		NormalizedAction == TEXT("remove") ||
		NormalizedAction == TEXT("clear");

	// Structural actions never accept a 'value' payload, since they change the array's shape, not cell contents.
	if (bIsStructuralAction)
	{
		if (InArgs->HasField(Args::Value))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("'array_action=%s' does not accept 'value'. Use 'set' to write element values."), *NormalizedAction));
		}
	}

	// Pre-flight: distinguish "not an array" from "array but locked" so the gate error below stays accurate.
	// SubPath cases defer this check to the leaf-resolver downstream; the top-level property is the
	// struct that owns the path, not the target array.
	if (bIsStructuralAction && ResolvedTemplate && PropertyPath.IsEmpty())
	{
		if (const FProperty* TargetProperty = ResolvedTemplate->GetClass()->FindPropertyByName(*PropertyName))
		{
			if (!CastField<FArrayProperty>(TargetProperty))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Property '%s' is not an array; structural 'array_action=%s' requires an array property."),
						*PropertyName, *NormalizedAction));
			}
		}
	}

	// Pre-flight gate: surface EditFixedSize / BlueprintReadOnly as an explicit error rather than a silent no-op.
	// CanModify* keys on top-level FName and would always fail at depth, so SubPath cases skip the
	// gate; locked nested arrays surface later via SetNodePropertyValueStructuralAtSubPath.
	auto CanMutateArray = [&](bool bStructural) -> bool
	{
		if (!PropertyPath.IsEmpty())
		{
			return true;
		}
		return bStructural
			? Node->CanModifyArraySize(*PropertyName, PropertyArgs.NodeInstance)
			: Node->CanModifyArrayContents(*PropertyName, PropertyArgs.NodeInstance);
	};
	auto MakeReadOnlyError = [&]() -> FSMAssistOperationResult
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Array property '%s' rejects '%s' (EditFixedSize, BlueprintReadOnly, or ExposedPropertyOverrides.bReadOnly)."),
				*PropertyName, *NormalizedAction));
	};

	// Detail appended to PropertyPath-write failures. The write path reports the actual cause when it
	// resolved one (unknown member naming the valid alternatives, or a genuinely unsplit parent);
	// the strict-split precondition is only assumed when it did not, since asserting it on an
	// unresolved segment sends the caller to re-split a pin that is already split.
	FString PathError;
	auto MakeSplitHint = [&]() -> FString
	{
		if (!PathError.IsEmpty())
		{
			return FString::Printf(TEXT(" %s"), *PathError);
		}
		return PropertyPath.IsEmpty()
			? FString()
			: TEXT(" The log carries the specific cause. One common cause: when using property_path, every struct parent in the chain must be split via SplitPin first (top-level pin AND every intermediate struct member).");
	};

	if (NormalizedAction == TEXT("clear"))
	{
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::Clear;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs, &PathError))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to clear array property '%s' on node.%s"), *PropertyName, *MakeSplitHint()));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("clear"));
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("remove"))
	{
		int32 RemoveIndex = 0;
		if (!InArgs->TryGetNumberField(Args::ArrayIndex, RemoveIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=remove' requires 'array_index'."));
		}
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::RemoveElement;
		PropertyArgs.PropertyIndex = RemoveIndex;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs, &PathError))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to remove element %d from array property '%s'.%s"), RemoveIndex, *PropertyName, *MakeSplitHint()));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("remove"));
		Payload->SetNumberField(Args::ArrayIndex, RemoveIndex);
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("add"))
	{
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::AddElement;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs, &PathError))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to add element to array property '%s'.%s"), *PropertyName, *MakeSplitHint()));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("add"));
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("insert"))
	{
		int32 InsertIndex = 0;
		if (!InArgs->TryGetNumberField(Args::ArrayIndex, InsertIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=insert' requires 'array_index'."));
		}
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::InsertElement;
		PropertyArgs.PropertyIndex = InsertIndex;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs, &PathError))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to insert element at %d into array property '%s'.%s"), InsertIndex, *PropertyName, *MakeSplitHint()));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("insert"));
		Payload->SetNumberField(Args::ArrayIndex, InsertIndex);
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("duplicate"))
	{
		int32 SourceIndex = 0;
		if (!InArgs->TryGetNumberField(Args::ArrayIndex, SourceIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=duplicate' requires 'array_index' (source element)."));
		}
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::DuplicateElement;
		PropertyArgs.PropertyIndex = SourceIndex;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs, &PathError))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to duplicate element %d in array property '%s'.%s"), SourceIndex, *PropertyName, *MakeSplitHint()));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("duplicate"));
		Payload->SetNumberField(Args::ArrayIndex, SourceIndex);
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("move"))
	{
		int32 SourceIndex = 0;
		if (!InArgs->TryGetNumberField(Args::ArrayIndex, SourceIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=move' requires 'array_index' (source element)."));
		}
		int32 DestIndex = 0;
		if (!InArgs->TryGetNumberField(Args::TargetIndex, DestIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=move' requires 'target_index' (destination element)."));
		}
		if (SourceIndex == DestIndex)
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=move' requires 'array_index' and 'target_index' to differ."));
		}
		if (!CanMutateArray(/*bStructural*/ false))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::MoveElement;
		PropertyArgs.PropertyIndex = SourceIndex;
		PropertyArgs.TargetIndex = DestIndex;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs, &PathError))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to move element %d to %d in array property '%s'.%s"), SourceIndex, DestIndex, *PropertyName, *MakeSplitHint()));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("move"));
		Payload->SetNumberField(Args::ArrayIndex, SourceIndex);
		Payload->SetNumberField(Args::TargetIndex, DestIndex);
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (!NormalizedAction.IsEmpty() && NormalizedAction != TEXT("set"))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Unknown 'array_action' '%s'. Expected 'set', 'add', 'insert', 'duplicate', 'move', 'remove', or 'clear'."), *ArrayAction));
	}

	TArray<FString> ValueStrings;
	bool bValueIsArray = false;
	FString ValueError;
	const TSharedPtr<FJsonValue> ValueField = InArgs->TryGetField(Args::Value);
	if (!LD::Assist::Private::JsonValueToDefaultStrings(ValueField, ValueStrings, bValueIsArray, ValueError))
	{
		return FSMAssistOperationResult::MakeError(ValueError);
	}

	int32 StartIndex = 0;
	const bool bHasExplicitIndex = InArgs->TryGetNumberField(Args::ArrayIndex, StartIndex);
	if (bValueIsArray && bHasExplicitIndex)
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("'array_index' cannot be combined with an array 'value'; elements are written starting at index 0."));
	}

	if (!PropertyPath.IsEmpty() && bValueIsArray)
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("'property_path' addresses a single leaf; 'value' must be a scalar, not an array."));
	}

	PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::SetElement;
	for (int32 Idx = 0; Idx < ValueStrings.Num(); ++Idx)
	{
		PropertyArgs.PropertyIndex = bValueIsArray ? Idx : StartIndex;
		PropertyArgs.PropertyDefaultValue = ValueStrings[Idx];
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs, &PathError))
		{
			const FString PathSuffix = PropertyPath.IsEmpty()
				? FString()
				: FString::Printf(TEXT(" path '%s'"), *PropertyPath);
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to set property '%s'%s at index %d on node.%s"),
					*PropertyName, *PathSuffix, PropertyArgs.PropertyIndex, *MakeSplitHint()));
		}
	}

	if (bValueIsArray)
	{
		Payload->SetNumberField(Args::ElementCount, ValueStrings.Num());
	}
	else if (bHasExplicitIndex)
	{
		Payload->SetNumberField(Args::ArrayIndex, StartIndex);
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::Compile(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	UBlueprint* Blueprint = LD::Assist::Utils::LoadBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	const EBlueprintStatus Status = Blueprint->Status;
	const bool bSuccess = Status == BS_UpToDate || Status == BS_UpToDateWithWarnings;

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetBoolField(Args::UpToDate, Status == BS_UpToDate);
	Payload->SetBoolField(Args::HasWarnings, Status == BS_UpToDateWithWarnings);
	Payload->SetBoolField(Args::HasErrors, Status == BS_Error);
	Payload->SetNumberField(Args::Status, static_cast<int32>(Status));

	if (!bSuccess)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Blueprint compile reported errors."), Payload);
	}

	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::RenameState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	const TCHAR* GuidArgName = Args::StateGuid;
	{
		FString GuidArgError;
		if (!LD::Assist::Private::TryGetStateGuidArg(InArgs, NodeGuidStr, GuidArgName, GuidArgError))
		{
			return FSMAssistOperationResult::MakeError(GuidArgError);
		}
	}

	FString NewName;
	if (!InArgs->TryGetStringField(Args::NewName, NewName) || NewName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'new_name'."));
	}

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(NewName, TEXT("new_name"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid '%s' '%s'."), GuidArgName, *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_StateNodeBase* StateNode = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, NodeGuid);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find state node with guid '%s'."), *NodeGuidStr));
	}

	FText RenameError;
	if (!StateNode->SetNodeName(NewName, RenameError))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Rename rejected: %s"), *RenameError.ToString()));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, NodeGuidStr);
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SetInitialState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString StateGuidStr;
	const TCHAR* GuidArgName = Args::StateGuid;
	{
		FString GuidArgError;
		if (!LD::Assist::Private::TryGetStateGuidArg(InArgs, StateGuidStr, GuidArgName, GuidArgError))
		{
			return FSMAssistOperationResult::MakeError(GuidArgError);
		}
	}

	FGuid StateGuid;
	if (!FGuid::Parse(StateGuidStr, StateGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid '%s' '%s'."), GuidArgName, *StateGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_StateNodeBase* StateNode = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, StateGuid);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find state node with guid '%s'."), *StateGuidStr));
	}

	FString StateError;
	if (!LD::Assist::Private::ValidateInitialStateNode(StateNode, StateError))
	{
		return FSMAssistOperationResult::MakeError(StateError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	// Core SetInitialState breaks the old entry link and wires the new one with no transaction;
	// the pin primitives Modify both endpoints, so an op-level scope captures the full rewire.
	bool bInitialStateSet = false;
	{
		FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistSetInitialState", "Set Initial State (Assist)"));
		bInitialStateSet = GraphGen->SetInitialState(StateNode);
		if (!bInitialStateSet)
		{
			Transaction.Cancel();
		}
	}
	if (!bInitialStateSet)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Failed to set initial state for node '%s'."), *StateGuidStr));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateGuidStr);
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddStateStack(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString StateGuidStr;
	const TCHAR* GuidArgName = Args::StateGuid;
	{
		FString GuidArgError;
		if (!LD::Assist::Private::TryGetStateGuidArg(InArgs, StateGuidStr, GuidArgName, GuidArgError))
		{
			return FSMAssistOperationResult::MakeError(GuidArgError);
		}
	}

	FString StateClassPath;
	if (!InArgs->TryGetStringField(Args::StateClass, StateClassPath) || StateClassPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'state_class'."));
	}

	FGuid StateGuid;
	if (!FGuid::Parse(StateGuidStr, StateGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid '%s' '%s'."), GuidArgName, *StateGuidStr));
	}

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(StateClassPath, TEXT("state_class"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	UClass* StackClass = LoadClass<USMStateInstance>(nullptr, *StateClassPath);
	if (!StackClass)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not load 'state_class' '%s'."), *StateClassPath));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_StateNodeBase* StateNodeBase = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, StateGuid);
	USMGraphNode_StateNode* StateNode = Cast<USMGraphNode_StateNode>(StateNodeBase);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' is not a state node that supports a state stack."), *StateGuidStr));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateStateStackArgs StackArgs;
	StackArgs.StateStackInstanceClass = StackClass;
	StackArgs.StateStackIndex = INDEX_NONE;

	int32 RequestedIndex = 0;
	if (InArgs->TryGetNumberField(Args::StackIndex, RequestedIndex))
	{
		StackArgs.StateStackIndex = RequestedIndex;
	}

	USMStateInstance* StackInstance = GraphGen->CreateStateStackInstance(StateNode, StackArgs);
	if (!StackInstance)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Failed to add state stack '%s' to state '%s'."), *StateClassPath, *StateGuidStr));
	}

	const TArray<FStateStackContainer>& Stack = StateNode->GetAllNodeStackTemplates();
	int32 ResolvedIndex = INDEX_NONE;
	FGuid TemplateGuid;
	for (int32 Idx = 0; Idx < Stack.Num(); ++Idx)
	{
		if (Stack[Idx].NodeStackInstanceTemplate == StackInstance)
		{
			ResolvedIndex = Idx;
			TemplateGuid = Stack[Idx].TemplateGuid;
			break;
		}
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateGuidStr);
	Payload->SetStringField(Args::StateClass, StackClass->GetPathName());
	Payload->SetNumberField(Args::StackIndex, ResolvedIndex);
	Payload->SetStringField(Args::TemplateGuid, TemplateGuid.ToString());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddTransitionStack(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString TransitionGuidStr;
	if (!InArgs->TryGetStringField(Args::TransitionGuid, TransitionGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'transition_guid'."));
	}

	FString TransitionClassPath;
	if (!InArgs->TryGetStringField(Args::TransitionClass, TransitionClassPath) || TransitionClassPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'transition_class'."));
	}

	FGuid TransitionGuid;
	if (!FGuid::Parse(TransitionGuidStr, TransitionGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'transition_guid' '%s'."), *TransitionGuidStr));
	}

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(TransitionClassPath, TEXT("transition_class"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	UClass* StackClass = LoadClass<USMTransitionInstance>(nullptr, *TransitionClassPath);
	if (!StackClass)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not load 'transition_class' '%s'."), *TransitionClassPath));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, TransitionGuid);
	USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node);
	if (!TransitionEdge)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' is not a transition edge."), *TransitionGuidStr));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateTransitionStackArgs StackArgs;
	StackArgs.TransitionStackInstanceClass = StackClass;
	StackArgs.TransitionStackIndex = INDEX_NONE;

	int32 RequestedIndex = 0;
	if (InArgs->TryGetNumberField(Args::StackIndex, RequestedIndex))
	{
		StackArgs.TransitionStackIndex = RequestedIndex;
	}

	USMTransitionInstance* StackInstance = GraphGen->CreateTransitionStackInstance(TransitionEdge, StackArgs);
	if (!StackInstance)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Failed to add transition stack '%s' to transition '%s'."), *TransitionClassPath, *TransitionGuidStr));
	}

	const TArray<FTransitionStackContainer>& Stack = TransitionEdge->GetAllNodeStackTemplates();
	int32 ResolvedIndex = INDEX_NONE;
	FGuid TemplateGuid;
	for (int32 Idx = 0; Idx < Stack.Num(); ++Idx)
	{
		if (Stack[Idx].NodeStackInstanceTemplate == StackInstance)
		{
			ResolvedIndex = Idx;
			TemplateGuid = Stack[Idx].TemplateGuid;
			break;
		}
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::TransitionGuid, TransitionGuidStr);
	Payload->SetStringField(Args::TransitionClass, StackClass->GetPathName());
	Payload->SetNumberField(Args::StackIndex, ResolvedIndex);
	Payload->SetStringField(Args::TemplateGuid, TemplateGuid.ToString());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddConduit(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	USMGraph* TargetGraph = nullptr;
	{
		FString TargetGraphError;
		if (!LD::Assist::Private::ResolveTargetStateMachineGraph(InArgs, Blueprint, TargetGraph, TargetGraphError))
		{
			return FSMAssistOperationResult::MakeError(TargetGraphError);
		}
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;
	CreateArgs.GraphOwner = TargetGraph;
	CreateArgs.StateInstanceClass = USMConduitInstance::StaticClass();

	FString StateName;
	if (InArgs->TryGetStringField(Args::StateName, StateName))
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(StateName, TEXT("state_name"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}
		CreateArgs.StateName = StateName;
	}

	bool bIsEntry = false;
	if (InArgs->TryGetBoolField(Args::IsEntry, bIsEntry))
	{
		CreateArgs.bIsEntryState = bIsEntry;
	}

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	FString StateClassPath;
	if (InArgs->TryGetStringField(Args::StateClass, StateClassPath) && !StateClassPath.IsEmpty())
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(StateClassPath, TEXT("state_class"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}

		UClass* ConduitClass = LoadClass<USMConduitInstance>(nullptr, *StateClassPath);
		if (!ConduitClass)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not load 'state_class' '%s' as a USMConduitInstance subclass."), *StateClassPath));
		}
		CreateArgs.StateInstanceClass = ConduitClass;
	}

	bool bEvalWithTransitions = false;
	if (InArgs->TryGetBoolField(Args::EvalWithTransitions, bEvalWithTransitions))
	{
		CreateArgs.bConduitEvalWithTransitions = bEvalWithTransitions;
	}

	USMGraphNode_StateNodeBase* StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create conduit node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	if (const UClass* NodeClass = StateNode->GetNodeClass())
	{
		Payload->SetStringField(Args::StateClass, NodeClass->GetPathName());
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddReference(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	// Reference target is optional. Omitting it creates a state-machine state with no reference yet;
	// the caller can set the target later via ld.configure_reference.
	USMBlueprint* ReferencedBlueprint = nullptr;
	FString ReferencedPath;
	if (InArgs->TryGetStringField(Args::ReferenceAssetPath, ReferencedPath) && !ReferencedPath.IsEmpty())
	{
		FString ReferencedLoadError;
		ReferencedBlueprint = LD::Assist::Utils::LoadStateMachineBlueprint(ReferencedPath, ReferencedLoadError);
		if (!ReferencedBlueprint)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not load 'reference_asset_path': %s"), *ReferencedLoadError));
		}
		if (ReferencedBlueprint == Blueprint)
		{
			return FSMAssistOperationResult::MakeError(TEXT("A state machine blueprint cannot reference itself."));
		}
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	USMGraph* TargetGraph = nullptr;
	{
		FString TargetGraphError;
		if (!LD::Assist::Private::ResolveTargetStateMachineGraph(InArgs, Blueprint, TargetGraph, TargetGraphError))
		{
			return FSMAssistOperationResult::MakeError(TargetGraphError);
		}
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;
	CreateArgs.GraphOwner = TargetGraph;
	CreateArgs.StateInstanceClass = USMStateMachineInstance::StaticClass();
	if (ReferencedBlueprint)
	{
		CreateArgs.StateMachineReferenceConfig.ReferencedBlueprint = ReferencedBlueprint;
	}

	FString StateName;
	if (InArgs->TryGetStringField(Args::StateName, StateName))
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(StateName, TEXT("state_name"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}
		CreateArgs.StateName = StateName;
	}

	bool bIsEntry = false;
	if (InArgs->TryGetBoolField(Args::IsEntry, bIsEntry))
	{
		CreateArgs.bIsEntryState = bIsEntry;
	}

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	bool bUseIntermediateGraph = false;
	if (InArgs->TryGetBoolField(Args::UseIntermediateGraph, bUseIntermediateGraph))
	{
		CreateArgs.StateMachineReferenceConfig.bUseIntermediateGraph = bUseIntermediateGraph;
	}

	USMGraphNode_StateNodeBase* StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create reference node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	if (ReferencedBlueprint)
	{
		Payload->SetStringField(Args::ReferenceAssetPath, ReferencedBlueprint->GetPathName());
	}
	if (CreateArgs.StateMachineReferenceConfig.bUseIntermediateGraph.IsSet())
	{
		Payload->SetBoolField(Args::UseIntermediateGraph, CreateArgs.StateMachineReferenceConfig.bUseIntermediateGraph.GetValue());
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::ConfigureReference(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr) || NodeGuidStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid' (state-machine-reference state guid)."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No node with guid '%s' on '%s'."), *NodeGuidStr, *AssetPath));
	}

	USMGraphNode_StateMachineStateNode* RefNode = Cast<USMGraphNode_StateMachineStateNode>(Node);
	if (!RefNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' is not a state-machine-reference state. configure_reference only operates on USMGraphNode_StateMachineStateNode instances."), *NodeGuidStr));
	}

	ISMGraphGeneration::FConfigureStateMachineReferenceArgs ConfigureArgs;
	TArray<FString> AppliedFields;

	FString ReferencedAssetPath;
	if (InArgs->TryGetStringField(Args::ReferenceAssetPath, ReferencedAssetPath))
	{
		if (ReferencedAssetPath.IsEmpty())
		{
			ConfigureArgs.ReferencedBlueprint = nullptr;
			AppliedFields.Add(Args::ReferenceAssetPath);
		}
		else
		{
			FString ReferencedLoadError;
			USMBlueprint* ReferencedBlueprint = LD::Assist::Utils::LoadStateMachineBlueprint(ReferencedAssetPath, ReferencedLoadError);
			if (!ReferencedBlueprint)
			{
				return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Could not load 'reference_asset_path': %s"), *ReferencedLoadError));
			}
			if (ReferencedBlueprint == Blueprint)
			{
				return FSMAssistOperationResult::MakeError(TEXT("A state machine blueprint cannot reference itself."));
			}
			ConfigureArgs.ReferencedBlueprint = ReferencedBlueprint;
			AppliedFields.Add(Args::ReferenceAssetPath);
		}
	}

	bool bUseIntermediateGraph = false;
	if (InArgs->TryGetBoolField(Args::UseIntermediateGraph, bUseIntermediateGraph))
	{
		ConfigureArgs.bUseIntermediateGraph = bUseIntermediateGraph;
		AppliedFields.Add(Args::UseIntermediateGraph);
	}

	if (AppliedFields.Num() == 0)
	{
		return FSMAssistOperationResult::MakeError(TEXT("configure_reference requires at least one of 'reference_asset_path' or 'use_intermediate_graph'."));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	if (!GraphGen->ConfigureStateMachineReference(RefNode, ConfigureArgs))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("ConfigureStateMachineReference returned false on node '%s'."), *NodeGuidStr));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, RefNode->NodeGuid.ToString());
	if (USMBlueprint* CurrentRef = RefNode->GetStateMachineReference())
	{
		Payload->SetStringField(Args::ReferenceAssetPath, CurrentRef->GetPathName());
	}
	Payload->SetBoolField(Args::UseIntermediateGraph, RefNode->ShouldUseIntermediateGraph());
	TArray<TSharedPtr<FJsonValue>> AppliedJson;
	for (const FString& Field : AppliedFields)
	{
		AppliedJson.Add(MakeShared<FJsonValueString>(Field));
	}
	Payload->SetArrayField(Args::Applied, AppliedJson);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddAnyState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	USMGraph* TargetGraph = nullptr;
	{
		FString TargetGraphError;
		if (!LD::Assist::Private::ResolveTargetStateMachineGraph(InArgs, Blueprint, TargetGraph, TargetGraphError))
		{
			return FSMAssistOperationResult::MakeError(TargetGraphError);
		}
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;
	CreateArgs.GraphOwner = TargetGraph;
	CreateArgs.GraphNodeClass = USMGraphNode_AnyStateNode::StaticClass();
	// Any State has no bound graph and takes no node instance class. Leaving the struct
	// default (USMStateInstance) trips an ensure in FSMGraphSchemaAction_NewNode::PerformAction.
	CreateArgs.StateInstanceClass = nullptr;

	FString StateName;
	if (InArgs->TryGetStringField(Args::StateName, StateName))
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(StateName, TEXT("state_name"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}
		CreateArgs.StateName = StateName;
	}

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	USMGraphNode_StateNodeBase* StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create any state node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddLinkState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LinkToName;
	if (!InArgs->TryGetStringField(Args::LinkToStateName, LinkToName) || LinkToName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'link_to_state_name'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	USMGraph* TargetGraph = nullptr;
	{
		FString TargetGraphError;
		if (!LD::Assist::Private::ResolveTargetStateMachineGraph(InArgs, Blueprint, TargetGraph, TargetGraphError))
		{
			return FSMAssistOperationResult::MakeError(TargetGraphError);
		}
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;
	CreateArgs.GraphOwner = TargetGraph;
	CreateArgs.GraphNodeClass = USMGraphNode_LinkStateNode::StaticClass();
	// Link State has no bound graph and takes no node instance class. Leaving the struct
	// default (USMStateInstance) trips an ensure in FSMGraphSchemaAction_NewNode::PerformAction.
	CreateArgs.StateInstanceClass = nullptr;

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	USMGraphNode_StateNodeBase* StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create link state node."));
	}

	USMGraphNode_LinkStateNode* LinkNode = Cast<USMGraphNode_LinkStateNode>(StateNode);
	if (!LinkNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Created node was not a link state node."));
	}

	TArray<USMGraphNode_StateNodeBase*> Available;
	LinkNode->GetAvailableStatesToLink(Available);
	const bool bTargetExists = Available.ContainsByPredicate(
		[&LinkToName](const USMGraphNode_StateNodeBase* InState)
		{
			return InState && InState->GetStateName() == LinkToName;
		});

	if (!bTargetExists)
	{
		GraphGen->RemoveNode(LinkNode);
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("No state named '%s' is available to link in this graph."), *LinkToName));
	}

	LinkNode->LinkToState(LinkToName);

	if (!LinkNode->IsLinkedStateValid())
	{
		GraphGen->RemoveNode(LinkNode);
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Failed to link to state '%s'."), *LinkToName));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, LinkNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, LinkNode->GetStateName());
	if (const USMGraphNode_StateNodeBase* LinkedState = LinkNode->GetLinkedState())
	{
		Payload->SetStringField(Args::LinkedStateGuid, LinkedState->NodeGuid.ToString());
		Payload->SetStringField(Args::LinkToStateName, LinkedState->GetStateName());
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static TSharedRef<FJsonObject> DescribeProperty(const FProperty* InProperty, const void* InContainer, const UObject* InOwner, int32 InMaxDepth);

	// Additive: appends struct 'members' / array 'elements'; the flat exported value is left intact.
	static void AppendStructuredValue(const TSharedRef<FJsonObject>& InEntry, const FProperty* InProperty, const void* InValuePtr, const UObject* InOwner, int32 InMaxDepth)
	{
		if (InMaxDepth <= 0)
		{
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
		{
			TArray<TSharedPtr<FJsonValue>> Members;
			for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			{
				Members.Add(MakeShared<FJsonValueObject>(DescribeProperty(*It, InValuePtr, InOwner, InMaxDepth - 1)));
			}
			InEntry->SetArrayField(Args::Members, Members);
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
		{
			FScriptArrayHelper Helper(ArrayProperty, InValuePtr);
			TArray<TSharedPtr<FJsonValue>> Elements;
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				const void* ElementPtr = Helper.GetRawPtr(Index);
				const TSharedRef<FJsonObject> Element = MakeShared<FJsonObject>();
				Element->SetStringField(Args::Type, ArrayProperty->Inner->GetCPPType());

				FString ElementValue;
				ArrayProperty->Inner->ExportTextItem_Direct(ElementValue, ElementPtr, nullptr, const_cast<UObject*>(InOwner), PPF_None);
				Element->SetStringField(Args::Value, ElementValue);

				AppendStructuredValue(Element, ArrayProperty->Inner, ElementPtr, InOwner, InMaxDepth - 1);
				Elements.Add(MakeShared<FJsonValueObject>(Element));
			}
			InEntry->SetArrayField(Args::Elements, Elements);
		}
	}

	static TSharedRef<FJsonObject> DescribeProperty(const FProperty* InProperty, const void* InContainer, const UObject* InOwner, int32 InMaxDepth)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(Args::Name, InProperty->GetName());
		Entry->SetStringField(Args::Type, InProperty->GetCPPType());

		const FString Category = InProperty->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty())
		{
			Entry->SetStringField(Args::Category, Category);
		}

		const void* ValuePtr = InProperty->ContainerPtrToValuePtr<void>(InContainer);

		FString ValueString;
		InProperty->ExportTextItem_Direct(ValueString, ValuePtr, nullptr, const_cast<UObject*>(InOwner), PPF_None);
		Entry->SetStringField(Args::Value, ValueString);

		AppendStructuredValue(Entry, InProperty, ValuePtr, InOwner, InMaxDepth);
		return Entry;
	}
}

FSMAssistOperationResult LD::Assist::GetNodeProperties(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	int32 StackIndex = INDEX_NONE;
	const bool bHasStackIndex = InArgs->TryGetNumberField(Args::StackIndex, StackIndex) && StackIndex >= 0;

	USMNodeInstance* Template = bHasStackIndex ? Node->GetTemplateFromIndex(StackIndex) : Node->GetNodeTemplate();
	if (!Template)
	{
		if (bHasStackIndex)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("No stack template at index %d on node '%s'."), StackIndex, *NodeGuidStr));
		}
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' has no template instance."), *NodeGuidStr));
	}

	int32 MaxDepth = 0;
	InArgs->TryGetNumberField(Args::MaxDepth, MaxDepth);

	TArray<TSharedPtr<FJsonValue>> Properties;
	for (TFieldIterator<FProperty> It(Template->GetClass(), EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		if (LD::Assist::Private::IsPropertyHiddenOnInstanceTemplate(Property, Template))
		{
			continue;
		}
		Properties.Add(MakeShared<FJsonValueObject>(LD::Assist::Private::DescribeProperty(Property, Template, Template, MaxDepth)));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	Payload->SetStringField(Args::StateClass, Template->GetClass()->GetPathName());
	if (bHasStackIndex)
	{
		Payload->SetNumberField(Args::StackIndex, StackIndex);
	}
	Payload->SetArrayField(Args::Properties, Properties);
	Payload->SetNumberField(Args::Count, Properties.Num());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SetTransitionCondition(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString TransitionGuidStr;
	if (!InArgs->TryGetStringField(Args::TransitionGuid, TransitionGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'transition_guid'."));
	}

	bool bCondition = false;
	if (!InArgs->TryGetBoolField(Args::Condition, bCondition))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'condition' (boolean)."));
	}

	FGuid TransitionGuid;
	if (!FGuid::Parse(TransitionGuidStr, TransitionGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'transition_guid' '%s'."), *TransitionGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, TransitionGuid);
	USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node);
	if (!TransitionEdge)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' is not a transition edge."), *TransitionGuidStr));
	}

	USMTransitionGraph* TransitionGraph = TransitionEdge->GetTransitionGraph();
	if (!TransitionGraph || !TransitionGraph->ResultNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Transition '%s' has no result node to configure."), *TransitionGuidStr));
	}

	UEdGraphPin* EvaluationPin = TransitionGraph->ResultNode->GetTransitionEvaluationPin();
	if (!EvaluationPin)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Transition '%s' has no evaluation pin."), *TransitionGuidStr));
	}

	const UEdGraphSchema* Schema = TransitionGraph->GetSchema();
	if (!Schema)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Transition '%s' graph has no schema."), *TransitionGuidStr));
	}

	Schema->TrySetDefaultValue(*EvaluationPin, bCondition ? TEXT("True") : TEXT("False"));

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::TransitionGuid, TransitionGuidStr);
	Payload->SetBoolField(Args::Condition, bCondition);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::RuntimeGetState(const TSharedRef<FJsonObject>& InArgs)
{
	FString ActorIdentifier;
	if (!InArgs->TryGetStringField(Args::ActorIdentifier, ActorIdentifier) || ActorIdentifier.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'actor_identifier'."));
	}

	int32 PieInstance = 0;
	InArgs->TryGetNumberField(Args::PieInstance, PieInstance);

	bool bIncludeProperties = false;
	InArgs->TryGetBoolField(Args::IncludeProperties, bIncludeProperties);

	int32 MaxDepth = 0;
	InArgs->TryGetNumberField(Args::MaxDepth, MaxDepth);

	FString ComponentName;
	InArgs->TryGetStringField(Args::ComponentName, ComponentName);

	FString WorldError;
	UWorld* PieWorld = LD::Assist::Utils::GetActivePIEWorld(PieInstance, WorldError);
	if (!PieWorld)
	{
		return FSMAssistOperationResult::MakeError(WorldError);
	}

	AActor* Actor = LD::Assist::Utils::FindActorByIdentifier(PieWorld, ActorIdentifier);
	if (!Actor)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("No actor matching '%s' in the running PIE world."), *ActorIdentifier));
	}

	USMStateMachineComponent* Component = nullptr;
	if (!ComponentName.IsEmpty())
	{
		for (UActorComponent* Candidate : Actor->GetComponents())
		{
			if (Candidate && Candidate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				Component = Cast<USMStateMachineComponent>(Candidate);
				break;
			}
		}

		if (!Component)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Actor '%s' has no USMStateMachineComponent named '%s'."), *ActorIdentifier, *ComponentName));
		}
	}
	else
	{
		Component = Actor->FindComponentByClass<USMStateMachineComponent>();
		if (!Component)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Actor '%s' has no USMStateMachineComponent."), *ActorIdentifier));
		}
	}

	USMInstance* Instance = Component->GetInstance();
	if (!Instance)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Component '%s' on actor '%s' has no live state machine instance (not initialized)."),
				*Component->GetName(), *ActorIdentifier));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::Actor, Actor->GetActorNameOrLabel());
	Payload->SetStringField(Args::Component, Component->GetName());
	Payload->SetBoolField(Args::IsActive, Instance->IsActive());
	Payload->SetBoolField(Args::IsInEndState, Instance->IsInEndState());

	if (const USMStateInstance_Base* SingleActive = Instance->GetSingleActiveStateInstance())
	{
		Payload->SetStringField(Args::SingleActiveState, SingleActive->GetNodeName());
	}

	TArray<USMStateInstance_Base*> ActiveStates;
	Instance->GetAllActiveStateInstances(ActiveStates);

	TArray<TSharedPtr<FJsonValue>> ActiveStatesJson;
	ActiveStatesJson.Reserve(ActiveStates.Num());
	for (USMStateInstance_Base* StateInstance : ActiveStates)
	{
		if (!StateInstance)
		{
			continue;
		}

		const TSharedRef<FJsonObject> StateJson = MakeShared<FJsonObject>();
		StateJson->SetStringField(Args::StateName, StateInstance->GetNodeName());
		StateJson->SetStringField(Args::StateGuid, StateInstance->GetGuid().ToString());
		StateJson->SetStringField(Args::StateClass, StateInstance->GetClass()->GetPathName());
		StateJson->SetBoolField(Args::IsActive, StateInstance->IsActive());

		if (bIncludeProperties)
		{
			TArray<TSharedPtr<FJsonValue>> StateProperties;
			for (TFieldIterator<FProperty> It(StateInstance->GetClass(), EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated); It; ++It)
			{
				FProperty* Property = *It;
				if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
				{
					continue;
				}
				StateProperties.Add(MakeShared<FJsonValueObject>(LD::Assist::Private::DescribeProperty(Property, StateInstance, StateInstance, MaxDepth)));
			}
			StateJson->SetArrayField(Args::Properties, StateProperties);
		}

		ActiveStatesJson.Add(MakeShared<FJsonValueObject>(StateJson));
	}

	Payload->SetArrayField(Args::ActiveStates, ActiveStatesJson);
	Payload->SetNumberField(Args::Count, ActiveStatesJson.Num());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SetConduitCondition(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	bool bCondition = false;
	if (!InArgs->TryGetBoolField(Args::Condition, bCondition))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'condition' (boolean)."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	USMGraphNode_ConduitNode* ConduitNode = Cast<USMGraphNode_ConduitNode>(Node);
	if (!ConduitNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' is not a conduit."), *NodeGuidStr));
	}

	USMConduitGraph* ConduitGraph = Cast<USMConduitGraph>(ConduitNode->GetBoundGraph());
	if (!ConduitGraph || !ConduitGraph->ResultNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Conduit '%s' has no result node to configure."), *NodeGuidStr));
	}

	UEdGraphPin* EvaluationPin = ConduitGraph->ResultNode->GetTransitionEvaluationPin();
	if (!EvaluationPin)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Conduit '%s' has no evaluation pin."), *NodeGuidStr));
	}

	const UEdGraphSchema* Schema = ConduitGraph->GetSchema();
	if (!Schema)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Conduit '%s' graph has no schema."), *NodeGuidStr));
	}

	Schema->TrySetDefaultValue(*EvaluationPin, bCondition ? TEXT("True") : TEXT("False"));

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	Payload->SetBoolField(Args::Condition, bCondition);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SetNodeClass(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	// An omitted or empty node_class reverts the node to its default class.
	FString NodeClassPath;
	InArgs->TryGetStringField(Args::NodeClass, NodeClassPath);

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("No node found with guid '%s'."), *NodeGuidStr));
	}

	// A reference node's class is derived from the referenced blueprint's root, so setting it here
	// would be silently reverted by SetNodeClassFromReferenceTemplate on the next edit or compile.
	if (const USMGraphNode_StateMachineStateNode* SMNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
	{
		if (SMNode->IsStateMachineReference())
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(
				TEXT("Node '%s' is a state machine reference; its class is derived from the referenced blueprint and cannot be set directly."),
				*NodeGuidStr));
		}
	}

	const FName ClassPropertyName = Node->GetNodeClassPropertyName();
	if (ClassPropertyName == NAME_None)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Node '%s' has no assignable node class (entry, any-state, link, and reroute nodes have none)."),
			*NodeGuidStr));
	}

	const FClassProperty* ClassProperty = FindFProperty<FClassProperty>(Node->GetClass(), ClassPropertyName);
	UClass* RequiredBase = USMNodeInstance::StaticClass();
	if (ClassProperty && ClassProperty->MetaClass)
	{
		RequiredBase = ClassProperty->MetaClass;
	}

	UClass* ResolvedClass = nullptr;
	if (!NodeClassPath.IsEmpty())
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(NodeClassPath, TEXT("node_class"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}

		ResolvedClass = LoadClass<UObject>(nullptr, *NodeClassPath);
		if (!ResolvedClass)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not load 'node_class' '%s'."), *NodeClassPath));
		}

		if (!ResolvedClass->IsChildOf(RequiredBase))
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(
				TEXT("'node_class' '%s' is not a '%s' subclass, which this node requires."),
				*NodeClassPath, *RequiredBase->GetName()));
		}

		if (ResolvedClass->HasAnyClassFlags(CLASS_Abstract))
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(
				TEXT("'node_class' '%s' is abstract and cannot be assigned as a node class."), *NodeClassPath));
		}
	}

	{
		const FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistSetNodeClass", "Set Node Class (Assist)"));
		Blueprint->Modify();
		Node->Modify();
		Node->SetNodeClass(ResolvedClass);
		Node->CreateGraphPropertyGraphs();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	const UClass* AppliedClass = Node->GetNodeClass();
	if (!AppliedClass)
	{
		AppliedClass = Node->GetDefaultNodeClass();
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	if (AppliedClass)
	{
		Payload->SetStringField(Args::NodeClass, AppliedClass->GetPathName());
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SpawnActorContextComponent(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString TargetGraphPath;
	if (!InArgs->TryGetStringField(Args::TargetGraphPath, TargetGraphPath) || TargetGraphPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'target_graph_path'."));
	}

	FString ActorClassPath;
	if (!InArgs->TryGetStringField(Args::TargetActorClass, ActorClassPath) || ActorClassPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'target_actor_class'."));
	}

	FString ComponentClassPath;
	if (!InArgs->TryGetStringField(Args::ComponentClass, ComponentClassPath) || ComponentClassPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'component_class'."));
	}

	FString LoadError;
	UBlueprint* Blueprint = LD::Assist::Utils::LoadBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(ActorClassPath, TEXT("target_actor_class"), LengthError)
		|| !LD::Assist::Utils::IsWithinNameLength(ComponentClassPath, TEXT("component_class"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	UClass* ActorClass = LoadClass<AActor>(nullptr, *ActorClassPath);
	if (!ActorClass)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not load 'target_actor_class' '%s' as an AActor subclass."), *ActorClassPath));
	}

	UClass* ComponentClass = LoadClass<UActorComponent>(nullptr, *ComponentClassPath);
	if (!ComponentClass)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not load 'component_class' '%s' as a UActorComponent subclass."), *ComponentClassPath));
	}

	UEdGraph* TargetGraph = nullptr;
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && (Graph->GetPathName() == TargetGraphPath || Graph->GetName().Equals(TargetGraphPath, ESearchCase::IgnoreCase)))
		{
			TargetGraph = Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find graph '%s' on blueprint '%s'."), *TargetGraphPath, *AssetPath));
	}

	UFunction* GetContextFunction = USMNodeInstance::StaticClass()->FindFunctionByName(TEXT("GetContext"));
	UFunction* GetComponentFunction = AActor::StaticClass()->FindFunctionByName(TEXT("GetComponentByClass"));
	if (!GetContextFunction || !GetComponentFunction)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Could not resolve the GetContext or GetComponentByClass functions."));
	}

	int32 BaseX = 0;
	int32 BaseY = 0;
	InArgs->TryGetNumberField(Args::PositionX, BaseX);
	InArgs->TryGetNumberField(Args::PositionY, BaseY);

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// GetContext and GetComponentByClass are both const BlueprintCallable with a return value, so UHT promotes them
	// to BlueprintPure (no exec pins). The whole chain is a pure data cluster; the caller wires the returned
	// component pin downstream and the chain evaluates on demand.
	UK2Node_CallFunction* GetContextNode = nullptr;
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*TargetGraph);
		GetContextNode = Creator.CreateNode(false);
		GetContextNode->SetFromFunction(GetContextFunction);
		GetContextNode->NodePosX = BaseX;
		GetContextNode->NodePosY = BaseY;
		Creator.Finalize();
	}

	UK2Node_DynamicCast* CastNode = nullptr;
	{
		FGraphNodeCreator<UK2Node_DynamicCast> Creator(*TargetGraph);
		CastNode = Creator.CreateNode(false);
		CastNode->TargetType = ActorClass;
		CastNode->SetPurity(true);
		CastNode->NodePosX = BaseX + 280;
		CastNode->NodePosY = BaseY + 48;
		Creator.Finalize();
	}

	UK2Node_CallFunction* GetComponentNode = nullptr;
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*TargetGraph);
		GetComponentNode = Creator.CreateNode(false);
		GetComponentNode->SetFromFunction(GetComponentFunction);
		GetComponentNode->NodePosX = BaseX + 560;
		GetComponentNode->NodePosY = BaseY;
		Creator.Finalize();
	}

	if (UEdGraphPin* ComponentClassPin = GetComponentNode->FindPin(TEXT("ComponentClass"), EGPD_Input))
	{
		K2Schema->TrySetDefaultObject(*ComponentClassPin, ComponentClass);
		GetComponentNode->PinDefaultValueChanged(ComponentClassPin);
	}

	TArray<FString> WireFailures;
	const auto Connect = [&](UEdGraphPin* InA, UEdGraphPin* InB, const TCHAR* InLabel)
	{
		if (!InA || !InB || !K2Schema->TryCreateConnection(InA, InB))
		{
			WireFailures.Add(InLabel);
		}
	};

	Connect(GetContextNode->GetReturnValuePin(), CastNode->GetCastSourcePin(), TEXT("GetContext.ReturnValue -> Cast.Source"));
	Connect(CastNode->GetCastResultPin(), GetComponentNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input), TEXT("Cast.Result -> GetComponentByClass.Self"));

	// A reach chain whose wires did not connect delivers no value, so roll the cluster back and fail rather than
	// leave disconnected nodes behind.
	if (WireFailures.Num() > 0)
	{
		FBlueprintEditorUtils::RemoveNode(Blueprint, GetContextNode, true);
		FBlueprintEditorUtils::RemoveNode(Blueprint, CastNode, true);
		FBlueprintEditorUtils::RemoveNode(Blueprint, GetComponentNode, true);
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not wire the reach chain (%s); no nodes were created."), *FString::Join(WireFailures, TEXT("; "))));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UEdGraphPin* ComponentReturn = GetComponentNode->GetReturnValuePin();

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::GetContextNodeGuid, GetContextNode->NodeGuid.ToString());
	Payload->SetStringField(Args::CastNodeGuid, CastNode->NodeGuid.ToString());
	Payload->SetStringField(Args::GetComponentNodeGuid, GetComponentNode->NodeGuid.ToString());
	if (ComponentReturn)
	{
		Payload->SetStringField(Args::ComponentOutputPinId, ComponentReturn->PinId.ToString());
	}
	Payload->SetStringField(Args::TargetGraphPath, TargetGraph->GetPathName());

	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::GetGraphView(const TSharedRef<FJsonObject>& InArgs)
{
	check(IsInGameThread());

	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	bool bIncludeTransitions = true;
	InArgs->TryGetBoolField(Args::IncludeTransitions, bIncludeTransitions);

	bool bIncludePins = false;
	InArgs->TryGetBoolField(Args::IncludePins, bIncludePins);

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraph* TargetGraph = nullptr;
	{
		FString TargetGraphError;
		if (!LD::Assist::Private::ResolveTargetStateMachineGraph(InArgs, Blueprint, TargetGraph, TargetGraphError))
		{
			return FSMAssistOperationResult::MakeError(TargetGraphError);
		}
	}
	if (!TargetGraph)
	{
		TargetGraph = LD::Assist::Utils::GetRootStateMachineGraph(Blueprint);
		if (!TargetGraph)
		{
			return FSMAssistOperationResult::MakeError(TEXT("Blueprint has no root state machine graph."));
		}
	}

	FString EditorError;
	FBlueprintEditor* BlueprintEditor = LD::Assist::GraphView::FindOrOpenBlueprintEditor(Blueprint, EditorError);
	if (!BlueprintEditor)
	{
		return FSMAssistOperationResult::MakeError(EditorError);
	}

	// Widget sizes and overlaps are read off the focused panel, so the target graph has to be the one
	// on screen. Focusing a nested graph opens its tab exactly as double-clicking the container would.
	FString GraphError;
	TSharedPtr<SGraphEditor> GraphEditor = LD::Assist::GraphView::OpenAndFocusGraph(BlueprintEditor, TargetGraph, GraphError);
	if (!GraphEditor.IsValid())
	{
		return FSMAssistOperationResult::MakeError(GraphError);
	}

	SGraphPanel* Panel = GraphEditor->GetGraphPanel();
	if (!Panel)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Graph editor has no panel."));
	}

	// Every widget_size and overlap below is read straight off the node widgets.
	TArray<FString> MeasureWarnings;
	LD::Assist::GraphView::MeasureGraphNodeWidgets(GraphEditor.ToSharedRef(), Panel, TargetGraph, MeasureWarnings);

	// Map data nodes to their slate widgets so we can co-iterate. GetAllChildren includes off-viewport nodes,
	// which is what we want; GetChildren would only report the currently-visible ones.
	TMap<const UEdGraphNode*, TSharedRef<SGraphNode>> NodeToWidget;
	if (FChildren* AllChildren = Panel->GetAllChildren())
	{
		const int32 NumChildren = AllChildren->Num();
		for (int32 ChildIdx = 0; ChildIdx < NumChildren; ++ChildIdx)
		{
			TSharedRef<SWidget> Widget = AllChildren->GetChildAt(ChildIdx);
			TSharedRef<SGraphNode> NodeWidget = StaticCastSharedRef<SGraphNode>(Widget);
			if (UEdGraphNode* DataNode = Cast<UEdGraphNode>(NodeWidget->GetObjectBeingDisplayed()))
			{
				NodeToWidget.Add(DataNode, NodeWidget);
			}
		}
	}

	// Which transition each reroute node belongs to, and its place along that transition's rail counting
	// from the source state. Walked from the transitions because USMGraphNode_RerouteNode is MinimalAPI,
	// so its own chain accessors are not callable from here.
	//
	// IsPrimaryReroutedTransition is also true for a lone transition, so IsRerouted has to gate it or
	// every plain transition in the graph pays for a chain walk.
	struct FRerouteOwner
	{
		FGuid TransitionGuid;
		int32 ChainIndex = 0;
	};
	TMap<FGuid, FRerouteOwner> RerouteOwners;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node);
		if (!TransitionEdge || !TransitionEdge->IsRerouted() || !TransitionEdge->IsPrimaryReroutedTransition())
		{
			continue;
		}

		TArray<USMGraphNode_TransitionEdge*> ChainTransitions;
		TArray<USMGraphNode_RerouteNode*> ChainReroutes;
		TransitionEdge->GetAllReroutedTransitions(ChainTransitions, ChainReroutes);
		for (int32 ChainIdx = 0; ChainIdx < ChainReroutes.Num(); ++ChainIdx)
		{
			if (const USMGraphNode_RerouteNode* Reroute = ChainReroutes[ChainIdx])
			{
				RerouteOwners.Add(Reroute->NodeGuid, FRerouteOwner{ TransitionEdge->NodeGuid, ChainIdx });
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> TransitionsArray;

	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		const bool bIsTransition = Node->IsA<USMGraphNode_TransitionEdge>();
		if (bIsTransition && !bIncludeTransitions)
		{
			continue;
		}

		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(Args::NodeGuid, Node->NodeGuid.ToString());
		Entry->SetStringField(Args::Kind, LD::Assist::GraphView::ResolveNodeKind(Node));

		TArray<TSharedPtr<FJsonValue>> LogicalPosition;
		LogicalPosition.Add(MakeShared<FJsonValueNumber>(Node->NodePosX));
		LogicalPosition.Add(MakeShared<FJsonValueNumber>(Node->NodePosY));
		Entry->SetArrayField(Args::LogicalPosition, LogicalPosition);

		if (const TSharedRef<SGraphNode>* WidgetPtr = NodeToWidget.Find(Node))
		{
			const TSharedRef<SGraphNode>& NodeWidget = *WidgetPtr;
			Entry->SetArrayField(Args::WidgetPosition, LD::Assist::GraphView::Vec2fToJsonArray(NodeWidget->GetPosition2f()));
			Entry->SetArrayField(Args::WidgetSize, LD::Assist::GraphView::Vec2fToJsonArray(NodeWidget->GetDesiredSizeForMarquee2f()));
			Entry->SetStringField(Args::TitleText, NodeWidget->GetEditableNodeTitleAsText().ToString());
			Entry->SetArrayField(Args::BodyColor, LD::Assist::GraphView::ColorToJsonArray(NodeWidget->GetNodeBodyColor().GetSpecifiedColor()));
			Entry->SetArrayField(Args::TitleColor, LD::Assist::GraphView::ColorToJsonArray(NodeWidget->GetNodeTitleColor().GetSpecifiedColor()));
		}

		const FString NodeComment = Node->NodeComment;
		if (!NodeComment.IsEmpty())
		{
			Entry->SetStringField(Args::Comment, NodeComment);
		}

		Entry->SetBoolField(Args::IsSelected, Panel->SelectionManager.SelectedNodes.Contains(Node));

		if (bIsTransition)
		{
			if (USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node))
			{
				if (const USMGraphNode_StateNodeBase* FromState = TransitionEdge->GetFromState())
				{
					Entry->SetStringField(Args::FromStateGuid, FromState->NodeGuid.ToString());
				}
				if (const USMGraphNode_StateNodeBase* ToState = TransitionEdge->GetToState())
				{
					Entry->SetStringField(Args::ToStateGuid, ToState->NodeGuid.ToString());
				}

				// The segment fields, not from_state_guid and to_state_guid, say where a rerouted
				// transition is drawn: every segment of a chain reports the same two states.
				if (const USMGraphNode_StateNodeBase* SegmentFrom = TransitionEdge->GetFromState(/*bIncludeReroute=*/true))
				{
					Entry->SetStringField(Args::SegmentFromGuid, SegmentFrom->NodeGuid.ToString());
				}
				if (const USMGraphNode_StateNodeBase* SegmentTo = TransitionEdge->GetToState(/*bIncludeReroute=*/true))
				{
					Entry->SetStringField(Args::SegmentToGuid, SegmentTo->NodeGuid.ToString());
				}
				const USMGraphNode_TransitionEdge* PrimaryTransition = TransitionEdge->GetPrimaryReroutedTransition();
				Entry->SetStringField(Args::PrimaryTransitionGuid,
					(PrimaryTransition ? PrimaryTransition->NodeGuid : TransitionEdge->NodeGuid).ToString());
			}
		}
		else if (const FRerouteOwner* Owner = RerouteOwners.Find(Node->NodeGuid))
		{
			// A reroute whose chain could not be walked reaches neither branch, so it is emitted without
			// these two rather than with a guessed owner.
			Entry->SetStringField(Args::TransitionGuid, Owner->TransitionGuid.ToString());
			Entry->SetNumberField(Args::ChainIndex, Owner->ChainIndex);
		}

		if (bIncludePins)
		{
			TArray<TSharedPtr<FJsonValue>> Pins;
			LD::Assist::GraphView::BuildPinJson(Node, Pins);
			Entry->SetArrayField(Args::Pins, Pins);
		}

		if (bIsTransition)
		{
			TransitionsArray.Add(MakeShared<FJsonValueObject>(Entry));
		}
		else
		{
			NodesArray.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	// Reported so a caller can confirm a layout reads cleanly without capturing and looking at a PNG.
	TArray<TSharedPtr<FJsonValue>> OverlapsArray;
	TArray<TSharedPtr<FJsonValue>> TransitionOverlapsArray;
	LD::Assist::GraphView::BuildOverlapJson(TargetGraph, NodeToWidget, OverlapsArray, TransitionOverlapsArray);

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	for (const FString& Warning : MeasureWarnings)
	{
		WarningsArray.Add(MakeShared<FJsonValueString>(Warning));
	}

	const TSharedRef<FJsonObject> PanelView = MakeShared<FJsonObject>();
	{
		FVector2f ViewLocation = FVector2f::ZeroVector;
		float ZoomAmount = 1.0f;
		GraphEditor->GetViewLocation(ViewLocation, ZoomAmount);
		PanelView->SetNumberField(Args::Zoom, ZoomAmount);
		PanelView->SetArrayField(Args::ViewOffset, LD::Assist::GraphView::Vec2fToJsonArray(ViewLocation));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::GraphPath,
		LD::Assist::Private::FindGraphPathLabel(LD::Assist::Utils::GetRootStateMachineGraph(Blueprint), TargetGraph));
	Payload->SetObjectField(Args::PanelView, PanelView);
	Payload->SetArrayField(Args::Overlaps, OverlapsArray);
	Payload->SetArrayField(Args::TransitionOverlaps, TransitionOverlapsArray);
	Payload->SetArrayField(Args::MeasurementWarnings, WarningsArray);
	Payload->SetArrayField(Args::Nodes, NodesArray);
	if (bIncludeTransitions)
	{
		Payload->SetArrayField(Args::Transitions, TransitionsArray);
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::CaptureGraphView(const TSharedRef<FJsonObject>& InArgs)
{
	check(IsInGameThread());

	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	bool bClipToPanel = true;
	InArgs->TryGetBoolField(Args::ClipToPanel, bClipToPanel);

	bool bFitToContent = true;
	InArgs->TryGetBoolField(Args::FitToContent, bFitToContent);

	FString NodeGuidStr;
	const bool bHasNodeGuid = InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr) && !NodeGuidStr.IsEmpty();
	FGuid NodeGuid;
	if (bHasNodeGuid && !FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not parse node_guid '%s'."), *NodeGuidStr));
	}

	FString OutputSubdir = TEXT("LogicDriver");
	InArgs->TryGetStringField(Args::OutputSubdir, OutputSubdir);

	FString Prefix;
	InArgs->TryGetStringField(Args::Prefix, Prefix);

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString EditorError;
	FBlueprintEditor* BlueprintEditor = LD::Assist::GraphView::FindOrOpenBlueprintEditor(Blueprint, EditorError);
	if (!BlueprintEditor)
	{
		return FSMAssistOperationResult::MakeError(EditorError);
	}

	USMGraph* TargetGraph = nullptr;
	{
		FString TargetGraphError;
		if (!LD::Assist::Private::ResolveTargetStateMachineGraph(InArgs, Blueprint, TargetGraph, TargetGraphError))
		{
			return FSMAssistOperationResult::MakeError(TargetGraphError);
		}
	}

	FString DefaultPrefixName = Blueprint->GetName();
	if (TargetGraph)
	{
		DefaultPrefixName = FString::Printf(TEXT("%s_%s"), *Blueprint->GetName(), *TargetGraph->GetName());
	}
	else
	{
		TargetGraph = LD::Assist::Utils::GetRootStateMachineGraph(Blueprint);
		if (!TargetGraph)
		{
			return FSMAssistOperationResult::MakeError(TEXT("Blueprint has no root state machine graph."));
		}
	}

	USMGraphNode_Base* FocusNode = nullptr;
	if (bHasNodeGuid)
	{
		FocusNode = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
		if (!FocusNode)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
		}

		// FindNodeByGuid searches every graph in the asset, so the node can belong to a different graph
		// than the one being captured. Framing looks the node up on the captured graph's panel, finds no
		// widget, and gives up, which would return a PNG of that graph at whatever view it happened to
		// hold under a success status.
		if (FocusNode->GetGraph() != TargetGraph)
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(
				TEXT("Node '%s' is not in the graph being captured. Omit 'parent_state_guid' to capture the graph that owns it, or pass the guid of that graph's own state machine node."),
				*NodeGuidStr));
		}
	}

	return LD::Assist::GraphView::CaptureGraphToPng(
		BlueprintEditor, TargetGraph, Blueprint, FocusNode,
		bClipToPanel, bFitToContent, OutputSubdir, Prefix, DefaultPrefixName);
}

FSMAssistOperationResult LD::Assist::ClearScreenshots(const TSharedRef<FJsonObject>& InArgs)
{
	check(IsInGameThread());

	FString OutputSubdir = TEXT("LogicDriver");
	InArgs->TryGetStringField(Args::OutputSubdir, OutputSubdir);

	double OlderThanSeconds = 0.0;
	const bool bHasAgeFilter = InArgs->TryGetNumberField(Args::OlderThanSeconds, OlderThanSeconds) && OlderThanSeconds > 0.0;

	bool bDryRun = false;
	InArgs->TryGetBoolField(Args::DryRun, bDryRun);

	FString TargetDir;
	FString PathError;
	if (!LD::Assist::Utils::ResolveContainedScreenshotsDir(OutputSubdir, TargetDir, PathError))
	{
		return FSMAssistOperationResult::MakeError(PathError);
	}

	// An empty or root-resolving subdir ("" / "." / "sub/..") targets the whole Saved/Screenshots
	// directory and would delete manual F9 and high-res captures alongside tool output.
	FString ScreenshotsRoot;
	FString RootError;
	LD::Assist::Utils::ResolveContainedScreenshotsDir(FString(), ScreenshotsRoot, RootError);
	if (TargetDir == ScreenshotsRoot)
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("'output_subdir' must name a subdirectory under Saved/Screenshots; deleting from the root is not allowed."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::Directory, TargetDir);
	Payload->SetBoolField(Args::DryRun, bDryRun);

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*TargetDir))
	{
		Payload->SetNumberField(Args::DeletedCount, 0);
		Payload->SetNumberField(Args::FreedBytes, 0);
		Payload->SetArrayField(Args::Paths, TArray<TSharedPtr<FJsonValue>>());
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	TArray<FString> RelativeFiles;
	const FString PngFilter = FPaths::Combine(TargetDir, TEXT("*.png"));
	FileManager.FindFiles(RelativeFiles, *PngFilter, /*Files=*/true, /*Directories=*/false);

	const FDateTime Now = FDateTime::UtcNow();

	int32 RemovedCount = 0;
	int64 FreedBytes = 0;
	TArray<TSharedPtr<FJsonValue>> RemovedPaths;
	RemovedPaths.Reserve(RelativeFiles.Num());

	for (const FString& RelativeFile : RelativeFiles)
	{
		const FString AbsolutePath = FPaths::Combine(TargetDir, RelativeFile);

		if (bHasAgeFilter)
		{
			const FDateTime FileTime = FileManager.GetTimeStamp(*AbsolutePath);
			if (FileTime == FDateTime::MinValue())
			{
				continue;
			}
			const FTimespan Age = Now - FileTime;
			if (Age.GetTotalSeconds() < OlderThanSeconds)
			{
				continue;
			}
		}

		const int64 FileBytes = FileManager.FileSize(*AbsolutePath);

		if (!bDryRun)
		{
			if (!FileManager.Delete(*AbsolutePath, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true))
			{
				continue;
			}
		}

		++RemovedCount;
		if (FileBytes > 0)
		{
			FreedBytes += FileBytes;
		}
		RemovedPaths.Add(MakeShared<FJsonValueString>(AbsolutePath));
	}

	Payload->SetNumberField(Args::DeletedCount, RemovedCount);
	Payload->SetNumberField(Args::FreedBytes, FreedBytes);
	Payload->SetArrayField(Args::Paths, RemovedPaths);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static FGuid FindEntryStateGuid(USMGraph* InGraph)
	{
		if (!InGraph)
		{
			return FGuid();
		}
		USMGraphNode_StateMachineEntryNode* EntryNode = InGraph->GetEntryNode();
		if (!EntryNode)
		{
			return FGuid();
		}
		const UEdGraphPin* OutputPin = EntryNode->GetOutputPin();
		if (!OutputPin)
		{
			return FGuid();
		}
		for (UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
		{
			if (!LinkedPin)
			{
				continue;
			}
			if (USMGraphNode_StateNodeBase* State = Cast<USMGraphNode_StateNodeBase>(LinkedPin->GetOwningNode()))
			{
				return State->NodeGuid;
			}
		}
		return FGuid();
	}

	// Measured size of one node, or a zero vector when the panel holds no widget for it. The layout
	// module substitutes a per-kind default for a zero, so a caller never has to.
	static FVector2f MeasuredSize(
		const UEdGraphNode* InNode,
		const TMap<const UEdGraphNode*, TSharedRef<SGraphNode>>* InNodeToWidget)
	{
		if (!InNodeToWidget)
		{
			return FVector2f::ZeroVector;
		}
		if (const TSharedRef<SGraphNode>* WidgetPtr = InNodeToWidget->Find(InNode))
		{
			return (*WidgetPtr)->GetDesiredSizeForMarquee2f();
		}
		return FVector2f::ZeroVector;
	}

	// Translate one USMGraph into a Layout::FLayoutInput. NodeToWidget supplies measured widget
	// sizes for nodes whose Slate widget exists; the algorithm falls back to a per-kind default
	// table for the rest. Comment and reroute nodes are filtered out; only flow nodes (state,
	// conduit, reference, link_state, any_state) are passed to the algorithm. Transitions become
	// FLayoutEdge entries. The entry node is passed as a rectangle rather than as a node. It is never
	// moved and has no place in the layering, but the flow has to start clear of it.
	static void BuildLayoutInputForGraph(
		USMGraph* InGraph,
		const TMap<const UEdGraphNode*, TSharedRef<SGraphNode>>* InNodeToWidget,
		LD::Assist::Layout::FLayoutInput& OutInput)
	{
		if (!InGraph)
		{
			return;
		}
		OutInput.EntryGuid = FindEntryStateGuid(InGraph);

		for (UEdGraphNode* Node : InGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			if (Node->IsA<USMGraphNode_StateMachineEntryNode>())
			{
				OutInput.EntryNodePosition = FVector2f(static_cast<float>(Node->NodePosX), static_cast<float>(Node->NodePosY));
				OutInput.EntryNodeSize = MeasuredSize(Node, InNodeToWidget);
				if (OutInput.EntryNodeSize.X <= 0.0f || OutInput.EntryNodeSize.Y <= 0.0f)
				{
					// The entry node draws a fixed body: a border around a pin box with 10 units of
					// padding, no title and no property rows. Its size does not vary with content, so
					// a constant is a safe stand-in on a pass where its widget was not realized.
					OutInput.EntryNodeSize = FVector2f(48.0f, 40.0f);
				}
				continue;
			}
			if (Node->IsA<USMGraphNode_RerouteNode>())
			{
				continue;
			}
			if (Node->IsA<UEdGraphNode_Comment>())
			{
				continue;
			}
			if (USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node))
			{
				// A rerouted transition is a chain of transition edge nodes, and every segment reports the
				// same two states. Only the primary segment contributes an edge, so one logical edge gets
				// one rail. IsPrimaryReroutedTransition is also true for a lone transition, so a lone
				// transition whose bound graph is missing still contributes its edge.
				if (TransitionEdge->IsRerouted() && !TransitionEdge->IsPrimaryReroutedTransition())
				{
					continue;
				}
				const USMGraphNode_StateNodeBase* From = TransitionEdge->GetFromState();
				const USMGraphNode_StateNodeBase* To = TransitionEdge->GetToState();
				if (From && To && From != To)
				{
					LD::Assist::Layout::FLayoutEdge Edge;
					Edge.FromGuid = From->NodeGuid;
					Edge.ToGuid = To->NodeGuid;
					Edge.TransitionGuid = TransitionEdge->NodeGuid;
					OutInput.Edges.Add(Edge);
				}
				continue;
			}

			LD::Assist::Layout::FLayoutNode LayoutNode;
			LayoutNode.Node = Node;
			LayoutNode.NodeGuid = Node->NodeGuid;
			LayoutNode.Name = Node->GetNodeTitle(ENodeTitleType::EditableTitle).ToString();
			LayoutNode.Kind = LD::Assist::GraphView::ResolveNodeKind(Node);
			LayoutNode.OldPosition = FVector2f(static_cast<float>(Node->NodePosX), static_cast<float>(Node->NodePosY));
			LayoutNode.WidgetSize = MeasuredSize(Node, InNodeToWidget);
			OutInput.Nodes.Add(MoveTemp(LayoutNode));
		}
	}

	static TSharedRef<FJsonValue> Vec2fToJsonArrayValue(const FVector2f& InVec)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Add(MakeShared<FJsonValueNumber>(InVec.X));
		Array.Add(MakeShared<FJsonValueNumber>(InVec.Y));
		return MakeShared<FJsonValueArray>(Array);
	}

	// Apply NodePosX / NodePosY for a single node via the property pipeline so listeners and
	// dependent UI refresh the same way a details-panel edit would. Wraps Modify() for transaction
	// support; caller is responsible for the surrounding FScopedTransaction.
	static bool ApplyNodePosition(UEdGraphNode* InNode, const FVector2f& InNewPosition)
	{
		if (!InNode)
		{
			return false;
		}
		FProperty* PosXProperty = InNode->GetClass()->FindPropertyByName(TEXT("NodePosX"));
		FProperty* PosYProperty = InNode->GetClass()->FindPropertyByName(TEXT("NodePosY"));
		if (!PosXProperty || !PosYProperty)
		{
			return false;
		}
		InNode->Modify();
		LD::Editor::PropertyUtils::SetPropertyValue(
			PosXProperty,
			FString::FromInt(FMath::RoundToInt(InNewPosition.X)),
			InNode);
		LD::Editor::PropertyUtils::SetPropertyValue(
			PosYProperty,
			FString::FromInt(FMath::RoundToInt(InNewPosition.Y)),
			InNode);
		return true;
	}

	struct FLayoutGraphContext
	{
		USMGraph* Graph = nullptr;
		FString GraphPathLabel;
		FGuid ParentStateGuid;
		LD::Assist::Layout::FLayoutGraphResult Result;
		TMap<const UEdGraphNode*, TSharedRef<SGraphNode>> NodeToWidget;
		int32 ReroutesAdded = 0;
	};

	// Focus one graph, let its panel paint, and map every node in it to the widget that draws it. A node
	// widget reports a placeholder size until the panel has painted it, and the placeholder is several
	// times smaller than the rendered size. The layout spaces columns and rows by these sizes, so
	// measuring early packs the graph and overlaps every node that draws a body.
	//
	// Measuring pumps Slate, which dispatches user input, so the user can close the editor between two
	// calls. The editor is looked up on every call instead of being passed in, so a closed editor
	// becomes a warning rather than a dangling pointer.
	static bool MeasureGraphNodeSizes(
		USMBlueprint* InBlueprint,
		USMGraph* InGraph,
		TMap<const UEdGraphNode*, TSharedRef<SGraphNode>>& OutNodeToWidget,
		TArray<FString>& OutWarnings)
	{
		OutNodeToWidget.Reset();

		FBlueprintEditor* BlueprintEditor = LD::Assist::GraphView::FindOpenBlueprintEditor(InBlueprint);
		if (!BlueprintEditor)
		{
			OutWarnings.Add(FString::Printf(
				TEXT("The blueprint editor is no longer open, so graph '%s' was not measured; its nodes were spaced from default sizes."),
				*InGraph->GetName()));
			return false;
		}

		FString GraphError;
		const TSharedPtr<SGraphEditor> GraphEditor =
			LD::Assist::GraphView::OpenAndFocusGraph(BlueprintEditor, InGraph, GraphError);
		if (!GraphEditor.IsValid())
		{
			OutWarnings.Add(GraphError);
			return false;
		}

		SGraphPanel* Panel = GraphEditor->GetGraphPanel();
		if (!Panel)
		{
			OutWarnings.Add(FString::Printf(
				TEXT("Graph '%s' has no panel to measure; its nodes were spaced from default sizes."),
				*InGraph->GetName()));
			return false;
		}

		LD::Assist::GraphView::MeasureGraphNodeWidgets(GraphEditor.ToSharedRef(), Panel, InGraph, OutWarnings);

		if (FChildren* AllChildren = Panel->GetAllChildren())
		{
			const int32 NumChildren = AllChildren->Num();
			for (int32 ChildIdx = 0; ChildIdx < NumChildren; ++ChildIdx)
			{
				const TSharedRef<SWidget> Widget = AllChildren->GetChildAt(ChildIdx);
				const TSharedRef<SGraphNode> NodeWidget = StaticCastSharedRef<SGraphNode>(Widget);
				if (UEdGraphNode* DataNode = Cast<UEdGraphNode>(NodeWidget->GetObjectBeingDisplayed()))
				{
					OutNodeToWidget.Add(DataNode, NodeWidget);
				}
			}
		}
		return true;
	}

	// Bring each routed transition's reroute chain up to the length its plan asks for, then place every
	// reroute in the graph. A planned one goes to its planned point. A transition the plan does not
	// name has its reroutes spread along the straight line between its two states. A reroute the user
	// added then keeps following its wire after the states move.
	//
	// Nothing is ever removed. A reroute in the graph may be the user's own. Nothing tells it apart
	// from one this op placed, so the op only adds and repositions.
	static bool ApplyReroutePlan(
		USMBlueprint* InBlueprint,
		ISMGraphGeneration* InGraphGen,
		USMGraph* InGraph,
		const LD::Assist::Layout::FLayoutGraphResult& InResult,
		float InRerouteSize,
		int32& OutAdded,
		TArray<FString>& OutWarnings)
	{
		bool bChanged = false;
		// Indexed by chain position, so the plan may list reroutes in any order.
		TMap<FGuid, TArray<FVector2f>> PlanByTransition;
		for (const LD::Assist::Layout::FLayoutReroute& Reroute : InResult.Reroutes)
		{
			TArray<FVector2f>& Chain = PlanByTransition.FindOrAdd(Reroute.TransitionGuid);
			if (Chain.Num() <= Reroute.ChainIndex)
			{
				Chain.SetNum(Reroute.ChainIndex + 1);
			}
			Chain[Reroute.ChainIndex] = Reroute.Position;
		}

		TMap<FGuid, FVector2f> CenterByNode;
		CenterByNode.Reserve(InResult.Nodes.Num());
		for (const LD::Assist::Layout::FLayoutNode& Node : InResult.Nodes)
		{
			CenterByNode.Add(Node.NodeGuid, Node.NewPosition + Node.WidgetSize * 0.5f);
		}

		// Snapshotted before any splice, because creating a reroute appends to InGraph->Nodes.
		TArray<USMGraphNode_TransitionEdge*> PrimaryTransitions;
		for (UEdGraphNode* Node : InGraph->Nodes)
		{
			USMGraphNode_TransitionEdge* Transition = Cast<USMGraphNode_TransitionEdge>(Node);
			if (Transition && Transition->IsPrimaryReroutedTransition())
			{
				PrimaryTransitions.Add(Transition);
			}
		}

		for (USMGraphNode_TransitionEdge* Transition : PrimaryTransitions)
		{
			if (!IsValid(Transition))
			{
				continue;
			}

			const TArray<FVector2f>* Plan = PlanByTransition.Find(Transition->NodeGuid);
			const int32 PlanNum = Plan ? Plan->Num() : 0;

			TArray<USMGraphNode_TransitionEdge*> Segments;
			TArray<USMGraphNode_RerouteNode*> Reroutes;
			Transition->GetAllReroutedTransitions(Segments, Reroutes);

			// A splice always lands immediately after the transition it is given, so appending to the
			// end of a chain means splicing off its last segment. Splicing off the primary every time
			// would build the chain backwards and cross the wire over itself.
			for (int32 AddIdx = Reroutes.Num(); AddIdx < PlanNum; ++AddIdx)
			{
				ISMGraphGeneration::FCreateTransitionRerouteArgs RerouteArgs;
				RerouteArgs.GraphOwner = InGraph;
				RerouteArgs.TransitionEdge = Segments.Num() > 0 ? Segments.Last() : Transition;
				RerouteArgs.NodePosition = FVector2D((*Plan)[AddIdx]);
				if (!InGraphGen->CreateTransitionReroute(InBlueprint, RerouteArgs))
				{
					OutWarnings.AddUnique(FString::Printf(
						TEXT("Could not create a reroute for transition '%s' in graph '%s', so its rail was left incomplete."),
						*Transition->NodeGuid.ToString(), *InGraph->GetName()));
					break;
				}
				++OutAdded;
				bChanged = true;
				Transition->GetAllReroutedTransitions(Segments, Reroutes);
			}

			if (Reroutes.Num() == 0)
			{
				continue;
			}

			// Reroutes past the end of the plan are the user's own. They still have to follow the states.
			// They are spread evenly along a run to the destination state. The run starts at the last
			// planned point, or at the source state when nothing was planned.
			const int32 SurplusNum = Reroutes.Num() - PlanNum;
			FVector2f SurplusStart = FVector2f::ZeroVector;
			FVector2f SurplusEnd = FVector2f::ZeroVector;
			bool bHasSurplusRun = false;
			if (SurplusNum > 0)
			{
				const USMGraphNode_StateNodeBase* FromState = Transition->GetFromState();
				const USMGraphNode_StateNodeBase* ToState = Transition->GetToState();
				const FVector2f* StartCenter = FromState ? CenterByNode.Find(FromState->NodeGuid) : nullptr;
				const FVector2f* EndCenter = ToState ? CenterByNode.Find(ToState->NodeGuid) : nullptr;
				if (StartCenter && EndCenter)
				{
					const FVector2f HalfReroute(InRerouteSize * 0.5f);
					SurplusStart = PlanNum > 0 ? (*Plan)[PlanNum - 1] + HalfReroute : *StartCenter;
					SurplusEnd = *EndCenter;
					bHasSurplusRun = true;
				}
			}

			for (int32 RerouteIdx = 0; RerouteIdx < Reroutes.Num(); ++RerouteIdx)
			{
				USMGraphNode_RerouteNode* Reroute = Reroutes[RerouteIdx];
				if (!IsValid(Reroute))
				{
					continue;
				}

				FVector2f Target = FVector2f::ZeroVector;
				if (RerouteIdx < PlanNum)
				{
					Target = (*Plan)[RerouteIdx];
				}
				else if (bHasSurplusRun)
				{
					const float Alpha = static_cast<float>(RerouteIdx - PlanNum + 1) / static_cast<float>(SurplusNum + 1);
					Target = FMath::Lerp(SurplusStart, SurplusEnd, Alpha) - FVector2f(InRerouteSize * 0.5f);
				}
				else
				{
					continue;
				}

				const FVector2f Current(static_cast<float>(Reroute->NodePosX), static_cast<float>(Reroute->NodePosY));
				if (Current.Equals(Target, 1.0f))
				{
					continue;
				}
				if (ApplyNodePosition(Reroute, Target))
				{
					bChanged = true;
				}
			}
		}

		return bChanged;
	}

	// The property the settling pass writes. It is protected and editor-only on USMTransitionInstance, so
	// it is reached by name rather than by GET_MEMBER_NAME_CHECKED. A rename is reported as a warning
	// rather than left to look like a graph that needed no adjustment.
	static const FName IconLocationPropertyName(TEXT("IconLocationPercentage"));

	// Spread the markers of transitions that still draw on top of each other along their own wires.
	// The editor draws a marker at the midpoint between the two states, and it only separates markers
	// that share the same from-state and to-state. Two edges between different pairs can still have the
	// same midpoint, and moving the states does not change that. IconLocationPercentage moves a marker
	// along its own segment, so it separates them in place.
	//
	// Runs last, after the states and the reroutes have their final positions. A rerouted transition is
	// skipped. Its rail already moved its marker. Each segment of a chain also copies its template from
	// the primary, so a value written here would be overwritten.
	static int32 SettleTransitionMarkers(
		USMBlueprint* InBlueprint,
		TArray<FLayoutGraphContext>& InOutContexts,
		TArray<FString>& OutWarnings)
	{
		int32 AdjustedCount = 0;

		for (FLayoutGraphContext& Context : InOutContexts)
		{
			TArray<FString> GraphWarnings;
			MeasureGraphNodeSizes(InBlueprint, Context.Graph, Context.NodeToWidget, GraphWarnings);
			for (const FString& Warning : GraphWarnings)
			{
				OutWarnings.AddUnique(FString::Printf(TEXT("%s: %s"), *Context.GraphPathLabel, *Warning));
			}

			// The same set 'ld.get_graph_view' reports as transition_overlaps, so that every overlap it
			// reports is one this pass can act on. A rerouted transition takes part: each segment is its
			// own edge with its own template, and its marker slides along its own segment. A reroute node
			// takes part as an obstacle only. It is a positioned node the placement pass owns, so it
			// counts in the overlap test and never receives an icon percentage.
			struct FMarker
			{
				UEdGraphNode* Node = nullptr;
				USMGraphNode_TransitionEdge* Transition = nullptr;
				FString Title;
				FVector2f Min = FVector2f::ZeroVector;
				FVector2f Max = FVector2f::ZeroVector;
			};

			bool bAdjustedThisGraph = false;

			TArray<FMarker> Markers;
			for (UEdGraphNode* Node : Context.Graph->Nodes)
			{
				USMGraphNode_TransitionEdge* Transition = Cast<USMGraphNode_TransitionEdge>(Node);
				const bool bIsMovableMarker = Transition && Transition->GetNodeTemplateAs<USMTransitionInstance>();
				if (!bIsMovableMarker && !Node->IsA<USMGraphNode_RerouteNode>())
				{
					continue;
				}
				const TSharedRef<SGraphNode>* WidgetPtr = Context.NodeToWidget.Find(Node);
				if (!WidgetPtr)
				{
					continue;
				}

				FMarker Marker;
				Marker.Node = Node;
				Marker.Transition = bIsMovableMarker ? Transition : nullptr;
				Marker.Title = (*WidgetPtr)->GetEditableNodeTitleAsText().ToString();
				Marker.Min = (*WidgetPtr)->GetPosition2f();
				Marker.Max = Marker.Min + (*WidgetPtr)->GetDesiredSizeForMarquee2f();
				Markers.Add(MoveTemp(Marker));
			}

			// Markers that overlap, directly or through another marker, are spread as one group. Markers
			// stacked on one point form a clique. Three edges crossing between two layers form a chain
			// instead: the first and the last overlap the middle one but not each other. Treating that as
			// two pairs would leave the middle marker where it is. Union-find over the overlapping pairs
			// collects both shapes.
			TArray<int32> Parent;
			Parent.Reserve(Markers.Num());
			for (int32 MarkerIdx = 0; MarkerIdx < Markers.Num(); ++MarkerIdx)
			{
				Parent.Add(MarkerIdx);
			}

			auto FindRoot = [&Parent](int32 InIdx)
			{
				while (Parent[InIdx] != InIdx)
				{
					Parent[InIdx] = Parent[Parent[InIdx]];
					InIdx = Parent[InIdx];
				}
				return InIdx;
			};

			for (int32 FirstIdx = 0; FirstIdx < Markers.Num(); ++FirstIdx)
			{
				for (int32 SecondIdx = FirstIdx + 1; SecondIdx < Markers.Num(); ++SecondIdx)
				{
					const FMarker& First = Markers[FirstIdx];
					const FMarker& Second = Markers[SecondIdx];
					const bool bOverlaps = First.Min.X < Second.Max.X && Second.Min.X < First.Max.X
						&& First.Min.Y < Second.Max.Y && Second.Min.Y < First.Max.Y;
					if (!bOverlaps)
					{
						continue;
					}

					const int32 FirstRoot = FindRoot(FirstIdx);
					const int32 SecondRoot = FindRoot(SecondIdx);
					if (FirstRoot != SecondRoot)
					{
						Parent[SecondRoot] = FirstRoot;
					}
				}
			}

			TMap<int32, TArray<int32>> MarkersByRoot;
			for (int32 MarkerIdx = 0; MarkerIdx < Markers.Num(); ++MarkerIdx)
			{
				MarkersByRoot.FindOrAdd(FindRoot(MarkerIdx)).Add(MarkerIdx);
			}

			TArray<TArray<int32>> Groups;
			MarkersByRoot.GenerateValueArray(Groups);

			for (TArray<int32>& Group : Groups)
			{
				if (Group.Num() < 2)
				{
					continue;
				}

				// Sorted by guid so the same group gets the same result on every run.
				Group.Sort([&Markers](int32 A, int32 B)
				{
					return Markers[A].Node->NodeGuid < Markers[B].Node->NodeGuid;
				});

				// Where along the wires to spread the group. Transitions that all end at one state cannot
				// be separated by pushing their markers toward that state, because every wire arrives at
				// the same point; they separate near their sources, which are far apart. Transitions that
				// all leave one state are the mirror case. A mixed group has no shared end, so it spreads
				// around the midpoint.
				int32 SharedTargetCount = 0;
				int32 SharedSourceCount = 0;
				for (const int32 MemberIdx : Group)
				{
					USMGraphNode_TransitionEdge* Member = Markers[MemberIdx].Transition;
					if (!Member)
					{
						continue;
					}
					const USMGraphNode_StateNodeBase* ToState = Member->GetToState();
					const USMGraphNode_StateNodeBase* FromState = Member->GetFromState();
					for (const int32 OtherIdx : Group)
					{
						USMGraphNode_TransitionEdge* Other = Markers[OtherIdx].Transition;
						if (!Other || Other == Member)
						{
							continue;
						}
						if (ToState && Other->GetToState() == ToState)
						{
							++SharedTargetCount;
							break;
						}
						if (FromState && Other->GetFromState() == FromState)
						{
							++SharedSourceCount;
							break;
						}
					}
				}

				float SpanStart = 0.2f;
				float SpanEnd = 0.8f;
				if (SharedTargetCount > SharedSourceCount)
				{
					SpanStart = 0.15f;
					SpanEnd = 0.5f;
				}
				else if (SharedSourceCount > SharedTargetCount)
				{
					SpanStart = 0.5f;
					SpanEnd = 0.85f;
				}

				// Dividing by the gaps between members rather than by the member count keeps the span the
				// same for a pair as for a larger group. A pair divided by the count would use half of it.
				const float Step = (SpanEnd - SpanStart) / static_cast<float>(FMath::Max(1, Group.Num() - 1));
				for (int32 PositionIdx = 0; PositionIdx < Group.Num(); ++PositionIdx)
				{
					USMGraphNode_TransitionEdge* Transition = Markers[Group[PositionIdx]].Transition;
					if (!Transition)
					{
						continue;
					}
					USMTransitionInstance* Template = Transition->GetNodeTemplateAs<USMTransitionInstance>();
					FProperty* Property = Template
						? Template->GetClass()->FindPropertyByName(IconLocationPropertyName)
						: nullptr;
					if (!Property)
					{
						OutWarnings.AddUnique(FString::Printf(
							TEXT("No '%s' property on the transition template, so overlapping markers were left stacked."),
							*IconLocationPropertyName.ToString()));
						continue;
					}

					const float Percentage = FMath::Clamp(
						SpanStart + static_cast<float>(PositionIdx) * Step, 0.05f, 0.95f);

					Transition->Modify();
					Template->Modify();
					LD::Editor::PropertyUtils::SetPropertyValue(
						Property, FString::SanitizeFloat(Percentage), Template);
					++AdjustedCount;
					bAdjustedThisGraph = true;
				}
			}

			if (!bAdjustedThisGraph)
			{
				continue;
			}

			// A spread of 0.6 along each wire does not separate every group. Two markers on short wires
			// that cross at a shallow angle can still touch afterwards. Measure once more and name what
			// survived, so the payload does not report an adjustment the graph did not get.
			TArray<FString> RecheckWarnings;
			MeasureGraphNodeSizes(InBlueprint, Context.Graph, Context.NodeToWidget, RecheckWarnings);
			for (const FString& Warning : RecheckWarnings)
			{
				OutWarnings.AddUnique(FString::Printf(TEXT("%s: %s"), *Context.GraphPathLabel, *Warning));
			}

			for (int32 FirstIdx = 0; FirstIdx < Markers.Num(); ++FirstIdx)
			{
				for (int32 SecondIdx = FirstIdx + 1; SecondIdx < Markers.Num(); ++SecondIdx)
				{
					const TSharedRef<SGraphNode>* FirstWidget = Context.NodeToWidget.Find(Markers[FirstIdx].Node);
					const TSharedRef<SGraphNode>* SecondWidget = Context.NodeToWidget.Find(Markers[SecondIdx].Node);
					if (!FirstWidget || !SecondWidget)
					{
						continue;
					}

					const FVector2f FirstMin = (*FirstWidget)->GetPosition2f();
					const FVector2f FirstMax = FirstMin + (*FirstWidget)->GetDesiredSizeForMarquee2f();
					const FVector2f SecondMin = (*SecondWidget)->GetPosition2f();
					const FVector2f SecondMax = SecondMin + (*SecondWidget)->GetDesiredSizeForMarquee2f();
					const bool bStillOverlaps = FirstMin.X < SecondMax.X && SecondMin.X < FirstMax.X
						&& FirstMin.Y < SecondMax.Y && SecondMin.Y < FirstMax.Y;
					if (!bStillOverlaps)
					{
						continue;
					}

					OutWarnings.AddUnique(FString::Printf(
						TEXT("%s: '%s' and '%s' still overlap after the markers were spaced along their wires."),
						*Context.GraphPathLabel,
						*Markers[FirstIdx].Title,
						*Markers[SecondIdx].Title));
				}
			}
		}

		return AdjustedCount;
	}

	// Measure every graph again and report whether any of them still draws two flow nodes on top of each
	// other. Each context's widget map is replaced rather than reused, because a node that grew since the
	// last measurement is what this function detects.
	//
	// A graph that could not be measured contributes no overlaps, so a failure here would otherwise read
	// as a clean graph. Its warning goes to the caller instead of being dropped.
	static bool RemeasureAndFindOverlap(
		USMBlueprint* InBlueprint,
		TArray<FLayoutGraphContext>& InOutContexts,
		TArray<FString>& OutWarnings)
	{
		bool bAnyOverlap = false;
		for (FLayoutGraphContext& Context : InOutContexts)
		{
			TArray<FString> GraphWarnings;
			MeasureGraphNodeSizes(InBlueprint, Context.Graph, Context.NodeToWidget, GraphWarnings);
			for (const FString& Warning : GraphWarnings)
			{
				OutWarnings.AddUnique(FString::Printf(TEXT("%s: %s"), *Context.GraphPathLabel, *Warning));
			}

			TArray<TSharedPtr<FJsonValue>> NodeOverlaps;
			TArray<TSharedPtr<FJsonValue>> EdgeOverlaps;
			LD::Assist::GraphView::BuildOverlapJson(Context.Graph, Context.NodeToWidget, NodeOverlaps, EdgeOverlaps);
			if (NodeOverlaps.Num() > 0)
			{
				bAnyOverlap = true;
			}
		}
		return bAnyOverlap;
	}

	// Rendered size of a reroute node, taken from one already in the graph when there is one. Every
	// reroute measures the same (see Layout::DefaultRerouteSize), and that constant covers a graph that
	// has none yet. The router spaces its lanes by this.
	static float ResolveRerouteSize(const TMap<const UEdGraphNode*, TSharedRef<SGraphNode>>& InNodeToWidget)
	{
		for (const TPair<const UEdGraphNode*, TSharedRef<SGraphNode>>& Pair : InNodeToWidget)
		{
			if (!Pair.Key || !Pair.Key->IsA<USMGraphNode_RerouteNode>())
			{
				continue;
			}
			const FVector2f Size = Pair.Value->GetDesiredSizeForMarquee2f();
			if (Size.X > 0.0f && Size.Y > 0.0f)
			{
				return FMath::Max(Size.X, Size.Y);
			}
		}
		return LD::Assist::Layout::DefaultRerouteSize;
	}
}

FSMAssistOperationResult LD::Assist::LayoutStates(const TSharedRef<FJsonObject>& InArgs)
{
	check(IsInGameThread());

	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	LD::Assist::Layout::ELayoutStrategy Strategy = LD::Assist::Layout::ELayoutStrategy::LeftToRight;
	{
		FString StrategyStr;
		if (InArgs->TryGetStringField(Args::Strategy, StrategyStr) && !StrategyStr.IsEmpty())
		{
			if (!LD::Assist::Layout::TryParseStrategy(StrategyStr, Strategy))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Unknown strategy '%s'. Use 'left_to_right' or 'top_to_bottom'."), *StrategyStr));
			}
		}
	}

	bool bApply = false;
	InArgs->TryGetBoolField(Args::Apply, bApply);

	bool bScopeAll = false;
	{
		FString ScopeError;
		if (!LD::Assist::Private::TryParseGraphScope(InArgs, bScopeAll, ScopeError))
		{
			return FSMAssistOperationResult::MakeError(ScopeError);
		}
	}

	double ColumnGap = 80.0;
	InArgs->TryGetNumberField(Args::ColumnGap, ColumnGap);
	double RowGap = 40.0;
	InArgs->TryGetNumberField(Args::RowGap, RowGap);
	// With neither given, each graph is anchored off its own entry node: one column gap past it
	// along the flow axis, centered on it across. Giving either one pins the layout to that point
	// instead, for a caller placing a graph somewhere specific. Both axes move together, because
	// anchoring one axis to the entry node and the other to a fixed value reads as a mistake.
	double StartX = 0.0;
	double StartY = 0.0;
	const bool bHasStartX = InArgs->TryGetNumberField(Args::StartX, StartX);
	const bool bHasStartY = InArgs->TryGetNumberField(Args::StartY, StartY);
	const bool bStartExplicit = bHasStartX || bHasStartY;

	bool bRespectExistingOrder = true;
	InArgs->TryGetBoolField(Args::RespectExistingOrder, bRespectExistingOrder);
	bool bSnapToGrid = true;
	InArgs->TryGetBoolField(Args::SnapToGrid, bSnapToGrid);
	bool bRouteEdges = true;
	InArgs->TryGetBoolField(Args::RouteEdges, bRouteEdges);

	TSet<FGuid> PinnedGuids;
	{
		const TArray<TSharedPtr<FJsonValue>>* PinArray = nullptr;
		if (InArgs->TryGetArrayField(Args::PinNodeGuids, PinArray) && PinArray)
		{
			for (const TSharedPtr<FJsonValue>& Value : *PinArray)
			{
				FString GuidStr;
				if (Value.IsValid() && Value->TryGetString(GuidStr) && !GuidStr.IsEmpty())
				{
					FGuid Parsed;
					if (FGuid::Parse(GuidStr, Parsed))
					{
						PinnedGuids.Add(Parsed);
					}
				}
			}
		}
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraph* RootGraph = LD::Assist::Utils::GetRootStateMachineGraph(Blueprint);
	if (!RootGraph)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Blueprint has no root state machine graph."));
	}

	// 'parent_state_guid' moves the base of the layout to one nested state machine, and 'scope' keeps
	// its meaning against that base: 'root' lays out only that graph, 'all' lays out it and everything
	// nested under it. Without it there is no way to lay out a single nested graph, and a caller who
	// passes it expecting one, as ld.get_graph_view accepts it, gets the whole asset re-laid instead.
	USMGraph* BaseGraph = nullptr;
	{
		FString BaseGraphError;
		if (!LD::Assist::Private::ResolveTargetStateMachineGraph(InArgs, Blueprint, BaseGraph, BaseGraphError))
		{
			return FSMAssistOperationResult::MakeError(BaseGraphError);
		}
	}
	if (!BaseGraph)
	{
		BaseGraph = RootGraph;
	}

	FString EditorError;
	FBlueprintEditor* BlueprintEditor = LD::Assist::GraphView::FindOrOpenBlueprintEditor(Blueprint, EditorError);
	if (!BlueprintEditor)
	{
		return FSMAssistOperationResult::MakeError(EditorError);
	}

	// Snapshotted before any graph is opened. Focusing a graph makes it the focused graph, so a read
	// after the root graph is brought to front would always report the root graph. The restore at the
	// end would then move the caller to the root instead of leaving them where they were.
	UEdGraph* const FocusedGraphOnEntry = BlueprintEditor->GetFocusedGraph();

	// Only a check that the asset's own graph can be reached; RunLayoutPass focuses each graph itself.
	FString GraphError;
	if (!LD::Assist::GraphView::OpenAndFocusRootGraph(BlueprintEditor, Blueprint, GraphError).IsValid())
	{
		return FSMAssistOperationResult::MakeError(GraphError);
	}

	TArray<LD::Assist::Private::FStateMachineGraphEntry> GraphsToLayout;
	if (bScopeAll)
	{
		LD::Assist::Private::CollectStateMachineGraphs(BaseGraph, GraphsToLayout);
	}
	else
	{
		LD::Assist::Private::FStateMachineGraphEntry BaseEntry;
		BaseEntry.Graph = BaseGraph;
		BaseEntry.GraphPath = BaseGraph->GetName();
		GraphsToLayout.Add(MoveTemp(BaseEntry));
	}

	const float SnapGridSize = static_cast<float>(SNodePanel::GetSnapGridSize());

	TArray<FString> MeasureWarnings;

	// Work the op could not do, reported as the top-level 'skipped' array. Kept apart from
	// measurement_warnings. A caller reads those as nodes spaced against sizes that read too small, and
	// acts by re-running or by fixing the spacing by hand.
	TArray<FString> OpWarnings;

	TArray<LD::Assist::Private::FLayoutGraphContext> GraphContexts;
	GraphContexts.Reserve(GraphsToLayout.Num());
	for (const LD::Assist::Private::FStateMachineGraphEntry& GraphEntry : GraphsToLayout)
	{
		LD::Assist::Private::FLayoutGraphContext Context;
		Context.Graph = GraphEntry.Graph;
		Context.GraphPathLabel = GraphEntry.GraphPath;
		Context.ParentStateGuid = GraphEntry.ParentStateGuid;
		GraphContexts.Add(MoveTemp(Context));
	}

	// Measure every graph in scope and lay each one out from what it actually renders at. Each graph
	// is measured on its own panel, so a nested graph is spaced by the same real sizes as the root.
	auto RunLayoutPass = [&](TArray<FString>& OutWarnings)
	{
		for (LD::Assist::Private::FLayoutGraphContext& Context : GraphContexts)
		{
			TArray<FString> GraphMeasureWarnings;
			LD::Assist::Private::MeasureGraphNodeSizes(
				Blueprint, Context.Graph, Context.NodeToWidget, GraphMeasureWarnings);
			for (const FString& Warning : GraphMeasureWarnings)
			{
				OutWarnings.Add(FString::Printf(TEXT("%s: %s"), *Context.GraphPathLabel, *Warning));
			}

			LD::Assist::Layout::FLayoutInput Input;
			Input.Strategy = Strategy;
			Input.ColumnGap = static_cast<float>(ColumnGap);
			Input.RowGap = static_cast<float>(RowGap);
			Input.Start = FVector2f(static_cast<float>(StartX), static_cast<float>(StartY));
			Input.bStartExplicit = bStartExplicit;
			Input.PinnedGuids = PinnedGuids;
			Input.bRespectExistingOrder = bRespectExistingOrder;
			Input.bSnapToGrid = bSnapToGrid;
			Input.SnapGridSize = SnapGridSize > 0.0f ? SnapGridSize : 16.0f;
			Input.RerouteSize = LD::Assist::Private::ResolveRerouteSize(Context.NodeToWidget);
			Input.bRouteEdges = bRouteEdges;

			LD::Assist::Private::BuildLayoutInputForGraph(Context.Graph, &Context.NodeToWidget, Input);

			Context.Result = LD::Assist::Layout::ComputeLayout(Input);
		}
	};

	RunLayoutPass(MeasureWarnings);

	// Apply path: single transaction wraps all writes across all graphs so undo is one step.
	int32 PassesRun = 1;
	int32 IconAdjustments = 0;
	if (bApply)
	{
		FScopedTransaction Transaction(NSLOCTEXT("SMAssist", "LayoutStatesTransaction", "Auto-Layout States"));

		ISMGraphGeneration* GraphGen = nullptr;
		if (bRouteEdges)
		{
			FString GraphGenError;
			GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
			if (!GraphGen)
			{
				OpWarnings.Add(FString::Printf(
					TEXT("Edge routing was skipped and no reroute was placed: %s"), *GraphGenError));
			}
		}

		auto ApplyComputedLayout = [&]()
		{
			TSet<USMGraph*> DirtyGraphs;
			for (LD::Assist::Private::FLayoutGraphContext& Context : GraphContexts)
			{
				if (!IsValid(Context.Graph))
				{
					continue;
				}
				for (const LD::Assist::Layout::FLayoutNode& Node : Context.Result.Nodes)
				{
					if (!Node.Node || Node.bPinned)
					{
						continue;
					}
					if (Node.NewPosition == Node.OldPosition)
					{
						continue;
					}
					if (LD::Assist::Private::ApplyNodePosition(Node.Node, Node.NewPosition))
					{
						DirtyGraphs.Add(Context.Graph);
					}
				}

				if (GraphGen)
				{
					// Moving an existing reroute counts as a change, not only adding one. A re-run on a
					// graph that already has its rails repositions them and nothing else. Without the
					// dirty mark the panel would keep drawing them where they were.
					if (LD::Assist::Private::ApplyReroutePlan(
						Blueprint,
						GraphGen,
						Context.Graph,
						Context.Result,
						LD::Assist::Private::ResolveRerouteSize(Context.NodeToWidget),
						Context.ReroutesAdded,
						OpWarnings))
					{
						DirtyGraphs.Add(Context.Graph);
					}
				}
			}

			for (USMGraph* DirtyGraph : DirtyGraphs)
			{
				if (DirtyGraph)
				{
					DirtyGraph->NotifyGraphChanged();
				}
			}
			if (DirtyGraphs.Num() > 0)
			{
				Blueprint->GetPackage()->MarkPackageDirty();
				LD::Assist::GraphView::EnsureSlateLayoutReady();
			}
		};

		ApplyComputedLayout();

		// Measuring pumps Slate, which dispatches user input, so the editor can be closed between phases.
		// MeasureGraphNodeSizes guards itself. This check is what reports the skip instead of running a
		// second pass and the settling against an editor that is gone.
		auto IsEditorStillOpen = [Blueprint, BlueprintEditor]()
		{
			return LD::Assist::GraphView::FindOpenBlueprintEditor(Blueprint) == BlueprintEditor;
		};

		if (!IsEditorStillOpen())
		{
			OpWarnings.Add(TEXT("The blueprint editor closed while the layout was running, so the second pass and the transition-marker settling were skipped."));
		}
		else
		{
			// A node grows when its class or one of its exposed properties is set. A caller normally
			// does that in the same batch as the states. The sizes this pass spaced by were read before
			// those writes landed, so measure again and lay out once more when anything still overlaps.
			// Two passes is the cap: a third would only repeat the second, since nothing changes the
			// sizes between them.
			if (LD::Assist::Private::RemeasureAndFindOverlap(Blueprint, GraphContexts, MeasureWarnings))
			{
				TArray<FString> SecondPassWarnings;
				RunLayoutPass(SecondPassWarnings);
				for (const FString& Warning : SecondPassWarnings)
				{
					MeasureWarnings.AddUnique(Warning);
				}
				ApplyComputedLayout();
				++PassesRun;
			}

			if (IsEditorStillOpen())
			{
				IconAdjustments = LD::Assist::Private::SettleTransitionMarkers(
					Blueprint, GraphContexts, MeasureWarnings);
				if (IconAdjustments > 0)
				{
					Blueprint->GetPackage()->MarkPackageDirty();
					LD::Assist::GraphView::EnsureSlateLayoutReady();
				}
			}
		}
	}

	// The caller is left on the tab they started on, whatever measuring had to focus along the way. The
	// editor is looked up again because measuring can have let the user close it.
	if (FocusedGraphOnEntry)
	{
		if (FBlueprintEditor* EditorToRestore = LD::Assist::GraphView::FindOpenBlueprintEditor(Blueprint))
		{
			FString RestoreError;
			LD::Assist::GraphView::OpenAndFocusGraph(EditorToRestore, FocusedGraphOnEntry, RestoreError);
		}
	}

	// Build response payload.
	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::Strategy, LD::Assist::Layout::StrategyToString(Strategy));
	Payload->SetStringField(Args::Scope, bScopeAll ? TEXT("all") : TEXT("root"));
	Payload->SetBoolField(Args::Applied, bApply);
	Payload->SetNumberField(Args::Passes, PassesRun);
	Payload->SetNumberField(Args::IconLocationAdjustments, IconAdjustments);

	int32 EdgesThroughStatesTotal = 0;
	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	GraphsArray.Reserve(GraphContexts.Num());
	for (const LD::Assist::Private::FLayoutGraphContext& Context : GraphContexts)
	{
		const TSharedRef<FJsonObject> GraphEntry = MakeShared<FJsonObject>();
		GraphEntry->SetStringField(Args::GraphPath, Context.GraphPathLabel);
		if (Context.ParentStateGuid.IsValid())
		{
			GraphEntry->SetStringField(Args::ParentStateGuid, Context.ParentStateGuid.ToString());
		}

		TArray<TSharedPtr<FJsonValue>> NodeLayoutArray;
		NodeLayoutArray.Reserve(Context.Result.Nodes.Num());
		for (const LD::Assist::Layout::FLayoutNode& Node : Context.Result.Nodes)
		{
			const TSharedRef<FJsonObject> NodeEntry = MakeShared<FJsonObject>();
			NodeEntry->SetStringField(Args::NodeGuid, Node.NodeGuid.ToString());
			NodeEntry->SetStringField(Args::Name, Node.Name);
			NodeEntry->SetStringField(Args::Kind, Node.Kind);
			NodeEntry->SetField(Args::LogicalPosition, LD::Assist::Private::Vec2fToJsonArrayValue(Node.OldPosition));
			NodeEntry->SetField(Args::ProposedPosition, LD::Assist::Private::Vec2fToJsonArrayValue(Node.NewPosition));
			NodeEntry->SetField(Args::Delta, LD::Assist::Private::Vec2fToJsonArrayValue(Node.NewPosition - Node.OldPosition));
			if (Node.Layer != INDEX_NONE && Node.Lane == LD::Assist::Layout::ELayoutLane::Main)
			{
				NodeEntry->SetNumberField(Args::Layer, Node.Layer);
			}
			NodeEntry->SetStringField(Args::Lane, LD::Assist::Layout::LaneToString(Node.Lane));
			if (Node.bPinned)
			{
				NodeEntry->SetBoolField(TEXT("pinned"), true);
			}
			NodeLayoutArray.Add(MakeShared<FJsonValueObject>(NodeEntry));
		}
		GraphEntry->SetArrayField(Args::NodeLayout, NodeLayoutArray);

		TArray<TSharedPtr<FJsonValue>> WarningsArray;
		WarningsArray.Reserve(Context.Result.Warnings.Num());
		for (const FString& Warning : Context.Result.Warnings)
		{
			WarningsArray.Add(MakeShared<FJsonValueString>(Warning));
		}
		GraphEntry->SetArrayField(Args::Warnings, WarningsArray);

		TArray<TSharedPtr<FJsonValue>> RerouteArray;
		RerouteArray.Reserve(Context.Result.Reroutes.Num());
		for (const LD::Assist::Layout::FLayoutReroute& Reroute : Context.Result.Reroutes)
		{
			const TSharedRef<FJsonObject> RerouteEntry = MakeShared<FJsonObject>();
			RerouteEntry->SetStringField(Args::TransitionGuid, Reroute.TransitionGuid.ToString());
			RerouteEntry->SetNumberField(Args::ChainIndex, Reroute.ChainIndex);
			RerouteEntry->SetField(Args::ProposedPosition, LD::Assist::Private::Vec2fToJsonArrayValue(Reroute.Position));
			RerouteArray.Add(MakeShared<FJsonValueObject>(RerouteEntry));
		}
		GraphEntry->SetArrayField(Args::Reroutes, RerouteArray);
		GraphEntry->SetNumberField(Args::ReroutesAdded, Context.ReroutesAdded);
		GraphEntry->SetNumberField(Args::EdgesThroughStates, Context.Result.EdgesThroughStates);
		EdgesThroughStatesTotal += Context.Result.EdgesThroughStates;

		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphEntry));
	}
	Payload->SetArrayField(Args::Graphs, GraphsArray);
	Payload->SetNumberField(Args::EdgesThroughStates, EdgesThroughStatesTotal);

	// Op-level rather than per-graph: every graph in scope is measured, and each entry is prefixed with
	// the graph path it came from. A layout computed from partial measurements can still overlap. A
	// non-empty array means the spacing in the graph it names was worked out from sizes that read too
	// small. Separate from the per-graph 'warnings', which are layout notes rather than measurement.
	TArray<TSharedPtr<FJsonValue>> MeasureWarningsArray;
	MeasureWarningsArray.Reserve(MeasureWarnings.Num());
	for (const FString& Warning : MeasureWarnings)
	{
		MeasureWarningsArray.Add(MakeShared<FJsonValueString>(Warning));
	}
	Payload->SetArrayField(Args::MeasurementWarnings, MeasureWarningsArray);

	TArray<TSharedPtr<FJsonValue>> OpWarningsArray;
	OpWarningsArray.Reserve(OpWarnings.Num());
	for (const FString& Warning : OpWarnings)
	{
		OpWarningsArray.Add(MakeShared<FJsonValueString>(Warning));
	}
	Payload->SetArrayField(Args::Skipped, OpWarningsArray);

	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static FString PinTypeToShortString(const FEdGraphPinType& InType)
	{
		FString Out = InType.PinCategory.ToString();
		if (!InType.PinSubCategory.IsNone())
		{
			Out += TEXT("/");
			Out += InType.PinSubCategory.ToString();
		}
		if (InType.PinSubCategoryObject.IsValid())
		{
			Out += TEXT(":");
			Out += InType.PinSubCategoryObject->GetName();
		}
		switch (InType.ContainerType)
		{
		case EPinContainerType::Array: Out += TEXT("[]"); break;
		case EPinContainerType::Set:   Out += TEXT("{set}"); break;
		case EPinContainerType::Map:   Out += TEXT("{map}"); break;
		default: break;
		}
		return Out;
	}

	static TSharedRef<FJsonObject> PinTreeToJson(const UEdGraphPin* InPin, int32 InDepth)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("pin_name"), InPin->PinName.ToString());
		Obj->SetStringField(TEXT("pin_id"), InPin->PinId.ToString());
		Obj->SetStringField(TEXT("pin_type"), PinTypeToShortString(InPin->PinType));
		Obj->SetStringField(TEXT("default_value"), InPin->DefaultValue);
		Obj->SetStringField(TEXT("autogenerated_default_value"), InPin->AutogeneratedDefaultValue);
		Obj->SetBoolField(TEXT("matches_autogenerated"), InPin->DoesDefaultValueMatchAutogenerated());
		Obj->SetNumberField(TEXT("linked_to_count"), InPin->LinkedTo.Num());
		Obj->SetNumberField(TEXT("sub_pins_count"), InPin->SubPins.Num());
		Obj->SetNumberField(TEXT("depth"), InDepth);
		Obj->SetBoolField(TEXT("is_root_result"), InPin->ParentPin == nullptr);

		if (InPin->SubPins.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Children;
			Children.Reserve(InPin->SubPins.Num());
			for (UEdGraphPin* Sub : InPin->SubPins)
			{
				if (!Sub)
				{
					continue;
				}
				Children.Add(MakeShared<FJsonValueObject>(PinTreeToJson(Sub, InDepth + 1)));
			}
			Obj->SetArrayField(TEXT("sub_pins"), Children);
		}
		return Obj;
	}
}

FSMAssistOperationResult LD::Assist::GetPropertyPins(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	FString FilterVariable;
	InArgs->TryGetStringField(Args::VariableName, FilterVariable);

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(FilterVariable, TEXT("variable_name"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	const FName VariableFilter = FilterVariable.IsEmpty() ? NAME_None : FName(*FilterVariable);

	TArray<TSharedPtr<FJsonValue>> PropArr;
	for (const TPair<FGuid, TObjectPtr<USMGraphK2Node_PropertyNode_Base>>& Pair : Node->GetAllPropertyGraphNodes())
	{
		USMGraphK2Node_PropertyNode_Base* ResultNode = Pair.Value.Get();
		if (!ResultNode)
		{
			continue;
		}

		FName VariableName;
		bool bFlagSplit = false;
		if (const FSMGraphProperty_Base* Prop = ResultNode->GetPropertyNodeConst())
		{
			VariableName = Prop->VariableName;
			bFlagSplit = Prop->bSplit;
		}
		if (!VariableFilter.IsNone() && VariableName != VariableFilter)
		{
			continue;
		}

		UEdGraphPin* Root = ResultNode->GetResultPin(EGPD_Input);
		if (!Root)
		{
			continue;
		}

		const TSharedRef<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(Args::VariableName, VariableName.ToString());
		PropObj->SetStringField(TEXT("guid"), Pair.Key.ToString());
		PropObj->SetBoolField(TEXT("is_split_struct"), LD::Editor::PropertyUtils::IsSplitStructResultNode(ResultNode));
		PropObj->SetBoolField(TEXT("flag_b_split"), bFlagSplit);
		PropObj->SetObjectField(TEXT("result_pin"), LD::Assist::Private::PinTreeToJson(Root, 0));
		PropArr.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	Payload->SetNumberField(Args::Count, PropArr.Num());
	Payload->SetArrayField(Args::Properties, PropArr);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	struct FResolvePropertyGraphInputs
	{
		USMBlueprint* Blueprint = nullptr;
		USMGraphNode_Base* Node = nullptr;
		FString VariableName;
		FString PropertyPath;
	};

	static bool ParseResolvePropertyGraphInputs(const TSharedRef<FJsonObject>& InArgs,
		FResolvePropertyGraphInputs& OutInputs, FString& OutError)
	{
		FString AssetPath;
		if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("Missing required arg 'asset_path'.");
			return false;
		}

		FString NodeGuidStr;
		if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
		{
			OutError = TEXT("Missing required arg 'node_guid'.");
			return false;
		}
		FGuid NodeGuid;
		if (!FGuid::Parse(NodeGuidStr, NodeGuid))
		{
			OutError = FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr);
			return false;
		}

		if (!InArgs->TryGetStringField(Args::VariableName, OutInputs.VariableName) || OutInputs.VariableName.IsEmpty())
		{
			OutError = TEXT("Missing required arg 'variable_name'.");
			return false;
		}

		InArgs->TryGetStringField(Args::PropertyPath, OutInputs.PropertyPath);

		OutInputs.Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, OutError);
		if (!OutInputs.Blueprint)
		{
			return false;
		}

		OutInputs.Node = LD::Assist::Utils::FindNodeByGuid(OutInputs.Blueprint, NodeGuid);
		if (!OutInputs.Node)
		{
			OutError = FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr);
			return false;
		}
		return true;
	}
}

FSMAssistOperationResult LD::Assist::GetPropertyGraph(const TSharedRef<FJsonObject>& InArgs)
{
	FString Error;
	LD::Assist::Private::FResolvePropertyGraphInputs Inputs;
	if (!LD::Assist::Private::ParseResolvePropertyGraphInputs(InArgs, Inputs, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(Error);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	bool bIncludePinTree = false;
	InArgs->TryGetBoolField(Args::IncludePinTree, bIncludePinTree);

	ISMGraphGeneration::FFindPropertyGraphArgs FindArgs;
	FindArgs.VariableName = *Inputs.VariableName;
	FindArgs.SubPath = Inputs.PropertyPath;

	ISMGraphGeneration::FFindPropertyGraphResult Resolution;
	if (!GraphGen->FindPropertyGraph(Inputs.Node, FindArgs, Resolution, &Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Inputs.Blueprint->GetPathName());
	Payload->SetStringField(Args::GraphPath, Resolution.Graph->GetPathName());
	Payload->SetStringField(Args::GraphName, Resolution.Graph->GetName());
	Payload->SetStringField(Args::GraphGuid, Resolution.Graph->GraphGuid.ToString());
	Payload->SetStringField(Args::ResultNodeName, Resolution.ResultNode->GetName());
	Payload->SetStringField(Args::ResultPinName, Resolution.ResultPin->PinName.ToString());
	Payload->SetNumberField(Args::BucketIndex, Resolution.BucketIndex);
	Payload->SetStringField(Args::ElementType, LD::Assist::Private::PinTypeToShortString(Resolution.ResultPin->PinType));

	if (bIncludePinTree)
	{
		if (UEdGraphPin* ResultPin = Resolution.ResultNode->GetResultPin(EGPD_Input))
		{
			Payload->SetObjectField(Args::ResultPin, LD::Assist::Private::PinTreeToJson(ResultPin, 0));
		}
	}

	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SetPropertyGraphEditMode(const TSharedRef<FJsonObject>& InArgs)
{
	FString Error;
	LD::Assist::Private::FResolvePropertyGraphInputs Inputs;
	if (!LD::Assist::Private::ParseResolvePropertyGraphInputs(InArgs, Inputs, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	bool bEnable = false;
	if (!InArgs->TryGetBoolField(Args::Enable, bEnable))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'b_enable'."));
	}

	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(Error);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	ISMGraphGeneration::FFindPropertyGraphArgs FindArgs;
	FindArgs.VariableName = *Inputs.VariableName;
	FindArgs.SubPath = Inputs.PropertyPath;

	ISMGraphGeneration::FFindPropertyGraphResult Resolution;
	if (!GraphGen->FindPropertyGraph(Inputs.Node, FindArgs, Resolution, &Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	if (!GraphGen->SetPropertyGraphEditMode(Resolution.Graph, bEnable))
	{
		if (Resolution.Graph->IsVariableReadOnly())
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Cannot toggle edit mode on '%s%s%s': the underlying variable is read-only."),
				*Inputs.VariableName, Inputs.PropertyPath.IsEmpty() ? TEXT("") : TEXT("."), *Inputs.PropertyPath));
		}
		return FSMAssistOperationResult::MakeError(TEXT("SetPropertyGraphEditMode failed on the resolved graph."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Inputs.Blueprint->GetPathName());
	Payload->SetStringField(Args::GraphPath, Resolution.Graph->GetPathName());
	Payload->SetBoolField(Args::Enable, bEnable);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	struct FSplitRecombineTarget
	{
		USMGraphK2Node_PropertyNode_Base* ResultNode = nullptr;
		USMPropertyGraph* PropertyGraph = nullptr;
		UEdGraphPin* RootPin = nullptr;
		UEdGraphPin* TargetSubPin = nullptr;
		FGuid RequestedPinId;
		bool bHasPinId = false;
	};

	static bool ResolveSplitRecombineTarget(const TSharedRef<FJsonObject>& InArgs, FSplitRecombineTarget& OutTarget, FString& OutError)
	{
		FString AssetPath;
		if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("Missing required arg 'asset_path'.");
			return false;
		}

		FString NodeGuidStr;
		if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
		{
			OutError = TEXT("Missing required arg 'node_guid'.");
			return false;
		}

		FGuid NodeGuid;
		if (!FGuid::Parse(NodeGuidStr, NodeGuid))
		{
			OutError = FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr);
			return false;
		}

		FString VariableName;
		if (!InArgs->TryGetStringField(Args::VariableName, VariableName) || VariableName.IsEmpty())
		{
			OutError = TEXT("Missing required arg 'variable_name'.");
			return false;
		}

		FString PinIdStr;
		OutTarget.bHasPinId = InArgs->TryGetStringField(Args::PinId, PinIdStr) && !PinIdStr.IsEmpty();
		if (OutTarget.bHasPinId && !FGuid::Parse(PinIdStr, OutTarget.RequestedPinId))
		{
			OutError = FString::Printf(TEXT("Invalid 'pin_id' '%s'."), *PinIdStr);
			return false;
		}

		USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, OutError);
		if (!Blueprint)
		{
			return false;
		}

		USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr);
			return false;
		}

		const FName VariableFilter(*VariableName);
		USMGraphK2Node_PropertyNode_Base* FirstCandidate = nullptr;
		for (const TPair<FGuid, TObjectPtr<USMGraphK2Node_PropertyNode_Base>>& Pair : Node->GetAllPropertyGraphNodes())
		{
			USMGraphK2Node_PropertyNode_Base* Candidate = Pair.Value.Get();
			if (!Candidate)
			{
				continue;
			}
			const FSMGraphProperty_Base* Prop = Candidate->GetPropertyNodeConst();
			if (!Prop || Prop->VariableName != VariableFilter)
			{
				continue;
			}
			if (!FirstCandidate)
			{
				FirstCandidate = Candidate;
			}
			if (!OutTarget.bHasPinId)
			{
				continue;
			}
			// TArray<Struct> properties expose one bucket per element; the supplied PinId may live under any bucket's root or sub-pin tree.
			UEdGraphPin* CandidateRoot = Candidate->GetResultPin(EGPD_Input);
			if (!CandidateRoot)
			{
				continue;
			}
			if (CandidateRoot->PinId == OutTarget.RequestedPinId)
			{
				OutTarget.ResultNode = Candidate;
				OutTarget.RootPin = CandidateRoot;
				break;
			}
			if (UEdGraphPin* Sub = LD::Editor::PropertyUtils::FindSubPinByPinId(CandidateRoot, OutTarget.RequestedPinId))
			{
				OutTarget.ResultNode = Candidate;
				OutTarget.RootPin = CandidateRoot;
				OutTarget.TargetSubPin = Sub;
				break;
			}
		}

		if (!OutTarget.ResultNode)
		{
			if (!FirstCandidate)
			{
				OutError = FString::Printf(TEXT("No exposed property '%s' on node '%s'."), *VariableName, *NodeGuidStr);
				return false;
			}
			if (OutTarget.bHasPinId)
			{
				OutError = FString::Printf(TEXT("Pin id '%s' not found under any bucket of property '%s' on node '%s'."),
					*OutTarget.RequestedPinId.ToString(), *VariableName, *NodeGuidStr);
				return false;
			}
			OutTarget.ResultNode = FirstCandidate;
		}

		OutTarget.PropertyGraph = OutTarget.ResultNode->GetPropertyGraph();
		if (!OutTarget.PropertyGraph)
		{
			OutError = FString::Printf(TEXT("Property graph unavailable for '%s' on node '%s'."), *VariableName, *NodeGuidStr);
			return false;
		}

		if (!OutTarget.RootPin)
		{
			OutTarget.RootPin = OutTarget.ResultNode->GetResultPin(EGPD_Input);
			if (!OutTarget.RootPin)
			{
				OutError = FString::Printf(TEXT("Property '%s' has no input result pin to split or recombine."), *VariableName);
				return false;
			}
		}

		return true;
	}

	static TSharedRef<FJsonObject> BuildSplitRecombinePayload(const FSplitRecombineTarget& InTarget, bool bInApplied)
	{
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetBoolField(Args::Applied, bInApplied);
		Payload->SetBoolField(TEXT("is_split_struct"),
			LD::Editor::PropertyUtils::IsSplitStructResultNode(InTarget.ResultNode));

		if (const FSMGraphProperty_Base* Prop = InTarget.ResultNode->GetPropertyNodeConst())
		{
			Payload->SetStringField(Args::VariableName, Prop->VariableName.ToString());
			Payload->SetBoolField(TEXT("flag_b_split"), Prop->bSplit);
		}

		if (InTarget.bHasPinId)
		{
			Payload->SetStringField(Args::PinId, InTarget.RequestedPinId.ToString());
		}

		if (UEdGraphPin* Root = InTarget.ResultNode->GetResultPin(EGPD_Input))
		{
			Payload->SetObjectField(TEXT("result_pin"), LD::Assist::Private::PinTreeToJson(Root, 0));
		}
		return Payload;
	}
}

FSMAssistOperationResult LD::Assist::SplitPin(const TSharedRef<FJsonObject>& InArgs)
{
	LD::Assist::Private::FSplitRecombineTarget Target;
	FString Error;
	if (!LD::Assist::Private::ResolveSplitRecombineTarget(InArgs, Target, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	if (!Target.TargetSubPin)
	{
		if (!Target.PropertyGraph->CanSplitResultPin())
		{
			// Already-split gets its own wording: it is the one refusal the caller can act on. Mirrors
			// the SubPins-only condition in CanSplitResultPin so the two cannot disagree.
			const FSMGraphProperty_Base* Prop = Target.ResultNode ? Target.ResultNode->GetPropertyNodeConst() : nullptr;
			const UEdGraphPin* RootPin = Target.ResultNode ? Target.ResultNode->GetResultPin(EGPD_Input) : nullptr;
			if (Prop && RootPin && RootPin->SubPins.Num() > 0)
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Property '%s' is already split; splitting again would reset it and every sub-pin to the class default. Write sub-pin values with property_path instead. RecombinePin collapses a split pin, but a property inlined by ShowOnlyInnerProperties is permanently split and re-splits on the next reconstruction."),
						*Prop->VariableName.ToString()));
			}
			return FSMAssistOperationResult::MakeError(
				TEXT("Property is not splittable (CanSplitResultPin=false). Type may not be a splittable struct, or the property may opt out via CanEverSplit."));
		}
		Target.PropertyGraph->SplitResultPin();
	}
	else
	{
		if (!Target.PropertyGraph->CanSplitSubPin(Target.TargetSubPin))
		{
			if (Target.TargetSubPin->SubPins.Num() > 0)
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Sub-pin '%s' is already split. Write its members with property_path, or RecombinePin first to collapse it."),
						*Target.TargetSubPin->PinName.ToString()));
			}
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Sub-pin '%s' is not splittable (CanSplitSubPin=false). Type may not be a splittable struct, or the owning graph property opts out."),
					*Target.TargetSubPin->PinName.ToString()));
		}
		Target.PropertyGraph->SplitSubPin(Target.TargetSubPin);
	}

	return FSMAssistOperationResult::MakeSuccess(LD::Assist::Private::BuildSplitRecombinePayload(Target, true));
}

FSMAssistOperationResult LD::Assist::RecombinePin(const TSharedRef<FJsonObject>& InArgs)
{
	LD::Assist::Private::FSplitRecombineTarget Target;
	FString Error;
	if (!LD::Assist::Private::ResolveSplitRecombineTarget(InArgs, Target, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	if (!Target.TargetSubPin)
	{
		if (!LD::Editor::PropertyUtils::IsSplitStructResultNode(Target.ResultNode))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("Property is not currently split; nothing to recombine."));
		}
		Target.PropertyGraph->RecombineResultPin();
	}
	else
	{
		if (Target.TargetSubPin->SubPins.Num() == 0)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Sub-pin '%s' is not currently split; nothing to recombine."),
					*Target.TargetSubPin->PinName.ToString()));
		}
		Target.PropertyGraph->RecombineSubPin(Target.TargetSubPin);
	}

	return FSMAssistOperationResult::MakeSuccess(LD::Assist::Private::BuildSplitRecombinePayload(Target, true));
}

FSMAssistOperationResult LD::Assist::ResetNodeProperty(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FString PropertyName;
	if (!InArgs->TryGetStringField(Args::PropertyName, PropertyName) || PropertyName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'property_name'."));
	}

	FString PropertyNameLengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(PropertyName, TEXT("property_name"), PropertyNameLengthError))
	{
		return FSMAssistOperationResult::MakeError(PropertyNameLengthError);
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	USMNodeInstance* TargetTemplate = nullptr;
	int32 StackIndex = INDEX_NONE;
	const bool bStackIndexProvided = InArgs->TryGetNumberField(Args::StackIndex, StackIndex) && StackIndex >= 0;
	if (bStackIndexProvided)
	{
		TargetTemplate = Node->GetTemplateFromIndex(StackIndex);
		if (!TargetTemplate)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("No stack template at index %d on node '%s'."), StackIndex, *NodeGuidStr));
		}
	}

	int32 ArrayIndex = 0;
	InArgs->TryGetNumberField(Args::ArrayIndex, ArrayIndex);

	// Without this the reset below fails with the graph-property message, which misdiagnoses a
	// deprecated name as an exposure problem. set_node_property refuses the same names.
	if (const USMNodeInstance* ResolvedTemplate = TargetTemplate ? TargetTemplate : Node->GetNodeTemplate())
	{
		const FProperty* TemplateProperty = ResolvedTemplate->GetClass()->FindPropertyByName(*PropertyName);
		if (TemplateProperty && TemplateProperty->HasAnyPropertyFlags(CPF_Deprecated))
		{
			return LD::Assist::Private::MakeDeprecatedPropertyError(PropertyName);
		}
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FResetNodePropertyArgs ResetArgs;
	ResetArgs.PropertyName = *PropertyName;
	ResetArgs.PropertyIndex = ArrayIndex;
	ResetArgs.NodeInstance = TargetTemplate;

	const bool bReset = GraphGen->ResetNodePropertyValue(Node, ResetArgs);
	if (!bReset)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Property '%s' could not be reset on node '%s' (not exposed as a graph property)."),
				*PropertyName, *NodeGuidStr));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	Payload->SetStringField(Args::PropertyName, PropertyName);
	if (bStackIndexProvided)
	{
		Payload->SetNumberField(Args::StackIndex, StackIndex);
	}
	if (InArgs->HasField(Args::ArrayIndex))
	{
		Payload->SetNumberField(Args::ArrayIndex, ArrayIndex);
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static bool ResolveTerminalType(const FString& InTypeStr, FName& OutCategory, FName& OutSubCategory, UObject*& OutSubCategoryObject)
	{
		return LD::Assist::Utils::ResolveTerminalType(InTypeStr, OutCategory, OutSubCategory, OutSubCategoryObject);
	}

	static bool ResolveContainerType(const FString& InContainerTypeStr, EPinContainerType& OutContainerType)
	{
		if (InContainerTypeStr.IsEmpty() || InContainerTypeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			OutContainerType = EPinContainerType::None;
			return true;
		}
		if (InContainerTypeStr.Equals(TEXT("Array"), ESearchCase::IgnoreCase))
		{
			OutContainerType = EPinContainerType::Array;
			return true;
		}
		if (InContainerTypeStr.Equals(TEXT("Map"), ESearchCase::IgnoreCase))
		{
			OutContainerType = EPinContainerType::Map;
			return true;
		}
		if (InContainerTypeStr.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
		{
			OutContainerType = EPinContainerType::Set;
			return true;
		}
		return false;
	}

	static const TCHAR* ContainerTypeToString(EPinContainerType InContainerType)
	{
		switch (InContainerType)
		{
		case EPinContainerType::Array: return TEXT("Array");
		case EPinContainerType::Map:   return TEXT("Map");
		case EPinContainerType::Set:   return TEXT("Set");
		default:                       return TEXT("None");
		}
	}

	static const TCHAR* GetAcceptedTypeTokens()
	{
		return TEXT("bool, int, int64, byte, float, single, string, name, text, vector, vector2d, rotator, transform, linearcolor, color, guid, or a class/struct path (e.g. /Script/Engine.Actor)");
	}

	// Map semantics in FEdGraphPinType: the outer PinCategory/SubCategory/SubCategoryObject describe
	// the KEY type; PinValueType describes the VALUE type. This is the reverse of how Blueprint's
	// pin-type widget displays them, but it matches the engine's serialization.
	static bool ResolveVariablePinType(
		const FString& InVarType,
		const FString& InContainerTypeStr,
		const FString& InKeyType,
		FEdGraphPinType& OutPinType,
		FString& OutError)
	{
		FName ValueCategory;
		FName ValueSubCategory;
		UObject* ValueSubCategoryObject = nullptr;
		if (!ResolveTerminalType(InVarType, ValueCategory, ValueSubCategory, ValueSubCategoryObject))
		{
			OutError = FString::Printf(TEXT("Unrecognized 'var_type' '%s'. Accepted values: %s."),
				*InVarType, GetAcceptedTypeTokens());
			return false;
		}

		EPinContainerType ContainerType = EPinContainerType::None;
		if (!ResolveContainerType(InContainerTypeStr, ContainerType))
		{
			OutError = FString::Printf(TEXT("Unrecognized 'container_type' '%s'. Accepted: None, Array, Map, Set."),
				*InContainerTypeStr);
			return false;
		}

		if (ContainerType == EPinContainerType::Map)
		{
			if (InKeyType.IsEmpty())
			{
				OutError = TEXT("Missing 'key_type' for container_type='Map'. Required vocabulary matches 'var_type'.");
				return false;
			}

			FName KeyCategory;
			FName KeySubCategory;
			UObject* KeySubCategoryObject = nullptr;
			if (!ResolveTerminalType(InKeyType, KeyCategory, KeySubCategory, KeySubCategoryObject))
			{
				OutError = FString::Printf(TEXT("Unrecognized 'key_type' '%s'. Accepted values: %s."),
					*InKeyType, GetAcceptedTypeTokens());
				return false;
			}

			OutPinType = FEdGraphPinType(KeyCategory, KeySubCategory, KeySubCategoryObject, EPinContainerType::Map, false, FEdGraphTerminalType());
			OutPinType.PinValueType.TerminalCategory = ValueCategory;
			OutPinType.PinValueType.TerminalSubCategory = ValueSubCategory;
			OutPinType.PinValueType.TerminalSubCategoryObject = ValueSubCategoryObject;
			return true;
		}

		if (!InKeyType.IsEmpty())
		{
			OutError = FString::Printf(TEXT("'key_type' is only valid when container_type='Map'; got container_type='%s', key_type='%s'."),
				*InContainerTypeStr, *InKeyType);
			return false;
		}

		OutPinType = FEdGraphPinType(ValueCategory, ValueSubCategory, ValueSubCategoryObject, ContainerType, false, FEdGraphTerminalType());
		return true;
	}
}

FSMAssistOperationResult LD::Assist::AddSMVariable(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString VarName;
	if (!InArgs->TryGetStringField(Args::VariableName, VarName) || VarName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'variable_name'."));
	}

	FString VarType;
	if (!InArgs->TryGetStringField(Args::VarType, VarType) || VarType.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'var_type'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString ContainerTypeStr;
	InArgs->TryGetStringField(Args::ContainerType, ContainerTypeStr);

	FString KeyType;
	InArgs->TryGetStringField(Args::KeyType, KeyType);

	FEdGraphPinType PinType;
	FString ResolveError;
	if (!LD::Assist::Private::ResolveVariablePinType(VarType, ContainerTypeStr, KeyType, PinType, ResolveError))
	{
		return FSMAssistOperationResult::MakeError(ResolveError);
	}

	FString DefaultValue;
	InArgs->TryGetStringField(Args::DefaultValue, DefaultValue);

	FString VarNameLengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(VarName, TEXT("variable_name"), VarNameLengthError))
	{
		return FSMAssistOperationResult::MakeError(VarNameLengthError);
	}

	const FName VarFName(*VarName);
	const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarFName, PinType, DefaultValue);
	if (!bAdded)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("AddMemberVariable failed for '%s' (likely duplicate name or unsupported type)."), *VarName));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::VariableName, VarName);
	Payload->SetStringField(Args::VarType, VarType);
	if (!DefaultValue.IsEmpty())
	{
		Payload->SetStringField(Args::DefaultValue, DefaultValue);
	}
	if (PinType.ContainerType != EPinContainerType::None)
	{
		Payload->SetStringField(Args::ContainerType, LD::Assist::Private::ContainerTypeToString(PinType.ContainerType));
		if (PinType.ContainerType == EPinContainerType::Map)
		{
			Payload->SetStringField(Args::KeyType, KeyType);
		}
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static USCS_Node* FindSCSNodeByName(const UBlueprint* InBlueprint, const FName& InComponentName)
	{
		if (!InBlueprint || !InBlueprint->SimpleConstructionScript)
		{
			return nullptr;
		}
		for (USCS_Node* Node : InBlueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName() == InComponentName)
			{
				return Node;
			}
		}
		return nullptr;
	}

	static bool ResolveNetworkConfigType(const FString& InValue, ESMNetworkConfigurationType& OutType)
	{
		if (InValue.Equals(TEXT("Client"), ESearchCase::IgnoreCase)
			|| InValue.Equals(TEXT("SM_Client"), ESearchCase::IgnoreCase))
		{
			OutType = SM_Client;
			return true;
		}
		if (InValue.Equals(TEXT("Server"), ESearchCase::IgnoreCase)
			|| InValue.Equals(TEXT("SM_Server"), ESearchCase::IgnoreCase))
		{
			OutType = SM_Server;
			return true;
		}
		if (InValue.Equals(TEXT("ClientAndServer"), ESearchCase::IgnoreCase)
			|| InValue.Equals(TEXT("SM_ClientAndServer"), ESearchCase::IgnoreCase))
		{
			OutType = SM_ClientAndServer;
			return true;
		}
		return false;
	}
}

FSMAssistOperationResult LD::Assist::ConfigureSMComponentOnActor(const TSharedRef<FJsonObject>& InArgs)
{
	FString ActorBPPath;
	if (!InArgs->TryGetStringField(Args::ActorBlueprint, ActorBPPath) || ActorBPPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'actor_blueprint'."));
	}

	FString ComponentName;
	if (!InArgs->TryGetStringField(Args::ComponentName, ComponentName) || ComponentName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'component_name'."));
	}

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(ActorBPPath, TEXT("actor_blueprint"), LengthError)
		|| !LD::Assist::Utils::IsWithinNameLength(ComponentName, TEXT("component_name"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	UBlueprint* ActorBP = LoadObject<UBlueprint>(nullptr, *ActorBPPath);
	if (!ActorBP)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not load 'actor_blueprint' '%s'."), *ActorBPPath));
	}
	if (!ActorBP->SimpleConstructionScript)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Actor blueprint '%s' has no SimpleConstructionScript (not an AActor subclass)."), *ActorBPPath));
	}

	USCS_Node* TargetNode = LD::Assist::Private::FindSCSNodeByName(ActorBP, FName(*ComponentName));
	if (!TargetNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("No SCS component named '%s' on '%s'."), *ComponentName, *ActorBPPath));
	}

	USMStateMachineComponent* Template = Cast<USMStateMachineComponent>(TargetNode->ComponentTemplate);
	if (!Template)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Component '%s' is not a USMStateMachineComponent (template class '%s')."),
				*ComponentName,
				TargetNode->ComponentTemplate ? *TargetNode->ComponentTemplate->GetClass()->GetName() : TEXT("null")));
	}

	// Parse extra_config_json before any mutation so a malformed payload can't leave the component template
	// half-reconfigured; the promoted-field writes below are applied in place with no rollback.
	FString ExtraConfigJsonStr;
	TSharedPtr<FJsonObject> ExtraObj;
	if (InArgs->TryGetStringField(Args::ExtraConfigJson, ExtraConfigJsonStr) && !ExtraConfigJsonStr.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExtraConfigJsonStr);
		if (!FJsonSerializer::Deserialize(Reader, ExtraObj) || !ExtraObj.IsValid())
		{
			return FSMAssistOperationResult::MakeError(TEXT("'extra_config_json' is not valid JSON."));
		}
	}

	ActorBP->Modify();
	Template->Modify();

	TArray<FString> Applied;
	TArray<FString> UnknownKeys;

	FString SMClassPath;
	if (InArgs->TryGetStringField(Args::StateMachineClass, SMClassPath) && !SMClassPath.IsEmpty())
	{
		FString LoadError;
		USMBlueprint* SMBP = LD::Assist::Utils::LoadStateMachineBlueprint(SMClassPath, LoadError);
		if (!SMBP)
		{
			return FSMAssistOperationResult::MakeError(LoadError);
		}
		Template->StateMachineClass = SMBP->GeneratedClass;
		Applied.Add(TEXT("StateMachineClass"));
	}

	auto ApplyBool = [&](const TCHAR* JsonKey, auto Setter, const TCHAR* PropName)
	{
		bool Value;
		if (InArgs->TryGetBoolField(JsonKey, Value))
		{
			Setter(Value);
			Applied.Add(PropName);
		}
	};

	ApplyBool(Args::StartOnBeginPlay, [&](bool V) { Template->bStartOnBeginPlay = V; }, TEXT("bStartOnBeginPlay"));
	ApplyBool(Args::InitializeOnBeginPlay, [&](bool V) { Template->bInitializeOnBeginPlay = V; }, TEXT("bInitializeOnBeginPlay"));
	ApplyBool(Args::StopOnEndPlay, [&](bool V) { Template->bStopOnEndPlay = V; }, TEXT("bStopOnEndPlay"));
	ApplyBool(Args::ReuseInstanceAfterShutdown, [&](bool V) { Template->bReuseInstanceAfterShutdown = V; }, TEXT("bReuseInstanceAfterShutdown"));
	ApplyBool(Args::Replicates, [&](bool V) { Template->SetIsReplicated(V); }, TEXT("bReplicates"));
	ApplyBool(Args::IncludeSimulatedProxies, [&](bool V) { Template->bIncludeSimulatedProxies = V; }, TEXT("bIncludeSimulatedProxies"));
	ApplyBool(Args::WaitForTransactionsFromServer, [&](bool V) { Template->bWaitForTransactionsFromServer = V; }, TEXT("bWaitForTransactionsFromServer"));
	ApplyBool(Args::HandleControllerChange, [&](bool V) { Template->bHandleControllerChange = V; }, TEXT("bHandleControllerChange"));

	auto ApplyEnum = [&](const TCHAR* JsonKey, TEnumAsByte<ESMNetworkConfigurationType>& Target, const TCHAR* PropName)
	{
		FString In;
		if (!InArgs->TryGetStringField(JsonKey, In) || In.IsEmpty())
		{
			return;
		}
		ESMNetworkConfigurationType Resolved;
		if (LD::Assist::Private::ResolveNetworkConfigType(In, Resolved))
		{
			Target = Resolved;
			Applied.Add(PropName);
		}
		else
		{
			UnknownKeys.Add(FString::Printf(TEXT("%s=%s"), PropName, *In));
		}
	};

	ApplyEnum(Args::StateChangeAuthority, Template->StateChangeAuthority, TEXT("StateChangeAuthority"));
	ApplyEnum(Args::NetworkTickConfiguration, Template->NetworkTickConfiguration, TEXT("NetworkTickConfiguration"));
	ApplyEnum(Args::NetworkStateExecution, Template->NetworkStateExecution, TEXT("NetworkStateExecution"));
	ApplyEnum(Args::NetworkTransitionEnteredConfiguration, Template->NetworkTransitionEnteredConfiguration, TEXT("NetworkTransitionEnteredConfiguration"));

	if (ExtraObj.IsValid())
	{
		UClass* TemplateClass = Template->GetClass();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ExtraObj->Values)
		{
			FString KeyLengthError;
			if (!LD::Assist::Utils::IsWithinNameLength(Pair.Key, TEXT("extra_config_json key"), KeyLengthError))
			{
				UnknownKeys.Add(FString::Printf(TEXT("%s... (%s)"), *Pair.Key.Left(64), *KeyLengthError));
				continue;
			}

			FProperty* Property = TemplateClass->FindPropertyByName(FName(*Pair.Key));
			if (!Property)
			{
				UnknownKeys.Add(Pair.Key);
				continue;
			}
			// Raw reflection reaches internal UPROPERTYs (CreationMethod, replication state) that the
			// details panel never exposes; writing those corrupts the SCS template. Only allow what a
			// user could edit.
			if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_EditConst))
			{
				UnknownKeys.Add(FString::Printf(TEXT("%s (not editable on the component template)"), *Pair.Key));
				continue;
			}
			const FString ValueAsText = Pair.Value->AsString();
			void* PropAddr = Property->ContainerPtrToValuePtr<void>(Template);
			const TCHAR* Imported = Property->ImportText_Direct(*ValueAsText, PropAddr, Template, PPF_None, nullptr);
			if (Imported != nullptr)
			{
				Applied.Add(Pair.Key);
			}
			else
			{
				UnknownKeys.Add(FString::Printf(TEXT("%s (import failed for value '%s')"), *Pair.Key, *ValueAsText));
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ActorBP);

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::ActorBlueprint, ActorBP->GetPathName());
	Payload->SetStringField(Args::ComponentName, ComponentName);
	if (Template->StateMachineClass)
	{
		Payload->SetStringField(Args::StateMachineClass, Template->StateMachineClass->GetPathName());
	}
	TArray<TSharedPtr<FJsonValue>> AppliedJson;
	for (const FString& AppliedField : Applied)
	{
		AppliedJson.Add(MakeShared<FJsonValueString>(AppliedField));
	}
	Payload->SetArrayField(Args::Applied, AppliedJson);
	TArray<TSharedPtr<FJsonValue>> UnknownJson;
	for (const FString& UnknownKey : UnknownKeys)
	{
		UnknownJson.Add(MakeShared<FJsonValueString>(UnknownKey));
	}
	Payload->SetArrayField(Args::UnknownKeys, UnknownJson);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	// Forward decl; defined with the get_local_graph serializers below so spawn read/write return the same node payload.
	static TSharedRef<FJsonObject> LocalGraphNodeToJson(UEdGraphNode* InNode, bool bIncludePins, const FString& InResultNodeName);

	// The node that owns the compiled bound graph, plus the graph. Shared by every local-graph op so
	// resolution (including reroute normalization) stays identical between read and write.
	struct FResolvedLocalGraph
	{
		USMBlueprint* Blueprint = nullptr;
		USMGraphNode_Base* RequestedNode = nullptr;
		USMGraphNode_Base* OwnerNode = nullptr;
		UEdGraph* Graph = nullptr;
		bool bIsRerouted = false;
	};

	// Defined below; resolves asset_path + node_guid to the bound graph, normalizing reroutes to the primary transition.
	static bool ResolveLocalGraph(const TSharedRef<FJsonObject>& InArgs, FResolvedLocalGraph& Out, FString& OutError);

	static bool ResolveLocalGraphReadType(const FString& InValue, ISMGraphGeneration::ELocalGraphReadNodeType& OutType)
	{
		const FString Lower = InValue.ToLower();
		if (Lower == TEXT("timeinstate") || Lower == TEXT("time_in_state"))
		{
			OutType = ISMGraphGeneration::ELocalGraphReadNodeType::TimeInState;
			return true;
		}
		if (Lower == TEXT("hasstateupdated") || Lower == TEXT("has_state_updated"))
		{
			OutType = ISMGraphGeneration::ELocalGraphReadNodeType::HasStateUpdated;
			return true;
		}
		if (Lower == TEXT("canevaluate") || Lower == TEXT("can_evaluate"))
		{
			OutType = ISMGraphGeneration::ELocalGraphReadNodeType::CanEvaluate;
			return true;
		}
		if (Lower == TEXT("canevaluatefromevent") || Lower == TEXT("can_evaluate_from_event"))
		{
			OutType = ISMGraphGeneration::ELocalGraphReadNodeType::CanEvaluateFromEvent;
			return true;
		}
		if (Lower == TEXT("getstateinformation") || Lower == TEXT("get_state_information"))
		{
			OutType = ISMGraphGeneration::ELocalGraphReadNodeType::GetStateInformation;
			return true;
		}
		if (Lower == TEXT("gettransitioninformation") || Lower == TEXT("get_transition_information"))
		{
			OutType = ISMGraphGeneration::ELocalGraphReadNodeType::GetTransitionInformation;
			return true;
		}
		if (Lower == TEXT("getstatemachinereference") || Lower == TEXT("get_state_machine_reference"))
		{
			OutType = ISMGraphGeneration::ELocalGraphReadNodeType::GetStateMachineReference;
			return true;
		}
		if (Lower == TEXT("getnodeinstance") || Lower == TEXT("get_node_instance"))
		{
			OutType = ISMGraphGeneration::ELocalGraphReadNodeType::GetNodeInstance;
			return true;
		}
		if (Lower == TEXT("inendstate") || Lower == TEXT("in_end_state"))
		{
			OutType = ISMGraphGeneration::ELocalGraphReadNodeType::InEndState;
			return true;
		}
		return false;
	}
}

FSMAssistOperationResult LD::Assist::SpawnLocalGraphReadNode(const TSharedRef<FJsonObject>& InArgs)
{
	FString NodeTypeStr;
	if (!InArgs->TryGetStringField(Args::Type, NodeTypeStr) || NodeTypeStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'type'."));
	}

	ISMGraphGeneration::ELocalGraphReadNodeType NodeType;
	if (!LD::Assist::Private::ResolveLocalGraphReadType(NodeTypeStr, NodeType))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Unrecognized 'type' '%s'. Accepted: TimeInState, HasStateUpdated, CanEvaluate, CanEvaluateFromEvent, GetStateInformation, GetTransitionInformation, GetStateMachineReference, InEndState, GetNodeInstance."),
			*NodeTypeStr));
	}

	// Shared resolver so a reroute-waypoint guid normalizes to the primary transition, matching get_local_graph/add/connect.
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}
	USMBlueprint* Blueprint = Resolved.Blueprint;
	UEdGraph* TargetGraph = Resolved.Graph;

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateLocalGraphReadNodeArgs CreateArgs;
	CreateArgs.NodeType = NodeType;
	CreateArgs.TargetGraph = TargetGraph;

	double PosX = 0.0;
	double PosY = 0.0;
	InArgs->TryGetNumberField(Args::PositionX, PosX);
	InArgs->TryGetNumberField(Args::PositionY, PosY);
	CreateArgs.NodePosition = FVector2D(PosX, PosY);

	FString NodeInstanceGuidStr;
	if (InArgs->TryGetStringField(Args::NodeInstanceGuid, NodeInstanceGuidStr) && !NodeInstanceGuidStr.IsEmpty())
	{
		if (!FGuid::Parse(NodeInstanceGuidStr, CreateArgs.NodeInstanceGuid))
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Invalid 'node_instance_guid' '%s'."), *NodeInstanceGuidStr));
		}
	}

	int32 NodeInstanceIndex = INDEX_NONE;
	if (InArgs->TryGetNumberField(Args::NodeInstanceIndex, NodeInstanceIndex))
	{
		CreateArgs.NodeInstanceIndex = NodeInstanceIndex;
	}

	UEdGraphNode* NewNode = GraphGen->CreateLocalGraphReadNode(Blueprint, CreateArgs);
	if (!NewNode)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("CreateLocalGraphReadNode failed for type '%s' on graph '%s' (likely the type is not compatible with the target graph context)."),
			*NodeTypeStr, *TargetGraph->GetName()));
	}

	const TSharedRef<FJsonObject> Payload = LD::Assist::Private::LocalGraphNodeToJson(NewNode, true, FString());
	Payload->SetStringField(Args::NodeGuid, NewNode->NodeGuid.ToString());
	Payload->SetStringField(Args::Type, NodeTypeStr);
	Payload->SetStringField(Args::TargetGraphPath, TargetGraph->GetPathName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static bool ResolveLocalGraphWriteType(const FString& InValue, ISMGraphGeneration::ELocalGraphWriteNodeType& OutType)
	{
		const FString Lower = InValue.ToLower();
		if (Lower == TEXT("canevaluate") || Lower == TEXT("can_evaluate"))
		{
			OutType = ISMGraphGeneration::ELocalGraphWriteNodeType::CanEvaluate;
			return true;
		}
		if (Lower == TEXT("canevaluatefromevent") || Lower == TEXT("can_evaluate_from_event"))
		{
			OutType = ISMGraphGeneration::ELocalGraphWriteNodeType::CanEvaluateFromEvent;
			return true;
		}
		return false;
	}

	static bool ResolveDelegateOwnerInstance(const FString& InValue, ESMDelegateOwner& OutOwner)
	{
		const FString Lower = InValue.ToLower();
		if (Lower == TEXT("this") || Lower == TEXT("smdo_this"))
		{
			OutOwner = SMDO_This;
			return true;
		}
		if (Lower == TEXT("context") || Lower == TEXT("smdo_context"))
		{
			OutOwner = SMDO_Context;
			return true;
		}
		if (Lower == TEXT("previous") || Lower == TEXT("previous_state") || Lower == TEXT("previousstate") || Lower == TEXT("smdo_previousstate"))
		{
			OutOwner = SMDO_PreviousState;
			return true;
		}
		return false;
	}
}

FSMAssistOperationResult LD::Assist::SpawnLocalGraphWriteNode(const TSharedRef<FJsonObject>& InArgs)
{
	FString NodeTypeStr;
	if (!InArgs->TryGetStringField(Args::Type, NodeTypeStr) || NodeTypeStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'type'."));
	}

	ISMGraphGeneration::ELocalGraphWriteNodeType NodeType;
	if (!LD::Assist::Private::ResolveLocalGraphWriteType(NodeTypeStr, NodeType))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Unrecognized 'type' '%s'. Accepted: CanEvaluate, CanEvaluateFromEvent."),
			*NodeTypeStr));
	}

	// Shared resolver so a reroute-waypoint guid normalizes to the primary transition, matching get_local_graph/add/connect.
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}
	USMBlueprint* Blueprint = Resolved.Blueprint;
	UEdGraph* TargetGraph = Resolved.Graph;

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateLocalGraphWriteNodeArgs CreateArgs;
	CreateArgs.NodeType = NodeType;
	CreateArgs.TargetGraph = TargetGraph;

	double PosX = 0.0;
	double PosY = 0.0;
	InArgs->TryGetNumberField(Args::PositionX, PosX);
	InArgs->TryGetNumberField(Args::PositionY, PosY);
	CreateArgs.NodePosition = FVector2D(PosX, PosY);

	bool bDefaultValue = false;
	if (InArgs->TryGetBoolField(Args::DefaultValue, bDefaultValue))
	{
		CreateArgs.bDefaultValue = bDefaultValue;
	}

	UEdGraphNode* NewNode = GraphGen->CreateLocalGraphWriteNode(Blueprint, CreateArgs);
	if (!NewNode)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("CreateLocalGraphWriteNode failed for type '%s' on graph '%s' (likely the type is not compatible with the target graph context: CanEvaluate is transition+conduit, CanEvaluateFromEvent is transition only)."),
			*NodeTypeStr, *TargetGraph->GetName()));
	}

	const TSharedRef<FJsonObject> Payload = LD::Assist::Private::LocalGraphNodeToJson(NewNode, true, FString());
	Payload->SetStringField(Args::NodeGuid, NewNode->NodeGuid.ToString());
	Payload->SetStringField(Args::Type, NodeTypeStr);
	Payload->SetStringField(Args::TargetGraphPath, TargetGraph->GetPathName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	// Shared so the OnStateBegin guard in SpawnLocalGraphEventNode keys off the same normalization the
	// resolver does. Widening one without the other turns that guard's advice back into a typo error.
	static FString NormalizeEventTypeKey(const FString& InValue)
	{
		return InValue.ToLower().Replace(TEXT("_"), TEXT(""));
	}

	static bool ResolveLocalGraphEventType(const FString& InValue, ISMGraphGeneration::ELocalGraphEventNodeType& OutType)
	{
		const FString Key = NormalizeEventTypeKey(InValue);
		if (Key == TEXT("oninitialized") || Key == TEXT("initialized"))
		{
			OutType = ISMGraphGeneration::ELocalGraphEventNodeType::OnInitialized;
			return true;
		}
		if (Key == TEXT("onshutdown") || Key == TEXT("shutdown"))
		{
			OutType = ISMGraphGeneration::ELocalGraphEventNodeType::OnShutdown;
			return true;
		}
		if (Key == TEXT("onstateupdate") || Key == TEXT("stateupdate"))
		{
			OutType = ISMGraphGeneration::ELocalGraphEventNodeType::OnStateUpdate;
			return true;
		}
		if (Key == TEXT("onstateend") || Key == TEXT("stateend"))
		{
			OutType = ISMGraphGeneration::ELocalGraphEventNodeType::OnStateEnd;
			return true;
		}
		if (Key == TEXT("ontransitionentered") || Key == TEXT("transitionentered") || Key == TEXT("entered"))
		{
			OutType = ISMGraphGeneration::ELocalGraphEventNodeType::OnTransitionEntered;
			return true;
		}
		if (Key == TEXT("ontransitionpreevaluate") || Key == TEXT("transitionpreevaluate") || Key == TEXT("preevaluate"))
		{
			OutType = ISMGraphGeneration::ELocalGraphEventNodeType::OnTransitionPreEvaluate;
			return true;
		}
		if (Key == TEXT("ontransitionpostevaluate") || Key == TEXT("transitionpostevaluate") || Key == TEXT("postevaluate"))
		{
			OutType = ISMGraphGeneration::ELocalGraphEventNodeType::OnTransitionPostEvaluate;
			return true;
		}
		if (Key == TEXT("onrootstatemachinestart") || Key == TEXT("rootstatemachinestart") || Key == TEXT("statemachinestart"))
		{
			OutType = ISMGraphGeneration::ELocalGraphEventNodeType::OnRootStateMachineStart;
			return true;
		}
		if (Key == TEXT("onrootstatemachinestop") || Key == TEXT("rootstatemachinestop") || Key == TEXT("statemachinestop"))
		{
			OutType = ISMGraphGeneration::ELocalGraphEventNodeType::OnRootStateMachineStop;
			return true;
		}
		return false;
	}
}

FSMAssistOperationResult LD::Assist::SpawnLocalGraphEventNode(const TSharedRef<FJsonObject>& InArgs)
{
	FString NodeTypeStr;
	if (!InArgs->TryGetStringField(Args::Type, NodeTypeStr) || NodeTypeStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'type'."));
	}

	// Shared resolver so a reroute-waypoint guid normalizes to the primary transition, matching get_local_graph/add/connect.
	// Resolved before the type is, because the three state lifecycle names below are only refused on a state graph.
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}
	USMBlueprint* Blueprint = Resolved.Blueprint;
	UEdGraph* TargetGraph = Resolved.Graph;

	const FString NormalizedType = LD::Assist::Private::NormalizeEventTypeKey(NodeTypeStr);
	const bool bIsStateGraph = TargetGraph->IsA<USMStateGraph>();

	// Named as non-spawnable by the op description and by the two state-graph kinds below, so recognize
	// it rather than calling it a typo. It has no ELocalGraphEventNodeType, so it cannot go with them.
	if (bIsStateGraph && (NormalizedType == TEXT("onstatebegin") || NormalizedType == TEXT("statebegin")))
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("'OnStateBegin' cannot be spawned: it is the graph's entry node, present from the moment the state is created and impossible to delete. Call ld.get_local_graph on this node, find the node titled 'On State Begin', and wire from its exec pin."));
	}

	ISMGraphGeneration::ELocalGraphEventNodeType NodeType;
	if (!LD::Assist::Private::ResolveLocalGraphEventType(NodeTypeStr, NodeType))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Unrecognized 'type' '%s'. Accepted: OnInitialized, OnShutdown, OnTransitionEntered, OnTransitionPreEvaluate, OnTransitionPostEvaluate, OnRootStateMachineStart, OnRootStateMachineStop. Snake_case variants accepted too."),
			*NodeTypeStr));
	}

	// Resolved rather than rejected as unrecognized so the caller learns the node exists instead of
	// hunting for a spelling error. On any other graph these are simply incompatible, which the
	// spawn failure below reports.
	if (bIsStateGraph
		&& (NodeType == ISMGraphGeneration::ELocalGraphEventNodeType::OnStateUpdate
			|| NodeType == ISMGraphGeneration::ELocalGraphEventNodeType::OnStateEnd))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("'%s' cannot be spawned: every state graph is created with On State Begin, On State Update, and On State End already in it, and they cannot be deleted. Call ld.get_local_graph on this node, find the node titled '%s', and wire from its exec pin."),
			*NodeTypeStr,
			NodeType == ISMGraphGeneration::ELocalGraphEventNodeType::OnStateUpdate ? TEXT("On State Update") : TEXT("On State End")));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateLocalGraphEventNodeArgs CreateArgs;
	CreateArgs.NodeType = NodeType;
	CreateArgs.TargetGraph = TargetGraph;

	double PosX = 0.0;
	double PosY = 0.0;
	InArgs->TryGetNumberField(Args::PositionX, PosX);
	InArgs->TryGetNumberField(Args::PositionY, PosY);
	CreateArgs.NodePosition = FVector2D(PosX, PosY);

	UEdGraphNode* NewNode = GraphGen->CreateLocalGraphEventNode(Blueprint, CreateArgs);
	if (!NewNode)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("CreateLocalGraphEventNode failed for type '%s' on graph '%s' (the type is not compatible with the target graph context, or a singleton event kind is already present)."),
			*NodeTypeStr, *TargetGraph->GetName()));
	}

	const TSharedRef<FJsonObject> Payload = LD::Assist::Private::LocalGraphNodeToJson(NewNode, true, FString());
	Payload->SetStringField(Args::NodeGuid, NewNode->NodeGuid.ToString());
	Payload->SetStringField(Args::Type, NodeTypeStr);
	Payload->SetStringField(Args::TargetGraphPath, TargetGraph->GetPathName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	// A reroute waypoint owns no compiled graph. Its condition lives on the primary transition, reached
	// through a connected transition edge using only exported API (USMGraphNode_RerouteNode is MinimalAPI,
	// so its own GetPrimaryTransition is not linkable from this module).
	static USMGraphNode_TransitionEdge* FindTransitionEdgeFromReroute(const USMGraphNode_RerouteNode* InReroute)
	{
		if (!InReroute)
		{
			return nullptr;
		}
		for (const UEdGraphPin* Pin : InReroute->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked)
				{
					continue;
				}
				if (USMGraphNode_TransitionEdge* Edge = Cast<USMGraphNode_TransitionEdge>(Linked->GetOwningNode()))
				{
					return Edge;
				}
			}
		}
		return nullptr;
	}

	static bool ResolveLocalGraph(const TSharedRef<FJsonObject>& InArgs, FResolvedLocalGraph& Out, FString& OutError)
	{
		FString AssetPath;
		if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("Missing required arg 'asset_path'.");
			return false;
		}

		FString NodeGuidStr;
		if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr) || NodeGuidStr.IsEmpty())
		{
			OutError = TEXT("Missing required arg 'node_guid' (state, transition, conduit, or reroute node whose local graph to target).");
			return false;
		}

		FGuid NodeGuid;
		if (!FGuid::Parse(NodeGuidStr, NodeGuid))
		{
			OutError = FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr);
			return false;
		}

		Out.Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, OutError);
		if (!Out.Blueprint)
		{
			return false;
		}

		Out.RequestedNode = LD::Assist::Utils::FindNodeByGuid(Out.Blueprint, NodeGuid);
		if (!Out.RequestedNode)
		{
			OutError = FString::Printf(TEXT("No SM graph node with guid '%s' on '%s'."), *NodeGuidStr, *AssetPath);
			return false;
		}

		// Normalize to the node that owns the compiled local graph. Reroute waypoints and non-primary
		// rerouted transition segments delegate their single graph to the primary transition.
		Out.OwnerNode = Out.RequestedNode;
		if (USMGraphNode_RerouteNode* Reroute = Cast<USMGraphNode_RerouteNode>(Out.RequestedNode))
		{
			USMGraphNode_TransitionEdge* Edge = FindTransitionEdgeFromReroute(Reroute);
			if (!Edge)
			{
				OutError = FString::Printf(TEXT("Reroute waypoint '%s' is not connected to a transition, so it owns no local graph. Pass the transition's guid instead."), *NodeGuidStr);
				return false;
			}
			USMGraphNode_TransitionEdge* Primary = Edge->GetPrimaryReroutedTransition();
			Out.OwnerNode = Primary ? Primary : Edge;
			Out.bIsRerouted = true;
		}
		else if (USMGraphNode_TransitionEdge* Edge = Cast<USMGraphNode_TransitionEdge>(Out.RequestedNode))
		{
			Out.bIsRerouted = Edge->IsRerouted();
			if (USMGraphNode_TransitionEdge* Primary = Edge->GetPrimaryReroutedTransition())
			{
				Out.OwnerNode = Primary;
			}
		}

		Out.Graph = Out.OwnerNode->GetBoundGraph();
		if (!Out.Graph)
		{
			OutError = FString::Printf(
				TEXT("Node '%s' (%s) owns no local graph. Any State, Link State, and entry nodes have no editable local graph."),
				*Out.OwnerNode->GetName(), *Out.OwnerNode->GetClass()->GetName());
			return false;
		}
		return true;
	}

	// Locate a node in a graph by its serialized 'id' (object name from get_local_graph) or by its NodeGuid
	// (as returned by the spawn ops), so the write ops compose with either identifier.
	static UEdGraphNode* FindNodeInGraphByIdOrGuid(UEdGraph* InGraph, const FString& InIdOrGuid)
	{
		if (!InGraph || InIdOrGuid.IsEmpty())
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : InGraph->Nodes)
		{
			if (Node && Node->GetName() == InIdOrGuid)
			{
				return Node;
			}
		}
		FGuid AsGuid;
		if (FGuid::Parse(InIdOrGuid, AsGuid))
		{
			for (UEdGraphNode* Node : InGraph->Nodes)
			{
				if (Node && Node->NodeGuid == AsGuid)
				{
					return Node;
				}
			}
		}
		return nullptr;
	}

	static UEdGraphPin* FindPinByNameOrId(UEdGraphNode* InNode, const FString& InNameOrId, EEdGraphPinDirection InDir)
	{
		if (!InNode || InNameOrId.IsEmpty())
		{
			return nullptr;
		}
		FGuid AsPinId;
		const bool bIsPinId = FGuid::Parse(InNameOrId, AsPinId);
		for (UEdGraphPin* PinIt : InNode->Pins)
		{
			if (!PinIt || (InDir != EGPD_MAX && PinIt->Direction != InDir))
			{
				continue;
			}
			if (bIsPinId ? (PinIt->PinId == AsPinId) : (PinIt->PinName.ToString() == InNameOrId))
			{
				return PinIt;
			}
		}
		if (bIsPinId)
		{
			return nullptr;
		}
		for (UEdGraphPin* PinIt : InNode->Pins)
		{
			if (!PinIt || (InDir != EGPD_MAX && PinIt->Direction != InDir))
			{
				continue;
			}
			if (PinIt->PinName.ToString().Equals(InNameOrId, ESearchCase::IgnoreCase))
			{
				return PinIt;
			}
		}
		return nullptr;
	}

	static FString AvailablePinNames(const UEdGraphNode* InNode)
	{
		TArray<FString> Names;
		for (const UEdGraphPin* PinIt : InNode->Pins)
		{
			if (PinIt && !PinIt->bHidden)
			{
				Names.Add(FString::Printf(TEXT("%s(%s)"), *PinIt->PinName.ToString(), PinIt->Direction == EGPD_Input ? TEXT("in") : TEXT("out")));
			}
		}
		return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : TEXT("(none)");
	}

	// Resolve a UFunction for a CallFunction node. When function_class is given it is authoritative. Otherwise
	// the common Kismet libraries and the owning FSM class are searched so simple math/utility calls just work.
	static UFunction* ResolveLocalGraphFunction(const FString& InFuncName, const FString& InClass, UBlueprint* InBlueprint)
	{
		FString LengthError;
		if (InFuncName.IsEmpty()
			|| !LD::Assist::Utils::IsWithinNameLength(InFuncName, TEXT("function_name"), LengthError)
			|| !LD::Assist::Utils::IsWithinNameLength(InClass, TEXT("function_class"), LengthError))
		{
			return nullptr;
		}
		const FName FuncName(*InFuncName);

		if (!InClass.IsEmpty())
		{
			UClass* OwnerClass = nullptr;
			if (InClass.Contains(TEXT("/")) || InClass.Contains(TEXT(".")))
			{
				OwnerClass = FindObject<UClass>(nullptr, *InClass);
				if (!OwnerClass)
				{
					OwnerClass = LoadObject<UClass>(nullptr, *InClass);
				}
			}
			if (!OwnerClass)
			{
				OwnerClass = FindFirstObject<UClass>(*InClass, EFindFirstObjectOptions::NativeFirst);
			}
			return OwnerClass ? OwnerClass->FindFunctionByName(FuncName) : nullptr;
		}

		TArray<UClass*> Candidates;
		Candidates.Add(UKismetMathLibrary::StaticClass());
		Candidates.Add(UKismetSystemLibrary::StaticClass());
		if (InBlueprint && InBlueprint->GeneratedClass)
		{
			Candidates.Add(InBlueprint->GeneratedClass);
		}
		for (UClass* Candidate : Candidates)
		{
			if (Candidate)
			{
				if (UFunction* Found = Candidate->FindFunctionByName(FuncName))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	// Resolve a UClass from a path (/Script/Engine.Actor, /Game/Foo/BP_Bar) or a bare name.
	static UClass* ResolveClassSpec(const FString& InSpec)
	{
		FString LengthError;
		if (InSpec.IsEmpty() || !LD::Assist::Utils::IsWithinNameLength(InSpec, TEXT("class"), LengthError))
		{
			return nullptr;
		}
		UClass* Resolved = nullptr;
		if (InSpec.Contains(TEXT("/")) || InSpec.Contains(TEXT(".")))
		{
			Resolved = FindObject<UClass>(nullptr, *InSpec);
			if (!Resolved)
			{
				Resolved = LoadObject<UClass>(nullptr, *InSpec);
			}
			// Blueprint asset path -> the generated class is <path>.<leaf>_C.
			if (!Resolved && !InSpec.EndsWith(TEXT("_C")))
			{
				int32 LastSlash = INDEX_NONE;
				if (InSpec.FindLastChar(TEXT('/'), LastSlash))
				{
					const FString Leaf = InSpec.Mid(LastSlash + 1);
					Resolved = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("%s.%s_C"), *InSpec, *Leaf));
				}
			}
		}
		if (!Resolved)
		{
			Resolved = FindFirstObject<UClass>(*InSpec, EFindFirstObjectOptions::NativeFirst);
		}
		return Resolved;
	}

	// Resolve the K2 node class to spawn from a class name, a class path, or a friendly alias.
	static UClass* ResolveGraphNodeClass(const FString& InSpec)
	{
		const FString Lower = InSpec.ToLower();
		FString ClassName = InSpec;
		if (Lower == TEXT("call_function") || Lower == TEXT("callfunction") || Lower == TEXT("function"))
		{
			ClassName = TEXT("K2Node_CallFunction");
		}
		else if (Lower == TEXT("branch") || Lower == TEXT("if") || Lower == TEXT("ifthenelse") || Lower == TEXT("if_then_else"))
		{
			ClassName = TEXT("K2Node_IfThenElse");
		}
		else if (Lower == TEXT("get_variable") || Lower == TEXT("variable_get") || Lower == TEXT("variableget") || Lower == TEXT("get"))
		{
			ClassName = TEXT("K2Node_VariableGet");
		}
		else if (Lower == TEXT("set_variable") || Lower == TEXT("variable_set") || Lower == TEXT("variableset") || Lower == TEXT("set"))
		{
			ClassName = TEXT("K2Node_VariableSet");
		}
		else if (Lower == TEXT("sequence") || Lower == TEXT("execution_sequence") || Lower == TEXT("executionsequence"))
		{
			ClassName = TEXT("K2Node_ExecutionSequence");
		}
		else if (Lower == TEXT("cast") || Lower == TEXT("dynamic_cast") || Lower == TEXT("dynamiccast"))
		{
			ClassName = TEXT("K2Node_DynamicCast");
		}
		else if (Lower == TEXT("self") || Lower == TEXT("get_self"))
		{
			ClassName = TEXT("K2Node_Self");
		}

		UClass* Resolved = ResolveClassSpec(ClassName);
		if (!Resolved && !ClassName.StartsWith(TEXT("K2Node_")) && !ClassName.Contains(TEXT("/")) && !ClassName.Contains(TEXT(".")))
		{
			Resolved = ResolveClassSpec(FString(TEXT("K2Node_")) + ClassName);
		}
		return Resolved;
	}

	static TSharedRef<FJsonObject> LocalGraphPinToJson(const UEdGraphPin* InPin)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("id"), InPin->PinId.ToString());
		Obj->SetStringField(TEXT("name"), InPin->PinName.ToString());
		Obj->SetStringField(TEXT("direction"), InPin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		Obj->SetStringField(TEXT("type"), PinTypeToShortString(InPin->PinType));
		if (!InPin->DefaultValue.IsEmpty())
		{
			Obj->SetStringField(TEXT("default_value"), InPin->DefaultValue);
		}
		if (InPin->DefaultObject)
		{
			Obj->SetStringField(TEXT("default_object"), InPin->DefaultObject->GetPathName());
		}

		// Mirror Monolith's "NodeName.PinName" reference form so connect-pin ids line up across transports.
		TArray<TSharedPtr<FJsonValue>> Connected;
		for (const UEdGraphPin* Linked : InPin->LinkedTo)
		{
			if (!Linked || !Linked->GetOwningNode())
			{
				continue;
			}
			Connected.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s.%s"),
				*Linked->GetOwningNode()->GetName(), *Linked->PinName.ToString())));
		}
		Obj->SetArrayField(TEXT("connected_to"), Connected);
		return Obj;
	}

	static TSharedRef<FJsonObject> LocalGraphNodeToJson(UEdGraphNode* InNode, bool bIncludePins, const FString& InResultNodeName)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("id"), InNode->GetName());
		// The write ops here accept either identifier, but every guid-addressed op outside this family
		// needs the guid.
		Obj->SetStringField(Args::NodeGuid, InNode->NodeGuid.ToString());
		Obj->SetStringField(TEXT("class"), InNode->GetClass()->GetName());
		Obj->SetStringField(TEXT("title"), InNode->GetNodeTitle(ENodeTitleType::ListView).ToString());

		TArray<TSharedPtr<FJsonValue>> Pos;
		Pos.Add(MakeShared<FJsonValueNumber>(InNode->NodePosX));
		Pos.Add(MakeShared<FJsonValueNumber>(InNode->NodePosY));
		Obj->SetArrayField(TEXT("pos"), Pos);

		if (!InNode->NodeComment.IsEmpty())
		{
			Obj->SetStringField(TEXT("comment"), InNode->NodeComment);
		}
		if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(InNode))
		{
			Obj->SetStringField(TEXT("function"), CallNode->FunctionReference.GetMemberName().ToString());
		}
		if (!InResultNodeName.IsEmpty() && InNode->GetName() == InResultNodeName)
		{
			Obj->SetBoolField(TEXT("is_result"), true);
		}

		if (bIncludePins)
		{
			TArray<TSharedPtr<FJsonValue>> Pins;
			for (const UEdGraphPin* Pin : InNode->Pins)
			{
				if (!Pin || Pin->bHidden)
				{
					continue;
				}
				Pins.Add(MakeShared<FJsonValueObject>(LocalGraphPinToJson(Pin)));
			}
			Obj->SetArrayField(TEXT("pins"), Pins);
		}
		return Obj;
	}
}

FSMAssistOperationResult LD::Assist::GetLocalGraph(const TSharedRef<FJsonObject>& InArgs)
{
	bool bIncludePins = true;
	InArgs->TryGetBoolField(Args::IncludePins, bIncludePins);

	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	USMBlueprint* Blueprint = Resolved.Blueprint;
	USMGraphNode_Base* OwnerNode = Resolved.OwnerNode;
	UEdGraph* Graph = Resolved.Graph;
	const bool bIsRerouted = Resolved.bIsRerouted;

	FString NodeGuidStr;
	InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr);

	// For condition-style graphs (transition / conduit) surface the wire-INTO anchor: the evaluation
	// pin a boolean condition connects to (the same pin ld.set_transition_condition writes a literal to).
	FString ResultNodeName;
	UEdGraphPin* ResultPin = nullptr;
	if (USMTransitionGraph* TransitionGraph = Cast<USMTransitionGraph>(Graph))
	{
		if (TransitionGraph->ResultNode)
		{
			ResultNodeName = TransitionGraph->ResultNode->GetName();
			ResultPin = TransitionGraph->ResultNode->GetTransitionEvaluationPin();
		}
	}
	else if (USMConduitGraph* ConduitGraph = Cast<USMConduitGraph>(Graph))
	{
		if (ConduitGraph->ResultNode)
		{
			ResultNodeName = ConduitGraph->ResultNode->GetName();
			ResultPin = ConduitGraph->ResultNode->GetTransitionEvaluationPin();
		}
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::RequestedNodeGuid, NodeGuidStr);
	Payload->SetStringField(Args::NodeGuid, OwnerNode->NodeGuid.ToString());
	Payload->SetStringField(Args::NodeClass, OwnerNode->GetClass()->GetName());
	Payload->SetStringField(Args::NodeKind, OwnerNode->GetFriendlyNodeName().ToString());
	Payload->SetBoolField(Args::IsRerouted, bIsRerouted);
	Payload->SetStringField(Args::GraphName, Graph->GetName());
	Payload->SetStringField(Args::GraphPath, Graph->GetPathName());
	Payload->SetStringField(Args::GraphGuid, Graph->GraphGuid.ToString());
	if (!ResultNodeName.IsEmpty())
	{
		Payload->SetStringField(Args::ResultNodeName, ResultNodeName);
	}
	if (ResultPin)
	{
		Payload->SetStringField(Args::ResultPinId, ResultPin->PinId.ToString());
		Payload->SetStringField(Args::ResultPinName, ResultPin->PinName.ToString());
	}

	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		Nodes.Add(MakeShared<FJsonValueObject>(LD::Assist::Private::LocalGraphNodeToJson(Node, bIncludePins, ResultNodeName)));
	}
	Payload->SetNumberField(Args::NodeCount, Nodes.Num());
	Payload->SetArrayField(Args::Nodes, Nodes);

	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::CaptureLocalGraph(const TSharedRef<FJsonObject>& InArgs)
{
	check(IsInGameThread());

	bool bClipToPanel = true;
	InArgs->TryGetBoolField(Args::ClipToPanel, bClipToPanel);

	bool bFitToContent = true;
	InArgs->TryGetBoolField(Args::FitToContent, bFitToContent);

	FString OutputSubdir = TEXT("LogicDriver");
	InArgs->TryGetStringField(Args::OutputSubdir, OutputSubdir);

	FString Prefix;
	InArgs->TryGetStringField(Args::Prefix, Prefix);

	// Resolve asset_path + node_guid to the node's bound graph, normalizing reroutes to the primary
	// transition exactly like ld.get_local_graph. Capturing this graph, rather than the root graph, is the
	// whole difference between this op and ld.capture_graph_view.
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}

	FString EditorError;
	FBlueprintEditor* BlueprintEditor = LD::Assist::GraphView::FindOrOpenBlueprintEditor(Resolved.Blueprint, EditorError);
	if (!BlueprintEditor)
	{
		return FSMAssistOperationResult::MakeError(EditorError);
	}

	const FString DefaultPrefixBase = FString::Printf(TEXT("%s_%s"), *Resolved.Blueprint->GetName(), *Resolved.Graph->GetName());
	return LD::Assist::GraphView::CaptureGraphToPng(
		BlueprintEditor, Resolved.Graph, Resolved.Blueprint, /*InFocusNode=*/nullptr,
		bClipToPanel, bFitToContent, OutputSubdir, Prefix, DefaultPrefixBase);
}

FSMAssistOperationResult LD::Assist::AddLocalGraphNode(const TSharedRef<FJsonObject>& InArgs)
{
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}
	UEdGraph* Graph = Resolved.Graph;
	USMBlueprint* Blueprint = Resolved.Blueprint;

	// 'node_class' is the generic spec (any K2 node class or a friendly alias). 'kind' is an accepted synonym.
	FString NodeSpec;
	if ((!InArgs->TryGetStringField(Args::NodeClass, NodeSpec) || NodeSpec.IsEmpty())
		&& (!InArgs->TryGetStringField(Args::Kind, NodeSpec) || NodeSpec.IsEmpty()))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_class' (any K2 node class such as K2Node_IfThenElse or K2Node_CallFunction, or a friendly alias: call_function, branch, get_variable, set_variable, sequence, cast, self)."));
	}

	UClass* NodeClass = LD::Assist::Private::ResolveGraphNodeClass(NodeSpec);
	if (!NodeClass)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Could not resolve node class '%s'. Pass a UK2Node class name (e.g. K2Node_MakeArray), a full class path, or a friendly alias."), *NodeSpec));
	}
	if (!NodeClass->IsChildOf(UK2Node::StaticClass()))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("'%s' resolves to '%s', which is not a UK2Node class."), *NodeSpec, *NodeClass->GetName()));
	}
	if (NodeClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Node class '%s' is abstract and cannot be spawned."), *NodeClass->GetName()));
	}

	// Passing the UK2Node check is not enough: Logic Driver structural nodes are created only by the
	// plugin's own machinery (the compiler CastChecked's them), a state machine graph accepts no plain
	// K2 nodes, and schema-foreign classes crash in PostPlacedNewNode (an AnimGraphNode_* CastChecked's
	// its blueprint to UAnimBlueprint). Reject each before the node exists.
	if (NodeClass->IsChildOf(USMGraphK2Node_Base::StaticClass()))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Node class '%s' is a Logic Driver structural node managed by the plugin's own tooling and cannot be spawned into a local graph."),
			*NodeClass->GetName()));
	}
	if (Cast<USMGraph>(Graph))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("The target graph '%s' is a state machine graph; K2 nodes can only be added to bound logic graphs (state, transition, or conduit graphs)."),
			*Graph->GetName()));
	}
	const UK2Node* NodeCDO = NodeClass->GetDefaultObject<UK2Node>();
	if (!NodeCDO || !NodeCDO->IsCompatibleWithGraph(Graph))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Node class '%s' is not compatible with graph '%s'."),
			*NodeClass->GetName(), *Graph->GetName()));
	}

	// Validate the reference-bearing node kinds before creating anything, so a bad request leaves no orphan.
	UFunction* CallTarget = nullptr;
	FProperty* VarProperty = nullptr;
	FString VarName;
	UClass* CastTarget = nullptr;

	if (NodeClass->IsChildOf(UK2Node_CallFunction::StaticClass()))
	{
		FString FuncName;
		if (!InArgs->TryGetStringField(Args::FunctionName, FuncName) || FuncName.IsEmpty())
		{
			return FSMAssistOperationResult::MakeError(TEXT("A call_function node requires 'function_name'."));
		}
		FString FuncClass;
		InArgs->TryGetStringField(Args::FunctionClass, FuncClass);
		CallTarget = LD::Assist::Private::ResolveLocalGraphFunction(FuncName, FuncClass, Blueprint);
		if (!CallTarget)
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(
				TEXT("Could not resolve function '%s'%s. Supply 'function_class' (e.g. /Script/Engine.KismetMathLibrary) when it is not on KismetMathLibrary, KismetSystemLibrary, or the FSM class."),
				*FuncName, FuncClass.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on class '%s'"), *FuncClass)));
		}
	}
	else if (NodeClass->IsChildOf(UK2Node_Variable::StaticClass()))
	{
		if (!InArgs->TryGetStringField(Args::VariableName, VarName) || VarName.IsEmpty())
		{
			return FSMAssistOperationResult::MakeError(TEXT("A variable get/set node requires 'variable_name' (a member variable on the FSM blueprint)."));
		}
		FString VarLengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(VarName, TEXT("variable_name"), VarLengthError))
		{
			return FSMAssistOperationResult::MakeError(VarLengthError);
		}
		const FName VarFName(*VarName);
		if (Blueprint->SkeletonGeneratedClass)
		{
			VarProperty = FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, VarFName);
		}
		if (!VarProperty && Blueprint->GeneratedClass)
		{
			VarProperty = FindFProperty<FProperty>(Blueprint->GeneratedClass, VarFName);
		}
		if (!VarProperty)
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(
				TEXT("Variable '%s' was not found on the FSM blueprint. Add it with ld.add_sm_variable and ld.compile before referencing it."), *VarName));
		}
	}
	else if (NodeClass->IsChildOf(UK2Node_DynamicCast::StaticClass()))
	{
		FString TargetClassStr;
		if (!InArgs->TryGetStringField(Args::TargetClass, TargetClassStr) || TargetClassStr.IsEmpty())
		{
			return FSMAssistOperationResult::MakeError(TEXT("A cast node requires 'target_class' (the class to cast to; there is no separate op to set it afterward)."));
		}
		CastTarget = LD::Assist::Private::ResolveClassSpec(TargetClassStr);
		if (!CastTarget)
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Could not resolve cast 'target_class' '%s'."), *TargetClassStr));
		}
	}

	double PosX = 0.0;
	double PosY = 0.0;
	InArgs->TryGetNumberField(Args::PositionX, PosX);
	InArgs->TryGetNumberField(Args::PositionY, PosY);

	// Spawn generically, mirroring UEdGraph::CreateNode: RF_Transactional so the node is undo-recordable,
	// RF_Transient propagated from a transient graph, and the reference configured before pins allocate.
	// AddNode does not Modify the graph itself, so record it explicitly inside the op transaction.
	const FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistAddLocalGraphNode", "Add Local Graph Node (Assist)"));
	Graph->Modify();
	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
	if (Graph->HasAnyFlags(RF_Transient))
	{
		NewNode->SetFlags(RF_Transient);
	}
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(NewNode))
	{
		CallNode->SetFromFunction(CallTarget);
	}
	else if (UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(NewNode))
	{
		VarNode->SetFromProperty(VarProperty, true, nullptr);
	}
	else if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(NewNode))
	{
		CastNode->TargetType = CastTarget;
	}

	Graph->AddNode(NewNode, false, false);
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	if (NewNode->Pins.Num() == 0)
	{
		NewNode->AllocateDefaultPins();
	}
	NewNode->NodePosX = static_cast<int32>(PosX);
	NewNode->NodePosY = static_cast<int32>(PosY);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	const TSharedRef<FJsonObject> Payload = LD::Assist::Private::LocalGraphNodeToJson(NewNode, true, FString());
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::NodeGuid, NewNode->NodeGuid.ToString());
	Payload->SetStringField(Args::NodeClass, NewNode->GetClass()->GetName());
	Payload->SetStringField(Args::GraphName, Graph->GetName());
	Payload->SetStringField(Args::GraphPath, Graph->GetPathName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::ConnectLocalGraphPins(const TSharedRef<FJsonObject>& InArgs)
{
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}
	UEdGraph* Graph = Resolved.Graph;

	FString FromNodeId;
	FString FromPinStr;
	FString ToNodeId;
	FString ToPinStr;
	if (!InArgs->TryGetStringField(Args::FromNodeId, FromNodeId) || FromNodeId.IsEmpty()
		|| !InArgs->TryGetStringField(Args::FromPin, FromPinStr) || FromPinStr.IsEmpty()
		|| !InArgs->TryGetStringField(Args::ToNodeId, ToNodeId) || ToNodeId.IsEmpty()
		|| !InArgs->TryGetStringField(Args::ToPin, ToPinStr) || ToPinStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Requires 'from_node_id', 'from_pin', 'to_node_id', 'to_pin'. Node ids and pins accept the values from ld.get_local_graph (or a node guid / pin id)."));
	}

	UEdGraphNode* FromNode = LD::Assist::Private::FindNodeInGraphByIdOrGuid(Graph, FromNodeId);
	if (!FromNode)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No node '%s' in local graph '%s'."), *FromNodeId, *Graph->GetName()));
	}
	UEdGraphNode* ToNode = LD::Assist::Private::FindNodeInGraphByIdOrGuid(Graph, ToNodeId);
	if (!ToNode)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No node '%s' in local graph '%s'."), *ToNodeId, *Graph->GetName()));
	}

	UEdGraphPin* SourcePin = LD::Assist::Private::FindPinByNameOrId(FromNode, FromPinStr, EGPD_Output);
	if (!SourcePin)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No output pin '%s' on node '%s'. Available: %s."),
			*FromPinStr, *FromNodeId, *LD::Assist::Private::AvailablePinNames(FromNode)));
	}
	UEdGraphPin* DestPin = LD::Assist::Private::FindPinByNameOrId(ToNode, ToPinStr, EGPD_Input);
	if (!DestPin)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No input pin '%s' on node '%s'. Available: %s."),
			*ToPinStr, *ToNodeId, *LD::Assist::Private::AvailablePinNames(ToNode)));
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Local graph has no schema."));
	}

	// TryCreateConnection can reconstruct nodes (wildcard/promotion/MakeArray split pins), freeing the pin
	// objects, so snapshot the display refs before wiring.
	const FString SourceRef = FString::Printf(TEXT("%s.%s"), *FromNode->GetName(), *SourcePin->PinName.ToString());
	const FString DestRef = FString::Printf(TEXT("%s.%s"), *ToNode->GetName(), *DestPin->PinName.ToString());

	const FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, DestPin);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Cannot connect %s -> %s: %s"),
			*SourceRef, *DestRef, *Response.Message.ToString()));
	}
	{
		// TryCreateConnection Modifies both endpoint nodes but nothing opens a transaction on this
		// path; scope it so the link participates in undo instead of silently mutating.
		FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistConnectLocalGraphPins", "Connect Local Graph Pins (Assist)"));
		if (!Schema->TryCreateConnection(SourcePin, DestPin))
		{
			Transaction.Cancel();
			return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Connection failed for %s -> %s."), *SourceRef, *DestRef));
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Resolved.Blueprint);
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Resolved.Blueprint->GetPathName());
	Payload->SetStringField(Args::GraphName, Graph->GetName());
	Payload->SetStringField(Args::FromNodeId, SourceRef);
	Payload->SetStringField(Args::ToNodeId, DestRef);
	Payload->SetBoolField(Args::Connected, true);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SetLocalGraphPinDefault(const TSharedRef<FJsonObject>& InArgs)
{
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}
	UEdGraph* Graph = Resolved.Graph;

	FString NodeId;
	FString PinStr;
	if (!InArgs->TryGetStringField(Args::NodeId, NodeId) || NodeId.IsEmpty()
		|| !InArgs->TryGetStringField(Args::Pin, PinStr) || PinStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Requires 'node_id' and 'pin'."));
	}
	FString Value;
	if (!InArgs->TryGetStringField(Args::Value, Value))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Requires 'value' (the literal to set on the input pin)."));
	}

	UEdGraphNode* Node = LD::Assist::Private::FindNodeInGraphByIdOrGuid(Graph, NodeId);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No node '%s' in local graph '%s'."), *NodeId, *Graph->GetName()));
	}
	UEdGraphPin* TargetPin = LD::Assist::Private::FindPinByNameOrId(Node, PinStr, EGPD_Input);
	if (!TargetPin)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No input pin '%s' on node '%s'. Available: %s."),
			*PinStr, *NodeId, *LD::Assist::Private::AvailablePinNames(Node)));
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Local graph has no schema."));
	}

	// TrySetDefaultValue writes the pin fields without a Modify of its own; record the owning node
	// inside an op transaction so the default participates in undo.
	const FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistSetLocalGraphPinDefault", "Set Local Graph Pin Default (Assist)"));
	Node->Modify();
	Schema->TrySetDefaultValue(*TargetPin, Value);
	Node->PinDefaultValueChanged(TargetPin);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Resolved.Blueprint);

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Resolved.Blueprint->GetPathName());
	Payload->SetStringField(Args::NodeId, Node->GetName());
	Payload->SetStringField(Args::Pin, TargetPin->PinName.ToString());
	Payload->SetStringField(Args::Value, TargetPin->DefaultValue);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::RemoveLocalGraphNode(const TSharedRef<FJsonObject>& InArgs)
{
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}
	UEdGraph* Graph = Resolved.Graph;

	FString NodeId;
	if (!InArgs->TryGetStringField(Args::NodeId, NodeId) || NodeId.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Requires 'node_id' (the node id from ld.get_local_graph, or a node guid)."));
	}

	UEdGraphNode* Node = LD::Assist::Private::FindNodeInGraphByIdOrGuid(Graph, NodeId);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No node '%s' in local graph '%s'."), *NodeId, *Graph->GetName()));
	}

	// The transition/conduit result node and state entry nodes are structural roots. The editor forbids
	// deleting them, and so do we (removing them would break the bound graph).
	if (!Node->CanUserDeleteNode())
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Node '%s' (%s) is a required root node (e.g. the transition result or a state entry node) and cannot be removed."),
			*Node->GetName(), *Node->GetClass()->GetName()));
	}

	const FString RemovedName = Node->GetName();
	{
		// RemoveNode Modifies the graph, node, and link counterparts but opens no transaction on
		// this path; scope it so undo restores the node with its links.
		const FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistRemoveLocalGraphNode", "Remove Local Graph Node (Assist)"));
		FBlueprintEditorUtils::RemoveNode(Resolved.Blueprint, Node, true);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Resolved.Blueprint);
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Resolved.Blueprint->GetPathName());
	Payload->SetStringField(Args::GraphName, Graph->GetName());
	Payload->SetStringField(Args::NodeId, RemovedName);
	Payload->SetBoolField(TEXT("removed"), true);
	Payload->SetNumberField(Args::NodeCount, Graph->Nodes.Num());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::DisconnectLocalGraphPins(const TSharedRef<FJsonObject>& InArgs)
{
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}
	UEdGraph* Graph = Resolved.Graph;

	FString FromNodeId;
	FString FromPinStr;
	FString ToNodeId;
	FString ToPinStr;
	if (!InArgs->TryGetStringField(Args::FromNodeId, FromNodeId) || FromNodeId.IsEmpty()
		|| !InArgs->TryGetStringField(Args::FromPin, FromPinStr) || FromPinStr.IsEmpty()
		|| !InArgs->TryGetStringField(Args::ToNodeId, ToNodeId) || ToNodeId.IsEmpty()
		|| !InArgs->TryGetStringField(Args::ToPin, ToPinStr) || ToPinStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Requires 'from_node_id', 'from_pin', 'to_node_id', 'to_pin' (the same identifiers ld.connect_local_graph_pins uses)."));
	}

	UEdGraphNode* FromNode = LD::Assist::Private::FindNodeInGraphByIdOrGuid(Graph, FromNodeId);
	if (!FromNode)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No node '%s' in local graph '%s'."), *FromNodeId, *Graph->GetName()));
	}
	UEdGraphNode* ToNode = LD::Assist::Private::FindNodeInGraphByIdOrGuid(Graph, ToNodeId);
	if (!ToNode)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No node '%s' in local graph '%s'."), *ToNodeId, *Graph->GetName()));
	}

	UEdGraphPin* SourcePin = LD::Assist::Private::FindPinByNameOrId(FromNode, FromPinStr, EGPD_MAX);
	if (!SourcePin)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No pin '%s' on node '%s'. Available: %s."),
			*FromPinStr, *FromNodeId, *LD::Assist::Private::AvailablePinNames(FromNode)));
	}
	UEdGraphPin* DestPin = LD::Assist::Private::FindPinByNameOrId(ToNode, ToPinStr, EGPD_MAX);
	if (!DestPin)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No pin '%s' on node '%s'. Available: %s."),
			*ToPinStr, *ToNodeId, *LD::Assist::Private::AvailablePinNames(ToNode)));
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Local graph has no schema."));
	}

	// BreakSinglePinLink can reconstruct nodes (e.g. MakeArray split pins), freeing the pin objects, so
	// snapshot the linked state and display refs before breaking.
	const bool bWereLinked = SourcePin->LinkedTo.Contains(DestPin);
	const FString SourceRef = FString::Printf(TEXT("%s.%s"), *FromNode->GetName(), *SourcePin->PinName.ToString());
	const FString DestRef = FString::Printf(TEXT("%s.%s"), *ToNode->GetName(), *DestPin->PinName.ToString());
	{
		// BreakLinkTo Modifies both endpoint nodes but nothing opens a transaction on this path;
		// scope it so the disconnect participates in undo.
		const FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistDisconnectLocalGraphPins", "Disconnect Local Graph Pins (Assist)"));
		Schema->BreakSinglePinLink(SourcePin, DestPin);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Resolved.Blueprint);
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Resolved.Blueprint->GetPathName());
	Payload->SetStringField(Args::GraphName, Graph->GetName());
	Payload->SetStringField(Args::FromNodeId, SourceRef);
	Payload->SetStringField(Args::ToNodeId, DestRef);
	Payload->SetBoolField(TEXT("disconnected"), bWereLinked);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SetLocalGraphNode(const TSharedRef<FJsonObject>& InArgs)
{
	FString Error;
	LD::Assist::Private::FResolvedLocalGraph Resolved;
	if (!LD::Assist::Private::ResolveLocalGraph(InArgs, Resolved, Error))
	{
		return FSMAssistOperationResult::MakeError(Error);
	}
	UEdGraph* Graph = Resolved.Graph;

	FString NodeId;
	if (!InArgs->TryGetStringField(Args::NodeId, NodeId) || NodeId.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Requires 'node_id' (the node id from ld.get_local_graph, or a node guid)."));
	}

	UEdGraphNode* Node = LD::Assist::Private::FindNodeInGraphByIdOrGuid(Graph, NodeId);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No node '%s' in local graph '%s'."), *NodeId, *Graph->GetName()));
	}

	bool bAny = false;
	bool bStructural = false;

	{
		// Direct field writes have no Modify of their own; record the node inside an op transaction
		// so the edit participates in undo. Canceled when no recognized field was supplied.
		FScopedTransaction Transaction(NSLOCTEXT("SMAssistOperations", "AssistSetLocalGraphNode", "Set Local Graph Node (Assist)"));
		Node->Modify();

		double PosX = 0.0;
		if (InArgs->TryGetNumberField(Args::PositionX, PosX))
		{
			Node->NodePosX = static_cast<int32>(PosX);
			bAny = true;
		}
		double PosY = 0.0;
		if (InArgs->TryGetNumberField(Args::PositionY, PosY))
		{
			Node->NodePosY = static_cast<int32>(PosY);
			bAny = true;
		}
		FString Comment;
		if (InArgs->TryGetStringField(Args::Comment, Comment))
		{
			Node->NodeComment = Comment;
			bAny = true;
		}
		bool bEnabled = true;
		if (InArgs->TryGetBoolField(Args::Enabled, bEnabled))
		{
			Node->SetEnabledState(bEnabled ? ENodeEnabledState::Enabled : ENodeEnabledState::Disabled, true);
			bAny = true;
			bStructural = true;
		}

		if (!bAny)
		{
			Transaction.Cancel();
			return FSMAssistOperationResult::MakeError(TEXT("Requires at least one field to change: 'position_x', 'position_y', 'comment', or 'enabled'."));
		}

		// Enabled state feeds compilation. Position and comment are cosmetic and only need the blueprint dirtied.
		if (bStructural)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Resolved.Blueprint);
		}
		else
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Resolved.Blueprint);
		}
	}

	const TSharedRef<FJsonObject> Payload = LD::Assist::Private::LocalGraphNodeToJson(Node, false, FString());
	Payload->SetStringField(Args::AssetPath, Resolved.Blueprint->GetPathName());
	Payload->SetStringField(Args::GraphName, Graph->GetName());
	Payload->SetBoolField(Args::Enabled, Node->IsNodeEnabled());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::ConfigureTransitionEvent(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString TransitionGuidStr;
	if (!InArgs->TryGetStringField(Args::TransitionGuid, TransitionGuidStr) || TransitionGuidStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'transition_guid'."));
	}

	FGuid TransitionGuid;
	if (!FGuid::Parse(TransitionGuidStr, TransitionGuid))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Invalid 'transition_guid' '%s'."), *TransitionGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, TransitionGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No node with guid '%s' on '%s'."), *TransitionGuidStr, *AssetPath));
	}

	USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node);
	if (!TransitionEdge)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Node '%s' is not a transition edge. configure_transition_event only operates on USMGraphNode_TransitionEdge instances."),
			*TransitionGuidStr));
	}

	ISMGraphGeneration::FConfigureTransitionEventArgs ConfigureArgs;
	TArray<FString> AppliedFields;

	FString OwnerInstanceStr;
	if (InArgs->TryGetStringField(Args::DelegateOwnerInstance, OwnerInstanceStr))
	{
		ESMDelegateOwner OwnerInstance;
		if (!LD::Assist::Private::ResolveDelegateOwnerInstance(OwnerInstanceStr, OwnerInstance))
		{
			return FSMAssistOperationResult::MakeError(FString::Printf(
				TEXT("Unrecognized 'delegate_owner_instance' '%s'. Accepted: This, Context, PreviousState."),
				*OwnerInstanceStr));
		}
		ConfigureArgs.DelegateOwnerInstance = OwnerInstance;
		AppliedFields.Add(Args::DelegateOwnerInstance);
	}

	FString OwnerClassPath;
	if (InArgs->TryGetStringField(Args::DelegateOwnerClass, OwnerClassPath))
	{
		if (OwnerClassPath.IsEmpty())
		{
			ConfigureArgs.DelegateOwnerClass = TSubclassOf<UObject>(nullptr);
			AppliedFields.Add(Args::DelegateOwnerClass);
		}
		else
		{
			FString OwnerLengthError;
			if (!LD::Assist::Utils::IsWithinNameLength(OwnerClassPath, TEXT("delegate_owner_class"), OwnerLengthError))
			{
				return FSMAssistOperationResult::MakeError(OwnerLengthError);
			}
			UClass* OwnerClass = LoadClass<UObject>(nullptr, *OwnerClassPath);
			if (!OwnerClass)
			{
				return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Could not load 'delegate_owner_class' '%s'."), *OwnerClassPath));
			}
			ConfigureArgs.DelegateOwnerClass = TSubclassOf<UObject>(OwnerClass);
			AppliedFields.Add(Args::DelegateOwnerClass);
		}
	}

	FString DelegateName;
	if (InArgs->TryGetStringField(Args::DelegatePropertyName, DelegateName))
	{
		FString DelegateLengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(DelegateName, TEXT("delegate_property_name"), DelegateLengthError))
		{
			return FSMAssistOperationResult::MakeError(DelegateLengthError);
		}
		ConfigureArgs.DelegatePropertyName = DelegateName.IsEmpty() ? FName(NAME_None) : FName(*DelegateName);
		AppliedFields.Add(Args::DelegatePropertyName);
	}

	bool bTargetedUpdate = false;
	if (InArgs->TryGetBoolField(Args::EventTriggersTargetedUpdate, bTargetedUpdate))
	{
		ConfigureArgs.bEventTriggersTargetedUpdate = bTargetedUpdate;
		AppliedFields.Add(Args::EventTriggersTargetedUpdate);
	}

	bool bFullUpdate = false;
	if (InArgs->TryGetBoolField(Args::EventTriggersFullUpdate, bFullUpdate))
	{
		ConfigureArgs.bEventTriggersFullUpdate = bFullUpdate;
		AppliedFields.Add(Args::EventTriggersFullUpdate);
	}

	if (AppliedFields.Num() == 0)
	{
		return FSMAssistOperationResult::MakeError(TEXT("configure_transition_event requires at least one of 'delegate_owner_instance', 'delegate_owner_class', 'delegate_property_name', 'event_triggers_targeted_update', 'event_triggers_full_update'."));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	const bool bApplied = GraphGen->ConfigureTransitionEvent(TransitionEdge, ConfigureArgs);
	if (!bApplied)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(
			TEXT("Failed to configure the transition event on transition '%s' (verify delegate_owner_instance / delegate_owner_class / delegate_property_name)."),
			*TransitionEdge->NodeGuid.ToString()));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::TransitionGuid, TransitionEdge->NodeGuid.ToString());
	Payload->SetBoolField(Args::Applied, bApplied);
	{
		TArray<TSharedPtr<FJsonValue>> AppliedJson;
		for (const FString& Field : AppliedFields)
		{
			AppliedJson.Add(MakeShared<FJsonValueString>(Field));
		}
		Payload->SetArrayField(Args::AppliedFields, AppliedJson);
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::FindNodeTypes(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr) || NodeGuidStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid' (state, transition, or conduit guid whose bound graph receives the discovery query)."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString TypeIdFilter;
	InArgs->TryGetStringField(Args::TypeIdFilter, TypeIdFilter);

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* OwnerNode = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!OwnerNode)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("No state, transition, or conduit node with guid '%s' on '%s'."), *NodeGuidStr, *AssetPath));
	}

	UEdGraph* TargetGraph = OwnerNode->GetBoundGraph();
	if (!TargetGraph)
	{
		return FSMAssistOperationResult::MakeError(FString::Printf(TEXT("Owner node '%s' has no bound graph."), *OwnerNode->GetName()));
	}

	LD::Assist::FFindLocalGraphNodeTypesArgs FindArgs;
	FindArgs.TargetGraph = TargetGraph;
	FindArgs.TypeIdFilter = TypeIdFilter;

	const LD::Assist::FFindLocalGraphNodeTypesResult Result = LD::Assist::FindLocalGraphNodeTypes(Blueprint, FindArgs);

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::TargetGraphPath, TargetGraph->GetPathName());

	TArray<TSharedPtr<FJsonValue>> ReadKindsJson;
	for (ISMGraphGeneration::ELocalGraphReadNodeType Kind : Result.ReadKinds)
	{
		const TSharedRef<FJsonObject> KindObj = MakeShared<FJsonObject>();
		KindObj->SetStringField(Args::Kind, Private::ReadKindName(Kind));
		KindObj->SetStringField(Args::SpawnOp, Ops::SpawnLocalGraphReadNode);
		KindObj->SetStringField(Args::SpawnType, Private::ReadKindName(Kind));
		ReadKindsJson.Add(MakeShared<FJsonValueObject>(KindObj));
	}
	Payload->SetArrayField(Args::ReadKinds, ReadKindsJson);

	TArray<TSharedPtr<FJsonValue>> WriteKindsJson;
	for (ISMGraphGeneration::ELocalGraphWriteNodeType Kind : Result.WriteKinds)
	{
		const TSharedRef<FJsonObject> KindObj = MakeShared<FJsonObject>();
		KindObj->SetStringField(Args::Kind, Private::WriteKindName(Kind));
		KindObj->SetStringField(Args::SpawnOp, Ops::SpawnLocalGraphWriteNode);
		KindObj->SetStringField(Args::SpawnType, Private::WriteKindName(Kind));
		WriteKindsJson.Add(MakeShared<FJsonValueObject>(KindObj));
	}
	Payload->SetArrayField(Args::WriteKinds, WriteKindsJson);

	TArray<TSharedPtr<FJsonValue>> EventKindsJson;
	for (ISMGraphGeneration::ELocalGraphEventNodeType Kind : Result.EventKinds)
	{
		const TSharedRef<FJsonObject> KindObj = MakeShared<FJsonObject>();
		KindObj->SetStringField(Args::Kind, Private::EventKindName(Kind));
		KindObj->SetStringField(Args::SpawnOp, Ops::SpawnLocalGraphEventNode);
		KindObj->SetStringField(Args::SpawnType, Private::EventKindName(Kind));
		EventKindsJson.Add(MakeShared<FJsonValueObject>(KindObj));
	}
	Payload->SetArrayField(Args::EventKinds, EventKindsJson);

	Payload->SetStringField(Args::EngineNodesHint,
		TEXT("Engine K2 nodes (math, function calls, etc.) are not enumerated here. As of UE 5.8, BlueprintTools.find_node_types rejects SM transition/conduit bound graphs with 'Cannot cast type ... to Blueprint'. Workaround: query find_node_types against any non-SM UBlueprint's EventGraph using the same type_id_filter; type_ids are universal across graphs, so the returned strings work in BlueprintTools.create_node when targeting an SM nested graph."));

	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static bool ResolveDirection(const FString& InValue, ESMGraphPropertyDirection& OutDirection)
	{
		if (InValue.Equals(TEXT("Input"), ESearchCase::IgnoreCase))
		{
			OutDirection = ESMGraphPropertyDirection::Input;
			return true;
		}
		if (InValue.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
		{
			OutDirection = ESMGraphPropertyDirection::Output;
			return true;
		}
		if (InValue.Equals(TEXT("Both"), ESearchCase::IgnoreCase))
		{
			OutDirection = ESMGraphPropertyDirection::Both;
			return true;
		}
		return false;
	}

	static const TCHAR* DirectionToString(ESMGraphPropertyDirection InDirection)
	{
		switch (InDirection)
		{
		case ESMGraphPropertyDirection::Input:
			return TEXT("Input");
		case ESMGraphPropertyDirection::Output:
			return TEXT("Output");
		case ESMGraphPropertyDirection::Both:
			return TEXT("Both");
		}
		return TEXT("");
	}

	static UBlueprint* LoadNodeClassBlueprint(const FString& InAssetPath, FString& OutError)
	{
		if (!LD::Assist::Utils::IsWithinNameLength(InAssetPath, TEXT("asset_path"), OutError))
		{
			return nullptr;
		}
		UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *InAssetPath);
		if (!BP)
		{
			OutError = FString::Printf(TEXT("Could not load 'asset_path' '%s'."), *InAssetPath);
			return nullptr;
		}
		if (!BP->ParentClass || !BP->ParentClass->IsChildOf(USMNodeInstance::StaticClass()))
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' is not a USMNodeInstance subclass."), *InAssetPath);
			return nullptr;
		}
		return BP;
	}

	static bool IsTransitionClassBlueprint(const UBlueprint* InBP)
	{
		return InBP && InBP->ParentClass && InBP->ParentClass->IsChildOf(USMTransitionInstance::StaticClass());
	}
}

FSMAssistOperationResult LD::Assist::AddNodeVariable(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString VarName;
	if (!InArgs->TryGetStringField(Args::VariableName, VarName) || VarName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'variable_name'."));
	}

	FString VarType;
	if (!InArgs->TryGetStringField(Args::VarType, VarType) || VarType.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'var_type'."));
	}

	FString LoadError;
	UBlueprint* Blueprint = LD::Assist::Private::LoadNodeClassBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString ContainerTypeStr;
	InArgs->TryGetStringField(Args::ContainerType, ContainerTypeStr);

	FString KeyType;
	InArgs->TryGetStringField(Args::KeyType, KeyType);

	FEdGraphPinType PinType;
	FString ResolveError;
	if (!LD::Assist::Private::ResolveVariablePinType(VarType, ContainerTypeStr, KeyType, PinType, ResolveError))
	{
		return FSMAssistOperationResult::MakeError(ResolveError);
	}

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(VarName, TEXT("variable_name"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	ISMGraphGeneration::FCreateNodeClassVariableArgs CreateArgs;
	CreateArgs.VariableName = FName(*VarName);
	CreateArgs.VariableType = PinType;
	InArgs->TryGetStringField(Args::DefaultValue, CreateArgs.DefaultValue);

	FString DirectionStr;
	if (InArgs->TryGetStringField(Args::Direction, DirectionStr) && !DirectionStr.IsEmpty())
	{
		ESMGraphPropertyDirection Direction;
		if (!LD::Assist::Private::ResolveDirection(DirectionStr, Direction))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Unrecognized 'direction' '%s'. Accepted: Input, Output, Both."), *DirectionStr));
		}
		CreateArgs.Direction = Direction;
	}

	bool BoolValue = false;
	if (InArgs->TryGetBoolField(Args::Hidden, BoolValue))
	{
		CreateArgs.bHidden = BoolValue;
	}
	if (InArgs->TryGetBoolField(Args::ReadOnly, BoolValue))
	{
		CreateArgs.bReadOnly = BoolValue;
	}

	const bool bWantsSMConfig = CreateArgs.Direction.IsSet() || CreateArgs.bHidden.IsSet() || CreateArgs.bReadOnly.IsSet();
	if (bWantsSMConfig && LD::Assist::Private::IsTransitionClassBlueprint(Blueprint))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Blueprint '%s' is a USMTransitionInstance subclass. Transition-class variables do not support directional / hidden / read-only configuration (matches editor filter). Omit 'direction', 'b_hidden', 'b_read_only' to add a plain variable."), *AssetPath));
	}

	const bool bIsMapOrSet = PinType.ContainerType == EPinContainerType::Map || PinType.ContainerType == EPinContainerType::Set;
	if (bWantsSMConfig && bIsMapOrSet)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Variable '%s' on '%s' is a %s container, which cannot be exposed on the graph node (matches editor's Variable Details panel filter). Omit 'direction', 'b_hidden', 'b_read_only' to add the variable as a plain property."),
				*VarName, *AssetPath, LD::Assist::Private::ContainerTypeToString(PinType.ContainerType)));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	if (!GraphGen->CreateNodeClassVariable(Blueprint, CreateArgs))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("CreateNodeClassVariable failed for '%s' on '%s'."), *VarName, *AssetPath));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::VariableName, VarName);
	Payload->SetStringField(Args::VarType, VarType);
	if (!CreateArgs.DefaultValue.IsEmpty())
	{
		Payload->SetStringField(Args::DefaultValue, CreateArgs.DefaultValue);
	}
	if (CreateArgs.Direction.IsSet())
	{
		Payload->SetStringField(Args::Direction, LD::Assist::Private::DirectionToString(CreateArgs.Direction.GetValue()));
	}
	if (CreateArgs.bHidden.IsSet())
	{
		Payload->SetBoolField(Args::Hidden, CreateArgs.bHidden.GetValue());
	}
	if (CreateArgs.bReadOnly.IsSet())
	{
		Payload->SetBoolField(Args::ReadOnly, CreateArgs.bReadOnly.GetValue());
	}
	if (PinType.ContainerType != EPinContainerType::None)
	{
		Payload->SetStringField(Args::ContainerType, LD::Assist::Private::ContainerTypeToString(PinType.ContainerType));
		if (PinType.ContainerType == EPinContainerType::Map)
		{
			Payload->SetStringField(Args::KeyType, KeyType);
		}
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddBlueprintVariable(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString VarName;
	if (!InArgs->TryGetStringField(Args::VariableName, VarName) || VarName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'variable_name'."));
	}

	FString VarType;
	if (!InArgs->TryGetStringField(Args::VarType, VarType) || VarType.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'var_type'."));
	}

	FString LoadError;
	UBlueprint* Blueprint = LD::Assist::Utils::LoadBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString ContainerTypeStr;
	InArgs->TryGetStringField(Args::ContainerType, ContainerTypeStr);

	FString KeyType;
	InArgs->TryGetStringField(Args::KeyType, KeyType);

	FEdGraphPinType PinType;
	FString ResolveError;
	if (!LD::Assist::Private::ResolveVariablePinType(VarType, ContainerTypeStr, KeyType, PinType, ResolveError))
	{
		return FSMAssistOperationResult::MakeError(ResolveError);
	}

	FString DefaultValue;
	InArgs->TryGetStringField(Args::DefaultValue, DefaultValue);

	FString VarNameLengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(VarName, TEXT("variable_name"), VarNameLengthError))
	{
		return FSMAssistOperationResult::MakeError(VarNameLengthError);
	}

	const FName VarFName(*VarName);
	const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarFName, PinType, DefaultValue);
	if (!bAdded)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("AddMemberVariable failed for '%s' (likely duplicate name or unsupported type)."), *VarName));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::VariableName, VarName);
	Payload->SetStringField(Args::VarType, VarType);
	if (!DefaultValue.IsEmpty())
	{
		Payload->SetStringField(Args::DefaultValue, DefaultValue);
	}
	if (PinType.ContainerType != EPinContainerType::None)
	{
		Payload->SetStringField(Args::ContainerType, LD::Assist::Private::ContainerTypeToString(PinType.ContainerType));
		if (PinType.ContainerType == EPinContainerType::Map)
		{
			Payload->SetStringField(Args::KeyType, KeyType);
		}
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::ConfigureNodeVariable(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString VarName;
	if (!InArgs->TryGetStringField(Args::VariableName, VarName) || VarName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'variable_name'."));
	}

	FString LoadError;
	UBlueprint* Blueprint = LD::Assist::Private::LoadNodeClassBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}
	if (LD::Assist::Private::IsTransitionClassBlueprint(Blueprint))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Blueprint '%s' is a USMTransitionInstance subclass. configure_node_variable sets directional / hidden / read-only state, which transition-class variables do not support."), *AssetPath));
	}

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(VarName, TEXT("variable_name"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	const FName VarFName(*VarName);

	ISMGraphGeneration::FConfigureNodeClassVariableArgs ConfigureArgs;
	ConfigureArgs.VariableName = VarFName;
	TArray<FString> Applied;

	bool bUpdateDirection = false;
	if (InArgs->TryGetBoolField(Args::UpdateDirection, bUpdateDirection) && bUpdateDirection)
	{
		FString DirectionStr;
		InArgs->TryGetStringField(Args::Direction, DirectionStr);
		ESMGraphPropertyDirection Direction;
		if (!LD::Assist::Private::ResolveDirection(DirectionStr, Direction))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Unrecognized 'direction' '%s'. Accepted: Input, Output, Both."), *DirectionStr));
		}
		ConfigureArgs.Direction = Direction;
		Applied.Add(Args::Direction);
	}

	bool bUpdateHidden = false;
	if (InArgs->TryGetBoolField(Args::UpdateHidden, bUpdateHidden) && bUpdateHidden)
	{
		bool HiddenValue = false;
		InArgs->TryGetBoolField(Args::Hidden, HiddenValue);
		ConfigureArgs.bHidden = HiddenValue;
		Applied.Add(Args::Hidden);
	}

	bool bUpdateReadOnly = false;
	if (InArgs->TryGetBoolField(Args::UpdateReadOnly, bUpdateReadOnly) && bUpdateReadOnly)
	{
		bool ReadOnlyValue = false;
		InArgs->TryGetBoolField(Args::ReadOnly, ReadOnlyValue);
		ConfigureArgs.bReadOnly = ReadOnlyValue;
		Applied.Add(Args::ReadOnly);
	}

	if (Applied.Num() == 0)
	{
		return FSMAssistOperationResult::MakeError(TEXT("configure_node_variable requires at least one of 'b_update_direction', 'b_update_hidden', 'b_update_read_only' set to true."));
	}

	// 'direction' / 'b_hidden' / 'b_read_only' describe how a variable is displayed on the graph node, so
	// they only apply to a variable that appears there. The editor hides these fields otherwise. Refuse
	// rather than stamp an override whose effect the caller could never observe.
	const FProperty* Property = Blueprint->GeneratedClass
		? FindFProperty<FProperty>(Blueprint->GeneratedClass, VarFName)
		: nullptr;
	if (!Property)
	{
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarFName) != INDEX_NONE)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Variable '%s' exists on '%s' but its FProperty is not yet materialized on the GeneratedClass. Compile the blueprint before configuring it."), *VarName, *AssetPath));
		}
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("No variable '%s' on '%s'."), *VarName, *AssetPath));
	}
	if (!LD::Editor::PropertyUtils::IsPropertyDisplayedOnGraphNode(Property))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Variable '%s' on '%s' is not displayed on the graph node, so it cannot take a 'direction' / 'b_hidden' / 'b_read_only' setting. Re-add it through ld.add_node_variable passing 'direction', which exposes it. A variable qualifies by being instance editable, or by being a graph-property type such as FSMTextGraphProperty; 'HideOnNode' metadata and non-blueprint-visible properties are excluded."), *VarName, *AssetPath));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	const bool bApplied = GraphGen->ConfigureNodeClassVariable(Blueprint, ConfigureArgs);
	if (!bApplied)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("ConfigureNodeClassVariable failed for '%s' on '%s' (variable may not exist)."), *VarName, *AssetPath));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::VariableName, VarName);
	{
		TArray<TSharedPtr<FJsonValue>> AppliedJson;
		for (const FString& Field : Applied)
		{
			AppliedJson.Add(MakeShared<FJsonValueString>(Field));
		}
		Payload->SetArrayField(Args::Applied, AppliedJson);
	}
	if (ConfigureArgs.Direction.IsSet())
	{
		Payload->SetStringField(Args::Direction, LD::Assist::Private::DirectionToString(ConfigureArgs.Direction.GetValue()));
	}
	if (ConfigureArgs.bHidden.IsSet())
	{
		Payload->SetBoolField(Args::Hidden, ConfigureArgs.bHidden.GetValue());
	}
	if (ConfigureArgs.bReadOnly.IsSet())
	{
		Payload->SetBoolField(Args::ReadOnly, ConfigureArgs.bReadOnly.GetValue());
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static bool BuildNodeVariableEndpoint(USMBlueprint* InBlueprint,
		const FString& InStateGuidStr,
		const FString& InStackKey,
		const FString& InVarName,
		const TSharedRef<FJsonObject>& InArgs,
		ISMGraphGeneration::FNodeVariableEndpoint& OutEndpoint,
		FString& OutError)
	{
		FGuid StateGuid;
		if (!FGuid::Parse(InStateGuidStr, StateGuid))
		{
			OutError = FString::Printf(TEXT("Invalid state guid '%s'."), *InStateGuidStr);
			return false;
		}
		USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(InBlueprint, StateGuid);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("No state node with guid '%s'."), *InStateGuidStr);
			return false;
		}
		if (!LD::Assist::Utils::IsWithinNameLength(InVarName, TEXT("variable_name"), OutError))
		{
			return false;
		}
		OutEndpoint.StateNode = Node;
		OutEndpoint.VariableName = FName(*InVarName);

		int32 StackIndex = INDEX_NONE;
		if (InArgs->TryGetNumberField(InStackKey, StackIndex) && StackIndex >= 0)
		{
			OutEndpoint.StackIndex = StackIndex;
		}
		return true;
	}
}

FSMAssistOperationResult LD::Assist::ConnectNodeVariableOutput(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString FromStateGuidStr;
	if (!InArgs->TryGetStringField(Args::FromStateGuid, FromStateGuidStr) || FromStateGuidStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'from_state_guid'."));
	}
	FString FromVarName;
	if (!InArgs->TryGetStringField(Args::FromVariableName, FromVarName) || FromVarName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'from_variable_name'."));
	}

	FString ToStateGuidStr;
	InArgs->TryGetStringField(Args::ToStateGuid, ToStateGuidStr);
	FString ToVarName;
	InArgs->TryGetStringField(Args::ToVariableName, ToVarName);
	FString ToOwningVar;
	InArgs->TryGetStringField(Args::ToOwningBlueprintVariable, ToOwningVar);

	const bool bHasToInputVariable = !ToStateGuidStr.IsEmpty() || !ToVarName.IsEmpty();
	const bool bHasToOwning = !ToOwningVar.IsEmpty();
	if (bHasToInputVariable == bHasToOwning)
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("Exactly one of (to_state_guid + to_variable_name) or to_owning_blueprint_variable must be supplied."));
	}
	if (bHasToInputVariable && (ToStateGuidStr.IsEmpty() || ToVarName.IsEmpty()))
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("Both 'to_state_guid' and 'to_variable_name' are required for node->node wiring."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	ISMGraphGeneration::FConnectNodeVariableOutputArgs ConnectArgs;
	FString EndpointError;
	if (!LD::Assist::Private::BuildNodeVariableEndpoint(Blueprint, FromStateGuidStr, Args::FromStackIndex, FromVarName, InArgs, ConnectArgs.FromOutputVariable, EndpointError))
	{
		return FSMAssistOperationResult::MakeError(EndpointError);
	}

	if (bHasToInputVariable)
	{
		ISMGraphGeneration::FNodeVariableEndpoint ToEndpoint;
		if (!LD::Assist::Private::BuildNodeVariableEndpoint(Blueprint, ToStateGuidStr, Args::ToStackIndex, ToVarName, InArgs, ToEndpoint, EndpointError))
		{
			return FSMAssistOperationResult::MakeError(EndpointError);
		}
		ConnectArgs.ToInputVariable = ToEndpoint;
	}
	else
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(ToOwningVar, TEXT("to_owning_blueprint_variable"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}
		ConnectArgs.ToOwningBlueprintVariable = FName(*ToOwningVar);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	if (!GraphGen->ConnectNodeVariableOutput(Blueprint, ConnectArgs))
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("ConnectNodeVariableOutput failed. See log for details. Likely causes: source variable not configured as Output (or destination not as Input); pin types incompatible; missing variable; or — for to_owning_blueprint_variable targets — the FSM blueprint has not been compiled since AddSMVariable (AddSMVariable does not auto-compile, call ld.compile to materialize the FProperty)."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::FromStateGuid, FromStateGuidStr);
	Payload->SetStringField(Args::FromVariableName, FromVarName);
	if (ConnectArgs.FromOutputVariable.StackIndex != INDEX_NONE)
	{
		Payload->SetNumberField(Args::FromStackIndex, ConnectArgs.FromOutputVariable.StackIndex);
	}
	if (bHasToInputVariable)
	{
		Payload->SetStringField(Args::ToStateGuid, ToStateGuidStr);
		Payload->SetStringField(Args::ToVariableName, ToVarName);
		if (ConnectArgs.ToInputVariable.GetValue().StackIndex != INDEX_NONE)
		{
			Payload->SetNumberField(Args::ToStackIndex, ConnectArgs.ToInputVariable.GetValue().StackIndex);
		}
	}
	else
	{
		Payload->SetStringField(Args::ToOwningBlueprintVariable, ToOwningVar);
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::DisconnectNodeVariableOutput(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString FromStateGuidStr;
	if (!InArgs->TryGetStringField(Args::FromStateGuid, FromStateGuidStr) || FromStateGuidStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'from_state_guid'."));
	}
	FString FromVarName;
	if (!InArgs->TryGetStringField(Args::FromVariableName, FromVarName) || FromVarName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'from_variable_name'."));
	}

	FString ToStateGuidStr;
	InArgs->TryGetStringField(Args::ToStateGuid, ToStateGuidStr);
	FString ToVarName;
	InArgs->TryGetStringField(Args::ToVariableName, ToVarName);
	FString ToOwningVar;
	InArgs->TryGetStringField(Args::ToOwningBlueprintVariable, ToOwningVar);

	const bool bHasToInputVariable = !ToStateGuidStr.IsEmpty() || !ToVarName.IsEmpty();
	const bool bHasToOwning = !ToOwningVar.IsEmpty();
	if (bHasToInputVariable == bHasToOwning)
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("Exactly one of (to_state_guid + to_variable_name) or to_owning_blueprint_variable must be supplied."));
	}
	if (bHasToInputVariable && (ToStateGuidStr.IsEmpty() || ToVarName.IsEmpty()))
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("Both 'to_state_guid' and 'to_variable_name' are required for node->node disconnect."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	ISMGraphGeneration::FConnectNodeVariableOutputArgs DisconnectArgs;
	FString EndpointError;
	if (!LD::Assist::Private::BuildNodeVariableEndpoint(Blueprint, FromStateGuidStr, Args::FromStackIndex, FromVarName, InArgs, DisconnectArgs.FromOutputVariable, EndpointError))
	{
		return FSMAssistOperationResult::MakeError(EndpointError);
	}

	if (bHasToInputVariable)
	{
		ISMGraphGeneration::FNodeVariableEndpoint ToEndpoint;
		if (!LD::Assist::Private::BuildNodeVariableEndpoint(Blueprint, ToStateGuidStr, Args::ToStackIndex, ToVarName, InArgs, ToEndpoint, EndpointError))
		{
			return FSMAssistOperationResult::MakeError(EndpointError);
		}
		DisconnectArgs.ToInputVariable = ToEndpoint;
	}
	else
	{
		FString LengthError;
		if (!LD::Assist::Utils::IsWithinNameLength(ToOwningVar, TEXT("to_owning_blueprint_variable"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}
		DisconnectArgs.ToOwningBlueprintVariable = FName(*ToOwningVar);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	const bool bBroke = GraphGen->DisconnectNodeVariableOutput(Blueprint, DisconnectArgs);

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::FromStateGuid, FromStateGuidStr);
	Payload->SetStringField(Args::FromVariableName, FromVarName);
	Payload->SetBoolField(Args::Applied, bBroke);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::CollapseToStateMachine(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	const TArray<TSharedPtr<FJsonValue>>* NodeGuidValues = nullptr;
	if (!InArgs->TryGetArrayField(Args::NodeGuids, NodeGuidValues) || !NodeGuidValues || NodeGuidValues->Num() == 0)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guids' (non-empty array of node guids)."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	ISMGraphGeneration::FCollapseNodesToStateMachineArgs CollapseArgs;
	CollapseArgs.Nodes.Reserve(NodeGuidValues->Num());
	for (const TSharedPtr<FJsonValue>& Value : *NodeGuidValues)
	{
		FString NodeGuidStr;
		if (!Value.IsValid() || !Value->TryGetString(NodeGuidStr) || NodeGuidStr.IsEmpty())
		{
			return FSMAssistOperationResult::MakeError(TEXT("'node_guids' must contain non-empty guid strings."));
		}

		FGuid NodeGuid;
		if (!FGuid::Parse(NodeGuidStr, NodeGuid))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Invalid node guid '%s' in 'node_guids'."), *NodeGuidStr));
		}

		USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
		if (!Node)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
		}
		CollapseArgs.Nodes.Add(Node);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	FString OpError;
	USMGraphNode_StateMachineStateNode* Container = GraphGen->CollapseNodesToStateMachine(CollapseArgs, &OpError);
	if (!Container)
	{
		return FSMAssistOperationResult::MakeError(OpError.IsEmpty() ? TEXT("CollapseNodesToStateMachine failed.") : OpError);
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, Container->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, Container->GetStateName());

	// Report what the container actually holds, which is not the requested set: boundary transitions stay
	// in the parent graph, and an interior transition the caller omitted is deleted by
	// CleanUpIsolatedTransitions rather than carried in. Collapse relocates the same node objects rather
	// than cloning them, so these guids stay valid.
	TArray<TSharedPtr<FJsonValue>> MovedGuids;
	if (const USMGraph* ContainerGraph = Cast<USMGraph>(Container->GetBoundGraph()))
	{
		MovedGuids.Reserve(ContainerGraph->Nodes.Num());
		for (const UEdGraphNode* MovedNode : ContainerGraph->Nodes)
		{
			if (!MovedNode || MovedNode->IsA<USMGraphNode_StateMachineEntryNode>())
			{
				continue;
			}
			if (MovedNode->IsA<USMGraphNode_Base>())
			{
				MovedGuids.Add(MakeShared<FJsonValueString>(MovedNode->NodeGuid.ToString()));
			}
		}
		Payload->SetStringField(Args::GraphPath,
			LD::Assist::Private::FindGraphPathLabel(LD::Assist::Utils::GetRootStateMachineGraph(Blueprint), ContainerGraph));
	}
	Payload->SetArrayField(Args::NodeGuids, MovedGuids);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::MergeStates(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString DestinationGuidStr;
	if (!InArgs->TryGetStringField(Args::DestinationStateGuid, DestinationGuidStr) || DestinationGuidStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'destination_state_guid'."));
	}

	FGuid DestinationGuid;
	if (!FGuid::Parse(DestinationGuidStr, DestinationGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'destination_state_guid' '%s'."), *DestinationGuidStr));
	}

	const TArray<TSharedPtr<FJsonValue>>* SourceGuidValues = nullptr;
	if (!InArgs->TryGetArrayField(Args::SourceStateGuids, SourceGuidValues) || !SourceGuidValues || SourceGuidValues->Num() == 0)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'source_state_guids' (non-empty array of state guids)."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* DestinationNode = LD::Assist::Utils::FindNodeByGuid(Blueprint, DestinationGuid);
	if (!DestinationNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find destination node with guid '%s'."), *DestinationGuidStr));
	}

	USMGraphNode_StateNode* DestinationState = Cast<USMGraphNode_StateNode>(DestinationNode);
	if (!DestinationState)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Destination node '%s' is not a plain state. merge_states only targets USMGraphNode_StateNode states."), *DestinationGuidStr));
	}

	ISMGraphGeneration::FMergeStatesArgs MergeArgs;
	MergeArgs.DestinationState = DestinationState;
	MergeArgs.SourceStates.Reserve(SourceGuidValues->Num());
	for (const TSharedPtr<FJsonValue>& Value : *SourceGuidValues)
	{
		FString SourceGuidStr;
		if (!Value.IsValid() || !Value->TryGetString(SourceGuidStr) || SourceGuidStr.IsEmpty())
		{
			return FSMAssistOperationResult::MakeError(TEXT("'source_state_guids' must contain non-empty guid strings."));
		}

		FGuid SourceGuid;
		if (!FGuid::Parse(SourceGuidStr, SourceGuid))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Invalid source guid '%s' in 'source_state_guids'."), *SourceGuidStr));
		}

		USMGraphNode_Base* SourceNode = LD::Assist::Utils::FindNodeByGuid(Blueprint, SourceGuid);
		if (!SourceNode)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not find source node with guid '%s'."), *SourceGuidStr));
		}

		USMGraphNode_StateNode* SourceState = Cast<USMGraphNode_StateNode>(SourceNode);
		if (!SourceState)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Source node '%s' is not a plain state. merge_states only merges USMGraphNode_StateNode states."), *SourceGuidStr));
		}
		MergeArgs.SourceStates.Add(SourceState);
	}

	InArgs->TryGetBoolField(Args::DestroyStates, MergeArgs.bDestroyStates);

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FMergeStatesResult MergeResult;
	FString OpError;
	if (!GraphGen->MergeStates(MergeArgs, MergeResult, &OpError))
	{
		return FSMAssistOperationResult::MakeError(OpError.IsEmpty() ? TEXT("MergeStates failed.") : OpError);
	}

	TArray<TSharedPtr<FJsonValue>> MergedGuids;
	MergedGuids.Reserve(MergeResult.MergedStackTemplateGuids.Num());
	for (const FGuid& MergedGuid : MergeResult.MergedStackTemplateGuids)
	{
		MergedGuids.Add(MakeShared<FJsonValueString>(MergedGuid.ToString()));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::DestinationStateGuid, DestinationState->NodeGuid.ToString());
	Payload->SetArrayField(Args::MergedStackTemplateGuids, MergedGuids);
	Payload->SetBoolField(Args::DestroyStates, MergeArgs.bDestroyStates);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static bool ParseReplaceNodeKind(const FString& InKind, ISMGraphGeneration::EReplaceNodeKind& OutKind)
	{
		if (InKind == TEXT("state"))
		{
			OutKind = ISMGraphGeneration::EReplaceNodeKind::State;
			return true;
		}
		if (InKind == TEXT("conduit"))
		{
			OutKind = ISMGraphGeneration::EReplaceNodeKind::Conduit;
			return true;
		}
		if (InKind == TEXT("state_machine"))
		{
			OutKind = ISMGraphGeneration::EReplaceNodeKind::StateMachine;
			return true;
		}
		if (InKind == TEXT("reference"))
		{
			OutKind = ISMGraphGeneration::EReplaceNodeKind::Reference;
			return true;
		}
		if (InKind == TEXT("parent"))
		{
			OutKind = ISMGraphGeneration::EReplaceNodeKind::Parent;
			return true;
		}
		return false;
	}
}

FSMAssistOperationResult LD::Assist::ReplaceNode(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr) || NodeGuidStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString KindStr;
	if (!InArgs->TryGetStringField(Args::Kind, KindStr) || KindStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'kind' (one of: state, conduit, state_machine, reference, parent)."));
	}

	ISMGraphGeneration::EReplaceNodeKind TargetKind = ISMGraphGeneration::EReplaceNodeKind::State;
	if (!LD::Assist::Private::ParseReplaceNodeKind(KindStr, TargetKind))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Unknown 'kind' '%s'. Expected one of: state, conduit, state_machine, reference, parent."), *KindStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FReplaceNodeArgs ReplaceArgs;
	ReplaceArgs.Node = Node;
	ReplaceArgs.TargetKind = TargetKind;

	FString OpError;
	USMGraphNode_Base* NewNode = GraphGen->ReplaceNode(ReplaceArgs, &OpError);
	if (!NewNode)
	{
		return FSMAssistOperationResult::MakeError(OpError.IsEmpty() ? TEXT("ReplaceNode failed.") : OpError);
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NewNode->NodeGuid.ToString());
	Payload->SetStringField(Args::Kind, KindStr);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::ConvertToReference(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr) || NodeGuidStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	USMGraphNode_StateMachineStateNode* StateMachineNode = Cast<USMGraphNode_StateMachineStateNode>(Node);
	if (!StateMachineNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' is not a state-machine node. convert_to_reference only operates on inline nested state machines (USMGraphNode_StateMachineStateNode)."), *NodeGuidStr));
	}

	ISMGraphGeneration::FConvertStateMachineToReferenceArgs ConvertArgs;
	ConvertArgs.StateMachineNode = StateMachineNode;
	InArgs->TryGetStringField(Args::Name, ConvertArgs.AssetName);
	InArgs->TryGetStringField(Args::Path, ConvertArgs.AssetPath);

	FString LengthError;
	if (!LD::Assist::Utils::IsWithinNameLength(ConvertArgs.AssetName, TEXT("name"), LengthError)
		|| !LD::Assist::Utils::IsWithinNameLength(ConvertArgs.AssetPath, TEXT("path"), LengthError))
	{
		return FSMAssistOperationResult::MakeError(LengthError);
	}

	FString ParentClassPath;
	if (InArgs->TryGetStringField(Args::ParentClass, ParentClassPath) && !ParentClassPath.IsEmpty())
	{
		if (!LD::Assist::Utils::IsWithinNameLength(ParentClassPath, TEXT("parent_class"), LengthError))
		{
			return FSMAssistOperationResult::MakeError(LengthError);
		}
		UClass* ParentClass = LoadClass<USMInstance>(nullptr, *ParentClassPath);
		if (!ParentClass)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not load 'parent_class' '%s'."), *ParentClassPath));
		}
		ConvertArgs.ParentClass = ParentClass;
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	FString OpError;
	USMBlueprint* Reference = GraphGen->ConvertStateMachineToReference(ConvertArgs, &OpError);
	if (!Reference)
	{
		return FSMAssistOperationResult::MakeError(OpError.IsEmpty() ? TEXT("ConvertStateMachineToReference failed.") : OpError);
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, StateMachineNode->NodeGuid.ToString());
	Payload->SetStringField(Args::ReferenceAssetPath, Reference->GetPathName());
	Payload->SetStringField(Args::Name, Reference->GetName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}
