// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"

#include "SMAssistLog.h"
#include "Operations/SMAssistGenericOpKeys.h"
#include "Operations/SMAssistGenericOps.h"
#include "Operations/SMAssistOpKeys.h"
#include "Operations/SMAssistOperations.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace LD::Assist::Private
{
	static TSharedRef<FJsonObject> MakePropertyObject(const TCHAR* InType, const TCHAR* InDescription)
	{
		const TSharedRef<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("type"), InType);
		Property->SetStringField(TEXT("description"), InDescription);
		return Property;
	}

	static TSharedRef<FJsonObject> MakeSchema(
		const TArray<TPair<FString, TSharedRef<FJsonObject>>>& InProperties,
		const TArray<FString>& InRequired)
	{
		const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedRef<FJsonObject>>& Pair : InProperties)
		{
			Properties->SetObjectField(Pair.Key, Pair.Value);
		}

		const TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), Properties);

		if (InRequired.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> RequiredArray;
			RequiredArray.Reserve(InRequired.Num());
			for (const FString& Name : InRequired)
			{
				RequiredArray.Add(MakeShared<FJsonValueString>(Name));
			}
			Schema->SetArrayField(TEXT("required"), RequiredArray);
		}

		return Schema;
	}
}

void USMAssistSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);
	RegisterBuiltInOperations();
}

void USMAssistSubsystem::Deinitialize()
{
	Operations.Empty();
	Super::Deinitialize();
}

bool USMAssistSubsystem::RegisterOperation(FSMAssistOperationInfo InInfo)
{
	if (InInfo.Name.IsNone() || !InInfo.Handler.IsBound())
	{
		return false;
	}

	if (Operations.Contains(InInfo.Name))
	{
		LDASSIST_LOG_WARNING(TEXT("Operation '%s' already registered."), *InInfo.Name.ToString());
		return false;
	}

	const FName Name = InInfo.Name;
	const FSMAssistOperationInfo& Stored = Operations.Add(Name, MoveTemp(InInfo));
	OnOperationRegisteredDelegate.Broadcast(Stored);
	return true;
}

bool USMAssistSubsystem::UnregisterOperation(FName InName)
{
	if (Operations.Remove(InName) > 0)
	{
		OnOperationUnregisteredDelegate.Broadcast(InName);
		return true;
	}
	return false;
}

bool USMAssistSubsystem::HasOperation(FName InName) const
{
	return Operations.Contains(InName);
}

FSMAssistOperationResult USMAssistSubsystem::ExecuteOperation(FName InName, const TSharedRef<FJsonObject>& InArgs)
{
	const FSMAssistOperationInfo* Info = Operations.Find(InName);
	if (!Info || !Info->Handler.IsBound())
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Unknown operation '%s'."), *InName.ToString()));
	}

	return Info->Handler.Execute(InArgs);
}

TArray<FName> USMAssistSubsystem::GetRegisteredOperationNames() const
{
	TArray<FName> Names;
	Operations.GetKeys(Names);
	Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return Names;
}

const FSMAssistOperationInfo* USMAssistSubsystem::FindOperationInfo(FName InName) const
{
	return Operations.Find(InName);
}

TArray<FSMAssistOperationInfo> USMAssistSubsystem::GetAllOperationInfos() const
{
	TArray<FSMAssistOperationInfo> Infos;
	Infos.Reserve(Operations.Num());
	for (const TPair<FName, FSMAssistOperationInfo>& Pair : Operations)
	{
		Infos.Add(Pair.Value);
	}
	Infos.Sort([](const FSMAssistOperationInfo& A, const FSMAssistOperationInfo& B)
	{
		return A.Name.LexicalLess(B.Name);
	});
	return Infos;
}

void USMAssistSubsystem::RegisterBuiltInOperations()
{
	using namespace LD::Assist::Private;
	namespace Args = LD::Assist::Args;
	namespace Ops = LD::Assist::Ops;

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::CreateBlueprint;
		Info.Description = TEXT("Create a new state machine blueprint asset.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::Name, MakePropertyObject(TEXT("string"), TEXT("Asset name (without path).")) },
				{ Args::Path, MakePropertyObject(TEXT("string"), TEXT("Content package path; defaults to the plugin's default location.")) }
			},
			{ Args::Name });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::CreateBlueprint);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddState;
		Info.Description = TEXT("Add a state node to an existing state machine blueprint's root graph. Layout convention (graphs must look human-authored): Entry sits at (0, 0) and the main flow runs left-to-right from Entry; place the first state at ~(200, 0) and space subsequent states +250 X. Use position_y only for deliberate parallel/branching rows, and keep >=150 units between rows so transitions never visually cross an unrelated node. Avoid crossing edges: if a new state breaks a linear chain, insert it along the same row rather than offsetting y. Positions set here are authoritative; sm.get_asset reports position_x/position_y back for verification.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::StateName, MakePropertyObject(TEXT("string"), TEXT("Optional state name.")) },
				{ Args::IsEntry, MakePropertyObject(TEXT("boolean"), TEXT("Mark the new state as the entry state.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Graph X coordinate. Entry is at x=0, so prefer positive values to place the state to the right of Entry (first state typically 200, later states +250 each).")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Graph Y coordinate. 0 aligns horizontally with Entry; use non-zero only for deliberate vertical layout.")) },
				{ Args::StateClass, MakePropertyObject(TEXT("string"), TEXT("Class path for a USMStateInstance_Base subclass.")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddState);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddTransition;
		Info.Description = TEXT("Add a transition edge between two existing states in a state machine blueprint.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::FromStateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the source state node.")) },
				{ Args::ToStateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the destination state node.")) },
				{ Args::TransitionClass, MakePropertyObject(TEXT("string"), TEXT("Class path for a USMTransitionInstance subclass.")) }
			},
			{ Args::AssetPath, Args::FromStateGuid, Args::ToStateGuid });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddTransition);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddTransitionReroute;
		Info.Description = TEXT("Add a transition reroute node. Reroute nodes are cosmetic graph nodes that let a transition curve bend around obstructions (e.g., back-edges in cyclic state machines). They have no runtime effect; the primary transition retains all configuration and the reroute chain compiles to the same runtime transition. Two modes, gated by transition_guid: when supplied, the reroute is spliced into that transition's outgoing pin chain (inline-insert); when omitted, the reroute is created standalone on the graph and the caller connects transitions to/from it later via sm.add_transition (reroute GUIDs are valid from/to endpoints).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::TransitionGuid, MakePropertyObject(TEXT("string"), TEXT("Optional. When supplied, the reroute is inserted into this transition's outgoing pin chain. When omitted, the reroute is created standalone on the root state machine graph.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Graph X coordinate for the reroute. Defaults to 0.")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Graph Y coordinate for the reroute. Defaults to 0. To V-shape a back-edge below a row of states, set positive Y (state row sits around y=-43).")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddTransitionReroute);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ListAssets;
		Info.Description = TEXT("List state machine blueprint assets, optionally filtered by content path prefix.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::PathPrefix, MakePropertyObject(TEXT("string"), TEXT("Optional content path prefix (e.g. '/Game/StateMachines').")) }
			},
			{});
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ListAssets);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::GetAsset;
		Info.Description = TEXT("Return the structure of a state machine blueprint. Each state entry reports state_guid, state_name, state_class, position_x, position_y, and is_entry. Each transition entry reports transition_guid, from_state_guid, to_state_guid, and transition_class. The top-level payload also includes entry_state_guids (array; multiple only when parallel entry is enabled).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::GetAsset);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::RemoveNode;
		Info.Description = TEXT("Remove a state or transition node by GUID, breaking all connected pins.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the state or transition node to remove.")) }
			},
			{ Args::AssetPath, Args::NodeGuid });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::RemoveNode);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::SetNodeProperty;
		Info.Description = TEXT("Set a value on a node property or mutate an array property's structure. Resolution order (when stack_index is omitted): graph-node properties first (NodePosX, NodePosY, NodeComment, bCommentBubblePinned, etc.), then the node's primary template (node-class fields). When stack_index is provided, targets the stack template directly. Supports scalar and array properties on templates; graph-node properties only support 'set'. Array actions (template-only): 'set' writes value(s) at array_index (auto-grows); 'add' appends a default element; 'insert' inserts a default at array_index; 'duplicate' clones the element at array_index (preserves pin literals and wired graphs); 'move' reorders array_index to target_index (preserves element guids); 'remove' removes array_index; 'clear' empties the array. Structural actions reject a 'value' payload and surface EditFixedSize / read-only arrays as explicit errors. For deeply-nested writes (sub-property of a split struct, text-graph property leaf, etc.) use 'property_path' to target the leaf directly: the write routes through an IPropertyHandle chain so PostEditChangeProperty fires with the full property chain, cascading through Logic Driver's HandleOnPropertyChangedEvent to refresh child property graphs (text-graph buckets, scalar-array buckets). Without 'property_path' only the top-level UPROPERTY is written and split sub-pin defaults / text-graph child graphs do not update.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the node containing the property.")) },
				{ Args::PropertyName, MakePropertyObject(TEXT("string"), TEXT("Name of the property. Resolves to graph-node fields (UEdGraphNode / USMGraphNode_Base) first, then falls back to the node template.")) },
				{ Args::Value, MakePropertyObject(TEXT("string"), TEXT("New value. Accepts string, number, boolean, null, or an array of those (array replaces all elements starting at index 0). Struct-typed properties (FLinearColor, FVector, etc.) take UE struct-text form wrapped as a JSON string, e.g. \"(R=1.0,G=0.0,B=0.0,A=1.0)\" for an FLinearColor. JSON objects ({\"R\":1.0,...}) are rejected; the handler wants a scalar that ImportText can parse. Required for 'set'; rejected by structural actions (add/insert/duplicate/move/remove/clear).")) },
				{ Args::ArrayIndex, MakePropertyObject(TEXT("number"), TEXT("Array index. 'set' scalar: element to write (auto-resizes). 'insert': insertion point (elements at/above shift up). 'duplicate': source element. 'move': source element. 'remove': element to remove. Must be omitted when 'value' is an array.")) },
				{ Args::TargetIndex, MakePropertyObject(TEXT("number"), TEXT("Destination index for 'array_action=move'. Must differ from 'array_index'. Unused by other actions.")) },
				{ Args::ArrayAction, MakePropertyObject(TEXT("string"), TEXT("Array operation mode. 'set' (default): write value(s). Structural (template-only): 'add', 'insert', 'duplicate', 'move', 'remove', 'clear'.")) },
				{ Args::StackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional state-stack template index. Omit to use the default resolution (graph-node then primary template); provide to target a stack element returned by sm.add_state_stack.")) },
				{ Args::PropertyPath, MakePropertyObject(TEXT("string"), TEXT("Optional dot-separated sub-path under 'property_name', with optional bracket indices for array elements, e.g. \"InnerStruct.TextMember\" or \"InnerArray[2].Field\". When set, the write walks an IPropertyHandle chain to the leaf so PostEditChangeProperty fires with the full property chain (which Logic Driver's HandleOnPropertyChangedEvent uses to refresh child property graphs). Extended graph properties (text-graph) are addressed by the property itself; callers do NOT include the internal Result subfield. Rejected with structural 'array_action' values. 'value' must be a scalar when 'property_path' is set.")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::PropertyName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SetNodeProperty);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::Compile;
		Info.Description = TEXT("Compile a Blueprint and return its compile status. Accepts any UBlueprint subclass: state-machine Blueprints (USMBlueprint), Logic Driver node-class Blueprints (USMNodeBlueprint child of USMStateInstance / USMTransitionInstance / etc.), and regular UBlueprints (actor, widget, component subclasses).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the Blueprint to compile.")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::Compile);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::RenameState;
		Info.Description = TEXT("Rename a state node by GUID. Uses the schema name validator.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::StateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the state node to rename.")) },
				{ Args::NewName, MakePropertyObject(TEXT("string"), TEXT("New display name for the state.")) }
			},
			{ Args::AssetPath, Args::StateGuid, Args::NewName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::RenameState);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::SetInitialState;
		Info.Description = TEXT("Rewire the entry pin of a state machine graph to target the given state.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::StateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the state node to wire as the initial state.")) }
			},
			{ Args::AssetPath, Args::StateGuid });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SetInitialState);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddStateStack;
		Info.Description = TEXT("Append a state stack instance to a state node. Returns the resolved stack index and template GUID. state_class must be a USMStateInstance subclass that differs from the state node's primary template class; passing the same class as the primary (e.g. /Script/SMSystem.SMStateInstance on a default state) is rejected by USMGraphNode_StateNode::AddStackNode's self-class check. Pick a different concrete subclass exposed by the project (e.g. /Script/SMDialogue.SMDialogueNode) or a Blueprint-derived class.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::StateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the state node that will own the stack entry.")) },
				{ Args::StateClass, MakePropertyObject(TEXT("string"), TEXT("Class path for a USMStateInstance subclass to add to the stack.")) },
				{ Args::StackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional insertion index. Omit to append to the end.")) }
			},
			{ Args::AssetPath, Args::StateGuid, Args::StateClass });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddStateStack);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddTransitionStack;
		Info.Description = TEXT("Append a transition stack instance to a transition edge. Returns the resolved stack index and template GUID. transition_class must be a USMTransitionInstance subclass that differs from the transition's primary template class; passing the same class as the primary (e.g. /Script/SMSystem.SMTransitionInstance on a default transition) is rejected by USMGraphNode_TransitionEdge::AddStackNode's self-class check. Pick a different concrete subclass (e.g. /Script/SMDialogue.SMDialogueTransition) or a Blueprint-derived class.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::TransitionGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the transition edge that will own the stack entry.")) },
				{ Args::TransitionClass, MakePropertyObject(TEXT("string"), TEXT("Class path for a USMTransitionInstance subclass to add to the stack.")) },
				{ Args::StackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional insertion index. Omit to append to the end.")) }
			},
			{ Args::AssetPath, Args::TransitionGuid, Args::TransitionClass });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddTransitionStack);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddConduit;
		Info.Description = TEXT("Add a conduit node to an existing state machine blueprint's root graph. Positioning follows the same row-based convention as states: Entry at (0, 0), positive X, match the Y of the row the conduit sits on, >=150 units between rows. A conduit must be wired into the flow in the same authoring step: once added, create inbound and outbound transitions so the conduit bridges two states or a state and a reference (e.g. replace a direct A->B edge with A->Conduit->B). Orphan conduits (no inbound or no outbound transition) are a layout failure.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::StateName, MakePropertyObject(TEXT("string"), TEXT("Optional conduit name.")) },
				{ Args::IsEntry, MakePropertyObject(TEXT("boolean"), TEXT("Mark the new conduit as the entry state.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Graph X coordinate. Entry is at x=0, so prefer positive values (~200+) to place the conduit to the right of Entry.")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Graph Y coordinate. 0 aligns horizontally with Entry; use non-zero only for deliberate vertical layout.")) },
				{ Args::StateClass, MakePropertyObject(TEXT("string"), TEXT("Optional class path for a USMConduitInstance subclass.")) },
				{ Args::EvalWithTransitions, MakePropertyObject(TEXT("boolean"), TEXT("When true, the conduit evaluates as part of the previous state's outbound transitions.")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddConduit);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddReference;
		Info.Description = TEXT("Add a state machine reference node to an existing state machine blueprint's root graph. Positioning follows the same row-based convention as states: Entry at (0, 0), positive X, match the Y of its row. A reference node must be wired into the flow in the same authoring step: add at least one inbound transition (and typically an outbound one) so the reference participates in the main flow. Orphan reference nodes are a layout failure. The referenced SMBlueprint is optional — omit reference_asset_path to create a structural state-machine state with no target yet, then set it later via sm.configure_reference. Pass use_intermediate_graph=true to enable the intermediate K2 graph at creation time so SpawnLocalGraphReadNode kinds (GetStateMachineReference, InEndState) are visible/editable inside the reference state; omit (default false) to keep the reference behaving as a plain sub-state-machine.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::ReferenceAssetPath, MakePropertyObject(TEXT("string"), TEXT("Optional. Object path to the SMBlueprint to reference. Omit to create the reference state without a target (assign later via sm.configure_reference).")) },
				{ Args::StateName, MakePropertyObject(TEXT("string"), TEXT("Optional node name.")) },
				{ Args::IsEntry, MakePropertyObject(TEXT("boolean"), TEXT("Mark the new reference as the entry state.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Graph X coordinate. Entry is at x=0, so prefer positive values (~200+) to place the reference to the right of Entry.")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Graph Y coordinate. 0 aligns horizontally with Entry; use non-zero only for deliberate vertical layout.")) },
				{ Args::UseIntermediateGraph, MakePropertyObject(TEXT("boolean"), TEXT("Enable the intermediate K2 graph on the new reference state. Required for GetStateMachineReference / InEndState reads to be visible/editable in the editor. Default false. Routes through USMGraphNode_StateMachineStateNode::SetUseIntermediateGraph.")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddReference);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ConfigureReference;
		Info.Description = TEXT("Reconfigure an existing state-machine-reference state after creation. Mirrors the Details panel operations on a USMGraphNode_StateMachineStateNode: swap the referenced SMBlueprint and/or toggle intermediate-graph use. At least one of 'reference_asset_path' or 'use_intermediate_graph' must be supplied; unset fields leave the existing state alone. Use this when the reference target needs to change, or when enabling the intermediate graph so GetStateMachineReference / InEndState reads become visible in the editor.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the SMBlueprint that owns the reference state.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the state-machine-reference state to reconfigure.")) },
				{ Args::ReferenceAssetPath, MakePropertyObject(TEXT("string"), TEXT("Optional. New referenced SMBlueprint object path. Empty string clears the reference. Omit to leave the existing reference alone.")) },
				{ Args::UseIntermediateGraph, MakePropertyObject(TEXT("boolean"), TEXT("Optional. Toggle the intermediate K2 graph. Routes through USMGraphNode_StateMachineStateNode::SetUseIntermediateGraph which creates or swaps the bound graph to match.")) }
			},
			{ Args::AssetPath, Args::NodeGuid });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ConfigureReference);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddAnyState;
		Info.Description = TEXT("Add an Any State node to a state machine blueprint's root graph. Any State has no graph of its own; transitions added from it are duplicated onto every other state in the same SM at runtime, so it acts as a shared 'from any state' source. Position it on its own row (e.g. y = -150 above the main flow) so its outbound transitions don't visually cross main-flow nodes. AnyState-specific properties (AnyStateTagQuery to limit affected states, bAllowInitialReentry, color overrides) are reachable through sm.set_node_property after creation. Wire at least one outbound transition in the same authoring step so the Any State actually contributes to flow.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::StateName, MakePropertyObject(TEXT("string"), TEXT("Optional Any State name.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Graph X coordinate. Entry is at x=0, so prefer positive values.")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Graph Y coordinate. Place on a separate row (e.g. -150) above or below the main flow to keep outbound transitions readable.")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddAnyState);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddLinkState;
		Info.Description = TEXT("Add a Link State node that forwards inbound transitions to another state in the same SM root graph. Use it to converge multiple flow paths on a single state without drawing crossing transitions back to the original. Link State is intentionally minimal: its only configurable surface is the target state ('link_to_state_name', required) and its graph position. The node name is auto-derived from the target ('Link to <target>'); it has no node class, no behavior graph, no state stack, and no Any State tags. Wire at least one inbound transition in the same authoring step or it contributes nothing. To retarget later, write 'LinkedStateName' via sm.set_node_property; to reposition, write 'NodePosX' / 'NodePosY'.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::LinkToStateName, MakePropertyObject(TEXT("string"), TEXT("Name of an existing state in the same root graph to link to. Must already exist.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Graph X coordinate. Entry is at x=0, so prefer positive values.")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Graph Y coordinate. 0 aligns with Entry; offset to a parallel row when the link converges from a side branch.")) }
			},
			{ Args::AssetPath, Args::LinkToStateName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddLinkState);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::GetNodeProperties;
		Info.Description = TEXT("List the editable properties on a node template. Returns name, type, category, and current value for each. With max_depth > 0, struct properties also return a 'members' array and array properties an 'elements' array, recursing one level per unit of depth. Supports an optional stack_index.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the state or transition node.")) },
				{ Args::StackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional stack template index. Omit to target the node's primary template.")) },
				{ Args::MaxDepth, MakePropertyObject(TEXT("number"), TEXT("Optional recursion depth for struct members and array elements. 0 (default) returns the flat exported value only.")) }
			},
			{ Args::AssetPath, Args::NodeGuid });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::GetNodeProperties);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::SetTransitionCondition;
		Info.Description = TEXT("Set the default evaluation result of a transition with no assigned node class to true or false.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::TransitionGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the transition edge to configure.")) },
				{ Args::Condition, MakePropertyObject(TEXT("boolean"), TEXT("Default evaluation result. True means the transition is always taken by default.")) }
			},
			{ Args::AssetPath, Args::TransitionGuid, Args::Condition });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SetTransitionCondition);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::GetGraphView;
		Info.Description = TEXT("Read live slate-widget geometry, displayed text, and colors for every node in the asset's root graph editor. Auto-opens the asset's blueprint editor and focuses the root graph if needed. Use this to verify how nodes actually render (real widget bounds, displayed titles, body/title colors) instead of guessing from logical positions. Each node entry returns node_guid, kind, logical_position ([x, y] from UEdGraphNode::NodePosX/Y), widget_position ([x, y] from SGraphNode::GetPosition2f), widget_size ([w, h] from GetDesiredSizeForMarquee2f), title_text, body_color/title_color (linear RGBA arrays; reads zero when the slate color is non-specified), comment (omitted if empty), and is_selected. Transition entries also include from_state_guid and to_state_guid. The top-level payload includes panel_view (zoom, view_offset).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::IncludeTransitions, MakePropertyObject(TEXT("boolean"), TEXT("Include transition edge widgets in the result. Default true.")) },
				{ Args::IncludePins, MakePropertyObject(TEXT("boolean"), TEXT("Include each node's pins (id, name, direction). Off by default since pin counts can be high.")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::GetGraphView);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::CaptureGraphView;
		Info.Description = TEXT("Capture the asset's blueprint editor as a PNG written to disk; returns the absolute path so the caller can read the image. Auto-opens the asset's blueprint editor and focuses the root graph if needed. Use this when 'sm.get_graph_view' alone isn't enough and you need to actually see how the graph renders (visual sanity check after layout work, debugging color/title rendering, demonstrating a result). With 'clip_to_panel=true' (default) only the SGraphPanel is captured; with false the entire editor window is captured (includes toolbar and side panels). Pass 'node_guid' to frame a single node (overrides 'fit_to_content'); otherwise 'fit_to_content=true' (default) reframes the panel to show every node before capture. Files are written under <Project>/Saved/Screenshots/<output_subdir>/<prefix>.png.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::ClipToPanel, MakePropertyObject(TEXT("boolean"), TEXT("True (default) captures just the SGraphPanel; false captures the whole blueprint editor window.")) },
				{ Args::FitToContent, MakePropertyObject(TEXT("boolean"), TEXT("True (default) reframes the panel to fit all nodes before capturing, so the screenshot reflects the whole graph regardless of any prior pan or zoom. Set false to capture the panel's current view as-is. Ignored when 'node_guid' is set.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Optional. When set, focuses and selects the node with this guid before capture so the screenshot frames a single state, conduit, reference, or transition. Takes precedence over 'fit_to_content'.")) },
				{ Args::OutputSubdir, MakePropertyObject(TEXT("string"), TEXT("Subdirectory under <Project>/Saved/Screenshots/ for the output PNG. Default 'LogicDriver'.")) },
				{ Args::Prefix, MakePropertyObject(TEXT("string"), TEXT("Filename prefix (no extension). Default '<BlueprintName>_<timestamp>'. Always followed by .png.")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::CaptureGraphView);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ClearScreenshots;
		Info.Description = TEXT("Delete PNG screenshots written by 'sm.capture_graph_view' under <Project>/Saved/Screenshots/<output_subdir>. Useful housekeeping when a session has accumulated many captures and only the latest matter. Only files with the .png extension are affected; non-PNG files in the directory are left alone. Returns the directory inspected, deleted_count, freed_bytes, and the list of paths that were deleted (or would be deleted in dry_run mode).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::OutputSubdir, MakePropertyObject(TEXT("string"), TEXT("Subdirectory under <Project>/Saved/Screenshots/ to clean. Default 'LogicDriver' (matches sm.capture_graph_view's default).")) },
				{ Args::OlderThanSeconds, MakePropertyObject(TEXT("number"), TEXT("Optional. When set, only delete files whose age (now minus modification timestamp) exceeds this many seconds. Omit to delete every PNG in the directory.")) },
				{ Args::DryRun, MakePropertyObject(TEXT("boolean"), TEXT("False (default) deletes the matching files. True returns the same payload but performs no deletions, so callers can preview before clearing.")) }
			},
			{});
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ClearScreenshots);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::LayoutStates;
		Info.Description = TEXT("Auto-arrange the states, conduits, references, link states, and any-state nodes in a state machine asset using a layered (Sugiyama-style) layout. Default is dry-run: returns proposed positions without writing. Set 'apply=true' to commit; the writes are wrapped in a single editor transaction so Ctrl+Z reverts the whole layout. Strategy 'left_to_right' (default) flows positive X with stacking on Y; 'top_to_bottom' flows positive Y with stacking on X. AnyState nodes are placed in a perpendicular side lane (above the main flow for LR, left for TB) so their many runtime fan-out edges don't dominate layering. LinkStates are placed at the far-right (or bottom) end of the main flow. Cycles are handled by reversing back-edges for layering; a warning lists each reversed edge. Use 'pin_node_guids' to keep specific nodes at their authored position; the algorithm flows around them and warns on overlap. Scope 'root' (default) lays out only the root SM graph; 'all' walks every nested sub-state-machine and lays each out independently in the same transaction.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::Strategy, MakePropertyObject(TEXT("string"), TEXT("Layout direction. 'left_to_right' (default): entry on the left, flow rightward, stack siblings on Y. 'top_to_bottom': entry on top, flow downward, stack siblings on X. Aliases 'lr' and 'tb' are accepted.")) },
				{ Args::Apply, MakePropertyObject(TEXT("boolean"), TEXT("False (default) returns proposed positions without writing. True applies the layout in a single editor transaction (one Ctrl+Z reverts).")) },
				{ Args::Scope, MakePropertyObject(TEXT("string"), TEXT("'root' (default) lays out only the root state machine graph. 'all' recurses into every nested sub-state-machine and lays each out independently in the same transaction.")) },
				{ Args::ColumnGap, MakePropertyObject(TEXT("number"), TEXT("Gap between layers along the flow axis. Default 80.")) },
				{ Args::RowGap, MakePropertyObject(TEXT("number"), TEXT("Gap between siblings within a layer (perpendicular to flow). Default 40.")) },
				{ Args::StartX, MakePropertyObject(TEXT("number"), TEXT("X coordinate of the first laid-out state in the root graph. Default 200 for left_to_right (leaves room for the Entry node at (0,0)) and 0 for top_to_bottom.")) },
				{ Args::StartY, MakePropertyObject(TEXT("number"), TEXT("Y coordinate of the first laid-out state in the root graph. Default 0 for left_to_right and 200 for top_to_bottom (leaves room for the Entry node at (0,0)).")) },
				{ Args::PinNodeGuids, MakePropertyObject(TEXT("array"), TEXT("Optional list of node GUIDs whose positions should be preserved. Pinned nodes stay at their authored coordinates; the layout flows around them. Unknown GUIDs are warned-and-ignored.")) },
				{ Args::RespectExistingOrder, MakePropertyObject(TEXT("boolean"), TEXT("True (default) seeds within-layer ordering from each node's current secondary-axis position so the layout preserves authored intent. False seeds alphabetically for a fresh re-flow.")) },
				{ Args::SnapToGrid, MakePropertyObject(TEXT("boolean"), TEXT("True (default) rounds final positions to the editor's snap-grid size for crisp alignment.")) }
			},
			{ Args::AssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::LayoutStates);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::GetPropertyPins;
		Info.Description = TEXT("Read the property graph pin tree on a state-like SM node. Returns every property graph K2 result node, its result pin, recursive sub-pins, and per-pin diagnostics: default_value, autogenerated_default_value, matches_autogenerated, linked_to_count, sub_pins_count, depth, is_root_result. Use this to verify property graph pin state without opening the editor.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the SM graph node whose property pins should be read.")) },
				{ Args::VariableName, MakePropertyObject(TEXT("string"), TEXT("Optional. Filter to one variable (matches FSMGraphProperty_Base::VariableName). Omit to dump every property.")) }
			},
			{ Args::AssetPath, Args::NodeGuid });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::GetPropertyPins);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::GetPropertyGraph;
		Info.Description = TEXT("Resolve a USMPropertyGraph by node + variable + property_path. Returns enough handles (graph_path, graph_name, graph_guid, result_node_name, result_pin_name, bucket_index, element_type) for downstream MCP servers to wire general K2 logic into it. property_path uses the same syntax as sm.set_node_property: 'MemberA.MemberB[i].MemberC'. Indexed segments descend through nested-array buckets, switching to each bucket's child property graph for subsequent segments. Top-level array variables require a leading bare '[index]' segment. Strict-split contract: every struct parent in the path must be split first (use sm.split_pin). bucket_index is -1 for non-bucket leaves; use it as the bucket-element predicate. For text-graph property graphs, call sm.set_property_graph_edit_mode before wiring; it is idempotent on graphs already in edit mode.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the SM graph node owning the property.")) },
				{ Args::VariableName, MakePropertyObject(TEXT("string"), TEXT("Top-level FSMGraphProperty variable name (UPROPERTY name on the node template).")) },
				{ Args::PropertyPath, MakePropertyObject(TEXT("string"), TEXT("Optional. Dotted sub-path under variable_name: 'MemberA.MemberB[i].MemberC'. Empty resolves the top property graph; for top-level array variables, a leading bare '[index]' is required.")) },
				{ Args::IncludePinTree, MakePropertyObject(TEXT("boolean"), TEXT("Optional. When true, the response includes a result_pin sub-tree mirroring sm.get_property_pins output. Defaults to false to keep payloads small.")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::VariableName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::GetPropertyGraph);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::SetPropertyGraphEditMode;
		Info.Description = TEXT("Toggle graph-edit mode on a property graph. Required before wiring text graphs durably: USMTextPropertyGraph's compile pipeline only honors wired changes when the graph is in edit mode. Resolves the target graph the same way as sm.get_property_graph (node + variable + property_path), then routes through ISMGraphGeneration::SetPropertyGraphEditMode under a scoped transaction so single-step undo reverts the toggle.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the SM graph node owning the property.")) },
				{ Args::VariableName, MakePropertyObject(TEXT("string"), TEXT("Top-level FSMGraphProperty variable name (UPROPERTY name on the node template).")) },
				{ Args::PropertyPath, MakePropertyObject(TEXT("string"), TEXT("Optional. Dotted sub-path under variable_name; empty toggles the top property graph.")) },
				{ Args::Enable, MakePropertyObject(TEXT("boolean"), TEXT("Target edit-mode state: true to enable, false to disable.")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::VariableName, Args::Enable });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SetPropertyGraphEditMode);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::SplitPin;
		Info.Description = TEXT("Split a splittable-struct property pin on a state-like SM node, mirroring the editor's right-click 'Split Struct Pin' on the result pin (no pin_id) or any sub-pin row (pin_id). Targets the property graph identified by variable_name; resolves the result node via Node->GetAllPropertyGraphNodes(). When pin_id is omitted, gated by USMPropertyGraph::CanSplitResultPin (refused for non-struct types, struct types that opt out via CanEverSplit, FText-typed properties, the text-graph property, or properties already split). When pin_id is provided, the sub-pin is located by PinId in the result-pin tree (matching what sm.get_property_pins returns) and gated by USMPropertyGraph::CanSplitSubPin. Operation is transacted. Returns applied=true, is_split_struct (post-op), flag_b_split, and the post-op result_pin tree so callers can verify the split in one round-trip.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the SM graph node owning the property.")) },
				{ Args::VariableName, MakePropertyObject(TEXT("string"), TEXT("FSMGraphProperty_Base::VariableName of the exposed property to split (UPROPERTY name on the node template).")) },
				{ Args::PinId, MakePropertyObject(TEXT("string"), TEXT("Optional. PinId (FGuid) of the sub-pin to split. Omit to split the top-level result pin. PinId values match the 'pin_id' returned by sm.get_property_pins.")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::VariableName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SplitPin);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::RecombinePin;
		Info.Description = TEXT("Recombine a previously split struct pin on a state-like SM node, mirroring the editor's right-click 'Recombine Struct Pin'. Targets the property graph identified by variable_name; resolves the result node via Node->GetAllPropertyGraphNodes(). When pin_id is omitted, the top-level result pin is recombined (gated by LD::Editor::PropertyUtils::IsSplitStructResultNode; refused when the property is not currently split). When pin_id is provided, the sub-pin is located by PinId and recombined (refused when the sub-pin has no SubPins). Recombining a nested sub-pin flattens deeper splits beneath it (engine RecombinePin is recursive). Per-sub-pin wiring is discarded by the engine; the Logic Driver child-graph buckets that backed those sub-pins are cleaned up by the property graph. Operation is transacted. Returns applied=true, is_split_struct (post-op), flag_b_split, and the post-op result_pin tree.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the SM graph node owning the property.")) },
				{ Args::VariableName, MakePropertyObject(TEXT("string"), TEXT("FSMGraphProperty_Base::VariableName of the exposed property to recombine (UPROPERTY name on the node template).")) },
				{ Args::PinId, MakePropertyObject(TEXT("string"), TEXT("Optional. PinId (FGuid) of the sub-pin to recombine. Omit to recombine the top-level result pin. PinId values match the 'pin_id' returned by sm.get_property_pins.")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::VariableName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::RecombinePin);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ResetNodeProperty;
		Info.Description = TEXT("Reset a node template property to its archetype/CDO default. Mirrors the editor's right-click 'Reset Pin to Default Value' on an exposed graph property: routes through ISMGraphGeneration::ResetNodePropertyValue, which resolves the property graph and resets the graph + template default. Symmetric with sm.set_node_property. Returns node_guid and property_name; array_index and stack_index when provided.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the SM graph node containing the property to reset.")) },
				{ Args::PropertyName, MakePropertyObject(TEXT("string"), TEXT("Name of the property to reset (matches the FProperty name on the node template).")) },
				{ Args::ArrayIndex, MakePropertyObject(TEXT("number"), TEXT("Optional. Array index when resetting an array element. Defaults to 0.")) },
				{ Args::StackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional. Stack template index when the property lives on a state/transition stack instance rather than the root template.")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::PropertyName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ResetNodeProperty);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddSMVariable;
		Info.Description = TEXT("Add a member variable to a state-machine blueprint (USMBlueprint). Mirrors the editor's My Blueprint -> +Variable flow via FBlueprintEditorUtils::AddMemberVariable. Use this for FSM-blueprint-scoped variables; for node-instance variables on a USMStateInstance / USMConduitInstance / USMTransitionInstance subclass, use sm.add_node_variable instead. NOTE: this op does NOT compile the blueprint. The variable is added to the blueprint's NewVariables list but its FProperty is not materialized on the GeneratedClass until the next sm.compile. Downstream ops that look up the FProperty (notably sm.connect_node_variable_output with to_owning_blueprint_variable) will fail until you compile. Call sm.compile yourself when ready -- batch many adds before compiling for best performance.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::VariableName, MakePropertyObject(TEXT("string"), TEXT("Variable name (FName-style; no spaces).")) },
				{ Args::VarType, MakePropertyObject(TEXT("string"), TEXT("Type token for the element type. Accepted: bool, int, int64, byte, float, single, string, name, text, vector, vector2d, rotator, transform, linearcolor, color, guid; or a class/struct path such as /Script/Engine.Actor.")) },
				{ Args::ContainerType, MakePropertyObject(TEXT("string"), TEXT("Optional. Container wrapping the element type. Accepted: 'None', 'Array', 'Map', 'Set' (case-insensitive). Empty or 'None' = single value.")) },
				{ Args::KeyType, MakePropertyObject(TEXT("string"), TEXT("Optional. Required when container_type='Map'; same vocabulary as var_type. Must be empty when container_type is not 'Map'.")) },
				{ Args::DefaultValue, MakePropertyObject(TEXT("string"), TEXT("Optional. Default value as a string in UE property-text format (e.g. 'true', '1.25', '(R=1.0,G=0.0,B=0.0,A=1.0)'). Empty = engine default for the type.")) }
			},
			{ Args::AssetPath, Args::VariableName, Args::VarType });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddSMVariable);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::SpawnLocalGraphReadNode;
		Info.Description = TEXT("Spawn a Logic Driver local-graph-read K2 node into a state's local graph (OnStateBegin/Update/End share one bound graph per state) or a transition's CanEnterTransition graph. Compatibility by type: TimeInState / HasStateUpdated / GetNodeInstance work in state graphs and transition graphs; GetStateInformation is state-only; CanEvaluate / CanEvaluateFromEvent / GetTransitionInformation are transition-only; GetStateMachineReference is the intermediate graph of a state-machine-reference state (USMGraphNode_StateMachineStateNode); InEndState is a transition graph whose source state is a state-machine-reference state. The two sub-SM-only kinds reject plain state and transition graphs with 'is not compatible with graph' — call sm.add_reference first, then pass that state's guid (GetStateMachineReference) or the guid of a transition out of it (InEndState). Engine create_node does not expose these nodes, which is why this op exists.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the state or transition node whose bound graph receives the read node.")) },
				{ Args::Type, MakePropertyObject(TEXT("string"), TEXT("Local-graph-read node type. Accepted: TimeInState, HasStateUpdated, CanEvaluate, CanEvaluateFromEvent, GetStateInformation, GetTransitionInformation, GetStateMachineReference, GetNodeInstance, InEndState. Snake_case variants accepted too.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Local-graph X coordinate. Defaults to 0.")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Local-graph Y coordinate. Defaults to 0.")) },
				{ Args::NodeInstanceGuid, MakePropertyObject(TEXT("string"), TEXT("Only used by GetNodeInstance when targeting a specific stack instance. Empty/invalid = primary node template.")) },
				{ Args::NodeInstanceIndex, MakePropertyObject(TEXT("number"), TEXT("Only used by GetNodeInstance for stack lookups. -1 = primary node template.")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::Type });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SpawnLocalGraphReadNode);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ConfigureSMComponentOnActor;
		Info.Description = TEXT("Configure the USMStateMachineComponent template on an actor blueprint's SCS. Operates on the BP-class component (not a placed-instance component) so configuration persists into every placed actor. Sets StateMachineClass on the template when state_machine_class is supplied. Boolean fields use true/false; pass only the keys you want to change (others stay at their current template value). Enum fields accept display names: 'Client' | 'Server' | 'ClientAndServer'. The extra_config_json valve takes a JSON object of UPROPERTY name -> value for fields not promoted above; unrecognized keys are returned in 'unknown_keys' rather than failing the op.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::ActorBlueprint, MakePropertyObject(TEXT("string"), TEXT("Object path to the actor blueprint hosting the component (any UBlueprint with an SCS).")) },
				{ Args::StateMachineClass, MakePropertyObject(TEXT("string"), TEXT("Optional. Object path to a USMBlueprint whose generated class becomes the template's StateMachineClass. Empty = leave StateMachineClass alone.")) },
				{ Args::ComponentName, MakePropertyObject(TEXT("string"), TEXT("SCS variable name of the USMStateMachineComponent on the actor BP.")) },
				{ Args::StartOnBeginPlay, MakePropertyObject(TEXT("boolean"), TEXT("Automatically start the state machine on BeginPlay. Defaults to false on USMStateMachineComponent (manual Start() control); AI-authored slices that want the SM to run on placement should pass true.")) },
				{ Args::InitializeOnBeginPlay, MakePropertyObject(TEXT("boolean"), TEXT("Automatically initialize the state machine on InitializeComponent and BeginPlay.")) },
				{ Args::StopOnEndPlay, MakePropertyObject(TEXT("boolean"), TEXT("Automatically stop the state machine on EndPlay.")) },
				{ Args::ReuseInstanceAfterShutdown, MakePropertyObject(TEXT("boolean"), TEXT("Retain the runtime instance after Shutdown so the next Initialize reuses it.")) },
				{ Args::Replicates, MakePropertyObject(TEXT("boolean"), TEXT("Mark the component itself replicated (AActorComponent::SetIsReplicated).")) },
				{ Args::IncludeSimulatedProxies, MakePropertyObject(TEXT("boolean"), TEXT("Broadcast changes to simulated proxies, not just autonomous proxies.")) },
				{ Args::WaitForTransactionsFromServer, MakePropertyObject(TEXT("boolean"), TEXT("Client waits for server confirmation before applying state changes.")) },
				{ Args::HandleControllerChange, MakePropertyObject(TEXT("boolean"), TEXT("Handle pawn possession/unpossession by refreshing replication state.")) },
				{ Args::StateChangeAuthority, MakePropertyObject(TEXT("string"), TEXT("ESMNetworkConfigurationType display name: 'Client' | 'Server' | 'ClientAndServer'.")) },
				{ Args::NetworkTickConfiguration, MakePropertyObject(TEXT("string"), TEXT("ESMNetworkConfigurationType display name.")) },
				{ Args::NetworkStateExecution, MakePropertyObject(TEXT("string"), TEXT("ESMNetworkConfigurationType display name.")) },
				{ Args::NetworkTransitionEnteredConfiguration, MakePropertyObject(TEXT("string"), TEXT("ESMNetworkConfigurationType display name.")) },
				{ Args::ExtraConfigJson, MakePropertyObject(TEXT("string"), TEXT("JSON object of UPROPERTY name -> value for fields not promoted above. Values are applied via FProperty::ImportText_Direct. Unrecognized keys are reported in 'unknown_keys'.")) }
			},
			{ Args::ActorBlueprint, Args::ComponentName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ConfigureSMComponentOnActor);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::SpawnLocalGraphWriteNode;
		Info.Description = TEXT("Spawn a Logic Driver local-graph-write K2 node into a transition's CanEnterTransition graph or a conduit's bound graph. Compatibility by type: CanEvaluate works in transition and conduit graphs and disables/enables evaluation of the enclosing edge; CanEvaluateFromEvent is transition-only and disables/enables event-driven evaluation specifically. Both expose a single boolean input pin (seedable via 'default_value' or wireable to upstream K2 logic via blueprint connect_pins). TransitionEventReturn is intentionally NOT spawnable here: it is auto-placed as a side effect of binding a transition delegate; use sm.configure_transition_event instead. Engine create_node does not expose these nodes, which is why this op exists.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the transition or conduit node whose bound graph receives the write node.")) },
				{ Args::Type, MakePropertyObject(TEXT("string"), TEXT("Local-graph-write node type. Accepted: CanEvaluate, CanEvaluateFromEvent. Snake_case variants accepted too.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Local-graph X coordinate. Defaults to 0.")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Local-graph Y coordinate. Defaults to 0.")) },
				{ Args::DefaultValue, MakePropertyObject(TEXT("boolean"), TEXT("Optional. Seed the boolean input pin's literal default. Ignored if the pin ends up wired to an upstream node.")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::Type });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SpawnLocalGraphWriteNode);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ConfigureTransitionEvent;
		Info.Description = TEXT("Bind, rebind, or clear the auto-bound event on a transition edge, and/or update its trigger flags. Mirrors a user edit in the transition's Details panel exactly: applies each set field via PreEditChange/PostEditChangeProperty, so cascading resets (e.g. changing 'delegate_owner_instance' clears 'delegate_property_name' and 'delegate_owner_class' before later writes restore them) match the Details panel and any open panel auto-refreshes. Set 'delegate_property_name' to an empty string to clear the binding: this removes the auto-spawned event entry node from the transition's bound graph but PRESERVES any TransitionEventReturn the user wired downstream logic into (same behavior as a Details panel clear, so unbind/rebind cycles don't lose work). At least one field must be supplied. TransitionEventReturn is auto-placed on first bind. Never call sm.spawn_local_graph_write_node to create it.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the SMBlueprint owning the transition.")) },
				{ Args::TransitionGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the transition edge to reconfigure.")) },
				{ Args::DelegateOwnerInstance, MakePropertyObject(TEXT("string"), TEXT("Optional. Where the delegate lives. Accepted: 'This' (SM instance), 'Context', 'PreviousState'. Changing this resets delegate_owner_class and delegate_property_name as a side effect, so supply them together if you want to switch both.")) },
				{ Args::DelegateOwnerClass, MakePropertyObject(TEXT("string"), TEXT("Optional. Object path to the class owning the delegate property. Required when delegate_owner_instance is 'Context'. Empty string clears it. Changing this resets delegate_property_name.")) },
				{ Args::DelegatePropertyName, MakePropertyObject(TEXT("string"), TEXT("Optional. Name of the multicast delegate property to bind. Empty string clears the binding (removes the auto-spawned event entry node from the transition's bound graph but preserves any TransitionEventReturn node the user wired logic into).")) },
				{ Args::EventTriggersTargetedUpdate, MakePropertyObject(TEXT("boolean"), TEXT("Optional. Trigger a targeted SM update limited to this transition and destination state when the event fires. Propagates to existing TransitionEventReturn nodes that have bUseOwningTransitionSettings = true.")) },
				{ Args::EventTriggersFullUpdate, MakePropertyObject(TEXT("boolean"), TEXT("Optional. Trigger a full SM update when the event fires (legacy behavior; applied after targeted update). Propagates to existing TransitionEventReturn nodes with bUseOwningTransitionSettings = true.")) }
			},
			{ Args::AssetPath, Args::TransitionGuid });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ConfigureTransitionEvent);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::FindNodeTypes;
		Info.Description = TEXT("Enumerate Logic Driver K2 read/write kinds that are spawnable into a target state/transition/conduit bound graph. The Logic Driver companion to BlueprintTools.find_node_types: the engine action menu does not list LD K2 nodes (they're filtered out and spawned through dedicated sm.spawn_local_graph_*_node ops), so even a fully-fixed upstream find_node_types would never include them. Returns per-kind metadata: which spawn op to call, the spawn_type string to pass. Does NOT enumerate engine K2 nodes. For those, note that UE 5.8's BlueprintTools.find_node_types rejects SM transition/conduit bound graphs with 'Cannot cast type ... to Blueprint'; workaround is to query find_node_types against any non-SM UBlueprint's EventGraph (type_ids are universal across graphs).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the state, transition, or conduit node whose bound graph drives the compatibility check.")) },
				{ Args::TypeIdFilter, MakePropertyObject(TEXT("string"), TEXT("Optional case-insensitive substring match against the LD kind name (e.g. 'evaluate' matches CanEvaluate and CanEvaluateFromEvent). Empty = return every compatible kind.")) }
			},
			{ Args::AssetPath, Args::NodeGuid });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::FindNodeTypes);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddNodeVariable;
		Info.Description = TEXT("Add a Blueprint variable to a node-class Blueprint (a USMNodeInstance / USMStateInstance / USMConduitInstance / USMTransitionInstance subclass). Mirrors the editor's My Blueprint -> +Variable flow via FBlueprintEditorUtils::AddMemberVariable, and optionally stamps directional / hidden / read-only flags via the same path used by SMVariableCustomization. Transition-class blueprints accept plain variables but reject 'direction', 'b_hidden', 'b_read_only' (matches the editor's Variable Details panel filter). To author state/transition/conduit subclass variables in one call instead of post-add configure, set the directional flags here. COMPILE BEHAVIOR: when any of 'direction', 'b_hidden', 'b_read_only' is set, the blueprint is compiled in-call so the override can be stamped on the CDO; subsequent ops see the new FProperty immediately. When none of those fields are set (plain variable add), the blueprint is NOT compiled -- batch multiple adds and call sm.compile once at the end for best performance.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the node-class blueprint (USMNodeInstance subclass, including state, conduit, transition).")) },
				{ Args::VariableName, MakePropertyObject(TEXT("string"), TEXT("Variable name (FName-style; no spaces).")) },
				{ Args::VarType, MakePropertyObject(TEXT("string"), TEXT("Type token for the element type. Accepted: bool, int, int64, byte, float, single, string, name, text, vector, vector2d, rotator, transform, linearcolor, color, guid; or a class/struct path such as /Script/Engine.Actor.")) },
				{ Args::ContainerType, MakePropertyObject(TEXT("string"), TEXT("Optional. Container wrapping the element type. Accepted: 'None', 'Array', 'Map', 'Set' (case-insensitive). Maps and Sets cannot be graph-exposed on a node-class blueprint -- omit 'direction', 'b_hidden', 'b_read_only' for those.")) },
				{ Args::KeyType, MakePropertyObject(TEXT("string"), TEXT("Optional. Required when container_type='Map'; same vocabulary as var_type. Must be empty when container_type is not 'Map'.")) },
				{ Args::DefaultValue, MakePropertyObject(TEXT("string"), TEXT("Optional. Default value in UE property-text format (e.g. 'true', '1.25', '(R=1.0,G=0.0,B=0.0,A=1.0)'). Empty = engine default for the type.")) },
				{ Args::Direction, MakePropertyObject(TEXT("string"), TEXT("Optional. 'Input', 'Output', or 'Both'. Empty = no graph-pin exposure (plain BP variable). Transition-class blueprints reject this; Map / Set container variables reject this regardless of base class.")) },
				{ Args::Hidden, MakePropertyObject(TEXT("boolean"), TEXT("Optional. Hide the variable from on-node display. The property graph is still compiled and evaluated. Transition-class blueprints reject this; Map / Set container variables reject this regardless of base class.")) },
				{ Args::ReadOnly, MakePropertyObject(TEXT("boolean"), TEXT("Optional. Display the variable as read-only on the placed node. Transition-class blueprints reject this; Map / Set container variables reject this regardless of base class.")) }
			},
			{ Args::AssetPath, Args::VariableName, Args::VarType });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddNodeVariable);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::AddBlueprintVariable;
		Info.Description = TEXT("Add a member variable to ANY blueprint (UBlueprint), including plain Actor / GameMode / GameState / object blueprints. Mirrors the editor's My Blueprint -> +Variable flow via FBlueprintEditorUtils::AddMemberVariable, with full container support (Array / Map / Set). Use this when the engine's BlueprintTools.add_variable falls short -- notably for container-typed variables, which the engine op cannot author. For state-machine blueprints use sm.add_sm_variable; for node-class (USMNodeInstance subclass) blueprints use sm.add_node_variable. NOTE: this op does NOT compile the blueprint. The variable is added to the blueprint's NewVariables list but its FProperty is not materialized on the GeneratedClass until the next compile. Compile yourself when ready -- batch many adds before compiling for best performance.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target blueprint (any UBlueprint).")) },
				{ Args::VariableName, MakePropertyObject(TEXT("string"), TEXT("Variable name (FName-style; no spaces).")) },
				{ Args::VarType, MakePropertyObject(TEXT("string"), TEXT("Type token for the element type. Accepted: bool, int, int64, byte, float, single, string, name, text, vector, vector2d, rotator, transform, linearcolor, color, guid; or a class/struct path such as /Script/Engine.Actor.")) },
				{ Args::ContainerType, MakePropertyObject(TEXT("string"), TEXT("Optional. Container wrapping the element type. Accepted: 'None', 'Array', 'Map', 'Set' (case-insensitive). Empty or 'None' = single value.")) },
				{ Args::KeyType, MakePropertyObject(TEXT("string"), TEXT("Optional. Required when container_type='Map'; same vocabulary as var_type. Must be empty when container_type is not 'Map'.")) },
				{ Args::DefaultValue, MakePropertyObject(TEXT("string"), TEXT("Optional. Default value as a string in UE property-text format (e.g. 'true', '1.25', '(R=1.0,G=0.0,B=0.0,A=1.0)'). Empty = engine default for the type.")) }
			},
			{ Args::AssetPath, Args::VariableName, Args::VarType });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddBlueprintVariable);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ConfigureNodeVariable;
		Info.Description = TEXT("Reconfigure an existing variable on a node-class blueprint (USMNodeInstance / USMStateInstance / USMConduitInstance subclass). Mirrors the Direction combobox plus Hidden / ReadOnly toggles in SMVariableCustomization. Each field is gated by a paired 'b_update_*' flag so the AI can express 'leave alone' vs 'explicitly set' unambiguously. At least one of b_update_direction, b_update_hidden, b_update_read_only must be true. Transition-class blueprints are not supported (the function only sets directional / hidden / read-only state, which transition-class variables do not have).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the node-class blueprint.")) },
				{ Args::VariableName, MakePropertyObject(TEXT("string"), TEXT("Existing variable name on the blueprint.")) },
				{ Args::UpdateDirection, MakePropertyObject(TEXT("boolean"), TEXT("Gate for 'direction'. Must be true for the field to apply.")) },
				{ Args::Direction, MakePropertyObject(TEXT("string"), TEXT("New direction when gated. Accepted: Input, Output, Both.")) },
				{ Args::UpdateHidden, MakePropertyObject(TEXT("boolean"), TEXT("Gate for 'b_hidden'.")) },
				{ Args::Hidden, MakePropertyObject(TEXT("boolean"), TEXT("New hidden state when gated.")) },
				{ Args::UpdateReadOnly, MakePropertyObject(TEXT("boolean"), TEXT("Gate for 'b_read_only'.")) },
				{ Args::ReadOnly, MakePropertyObject(TEXT("boolean"), TEXT("New read-only state when gated.")) }
			},
			{ Args::AssetPath, Args::VariableName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ConfigureNodeVariable);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ConnectNodeVariableOutput;
		Info.Description = TEXT("Wire a node-class output variable to either another node's input variable (within the same FSM) or to a variable on the owning FSM blueprint. Spawns the matching IO reader/writer K2 node inside the relevant property sub-graph and links pins, identical to what a user does by opening the property graph and dragging. Stack-aware: when the source or destination variable lives on a stacked instance, set 'from_stack_index' / 'to_stack_index' (same convention as set_node_property; omit or -1 = primary template). Targets are mutually exclusive: supply EITHER 'to_state_guid' + 'to_variable_name' for node->node wiring, OR 'to_owning_blueprint_variable' for node->owner wiring. Idempotent: if the wire already exists, returns success without spawning duplicate IO nodes. Preconditions: source variable must be Output (or Both); destination variable must be Input (or Both) for node->node; owning-BP variable must exist for node->owner; pin types must be compatible.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the state-machine blueprint containing both endpoints.")) },
				{ Args::FromStateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the source state node (the output side).")) },
				{ Args::FromStackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional. Stack index on the source state. -1 or omitted = primary template.")) },
				{ Args::FromVariableName, MakePropertyObject(TEXT("string"), TEXT("Output variable name on the source template. Must be configured Output (or Both).")) },
				{ Args::ToStateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the destination state node. Omit (or empty) when wiring to the owning blueprint.")) },
				{ Args::ToStackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional. Stack index on the destination state. -1 or omitted = primary template.")) },
				{ Args::ToVariableName, MakePropertyObject(TEXT("string"), TEXT("Input variable name on the destination template. Required for node->node; omit when wiring to the owning blueprint.")) },
				{ Args::ToOwningBlueprintVariable, MakePropertyObject(TEXT("string"), TEXT("Variable name on the owning FSM blueprint to write into. Mutually exclusive with to_state_guid/to_variable_name.")) }
			},
			{ Args::AssetPath, Args::FromStateGuid, Args::FromVariableName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ConnectNodeVariableOutput);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::DisconnectNodeVariableOutput;
		Info.Description = TEXT("Break a previously-established node-output wire. Argument shape mirrors connect_node_variable_output exactly. Returns 'applied' = true when a matching wire was found and broken; false when no matching wire was present (idempotent).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the state-machine blueprint.")) },
				{ Args::FromStateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the source state node.")) },
				{ Args::FromStackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional. Stack index on the source state. -1 or omitted = primary template.")) },
				{ Args::FromVariableName, MakePropertyObject(TEXT("string"), TEXT("Output variable name on the source template.")) },
				{ Args::ToStateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the destination state node. Omit when disconnecting from the owning blueprint.")) },
				{ Args::ToStackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional. Stack index on the destination state.")) },
				{ Args::ToVariableName, MakePropertyObject(TEXT("string"), TEXT("Input variable name on the destination template.")) },
				{ Args::ToOwningBlueprintVariable, MakePropertyObject(TEXT("string"), TEXT("Variable name on the owning blueprint. Mutually exclusive with to_state_guid/to_variable_name.")) }
			},
			{ Args::AssetPath, Args::FromStateGuid, Args::FromVariableName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::DisconnectNodeVariableOutput);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::RuntimeGetState;
		Info.Description = TEXT("Introspect a running state machine during Play-In-Editor. Resolves the actor in the live PIE world, finds its USMStateMachineComponent, and reports the live instance's active state(s), is_active, and is_in_end_state. Use this for runtime validation instead of editor.run_python.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::ActorIdentifier, MakePropertyObject(TEXT("string"), TEXT("Object name or display label of the actor in the running PIE world.")) },
				{ Args::ComponentName, MakePropertyObject(TEXT("string"), TEXT("Optional component name; omit to use the first USMStateMachineComponent on the actor.")) },
				{ Args::IncludeProperties, MakePropertyObject(TEXT("boolean"), TEXT("Include each active state's exposed property values (live runtime values). Default false.")) },
				{ Args::MaxDepth, MakePropertyObject(TEXT("number"), TEXT("Optional recursion depth for struct members and array elements within included property values. 0 (default) returns the flat exported value only.")) },
				{ Args::PieInstance, MakePropertyObject(TEXT("number"), TEXT("Optional PIE world index for multi-client play. Default 0.")) }
			},
			{ Args::ActorIdentifier });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::RuntimeGetState);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::SetConduitCondition;
		Info.Description = TEXT("Set a conduit's default pass condition (its bCanEnterTransition result-pin default), mirroring set_transition_condition. Guidance: a conduit used as an always-true entry gate is better modeled as an empty state (no condition to evaluate, routes purely on transitions); use a conduit for real branch points and leave eval-with-transitions on (configure that via add_conduit or set_node_property).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the conduit node.")) },
				{ Args::Condition, MakePropertyObject(TEXT("boolean"), TEXT("Default evaluation result of the conduit (true = passes by default).")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::Condition });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SetConduitCondition);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::SpawnActorContextComponent;
		Info.Description = TEXT("Author the LD context-to-component reach chain in one call: GetContext -> Cast To <actor class> -> GetComponentByClass(<component class>), in a node-class graph. The state machine's context is cast to the supplied actor class (the context is not itself a component). All three nodes are pure (no exec pins), so this is a self-contained data cluster: wire the typed component from component_output_pin_id into wherever you need it and the chain evaluates on demand.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the node-class blueprint (USMNodeInstance subclass) to author into.")) },
				{ Args::TargetGraphPath, MakePropertyObject(TEXT("string"), TEXT("Name or full path of the destination graph (e.g. the event graph hosting an OnStateBegin event).")) },
				{ Args::TargetActorClass, MakePropertyObject(TEXT("string"), TEXT("Class path of the owning actor to cast GetContext() to.")) },
				{ Args::ComponentClass, MakePropertyObject(TEXT("string"), TEXT("Component class path passed to GetComponentByClass; the returned pin is typed to this class.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Optional base graph X for the cluster.")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Optional base graph Y for the cluster.")) }
			},
			{ Args::AssetPath, Args::TargetGraphPath, Args::TargetActorClass, Args::ComponentClass });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SpawnActorContextComponent);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::CollapseToStateMachine;
		Info.Description = TEXT("Collapse a set of state nodes into a new nested state machine in the same root graph, the headless equivalent of the editor's \"Collapse to State Machine\". Boundary transitions (edges crossing the selection) are rewired onto the new container; fully-interior states and edges move into its bound graph. The set must contain at least one state node and every node must belong to the same state machine graph. Reroute waypoints do not count toward the state-node requirement, and transitions between included states are pulled in automatically even if omitted. Returns state_guid (the new container's guid, kind 'state_machine_state') and state_name.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuids, MakePropertyObject(TEXT("array"), TEXT("Guids of the nodes to collapse (array of strings). Must include at least one state node; every node must belong to the same state machine graph.")) }
			},
			{ Args::AssetPath, Args::NodeGuids });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::CollapseToStateMachine);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::MergeStates;
		Info.Description = TEXT("Merge source states' node-class templates into a destination state's stack, the headless equivalent of the editor's \"Cut and Merge States\" (b_destroy_states=true) / \"Copy and Merge States\" (b_destroy_states=false). Each source contributes its non-default templates as new stack entries on the destination; a source on the default node class with no stack contributes nothing. The cut variant also destroys each source and rewires its transitions onto the destination. Destination and sources must be plain states (USMGraphNode_StateNode) in the same graph; a source equal to the destination is rejected. Returns destination_state_guid, merged_stack_template_guids (the minted stack-template guids; empty when no source contributed a non-default template), and b_destroy_states (echoes the applied mode).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::DestinationStateGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the plain state that receives the merged templates as new stack entries.")) },
				{ Args::SourceStateGuids, MakePropertyObject(TEXT("array"), TEXT("Guids of the source plain states whose templates are merged into the destination (array of strings). Each must differ from the destination and live in the same graph.")) },
				{ Args::DestroyStates, MakePropertyObject(TEXT("boolean"), TEXT("When true, destroy each merged source state and rewire its transitions onto the destination (cut). When false (default), leave the sources in place (copy).")) }
			},
			{ Args::AssetPath, Args::DestinationStateGuid, Args::SourceStateGuids });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::MergeStates);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ReplaceNode;
		Info.Description = TEXT("Replace a node in place with a different kind, the headless equivalent of the editor's \"Replace With ...\" entries. Existing transitions are preserved and the original node is removed; the replacement is a freshly minted node, so the returned node_guid differs from the input. The set of valid target kinds for a given node matches the editor's right-click availability (e.g. a transition cannot be replaced). 'parent' is only valid in a child state machine blueprint whose parent class exposes a state machine to override. Returns node_guid (the replacement node's guid, which differs from the input) and kind (echoes the input).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the node to replace.")) },
				{ Args::Kind, MakePropertyObject(TEXT("string"), TEXT("Target kind: 'state', 'conduit', 'state_machine' (inline nested), 'reference' (empty state-machine reference), or 'parent' (state-machine parent call).")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::Kind });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ReplaceNode);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::ConvertToReference;
		Info.Description = TEXT("Convert an inline nested state machine node into its own reusable state-machine-reference asset, the headless equivalent of the editor's \"Convert to State Machine Reference\" (no modal dialog). A new SMBlueprint is minted to hold the extracted graph, the node is rebound as a reference to it, and the original inline graph is emptied. The node keeps its guid. The node must be an inline nested state machine (USMGraphNode_StateMachineStateNode that is not already a reference and not a parent node). Returns node_guid (unchanged), reference_asset_path (the minted asset's object path), and name (the minted asset's short name).");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the source SMBlueprint that owns the inline nested state machine.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the inline nested state machine node to convert.")) },
				{ Args::Name, MakePropertyObject(TEXT("string"), TEXT("Optional. Name for the minted reference asset. Empty derives the name from the node's state name with the project's reference-name prefix.")) },
				{ Args::Path, MakePropertyObject(TEXT("string"), TEXT("Optional. Package folder for the minted asset. Empty places it alongside the source Blueprint.")) },
				{ Args::ParentClass, MakePropertyObject(TEXT("string"), TEXT("Optional. Class path for a USMInstance subclass to parent the minted reference Blueprint. Empty uses the factory default.")) }
			},
			{ Args::AssetPath, Args::NodeGuid });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::ConvertToReference);
		RegisterOperation(MoveTemp(Info));
	}

	RegisterGenericFallbackOperations();
}

void USMAssistSubsystem::RegisterGenericFallbackOperations()
{
	using namespace LD::Assist::Private;
	namespace GOps = LD::Assist::GenericOps::Ops;
	namespace GArgs = LD::Assist::GenericOps::Args;

	{
		FSMAssistOperationInfo Info;
		Info.Name = GOps::ReadProperty;
		Info.Description = TEXT("FALLBACK ONLY. Alternative to the official engine MCP blueprint.* tools; use only when those cannot express the read (e.g. TMap/TArray element values, or a target object other than self). Reads a property value via reflection from an asset CDO (target=edit) or a live PIE actor (target=runtime).");
		Info.InputSchema = MakeSchema(
			{
				{ GArgs::Object, MakePropertyObject(TEXT("string"), TEXT("target=edit: asset/object path (its CDO is read). target=runtime: PIE actor name or label.")) },
				{ GArgs::Target, MakePropertyObject(TEXT("string"), TEXT("'edit' (default) reads the asset CDO; 'runtime' reads a live PIE actor.")) },
				{ GArgs::PropertyPath, MakePropertyObject(TEXT("string"), TEXT("Reflection path: Member, nested Struct.Field, array Arr[3], or map Map[Key] (engine PropertyPathHelpers cannot resolve map elements).")) },
				{ GArgs::PieInstance, MakePropertyObject(TEXT("number"), TEXT("Optional PIE world index when target=runtime. Default 0.")) }
			},
			{ GArgs::Object, GArgs::PropertyPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::GenericOps::ReadProperty);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = GOps::WriteProperty;
		Info.Description = TEXT("FALLBACK ONLY. Alternative to the official engine MCP blueprint.* tools; use only when those cannot express the write (e.g. TMap/TArray element values, or a target object other than self). Writes a property value via reflection. 'value' uses UE property-text form (same as sm.set_node_property). Edit-target writes change the CDO/archetype and may need sm.compile to propagate; runtime writes are transient. Arrays are not grown; absent map keys are created.");
		Info.InputSchema = MakeSchema(
			{
				{ GArgs::Object, MakePropertyObject(TEXT("string"), TEXT("target=edit: asset/object path (its CDO is written). target=runtime: PIE actor name or label.")) },
				{ GArgs::Target, MakePropertyObject(TEXT("string"), TEXT("'edit' (default) writes the asset CDO; 'runtime' writes a live PIE actor.")) },
				{ GArgs::PropertyPath, MakePropertyObject(TEXT("string"), TEXT("Reflection path: Member, nested Struct.Field, array Arr[3], or map Map[Key].")) },
				{ GArgs::Value, MakePropertyObject(TEXT("string"), TEXT("New value in UE property-text form (e.g. true, 7, \"hi\", (X=1,Y=2)).")) },
				{ GArgs::PieInstance, MakePropertyObject(TEXT("number"), TEXT("Optional PIE world index when target=runtime. Default 0.")) }
			},
			{ GArgs::Object, GArgs::PropertyPath, GArgs::Value });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::GenericOps::WriteProperty);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = GOps::AddDispatcher;
		Info.Description = TEXT("FALLBACK ONLY. Alternative to the official engine MCP blueprint.add_event_dispatcher; use only when that fails (its dispatchers do not survive compile). Adds a multicast-delegate event dispatcher that survives compilation, with an optional parameter signature.");
		Info.InputSchema = MakeSchema(
			{
				{ GArgs::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target blueprint.")) },
				{ GArgs::Name, MakePropertyObject(TEXT("string"), TEXT("Dispatcher name. Must be unique on the blueprint.")) },
				{ GArgs::Params, MakePropertyObject(TEXT("array"), TEXT("Optional signature: array of {name, type}. type is a terminal token (bool, int, int64, float, name, string, text, vector, or an object/struct path).")) }
			},
			{ GArgs::AssetPath, GArgs::Name });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::GenericOps::AddDispatcher);
		RegisterOperation(MoveTemp(Info));
	}
}
