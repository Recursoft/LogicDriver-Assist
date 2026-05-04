// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"

#include "SMAssistLog.h"
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
		Info.Description = TEXT("Set a value on a node property or mutate an array property's structure. Resolution order (when stack_index is omitted): graph-node properties first (NodePosX, NodePosY, NodeComment, bCommentBubblePinned, etc.), then the node's primary template (node-class fields). When stack_index is provided, targets the stack template directly. Supports scalar and array properties on templates; graph-node properties only support 'set'. Array actions (template-only): 'set' writes value(s) at array_index (auto-grows); 'add' appends a default element; 'insert' inserts a default at array_index; 'duplicate' clones the element at array_index (preserves pin literals and wired graphs); 'move' reorders array_index to target_index (preserves element guids); 'remove' removes array_index; 'clear' empties the array. Structural actions reject a 'value' payload and surface EditFixedSize / read-only arrays as explicit errors.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the node containing the property.")) },
				{ Args::PropertyName, MakePropertyObject(TEXT("string"), TEXT("Name of the property. Resolves to graph-node fields (UEdGraphNode / USMGraphNode_Base) first, then falls back to the node template.")) },
				{ Args::Value, MakePropertyObject(TEXT("string"), TEXT("New value. Accepts string, number, boolean, null, or an array of those (array replaces all elements starting at index 0). Required for 'set'; rejected by structural actions (add/insert/duplicate/move/remove/clear).")) },
				{ Args::ArrayIndex, MakePropertyObject(TEXT("number"), TEXT("Array index. 'set' scalar: element to write (auto-resizes). 'insert': insertion point (elements at/above shift up). 'duplicate': source element. 'move': source element. 'remove': element to remove. Must be omitted when 'value' is an array.")) },
				{ Args::TargetIndex, MakePropertyObject(TEXT("number"), TEXT("Destination index for 'array_action=move'. Must differ from 'array_index'. Unused by other actions.")) },
				{ Args::ArrayAction, MakePropertyObject(TEXT("string"), TEXT("Array operation mode. 'set' (default): write value(s). Structural (template-only): 'add', 'insert', 'duplicate', 'move', 'remove', 'clear'.")) },
				{ Args::StackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional state-stack template index. Omit to use the default resolution (graph-node then primary template); provide to target a stack element returned by sm.add_state_stack.")) }
			},
			{ Args::AssetPath, Args::NodeGuid, Args::PropertyName });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::SetNodeProperty);
		RegisterOperation(MoveTemp(Info));
	}

	{
		FSMAssistOperationInfo Info;
		Info.Name = Ops::Compile;
		Info.Description = TEXT("Compile a state machine blueprint and return its compile status.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) }
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
		Info.Description = TEXT("Add a state machine reference node to an existing state machine blueprint's root graph. Points at another SMBlueprint. Positioning follows the same row-based convention as states: Entry at (0, 0), positive X, match the Y of its row. A reference node must be wired into the flow in the same authoring step: add at least one inbound transition (and typically an outbound one) so the reference participates in the main flow. Orphan reference nodes are a layout failure.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::ReferenceAssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the SMBlueprint to reference.")) },
				{ Args::StateName, MakePropertyObject(TEXT("string"), TEXT("Optional node name.")) },
				{ Args::IsEntry, MakePropertyObject(TEXT("boolean"), TEXT("Mark the new reference as the entry state.")) },
				{ Args::PositionX, MakePropertyObject(TEXT("number"), TEXT("Graph X coordinate. Entry is at x=0, so prefer positive values (~200+) to place the reference to the right of Entry.")) },
				{ Args::PositionY, MakePropertyObject(TEXT("number"), TEXT("Graph Y coordinate. 0 aligns horizontally with Entry; use non-zero only for deliberate vertical layout.")) }
			},
			{ Args::AssetPath, Args::ReferenceAssetPath });
		Info.Handler = FSMAssistOperationHandler::CreateStatic(&LD::Assist::AddReference);
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
		Info.Description = TEXT("List the editable properties on a node template. Returns name, type, category, and current value for each. Supports an optional stack_index.");
		Info.InputSchema = MakeSchema(
			{
				{ Args::AssetPath, MakePropertyObject(TEXT("string"), TEXT("Object path to the target SMBlueprint.")) },
				{ Args::NodeGuid, MakePropertyObject(TEXT("string"), TEXT("Guid of the state or transition node.")) },
				{ Args::StackIndex, MakePropertyObject(TEXT("number"), TEXT("Optional stack template index. Omit to target the node's primary template.")) }
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
}
