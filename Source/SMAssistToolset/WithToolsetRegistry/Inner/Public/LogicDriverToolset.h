// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "LogicDriverToolset.generated.h"

class USMBlueprint;

/**
 * Logic Driver state-machine inspection and authoring tools, exposed to AI assistants via the
 * engine's ToolsetRegistry / Model Context Protocol pipeline. Each UFUNCTION mirrors one
 * SMAssist operation; the marshal layer translates typed args into the JSON envelope that
 * USMAssistSubsystem::ExecuteOperation expects, then returns the resulting payload as a JSON
 * string. Wire envelope into SMAssist matches what SMAssistMonolithBridge produces, so the
 * same SMAssist behavior tests cover both transports.
 *
 * Convention note: every param is required at the MCP schema layer (UE 5.8's dispatcher rejects
 * omitted fields regardless of C++ defaults). C++ default values document the "use SMAssist
 * default" sentinel (empty string for FString, -1 for int32, -1.0 for "auto-layout"
 * positions/gaps and other double fields); the marshal helper skips these sentinels when
 * building the JSON, so SMAssist sees the same omit-vs-pass semantics it sees from Monolith.
 *
 * Authoring guidance for AI clients: prefer LayoutStates(apply=true) over manual position_x/y
 * for greenfield graphs. State nodes are roughly 130 to 150 px wide at 1:1 zoom and the editor
 * renders an Entry-pointer marker about 200 px to the left of the entry state, so entry states
 * placed near X=0 are visually eclipsed by the marker even though GetAsset reports them present.
 * GetAsset returns logical coordinates only; for visual verification use CaptureGraphView (see
 * its docstring for cost guidance on when to call).
 */
UCLASS(MinimalAPI)
class ULogicDriverToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Creates a new Logic Driver state-machine blueprint asset.
	 * @param Name Asset name (without path or extension). Required.
	 * @param Path Package path under /Game where the asset is created. Empty = /Game root.
	 * @return JSON: { asset_path: "/Game/.../<Name>.<Name>", name: "<Name>" }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString CreateBlueprint(
		const FString& Name,
		const FString& Path = TEXT(""));

	/**
	 * Lists all Logic Driver state-machine blueprint assets in the project.
	 * @param PathPrefix Filter to asset paths beginning with this prefix. Empty = no filter (all assets).
	 * @return JSON: { assets: [{ asset_path, name, parent_class? }, ...], count }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ListAssets(const FString& PathPrefix = TEXT(""));

	/**
	 * Returns the full graph structure of a state-machine blueprint: states, transitions, entry points.
	 * @param Blueprint The state-machine blueprint to inspect. Required.
	 * @return JSON: { asset_path, name, parent_class?, entry_state_guids:[...], states:[...], transitions:[...] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString GetAsset(USMBlueprint* Blueprint);

	/**
	 * Compiles a state-machine blueprint and reports the result.
	 * @param Blueprint The blueprint to compile. Required.
	 * @return JSON: { asset_path, up_to_date, has_warnings, has_errors, status }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString Compile(USMBlueprint* Blueprint);

	/**
	 * Adds a regular state node to a blueprint's root state machine graph.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param StateName Display name for the new state. Empty = SMAssist auto-names ("State", "State_1", ...).
	 * @param bIsEntry Whether this state becomes the graph's entry. Default false.
	 * @param PositionX Canvas X coordinate. Negative sentinel (e.g., -1.0) = SMAssist auto-positions. Prefer the sentinel or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionY Canvas Y coordinate. Negative sentinel (e.g., -1.0) = SMAssist auto-positions.
	 * @param StateClass Full path of a USMStateInstance_Base subclass. Empty = base USMStateInstance.
	 * @return JSON: { state_guid, state_name }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddState(
		USMBlueprint* Blueprint,
		const FString& StateName = TEXT(""),
		bool bIsEntry = false,
		double PositionX = -1.0,
		double PositionY = -1.0,
		const FString& StateClass = TEXT(""));

	/**
	 * Adds a conduit node to a blueprint's root state machine graph.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param StateName Display name for the new conduit. Empty = auto-name.
	 * @param bIsEntry Whether this conduit becomes the graph's entry. Default false.
	 * @param PositionX Canvas X coordinate. Negative sentinel (e.g., -1.0) = auto-position. Prefer the sentinel or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionY Canvas Y coordinate. Negative sentinel (e.g., -1.0) = auto-position.
	 * @param StateClass Full path of a USMConduitInstance subclass. Empty = base conduit.
	 * @param bEvalWithTransitions Whether the conduit evaluates inline with outgoing transitions. Default true.
	 * @return JSON: { state_guid, state_name, state_class? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddConduit(
		USMBlueprint* Blueprint,
		const FString& StateName = TEXT(""),
		bool bIsEntry = false,
		double PositionX = -1.0,
		double PositionY = -1.0,
		const FString& StateClass = TEXT(""),
		bool bEvalWithTransitions = true);

	/**
	 * Adds an AnyState node to a blueprint's root state machine graph.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param StateName Display name for the AnyState. Empty = auto-name.
	 * @param PositionX Canvas X coordinate. Negative sentinel (e.g., -1.0) = auto-position. Prefer the sentinel or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionY Canvas Y coordinate. Negative sentinel (e.g., -1.0) = auto-position.
	 * @return JSON: { state_guid, state_name }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddAnyState(
		USMBlueprint* Blueprint,
		const FString& StateName = TEXT(""),
		double PositionX = -1.0,
		double PositionY = -1.0);

	/**
	 * Adds a LinkState node pointing at an existing state by name.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param LinkToStateName Display name of the target state to link to. Required.
	 * @param PositionX Canvas X coordinate. Negative sentinel (e.g., -1.0) = auto-position. Prefer the sentinel or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionY Canvas Y coordinate. Negative sentinel (e.g., -1.0) = auto-position.
	 * @return JSON: { state_guid, state_name, linked_state_guid?, link_to_state_name? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddLinkState(
		USMBlueprint* Blueprint,
		const FString& LinkToStateName,
		double PositionX = -1.0,
		double PositionY = -1.0);

	/**
	 * Adds a reference node that embeds another state-machine blueprint into this graph.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param ReferenceBlueprint The blueprint to reference (rendered as a sub-state-machine). Required.
	 * @param StateName Display name for the reference node. Empty = auto-name.
	 * @param bIsEntry Whether this reference becomes the graph's entry. Default false.
	 * @param PositionX Canvas X coordinate. Negative sentinel (e.g., -1.0) = auto-position. Prefer the sentinel or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionY Canvas Y coordinate. Negative sentinel (e.g., -1.0) = auto-position.
	 * @return JSON: { state_guid, state_name, reference_asset_path }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddReference(
		USMBlueprint* Blueprint,
		USMBlueprint* ReferenceBlueprint,
		const FString& StateName = TEXT(""),
		bool bIsEntry = false,
		double PositionX = -1.0,
		double PositionY = -1.0);

	/**
	 * Connects two existing states with a transition.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param FromStateGuid GUID of the source state. Required.
	 * @param ToStateGuid GUID of the destination state. Required.
	 * @param TransitionClass Full path of a USMTransitionInstance subclass. Empty = base transition.
	 * @return JSON: { transition_guid, from_state_guid, to_state_guid }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddTransition(
		USMBlueprint* Blueprint,
		const FString& FromStateGuid,
		const FString& ToStateGuid,
		const FString& TransitionClass = TEXT(""));

	/**
	 * Stacks an additional state-instance class onto an existing state node.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param StateGuid GUID of the state to extend. Required.
	 * @param StateClass Full path of a USMStateInstance subclass to stack on. Required.
	 * @param StackIndex Position in the stack to insert at. -1 = append.
	 * @return JSON: { state_guid, state_class, stack_index, template_guid }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddStateStack(
		USMBlueprint* Blueprint,
		const FString& StateGuid,
		const FString& StateClass,
		int32 StackIndex = -1);

	/**
	 * Stacks an additional transition-instance class onto an existing transition.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param TransitionGuid GUID of the transition to extend. Required.
	 * @param TransitionClass Full path of a USMTransitionInstance subclass to stack on. Required.
	 * @param StackIndex Position in the stack to insert at. -1 = append.
	 * @return JSON: { transition_guid, transition_class, stack_index, template_guid }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddTransitionStack(
		USMBlueprint* Blueprint,
		const FString& TransitionGuid,
		const FString& TransitionClass,
		int32 StackIndex = -1);

	/**
	 * Sets a transition's condition result (always true / always false).
	 * @param Blueprint The blueprint to modify. Required.
	 * @param TransitionGuid GUID of the transition. Required.
	 * @param bCondition Constant condition value. True = always pass; false = always block. Required.
	 * @return JSON: { transition_guid, condition }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SetTransitionCondition(
		USMBlueprint* Blueprint,
		const FString& TransitionGuid,
		bool bCondition);

	/**
	 * Removes a state, transition, or other node from a blueprint by GUID.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the node (state or transition) to remove. Required.
	 * @return JSON: { node_guid }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString RemoveNode(
		USMBlueprint* Blueprint,
		const FString& NodeGuid);

	/**
	 * Renames a state.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param StateGuid GUID of the state to rename. Required.
	 * @param NewName New display name. Required.
	 * @return JSON: { state_guid, state_name }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString RenameState(
		USMBlueprint* Blueprint,
		const FString& StateGuid,
		const FString& NewName);

	/**
	 * Marks a state as the graph's entry point (replaces any existing entry).
	 * @param Blueprint The blueprint to modify. Required.
	 * @param StateGuid GUID of the state to mark as entry. Required.
	 * @return JSON: { state_guid, state_name }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SetInitialState(
		USMBlueprint* Blueprint,
		const FString& StateGuid);

	/**
	 * Sets a property on a state/transition node. Multiplexes set / add / insert / duplicate /
	 * move / remove / clear based on ArrayAction.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the node containing the property. Required.
	 * @param PropertyName Name of the property to set. Required.
	 * @param Value JSON-encoded value text (scalar, array, or object). Empty = no value (e.g., for clear).
	 * @param ArrayIndex Index into an array-typed property. -1 = not an array op.
	 * @param TargetIndex Destination index for ArrayAction=move. -1 = not a move op.
	 * @param ArrayAction One of "add" / "insert" / "duplicate" / "move" / "remove" / "clear". Empty = scalar set.
	 * @param StackIndex Which stacked instance to target (for stacked nodes). -1 = base.
	 * @return JSON: { node_guid, property_name, stack_index?, array_action?, array_index?, target_index?, element_count? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SetNodeProperty(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& PropertyName,
		const FString& Value = TEXT(""),
		int32 ArrayIndex = -1,
		int32 TargetIndex = -1,
		const FString& ArrayAction = TEXT(""),
		int32 StackIndex = -1);

	/**
	 * Resets a property on a node back to its default value (or removes one array element if ArrayIndex set).
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the node. Required.
	 * @param PropertyName Name of the property to reset. Required.
	 * @param ArrayIndex Index into an array-typed property. -1 = reset whole property.
	 * @param StackIndex Which stacked instance to target. -1 = base.
	 * @return JSON: { node_guid, property_name, stack_index?, array_index? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ResetNodeProperty(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& PropertyName,
		int32 ArrayIndex = -1,
		int32 StackIndex = -1);

	/**
	 * Lists the exposed properties of a node with their current values.
	 * @param Blueprint The blueprint to inspect. Required.
	 * @param NodeGuid GUID of the node. Required.
	 * @param StackIndex Which stacked instance to inspect. -1 = base.
	 * @return JSON: { node_guid, state_class, stack_index?, properties:[{ name, type, category?, value }], count }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString GetNodeProperties(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		int32 StackIndex = -1);

	/**
	 * Returns pin-level information for one or all variables on a node (pin id, type, defaults, links).
	 * @param Blueprint The blueprint to inspect. Required.
	 * @param NodeGuid GUID of the node. Required.
	 * @param VariableName Specific variable to inspect. Empty = return pins for all variables on the node.
	 * @return JSON: { node_guid, count, properties:[{ variable_name, guid, result_pin:{...} }] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString GetPropertyPins(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& VariableName = TEXT(""));

	/**
	 * Returns the on-canvas view of a state machine graph: node positions, sizes, colors, optional pins.
	 * @param Blueprint The blueprint to inspect. Required.
	 * @param bIncludeTransitions Include transition entries in the output. Default true.
	 * @param bIncludePins Include per-node pin metadata. Default false.
	 * @return JSON: { asset_path, panel_view, nodes:[...], transitions?:[...] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString GetGraphView(
		USMBlueprint* Blueprint,
		bool bIncludeTransitions = true,
		bool bIncludePins = false);

	/**
	 * Captures a screenshot of a state machine graph (whole graph or a single node), saved as PNG
	 * under the configured Saved/ subdirectory. The PNG is non-trivial in image tokens, so
	 * call only when the visual layout is in question: after manual position_x/y placement,
	 * after a LayoutStates pass the user wants to verify, or when the user explicitly asks how
	 * the graph looks. Don't call reflexively after every authoring step.
	 * @param Blueprint The blueprint to capture. Required.
	 * @param bClipToPanel Clip the capture to the editor's graph-panel widget. Default true.
	 * @param bFitToContent Auto-fit the view to the graph contents before capture. Default true.
	 * @param NodeGuid GUID of a single node to focus on. Empty = capture the whole graph.
	 * @param OutputSubdir Output folder under Saved/. Empty = "LogicDriver".
	 * @param Prefix File-name prefix for the screenshot. Empty = no prefix.
	 * @return JSON: { asset_path, path, width, height, bytes, mime }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString CaptureGraphView(
		USMBlueprint* Blueprint,
		bool bClipToPanel = true,
		bool bFitToContent = true,
		const FString& NodeGuid = TEXT(""),
		const FString& OutputSubdir = TEXT(""),
		const FString& Prefix = TEXT(""));

	/**
	 * Deletes screenshots from a Saved/ subdirectory, optionally filtered by age.
	 * @param OutputSubdir Subdir under Saved/ to clean. Empty = "LogicDriver".
	 * @param OlderThanSeconds Only delete files older than this many seconds. Negative sentinel (e.g., -1.0) = no time filter, delete all matching files.
	 * @param bDryRun Report what would be deleted without actually deleting. Default false.
	 * @return JSON: { directory, dry_run, deleted_count, freed_bytes, paths:[...] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ClearScreenshots(
		const FString& OutputSubdir = TEXT(""),
		double OlderThanSeconds = -1.0,
		bool bDryRun = false);

	/**
	 * Computes (and optionally applies) an automatic layout for a state machine graph. Recommended
	 * default after authoring a graph from scratch: greenfield Add* calls can leave nodes at
	 * positions that collide with the editor's Entry-pointer marker or overlap each other.
	 * Calling this with bApply=true after the last node is added produces a clean left-to-right
	 * layout. Use manual position_x/y on the Add* ops only when reproducing an existing layout
	 * the user already approved.
	 * @param Blueprint The blueprint to lay out. Required.
	 * @param Strategy Layout algorithm key (e.g., "topological"). Empty = SMAssist default.
	 * @param bApply Apply the computed layout to the asset (true) or return as proposal only (false). Default false.
	 * @param Scope "graph" / "selection" / etc. Empty = whole graph.
	 * @param ColumnGap Horizontal spacing between columns. Negative sentinel (e.g., -1.0) = SMAssist default gap.
	 * @param RowGap Vertical spacing between rows. Negative sentinel (e.g., -1.0) = SMAssist default gap.
	 * @param StartX Origin X for the layout. Negative sentinel (e.g., -1.0) = SMAssist default origin.
	 * @param StartY Origin Y for the layout. Negative sentinel (e.g., -1.0) = SMAssist default origin.
	 * @param PinNodeGuidsJson JSON-encoded array of state GUID strings that should remain pinned at their existing positions. Empty = no pins.
	 * @param bRespectExistingOrder Whether to preserve existing graph-order hints. Default false.
	 * @param bSnapToGrid Snap final positions to the editor grid. Default false.
	 * @return JSON: { asset_path, strategy, scope, applied, graphs:[...] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString LayoutStates(
		USMBlueprint* Blueprint,
		const FString& Strategy = TEXT(""),
		bool bApply = false,
		const FString& Scope = TEXT(""),
		double ColumnGap = -1.0,
		double RowGap = -1.0,
		double StartX = -1.0,
		double StartY = -1.0,
		const FString& PinNodeGuidsJson = TEXT(""),
		bool bRespectExistingOrder = false,
		bool bSnapToGrid = false);
};
