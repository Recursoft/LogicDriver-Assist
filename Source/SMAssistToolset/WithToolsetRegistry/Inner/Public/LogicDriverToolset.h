// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "LogicDriverToolset.generated.h"

class UBlueprint;
class USMBlueprint;

/**
 * Configuration bag for ConfigureSMComponentOnActor. Booleans are TOptional<bool>: pass true or
 * false to set; omit (unset) to leave the component template's existing value alone. Enum
 * strings use empty as "leave untouched"; accepted values are "Client", "Server",
 * "ClientAndServer". ExtraConfigJson is a long-tail valve for UPROPERTY names not promoted
 * to first-class fields; keys not on USMStateMachineComponent are returned in 'unknown_keys'.
 *
 * Note: bStartOnBeginPlay defaults to true rather than unset. AI-authored slices nearly always
 * want the SM to run on placement; pass false explicitly for manual Start() control.
 */
USTRUCT(BlueprintType)
struct FSMComponentConfig
{
	GENERATED_BODY()

	/** Unset resolves to true (the only field in this struct that doesn't pass through unchanged). The flipped default exists because USMStateMachineComponent::bStartOnBeginPlay is false at the runtime level: a configured-but-not-started SM constructs and inspects fine but never ticks, so the entry state's OnStateBegin never fires. Pass false explicitly for manual Start() control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver")
	TOptional<bool> bStartOnBeginPlay = true;

	/** Unset = leave the component template's value alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver")
	TOptional<bool> bInitializeOnBeginPlay;

	/** Unset = leave the component template's value alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver")
	TOptional<bool> bStopOnEndPlay;

	/** Unset = leave the component template's value alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver")
	TOptional<bool> bReuseInstanceAfterShutdown;

	/** Toggles AActorComponent::bReplicates on the component. Unset = leave alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver|Replication")
	TOptional<bool> bReplicates;

	/** Unset = leave the component template's value alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver|Replication")
	TOptional<bool> bIncludeSimulatedProxies;

	/** Unset = leave the component template's value alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver|Replication")
	TOptional<bool> bWaitForTransactionsFromServer;

	/** Unset = leave the component template's value alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver|Replication")
	TOptional<bool> bHandleControllerChange;

	/** Display-name string for ESMNetworkConfigurationType: "Client" | "Server" | "ClientAndServer". Empty = unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver|Replication")
	FString StateChangeAuthority;

	/** Display-name string for ESMNetworkConfigurationType. Empty = unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver|Replication")
	FString NetworkTickConfiguration;

	/** Display-name string for ESMNetworkConfigurationType. Empty = unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver|Replication")
	FString NetworkStateExecution;

	/** Display-name string for ESMNetworkConfigurationType. Empty = unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver|Replication")
	FString NetworkTransitionEnteredConfiguration;

	/** JSON object of UPROPERTY name to value for fields not promoted above. Keys not found on USMStateMachineComponent are returned in 'unknown_keys'. Empty = no extras. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LogicDriver")
	FString ExtraConfigJson;
};

/**
 * Logic Driver state-machine inspection and authoring tools, exposed to AI assistants via the
 * engine's ToolsetRegistry / Model Context Protocol pipeline. Each UFUNCTION mirrors one
 * SMAssist operation; the marshal layer translates typed args into the JSON envelope that
 * USMAssistSubsystem::ExecuteOperation expects, then returns the resulting payload as a JSON
 * string.
 *
 * Convention note: every param is required at the MCP schema layer (UE 5.8's dispatcher rejects
 * omitted fields regardless of C++ defaults). Most C++ default values document a "use SMAssist
 * default" sentinel (empty string for FString, -1 for int32, -1.0 for gap/duration doubles); the
 * marshal helper skips these sentinels when building the JSON, so passing a sentinel is equivalent
 * to omitting the field. Canvas coordinates are the exception: a negative coordinate is a real
 * value, so placement carries a companion bAutoPosition flag (bDefaultOrigin for LayoutStates)
 * that defaults to auto, and the coordinate is emitted only when that flag is false.
 *
 * Result envelope: on success a tool returns the operation's payload serialized as a JSON string,
 * which the engine delivers as the reply's `returnValue` field; parse that string to get the
 * payload object. There is no success flag or message field. On failure the operation raises a
 * tool-level MCP error carrying the SMAssist error text. So read a present `returnValue` as
 * success, and an MCP tool error as failure.
 *
 * Object arguments (Blueprint, and any other UObject parameter) resolve from a full object path,
 * for example "/Game/Path/SM_Foo.SM_Foo" (the asset_path that CreateBlueprint and GetAsset
 * return), not the bare package path "/Game/Path/SM_Foo". A bare path is rejected during argument
 * conversion, before the operation runs, so the call comes back as a parameter error rather than a
 * result.
 *
 * Authoring guidance for AI clients: prefer LayoutStates(apply=true) over manual position_x/y
 * for greenfield graphs. A bare state node is roughly 70 to 160 px wide at 1:1 zoom, almost all of
 * it the display name, and grows well past that for a state whose body draws properties; the editor
 * renders an Entry-pointer marker about 200 px to the left of the entry state, so entry states
 * placed near X=0 are visually eclipsed by the marker even though GetAsset reports them present.
 * GetAsset returns logical coordinates only. To verify a layout use GetGraphView, which measures
 * the rendered widgets and reports intersecting node boxes in 'overlaps'; reach for CaptureGraphView
 * only when you need to look at the graph rather than measure it (see its docstring for cost).
 *
 * K2 self-binding depends on which kind of graph you author in, and the two cases are opposite:
 *   - Local (inline) graphs authored directly on a state or transition node inside an FSM
 *     (USMBlueprint): implicit self is the FSM's USMInstance. Member calls on USMNodeInstance
 *     fail with "(self) is not a SMNodeInstance". Use the StateMachineInstances function library
 *     (StateMachineInstances::GetContext and friends), which binds self to the SM instance.
 *   - Node-class blueprints (a standalone USMStateInstance / USMTransitionInstance subclass):
 *     implicit self is the node instance (USMNodeInstance). Member calls on USMInstance through
 *     the StateMachineInstances library fail with "(self) is not a SMInstance, Target must have a
 *     connection". Use the NodeInstance function library (NodeInstance::GetContext and friends),
 *     which binds self to the node instance; GetStateMachineInstance retrieves the owning
 *     USMInstance when you need it.
 * Either way, wiring an explicit Target pin from the matching retrieval node also works.
 *
 * LD K2 specials such as TimeInState, HasStateUpdated, and CanEvaluate are spawned by the editor
 * based on the local state or transition scope and are not reachable through the engine's
 * generic create_node action-menu surface. Toolset endpoints that wrap LD's own node spawners
 * are the intended path; engine-side BlueprintTools.create_node cannot place these.
 *
 * USMStateMachineComponent::bStartOnBeginPlay defaults to false; the component runs only after
 * StateMachineComponent->Start() is called. AI-authored slices that want the SM to run on
 * placement should set bStartOnBeginPlay=true via ConfigureSMComponentOnActor.
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
	 * @param Scope Empty or "root" (default) reports the root state machine graph only. "all" also walks every nested state machine at any depth, adding graph_path and parent_state_guid to each state and transition so nesting levels can be told apart. is_entry is always relative to a node's own graph; entry_state_guids stays the root graph's.
	 * @return JSON: { asset_path, name, parent_class?, scope, entry_state_guids:[...], states:[...], transitions:[...] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString GetAsset(USMBlueprint* Blueprint, const FString& Scope = TEXT(""));

	/**
	 * Compiles a Blueprint and reports the result. Accepts any UBlueprint subclass: state-machine
	 * Blueprints (USMBlueprint), Logic Driver node-class Blueprints (USMNodeBlueprint child of
	 * USMStateInstance / USMTransitionInstance / etc.), and regular UBlueprints (actor, widget,
	 * component subclasses). Use this in place of engine-side compile_blueprint when the
	 * structured status payload is wanted.
	 * @param Blueprint The blueprint to compile. Required.
	 * @return JSON: { asset_path, up_to_date, has_warnings, has_errors, status }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString Compile(UBlueprint* Blueprint);

	/**
	 * Adds a regular state node to a blueprint's root state machine graph, or to a nested one when ParentStateGuid is supplied.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param StateName Display name for the new state; keep it short and human-readable (e.g. "Idle", "ChasePlayer") unless the user asks for a longer name. Empty = SMAssist auto-names ("State", "State_1", ...).
	 * @param bIsEntry Whether this state becomes the graph's entry. Default false.
	 * @param bAutoPosition True (default) = SMAssist auto-positions the node and PositionX/PositionY are ignored. Set false to place at PositionX/PositionY. Prefer auto or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionX Canvas X coordinate; used only when bAutoPosition is false. Negative values are valid (the default state row sits near y=-43).
	 * @param PositionY Canvas Y coordinate; used only when bAutoPosition is false.
	 * @param StateClass Full path of a USMStateInstance_Base subclass. Empty = base USMStateInstance (no per-node instance is created at runtime). OMIT for any behavior-less state (end states especially); an empty custom class is wasted overhead. Only set this when the state has logic or exposed properties.
	 * @param ParentStateGuid Guid of a nested state machine node (kind 'state_machine_state', from GetAsset) to place the new node inside that container's graph. Empty = the blueprint's root state machine graph. Works at any nesting depth; a node with a reference already assigned is rejected, since its states live in the referenced blueprint.
	 * @return JSON: { state_guid, state_name }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddState(
		USMBlueprint* Blueprint,
		const FString& StateName = TEXT(""),
		bool bIsEntry = false,
		bool bAutoPosition = true,
		double PositionX = 0.0,
		double PositionY = 0.0,
		const FString& StateClass = TEXT(""),
		const FString& ParentStateGuid = TEXT(""));

	/**
	 * Adds a conduit node to a blueprint's root state machine graph, or to a nested one when ParentStateGuid is supplied.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param StateName Display name for the new conduit; keep it short and human-readable unless the user asks for a longer name. Empty = auto-name.
	 * @param bIsEntry Whether this conduit becomes the graph's entry. Default false.
	 * @param bAutoPosition True (default) = auto-position; PositionX/PositionY are ignored. Set false to place at PositionX/PositionY. Prefer auto or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionX Canvas X coordinate; used only when bAutoPosition is false. Negative values are valid.
	 * @param PositionY Canvas Y coordinate; used only when bAutoPosition is false.
	 * @param StateClass Full path of a USMConduitInstance subclass. Empty = base conduit.
	 * @param bEvalWithTransitions Whether the conduit evaluates inline with outgoing transitions. Default true (matches the editor's default configuration for newly placed conduits).
	 * @param ParentStateGuid Guid of a nested state machine node (kind 'state_machine_state', from GetAsset) to place the new node inside that container's graph. Empty = the blueprint's root state machine graph. Works at any nesting depth; a node with a reference already assigned is rejected, since its states live in the referenced blueprint.
	 * @return JSON: { state_guid, state_name, state_class? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddConduit(
		USMBlueprint* Blueprint,
		const FString& StateName = TEXT(""),
		bool bIsEntry = false,
		bool bAutoPosition = true,
		double PositionX = 0.0,
		double PositionY = 0.0,
		const FString& StateClass = TEXT(""),
		bool bEvalWithTransitions = true,
		const FString& ParentStateGuid = TEXT(""));

	/**
	 * Adds an AnyState node to a blueprint's root state machine graph, or to a nested one when ParentStateGuid is supplied.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param StateName Display name for the AnyState; keep it short and human-readable unless the user asks for a longer name. Empty = auto-name.
	 * @param bAutoPosition True (default) = auto-position; PositionX/PositionY are ignored. Set false to place at PositionX/PositionY. Prefer auto or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionX Canvas X coordinate; used only when bAutoPosition is false. Negative values are valid.
	 * @param PositionY Canvas Y coordinate; used only when bAutoPosition is false.
	 * @param ParentStateGuid Guid of a nested state machine node (kind 'state_machine_state', from GetAsset) to place the new node inside that container's graph. Empty = the blueprint's root state machine graph. Works at any nesting depth; a node with a reference already assigned is rejected, since its states live in the referenced blueprint.
	 * @return JSON: { state_guid, state_name }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddAnyState(
		USMBlueprint* Blueprint,
		const FString& StateName = TEXT(""),
		bool bAutoPosition = true,
		double PositionX = 0.0,
		double PositionY = 0.0,
		const FString& ParentStateGuid = TEXT(""));

	/**
	 * Adds a LinkState node pointing at an existing state by name.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param LinkToStateName Display name of the target state to link to; it must live in the same graph as the new node, which is the nested graph when ParentStateGuid is set. Required.
	 * @param bAutoPosition True (default) = auto-position; PositionX/PositionY are ignored. Set false to place at PositionX/PositionY. Prefer auto or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionX Canvas X coordinate; used only when bAutoPosition is false. Negative values are valid.
	 * @param PositionY Canvas Y coordinate; used only when bAutoPosition is false.
	 * @param ParentStateGuid Guid of a nested state machine node (kind 'state_machine_state', from GetAsset) to place the new node inside that container's graph. Empty = the blueprint's root state machine graph. Works at any nesting depth; a node with a reference already assigned is rejected, since its states live in the referenced blueprint.
	 * @return JSON: { state_guid, state_name, linked_state_guid?, link_to_state_name? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddLinkState(
		USMBlueprint* Blueprint,
		const FString& LinkToStateName,
		bool bAutoPosition = true,
		double PositionX = 0.0,
		double PositionY = 0.0,
		const FString& ParentStateGuid = TEXT(""));

	/**
	 * Adds a reference node that embeds another state-machine blueprint into this graph.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param ReferenceBlueprint The blueprint to reference (rendered as a sub-state-machine). Required.
	 * @param StateName Display name for the reference node; keep it short and human-readable unless the user asks for a longer name. Empty = auto-name.
	 * @param bIsEntry Whether this reference becomes the graph's entry. Default false.
	 * @param bAutoPosition True (default) = auto-position; PositionX/PositionY are ignored. Set false to place at PositionX/PositionY. Prefer auto or a LayoutStates pass over manual placement; coordinates near (0, 0) collide with the editor's Entry-pointer marker and produce a visually broken graph for entry states.
	 * @param PositionX Canvas X coordinate; used only when bAutoPosition is false. Negative values are valid.
	 * @param PositionY Canvas Y coordinate; used only when bAutoPosition is false.
	 * @param bUseIntermediateGraph Enable the intermediate K2 graph on the new reference state. Default false (matches the LD runtime default). Required true so SpawnLocalGraphReadNode kinds (GetStateMachineReference, InEndState) are visible/editable inside the reference state; without it, double-clicking the reference state enters the sub-SM directly. Toggle after creation via ConfigureReference.
	 * @param ParentStateGuid Guid of a nested state machine node (kind 'state_machine_state', from GetAsset) to place the new node inside that container's graph. Empty = the blueprint's root state machine graph. Works at any nesting depth; a node with a reference already assigned is rejected, since its states live in the referenced blueprint.
	 * @return JSON: { state_guid, state_name, reference_asset_path, use_intermediate_graph? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddReference(
		USMBlueprint* Blueprint,
		USMBlueprint* ReferenceBlueprint,
		const FString& StateName = TEXT(""),
		bool bIsEntry = false,
		bool bAutoPosition = true,
		double PositionX = 0.0,
		double PositionY = 0.0,
		bool bUseIntermediateGraph = false,
		const FString& ParentStateGuid = TEXT(""));

	/**
	 * Reconfigures an existing state-machine-reference state after creation. Mirrors Details-panel
	 * operations on a USMGraphNode_StateMachineStateNode: swap the referenced SMBlueprint and/or
	 * toggle intermediate-graph use. At least one of ReferenceBlueprint or bUseIntermediateGraph
	 * must be supplied; sentinel values leave the existing state alone.
	 *
	 * Use this when the reference target needs to change, or when enabling the intermediate graph
	 * after the fact so GetStateMachineReference / InEndState reads become visible in the editor.
	 *
	 * @param Blueprint The state-machine blueprint that owns the reference state. Required.
	 * @param NodeGuid GUID of the state-machine-reference state to reconfigure. Required.
	 * @param ReferenceBlueprint Optional new referenced SMBlueprint. Null = leave the current reference alone.
	 * @param bUpdateIntermediateGraph When true, apply bUseIntermediateGraph; when false, leave the existing toggle alone.
	 * @param bUseIntermediateGraph Target value when bUpdateIntermediateGraph is true. Routes through SetUseIntermediateGraph so the bound graph is created or swapped to match.
	 * @return JSON: { state_guid, reference_asset_path?, use_intermediate_graph, applied:[...] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ConfigureReference(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		USMBlueprint* ReferenceBlueprint = nullptr,
		bool bUpdateIntermediateGraph = false,
		bool bUseIntermediateGraph = false);

	/**
	 * Connects two existing states with a transition.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param FromStateGuid GUID of the source state. Required.
	 * @param ToStateGuid GUID of the destination state. Required.
	 * @param TransitionClass Full path of a USMTransitionInstance subclass. Empty = base transition (no per-node instance is created). OMIT for an always-true transition or any rule expressible in the transition's own graph; only set a custom class when the condition needs instance or C++ logic.
	 * @return JSON: { transition_guid, from_state_guid, to_state_guid }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddTransition(
		USMBlueprint* Blueprint,
		const FString& FromStateGuid,
		const FString& ToStateGuid,
		const FString& TransitionClass = TEXT(""));

	/**
	 * Adds a transition reroute node. Reroutes are cosmetic graph nodes that let a transition
	 * curve bend around obstructions; they have no runtime effect and the primary transition
	 * retains all configuration.
	 *
	 * Two modes:
	 *  - Inline-insert: supply TransitionGuid to splice the reroute into that transition's
	 *    outgoing pin chain. Useful for V-shaping a long back-edge in a cyclic FSM so its curve
	 *    bows around the state row.
	 *  - Standalone: leave TransitionGuid empty. The reroute is placed on the root state machine
	 *    target graph (ParentStateGuid, else root) at PositionX/PositionY. Connect transitions to/from
	 *    it later via AddTransition
	 *    (reroute GUIDs are valid from/to endpoints).
	 *
	 * @param Blueprint The blueprint to modify. Required.
	 * @param TransitionGuid Optional. Empty = standalone reroute. When set, ParentStateGuid is ignored: the reroute follows the transition's own graph.
	 * @param PositionX Graph X coordinate for the reroute. Defaults to 0.
	 * @param PositionY Graph Y coordinate for the reroute. Defaults to 0. To V-shape a back-edge below a row of states, set positive Y (state row sits around y=-43).
	 * @param ParentStateGuid Guid of a nested state machine node (kind 'state_machine_state', from GetAsset) to place the new node inside that container's graph. Empty = the blueprint's root state machine graph. Works at any nesting depth; a node with a reference already assigned is rejected, since its states live in the referenced blueprint.
	 * @return JSON: { reroute_guid, transition_guid? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddTransitionReroute(
		USMBlueprint* Blueprint,
		const FString& TransitionGuid = TEXT(""),
		double PositionX = 0.0,
		double PositionY = 0.0,
		const FString& ParentStateGuid = TEXT(""));

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
	 * @param NewName New display name; keep it short and human-readable (e.g. "Idle", "ChasePlayer") unless the user asks for a longer name. Required.
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
	 * Sets (or resets) the node class on an existing state, conduit, transition, or nested state
	 * machine node - the headless equivalent of the Details panel "Node Class" dropdown. Swaps the
	 * node instance template and rebuilds its property graphs, so it is safe to call after creation
	 * (unlike SetNodeProperty, which refuses class/object-reference fields).
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the node whose class to set. Required.
	 * @param NodeClass Class path of the node instance subclass to assign; must match the node kind (a USMStateInstance subclass for states, USMConduitInstance for conduits, USMTransitionInstance for transitions, USMStateMachineInstance for nested state machines). Empty resets the node to its default class.
	 * @return JSON: { node_guid, node_class }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SetNodeClass(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& NodeClass);

	/**
	 * Sets a property on a state/transition node. Multiplexes set / add / insert / duplicate /
	 * move / remove / clear based on ArrayAction.
	 *
	 * PropertyPath is relative to PropertyName and must NOT repeat it: to reach member `Close` of
	 * property `Tuning`, pass PropertyName="Tuning" and PropertyPath="Close", never "Tuning.Close".
	 *
	 * Split-pin precondition for PropertyPath: when PropertyPath is non-empty, every struct
	 * parent in the chain (the top-level pin named by PropertyName AND every intermediate struct
	 * member) must be split first via SplitPin. Calls against any unsplit struct parent are
	 * rejected with an actionable error message and no template changes are applied. Arrays do
	 * not require splitting; writing to fields inside an array element struct requires splitting
	 * that element's struct pin separately. Top-level array index uses ArrayIndex, not PropertyPath.
	 *
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the node containing the property. Required.
	 * @param PropertyName Name of the property to set. Required.
	 * @param Value JSON-encoded value text. Scalars (string, number, bool, null) and arrays of scalars are accepted. Struct properties (FLinearColor, FVector, ...) take UE struct-text wrapped as a JSON string, e.g. `"(R=1.0,G=0.0,B=0.0,A=1.0)"` for FLinearColor, not a JSON object. Empty = no value (e.g., for clear).
	 * @param ArrayIndex Index into an array-typed property. -1 = not an array op.
	 * @param TargetIndex Destination index for ArrayAction=move. -1 = not a move op.
	 * @param ArrayAction One of "add" / "insert" / "duplicate" / "move" / "remove" / "clear". Empty = scalar set.
	 * @param StackIndex Which stacked instance to target (for stacked nodes). -1 = base.
	 * @param PropertyPath Optional dot-separated sub-path under PropertyName, with optional bracket
	 *        indices for array elements (e.g. `"InnerStruct.TextMember"` or `"InnerArray[2].Field"`).
	 *        Relative to PropertyName; do not repeat the property name as the first segment.
	 *        Requires every struct parent in the chain to be split via SplitPin first - the
	 *        top-level pin AND every intermediate struct member. Calls against any unsplit struct
	 *        parent are rejected; the error message identifies which segment is unsplit. Arrays
	 *        themselves do not need to be split, but writing to fields inside an array element
	 *        struct requires splitting that element's struct pin. When set, the write walks an
	 *        IPropertyHandle chain to the leaf so PostEditChangeProperty fires with the correct
	 *        property chain, cascading through Logic Driver's HandleOnPropertyChangedEvent to
	 *        refresh child property graphs (text-graph buckets, scalar-array buckets). Extended
	 *        graph properties (FSMTextGraphProperty) are addressed by the property itself; do NOT
	 *        include the internal Result subfield.
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
		int32 StackIndex = -1,
		const FString& PropertyPath = TEXT(""));

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
	 * @param MaxDepth Recursion depth for expanding struct/array member values in the report. Negative sentinel (e.g., -1) = SMAssist default (flat; no member recursion).
	 * @return JSON: { node_guid, state_class, stack_index?, properties:[{ name, type, category?, value }], count }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString GetNodeProperties(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		int32 StackIndex = -1,
		int32 MaxDepth = -1);

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
	 * Resolves a USMPropertyGraph by node + variable + property path, returning enough handles
	 * for downstream MCP servers (Monolith, Unreal-MCP) to wire general K2 logic into it. The
	 * resolved property graph is just an event graph; once GetPropertyGraph hands back graph_path
	 * / graph_name / graph_guid, callers compose general BlueprintTools create_node + connect_pins
	 * against it. No LD-specific wiring tools required.
	 *
	 * PropertyPath uses the same syntax as SetNodeProperty: "MemberA.MemberB[i].MemberC".
	 * Indexed segments descend through nested-array buckets, switching to each bucket's child
	 * property graph for subsequent segments. Top-level array variables require a leading bare
	 * "[index]" segment.
	 *
	 * Strict-split contract: every struct parent in the path must be split first via SplitPin.
	 * The error wording matches SetNodePropertyValue's actionable hint so callers can recover.
	 *
	 * For text-graph property graphs, wired changes only persist when the graph is in graph-edit
	 * mode. Callers wiring into a text graph should call SetPropertyGraphEditMode beforehand; it
	 * is idempotent on graphs already in edit mode.
	 *
	 * bucket_index is INDEX_NONE (-1) for non-bucket leaves and a non-negative integer when the
	 * path terminates inside an array-bucket child graph; use it as the bucket-element predicate.
	 *
	 * @param Blueprint The blueprint to inspect. Required.
	 * @param NodeGuid GUID of the SM graph node owning the property. Required.
	 * @param VariableName Top-level FSMGraphProperty variable name. Required.
	 * @param PropertyPath Optional sub-path; empty resolves the top property graph.
	 * @param bIncludePinTree Optional; false by default to keep responses small. When true, the
	 *        response includes a result_pin sub-tree mirroring GetPropertyPins output.
	 * @return JSON: { asset_path, graph_path, graph_name, graph_guid, result_node_name,
	 *                 result_pin_name, bucket_index, element_type, [result_pin] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString GetPropertyGraph(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& VariableName,
		const FString& PropertyPath = TEXT(""),
		bool bIncludePinTree = false);

	/**
	 * Toggles graph-edit mode on a property graph. Required before wiring text graphs durably:
	 * USMTextPropertyGraph's compile pipeline only honors wired changes when the graph is in
	 * edit mode. Resolves the target graph the same way as GetPropertyGraph (node + variable +
	 * property path), then routes through ISMGraphGeneration::SetPropertyGraphEditMode under a
	 * scoped transaction so single-step undo reverts the toggle.
	 *
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the SM graph node owning the property. Required.
	 * @param VariableName Top-level FSMGraphProperty variable name. Required.
	 * @param bEnable Target edit-mode state.
	 * @param PropertyPath Optional sub-path; empty toggles the top property graph.
	 * @return JSON: { asset_path, graph_path, b_enable }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SetPropertyGraphEditMode(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& VariableName,
		bool bEnable,
		const FString& PropertyPath = TEXT(""));

	/**
	 * Splits a splittable-struct property pin on a state-like SM node, mirroring the editor's
	 * right-click "Split Struct Pin" on the result pin (no PinId) or any sub-pin row (PinId).
	 * Targets the property graph by VariableName; resolved via Node->GetAllPropertyGraphNodes().
	 *
	 * Required as a precondition for SetNodeProperty and ResetNodeProperty calls that use a
	 * non-empty PropertyPath: every struct parent in the path must be split via this tool before
	 * the write. Each call splits one struct pin (top-level result pin when PinId is empty, or
	 * a specific sub-pin when PinId is supplied), so deep chains require one SplitPin call per
	 * nested struct level.
	 *
	 * When PinId is empty, the top-level result pin is split (gated by CanSplitResultPin). When
	 * PinId is supplied, the sub-pin is located by PinId in the result-pin tree (match the pin_id
	 * field returned by GetPropertyPins) and gated by CanSplitSubPin. Refused when the type is not
	 * a splittable struct, when the property opts out via CanEverSplit, or when the pin is already
	 * split. Operation is transacted.
	 *
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the SM graph node owning the property. Required.
	 * @param VariableName FSMGraphProperty_Base::VariableName of the property to split. Required.
	 * @param PinId Optional. PinId (FGuid) of the sub-pin to split. Empty = top-level result pin.
	 *              PinId values match the 'pin_id' returned by GetPropertyPins.
	 * @return JSON: { applied, is_split_struct, variable_name, flag_b_split, pin_id?, result_pin:{...} }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SplitPin(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& VariableName,
		const FString& PinId = TEXT(""));

	/**
	 * Recombines a previously split struct pin on a state-like SM node, mirroring the editor's
	 * right-click "Recombine Struct Pin". Targets the property graph by VariableName.
	 *
	 * Recombining a struct parent invalidates any subsequent SetNodeProperty or ResetNodeProperty
	 * call whose PropertyPath descends through it; the recombined parent is no longer split and
	 * the call will be rejected until SplitPin is re-applied. RecombinePin is recursive: deeper
	 * splits beneath the recombined pin are flattened in the same call.
	 *
	 * When PinId is empty, the top-level result pin is recombined (refused when the property is
	 * not currently split). When PinId is supplied, the sub-pin is located by PinId and recombined
	 * (refused when the sub-pin has no SubPins). Operation is transacted.
	 *
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the SM graph node owning the property. Required.
	 * @param VariableName FSMGraphProperty_Base::VariableName of the property to recombine. Required.
	 * @param PinId Optional. PinId (FGuid) of the sub-pin to recombine. Empty = top-level result pin.
	 * @return JSON: { applied, is_split_struct, variable_name, flag_b_split, pin_id?, result_pin:{...} }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString RecombinePin(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& VariableName,
		const FString& PinId = TEXT(""));

	/**
	 * Returns the on-canvas view of a state machine graph: node positions, sizes, colors, optional pins.
	 * Sizes are measured from the rendered widgets at 1:1 zoom whatever the panel is showing, so this is
	 * how to verify a layout without spending a screenshot.
	 * @param Blueprint The blueprint to inspect. Required.
	 * @param bIncludeTransitions Include transition entries in the output. Default true.
	 * @param bIncludePins Include per-node pin metadata. Default false.
	 * @param ParentStateGuid Guid of a nested state machine node (kind 'state_machine_state') whose graph to measure instead of the root graph. Sizes and overlaps are read off the focused panel, so this opens that graph's tab in the editor. Empty = the root state machine graph. The measured graph comes back as 'graph_path'.
	 * @return JSON: { asset_path, graph_path, panel_view, overlaps:[...], transition_overlaps:[...],
	 *         measurement_warnings:[...], nodes:[...], transitions?:[...] }
	 *         'overlaps' pairs every two boxes that intersect, as first_node_guid / first_title_text,
	 *         second_node_guid / second_title_text, overlap_extent ([w, h]). It covers the flow nodes plus
	 *         the entry node. LayoutStates flows around the entry node rather than moving it, and a state
	 *         placed on top of it hides it completely. Transitions, reroutes and comments are excluded.
	 *         'transition_overlaps' is the same shape and reports transition markers and reroutes stacked on
	 *         each other, which hides a transition and which state spacing does not fix; add a reroute to
	 *         separate them, and re-read the array afterwards since reroutes are scanned too.
	 *         A rerouted transition is drawn as one segment per reroute node plus one, and every segment
	 *         repeats the same from_state_guid and to_state_guid, so count transitions by
	 *         primary_transition_guid rather than by entries. Each transition entry also carries
	 *         segment_from_guid and segment_to_guid, the two nodes that segment is drawn between (either
	 *         may be a reroute); each reroute node entry carries transition_guid, equal to the
	 *         primary_transition_guid its segments report, and chain_index, its place along that
	 *         transition's rail counting from the source state. Together they give the polyline the
	 *         editor draws. Both reroute fields are omitted when the chain could not be walked.
	 *         Read 'measurement_warnings' first: when it is non-empty, part of the graph could not be
	 *         measured and empty overlap arrays then mean unmeasured rather than clean. Empty arrays mean
	 *         nothing collides, not that the graph reads well: a transition line routed across an
	 *         intervening state is invisible to both, so test the polyline above or capture the graph.
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString GetGraphView(
		USMBlueprint* Blueprint,
		bool bIncludeTransitions = true,
		bool bIncludePins = false,
		const FString& ParentStateGuid = TEXT(""));

	/**
	 * Captures a screenshot of a state machine graph (whole graph or a single node), saved as PNG
	 * under the configured Saved/ subdirectory. The PNG is non-trivial in image tokens, so
	 * call only when the visual layout is in question: after manual position_x/y placement,
	 * after a LayoutStates pass the user wants to verify, or when the user explicitly asks how
	 * the graph looks. Don't call reflexively after every authoring step.
	 * @param Blueprint The blueprint to capture. Required.
	 * @param bClipToPanel Clip the capture to the editor's graph-panel widget. Default true.
	 * @param bFitToContent Auto-fit the view to the graph contents before capture. Default true.
	 * @param NodeGuid GUID of a single node to focus on within the captured graph. Empty = capture the whole graph.
	 * @param OutputSubdir Output folder under Saved/. Empty = "LogicDriver".
	 * @param Prefix File-name prefix for the screenshot. Empty = "<BlueprintName>_<timestamp>", or "<BlueprintName>_<GraphName>_<timestamp>" when ParentStateGuid names a nested graph.
	 * @param ParentStateGuid Guid of a nested state machine node (kind 'state_machine_state') whose graph to capture instead of the root graph. Opens that graph's tab in the editor. Empty = the root state machine graph.
	 * @return JSON: { asset_path, path, width, height, bytes, mime }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString CaptureGraphView(
		USMBlueprint* Blueprint,
		bool bClipToPanel = true,
		bool bFitToContent = true,
		const FString& NodeGuid = TEXT(""),
		const FString& OutputSubdir = TEXT(""),
		const FString& Prefix = TEXT(""),
		const FString& ParentStateGuid = TEXT(""));

	/**
	 * Captures a single SM node's local (bound) graph as a PNG, saved under the configured Saved/
	 * subdirectory. This is the visual companion to GetLocalGraph: where that returns the bound-graph
	 * logic as data, this renders it. Use it to actually see the K2 logic inside a transition's
	 * CanEnterTransition graph, a conduit's graph, or a state's OnStateBegin/Update/End graph (e.g. a
	 * TimeInState -> Greater -> bCanEnterTransition gate) -- something CaptureGraphView cannot do, because
	 * that op frames a state machine graph. Use CaptureGraphView with ParentStateGuid for a nested state
	 * machine's own graph, and this op for the K2 logic bound to a single node. The bound graph is
	 * resolved from NodeGuid exactly like GetLocalGraph (reroutes normalize to the primary transition).
	 * The PNG is non-trivial in
	 * image tokens, so call only when the bound-graph logic is in visual question, not reflexively.
	 * @param Blueprint The blueprint owning the node. Required.
	 * @param NodeGuid GUID of the state/transition/conduit/reroute node whose local graph to capture. Required.
	 * @param bClipToPanel Clip the capture to the editor's graph-panel widget. Default true.
	 * @param bFitToContent Auto-fit the view to the bound graph's contents before capture. Default true.
	 * @param OutputSubdir Output folder under Saved/. Empty = "LogicDriver".
	 * @param Prefix File-name prefix for the screenshot. Empty = "<BlueprintName>_<GraphName>_<timestamp>".
	 * @return JSON: { asset_path, path, width, height, bytes, mime }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString CaptureLocalGraph(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		bool bClipToPanel = true,
		bool bFitToContent = true,
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
	 * default after authoring a graph from scratch: greenfield Add* calls leave nodes at positions that
	 * collide with the Entry node or with each other. Calling this with bApply=true after the last node
	 * is added produces a clean left-to-right layout, and one call is enough. It measures every graph in
	 * scope and lays each out past its own Entry node. Edges that would draw over a state are carried on
	 * reroute rails. A second pass runs if a node grew after it was measured. Use manual position_x/y on
	 * the Add* ops only when reproducing an existing layout the user already approved.
	 * @param Blueprint The blueprint to lay out. Required.
	 * @param Strategy "left_to_right" or "top_to_bottom". Empty = left_to_right.
	 * @param bApply Apply the computed layout to the asset (true) or return as proposal only (false). Default false.
	 * @param Scope "root" lays out only the root graph; "all" lays out every nested graph too. Empty = root.
	 * @param ColumnGap Smallest horizontal spacing between columns. Negative sentinel (e.g., -1.0) = SMAssist default gap. A single boundary is stretched past this when the extra room is what keeps a wire off the states it passes or a marker off the state it lands on, so read it as a floor rather than the spacing every boundary ends up with.
	 * @param RowGap Vertical spacing between rows. Negative sentinel (e.g., -1.0) = SMAssist default gap.
	 * @param bDefaultOrigin True (default) anchors each graph off its own Entry node, one column gap past it and centered on it, so the first state stays clear of Entry. StartX and StartY are then ignored. Set false to lay every graph in scope out from StartX/StartY instead.
	 * @param StartX Origin X for the layout; used only when bDefaultOrigin is false. Negative values are valid.
	 * @param StartY Origin Y for the layout; used only when bDefaultOrigin is false.
	 * @param PinNodeGuidsJson JSON-encoded array of state GUID strings that should remain pinned at their existing positions. Empty = no pins.
	 * @param bRespectExistingOrder True (default) seeds each layer's order from where the author already put the nodes, so a layer someone arranged keeps that arrangement. False seeds every node the same, which hands the layer to transition priority and then to name for a fresh, deterministic re-flow. Transition priority is read and never written: it decides which transition is evaluated first at runtime and is the author's to set.
	 * @param bSnapToGrid Snap final positions to the editor grid. Default true.
	 * @param bRouteEdges Carry every back-edge, and every forward edge spanning more than one layer, on a rail of two reroute nodes clear of the flow. Its marker then stops landing on the states in between. Also lays one state's fan of three or more siblings in the next layer out on a trunk, one reroute per sibling at that sibling's own row, so the last leg into it is a square corner; a sibling flat enough to reach with a clear straight wire keeps that wire, and each carried sibling gets its own lane a reroute width to the side of the last so two wires are never drawn along one line. The rail positions come back in graphs[].reroutes on a dry run and are created when bApply is true. Reroutes are cosmetic and change nothing at runtime. They are added and repositioned, never removed. Default true. False leaves those edges drawn straight and plans no rail, and the layer boundary is stretched instead.
	 * @param bOnlyIfImproved True (default) returns the graph untouched unless the computed layout is strictly better than the arrangement the nodes are already in, compared on overlapping node pairs, then transitions drawn through a state, then transition markers drawn on a state, then reroute nodes needed, in that order. A hand-arranged graph is often already as good as this algorithm can make it. When it declines, that graph's 'declined' is true and nothing is written for it. False always applies the computed layout.
	 * @return JSON: { asset_path, strategy, scope, applied, passes, icon_location_adjustments,
	 *         edges_through_states, node_overlaps, graphs_declined,
	 *         measurement_warnings:[...], skipped:[...], graphs:[...] }
	 *         Every graph in scope is measured on its own panel, so nested graphs are spaced from what
	 *         they render at. A non-empty 'measurement_warnings' names the graph and the part of it that
	 *         could not be measured, whose nodes were spaced against sizes that read too small. Separate
	 *         from graphs[].warnings, which are layout notes such as reversed back-edges and pinned-node
	 *         overlaps. 'skipped' lists work the op could not do, such as a rail it could not create or a
	 *         second pass dropped because the editor closed. 'passes' is 2 when a node grew after it was
	 *         measured and the layout had to run again. 'icon_location_adjustments' counts the transition
	 *         markers slid apart along their own wires afterwards. Each graphs[] entry also carries
	 *         'reroutes' (the planned rail positions) and 'reroutes_added'.
	 *         'edges_through_states' counts the transitions still drawn through a state once routing has
	 *         run, measured on the path the editor draws, and is reported per graph and as a total. Each
	 *         graphs[] entry also carries 'node_overlaps', 'markers_over_states', 'rails_planned' and
	 *         'fan_rails' for the graph as the call leaves it, 'input_node_overlaps',
	 *         'input_edges_through_states', 'input_markers_over_states' and 'input_rails_planned' for the
	 *         arrangement it arrived in, 'ordering_score' (the count the within-layer ordering chose by,
	 *         taken before routing over main-lane edges only, so it does not match
	 *         'edges_through_states') and 'declined'. 'markers_over_states' counts the transition markers
	 *         drawn on top of a state box, which a wire can cause while crossing no state at all.
	 *         'fan_rails' says how many of 'rails_planned' came from laying a fan out on a trunk.
	 *         On a declined graph 'node_overlaps', 'edges_through_states' and 'markers_over_states'
	 *         repeat the input set because nothing moved, while 'rails_planned', 'fan_rails' and
	 *         'ordering_score' are 0 because no rail was planned and no order was applied. The warning
	 *         gives the numbers the computed layout would have had. Every one of these counts inflates with RowGap, so none of them is
	 *         comparable across two values of it.
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString LayoutStates(
		USMBlueprint* Blueprint,
		const FString& Strategy = TEXT(""),
		bool bApply = false,
		const FString& Scope = TEXT(""),
		double ColumnGap = -1.0,
		double RowGap = -1.0,
		bool bDefaultOrigin = true,
		double StartX = 0.0,
		double StartY = 0.0,
		const FString& PinNodeGuidsJson = TEXT(""),
		bool bRespectExistingOrder = true,
		bool bSnapToGrid = true,
		bool bRouteEdges = true,
		bool bOnlyIfImproved = true);

	/**
	 * Adds a member variable to a state-machine blueprint. Mirrors the editor's My-Blueprint
	 * Variables flow. For node-class variables (state, conduit, transition subclass), use
	 * AddNodeVariable instead.
	 *
	 * Does NOT compile the blueprint. The variable lands in NewVariables but its FProperty
	 * is not on GeneratedClass until you call Compile. Downstream ops that look up the
	 * FProperty (notably ConnectNodeVariableOutput with ToOwningBlueprintVariable) will fail until
	 * the blueprint is compiled. Batch many adds before compiling when you can.
	 *
	 * @param Blueprint The state-machine blueprint to modify. Required.
	 * @param VarName Variable name (no spaces; FName-style). Required.
	 * @param VarType Element-type token. Accepted short names: bool, int, int64, byte, float, single,
	 *                string, name, text, vector, vector2d, rotator, transform, linearcolor,
	 *                color, guid. Or a class/struct object path such as /Script/Engine.Actor
	 *                or /Game/MyBP.MyBP_C. Required.
	 * @param DefaultValue Default value as a string in UE property-text format. Empty = engine
	 *                     default for the type. Examples: "true" for bool, "1.25" for float,
	 *                     "(R=1.0,G=0.0,B=0.0,A=1.0)" for FLinearColor. Containers default to empty.
	 * @param ContainerType Container wrapping the element type. Accepted: "None" (default),
	 *                      "Array", "Map", "Set". Case-insensitive.
	 * @param KeyType Required when ContainerType=="Map"; uses the same vocabulary as VarType.
	 *                Must be empty when ContainerType is not "Map".
	 * @return JSON: { asset_path, variable_name, var_type, default_value?, container_type?, key_type? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddSMVariable(
		USMBlueprint* Blueprint,
		const FString& VarName,
		const FString& VarType,
		const FString& DefaultValue = TEXT(""),
		const FString& ContainerType = TEXT("None"),
		const FString& KeyType = TEXT(""));

	/**
	 * Adds a Blueprint variable to a node-class Blueprint (a USMNodeInstance subclass, including
	 * state, conduit, and transition classes). Mirrors the editor's My Blueprint -> +Variable
	 * flow plus, for state/conduit BPs, the directional / hidden / read-only toggles in
	 * SMVariableCustomization.
	 *
	 * Transition-class BPs accept the plain variable but reject Direction / bHidden / bReadOnly
	 * (matches the editor's Variable Details panel filter). Pass an empty Direction and false
	 * for both booleans when adding a variable to a transition class.
	 *
	 * Container variables: Array is graph-exposable (Direction / bHidden / bReadOnly accepted).
	 * Map and Set are NOT graph-exposable on any node-class BP -- the editor's Variable Details
	 * panel filter does not surface direction controls for those container kinds. Direction /
	 * bHidden / bReadOnly must remain at their defaults when ContainerType is Map or Set.
	 *
	 * Compile behavior: when any of Direction / bHidden / bReadOnly is set, the blueprint is
	 * compiled in-call so the override can be stamped on the CDO and subsequent ops see the new
	 * FProperty immediately. When all three are unset (plain variable add), the blueprint is
	 * NOT compiled -- batch multiple adds and call Compile once at the end for best performance.
	 *
	 * @param NodeClassBlueprint The node-class Blueprint to modify. Required.
	 * @param VarName Variable name (FName-style; no spaces). Required.
	 * @param VarType Element-type token. Same forms as AddSMVariable. Required.
	 * @param DefaultValue Default value in UE property-text format. Empty = engine default.
	 * @param Direction One of "Input", "Output", "Both". Empty = no graph-pin exposure.
	 *                  Transition-class BPs reject non-empty values; Map / Set containers reject
	 *                  non-empty values regardless of base class.
	 * @param bHidden Hide from on-node display. The property graph is still compiled and
	 *                evaluated; only the on-node display is suppressed. Transition-class BPs
	 *                reject true; Map / Set containers reject true regardless of base class.
	 * @param bReadOnly Display as read-only on the placed node. Transition-class BPs reject true;
	 *                  Map / Set containers reject true regardless of base class.
	 * @param ContainerType Container wrapping the element type. Accepted: "None" (default),
	 *                      "Array", "Map", "Set". Case-insensitive.
	 * @param KeyType Required when ContainerType=="Map"; uses the same vocabulary as VarType.
	 *                Must be empty when ContainerType is not "Map".
	 * @return JSON: { asset_path, variable_name, var_type, default_value?, direction?, b_hidden?, b_read_only?, container_type?, key_type? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddNodeVariable(
		UBlueprint* NodeClassBlueprint,
		const FString& VarName,
		const FString& VarType,
		const FString& DefaultValue = TEXT(""),
		const FString& Direction = TEXT(""),
		bool bHidden = false,
		bool bReadOnly = false,
		const FString& ContainerType = TEXT("None"),
		const FString& KeyType = TEXT(""));

	/**
	 * Adds a member variable to ANY blueprint (a plain UBlueprint: Actor, GameMode, GameState,
	 * object, etc.), with full container support. Mirrors the editor's My-Blueprint Variables flow
	 * via FBlueprintEditorUtils::AddMemberVariable. Use this where the engine's BlueprintTools.add_variable
	 * falls short -- notably for Array / Map / Set variables, which the engine op cannot author.
	 * For state-machine blueprints use AddSMVariable; for node-class blueprints use AddNodeVariable.
	 *
	 * Does NOT compile the blueprint. The variable lands in NewVariables but its FProperty is not on
	 * GeneratedClass until the blueprint is compiled. Batch many adds before compiling when you can.
	 *
	 * @param Blueprint The blueprint to modify. Required.
	 * @param VarName Variable name (no spaces; FName-style). Required.
	 * @param VarType Element-type token. Accepted short names: bool, int, int64, byte, float, single,
	 *                string, name, text, vector, vector2d, rotator, transform, linearcolor,
	 *                color, guid. Or a class/struct object path such as /Script/Engine.Actor
	 *                or /Game/MyBP.MyBP_C. Required.
	 * @param DefaultValue Default value as a string in UE property-text format. Empty = engine
	 *                     default for the type. Containers default to empty.
	 * @param ContainerType Container wrapping the element type. Accepted: "None" (default),
	 *                      "Array", "Map", "Set". Case-insensitive.
	 * @param KeyType Required when ContainerType=="Map"; uses the same vocabulary as VarType.
	 *               Must be empty when ContainerType is not "Map".
	 * @return JSON: { asset_path, variable_name, var_type, default_value?, container_type?, key_type? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddBlueprintVariable(
		UBlueprint* Blueprint,
		const FString& VarName,
		const FString& VarType,
		const FString& DefaultValue = TEXT(""),
		const FString& ContainerType = TEXT("None"),
		const FString& KeyType = TEXT(""));

	/**
	 * Reconfigures an existing variable on a node-class Blueprint (state / conduit subclass).
	 * Mirrors the Direction combobox and Hidden / ReadOnly toggles in SMVariableCustomization.
	 * Each field is gated by a paired bUpdate* flag so the AI can express "leave alone" vs
	 * "explicitly set" unambiguously. At least one of bUpdateDirection / bUpdateHidden /
	 * bUpdateReadOnly must be true.
	 *
	 * Transition-class Blueprints are not supported (the function only sets directional /
	 * hidden / read-only state, which transition-class variables do not have).
	 *
	 * The variable must already be displayed on the graph node, since direction / hidden / read-only
	 * are attributes of that display and mean nothing without it. A variable qualifies by being
	 * instance editable, or by being a graph-property type such as FSMTextGraphProperty. Variables
	 * added through AddNodeVariable with a Direction are exposed automatically. One added without a
	 * Direction is a plain Blueprint variable and is refused, so pass Direction at creation or re-add
	 * it. The Blueprint must also have been compiled since the variable was added.
	 *
	 * @param NodeClassBlueprint The node-class Blueprint. Required.
	 * @param VarName Existing variable name. Required.
	 * @param bUpdateDirection Gate for Direction; must be true for Direction to apply.
	 * @param Direction "Input" | "Output" | "Both" when gated.
	 * @param bUpdateHidden Gate for bHidden.
	 * @param bHidden New hidden state when gated.
	 * @param bUpdateReadOnly Gate for bReadOnly.
	 * @param bReadOnly New read-only state when gated.
	 * @return JSON: { asset_path, variable_name, applied:[...], direction?, b_hidden?, b_read_only? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ConfigureNodeVariable(
		UBlueprint* NodeClassBlueprint,
		const FString& VarName,
		bool bUpdateDirection = false,
		const FString& Direction = TEXT(""),
		bool bUpdateHidden = false,
		bool bHidden = false,
		bool bUpdateReadOnly = false,
		bool bReadOnly = false);

	/**
	 * Wires a node-class output variable to either another node's input variable (within the
	 * same FSM) or a variable on the owning FSM blueprint. Spawns the matching IO reader/writer
	 * K2 node inside the relevant property sub-graph and links pins, identical to what a user
	 * does by opening the property graph and dragging.
	 *
	 * Stack-aware: when the source or destination variable lives on a stacked instance, set
	 * the corresponding StackIndex (same convention as SetNodeProperty; -1 = primary template).
	 * Variable names alone are ambiguous across stacks; the (NodeGuid, StackIndex, VarName)
	 * tuple is unambiguous.
	 *
	 * Targets are mutually exclusive: supply EITHER (ToStateGuid, ToVarName) for node->node
	 * wiring, OR ToOwningBlueprintVariable for node->owner wiring. Idempotent: if the wire
	 * already exists, returns success without spawning duplicate IO nodes.
	 *
	 * Preconditions: source variable must be Output (or Both); destination variable must be
	 * Input (or Both) for node->node; owning-BP variable must exist for node->owner; pin types
	 * must be compatible (caught by the schema's TryCreateConnection).
	 *
	 * @param Blueprint The state-machine Blueprint containing both endpoints. Required.
	 * @param FromStateGuid Guid of the source state node. Required.
	 * @param FromStackIndex Stack index on the source state. -1 = primary template.
	 * @param FromVarName Output variable name on the source template. Required.
	 * @param ToStateGuid Guid of the destination state node. Empty when wiring to owner.
	 * @param ToStackIndex Stack index on the destination state. -1 = primary template.
	 * @param ToVarName Input variable name on the destination template. Empty when wiring to owner.
	 * @param ToOwningBlueprintVariable Variable name on the owning FSM Blueprint. Empty when wiring node->node.
	 * @return JSON: { asset_path, from_state_guid, from_variable_name, from_stack_index?, to_state_guid?, to_variable_name?, to_stack_index?, to_owning_blueprint_variable? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ConnectNodeVariableOutput(
		USMBlueprint* Blueprint,
		const FString& FromStateGuid,
		int32 FromStackIndex,
		const FString& FromVarName,
		const FString& ToStateGuid = TEXT(""),
		int32 ToStackIndex = -1,
		const FString& ToVarName = TEXT(""),
		const FString& ToOwningBlueprintVariable = TEXT(""));

	/**
	 * Breaks a previously-established node-variable-output wire. Argument shape mirrors
	 * ConnectNodeVariableOutput exactly. The matching wire is identified by the (From, To)
	 * endpoint pair; only that wire is broken (other consumers of the same source output remain
	 * wired). Returns 'applied' = true when a matching wire was found and broken, false when none
	 * was present (idempotent).
	 *
	 * @return JSON: { asset_path, from_state_guid, from_variable_name, applied }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString DisconnectNodeVariableOutput(
		USMBlueprint* Blueprint,
		const FString& FromStateGuid,
		int32 FromStackIndex,
		const FString& FromVarName,
		const FString& ToStateGuid = TEXT(""),
		int32 ToStackIndex = -1,
		const FString& ToVarName = TEXT(""),
		const FString& ToOwningBlueprintVariable = TEXT(""));

	/**
	 * Configures the USMStateMachineComponent template living on an actor blueprint's SCS. Operates
	 * on the BP-class component (not a placed-instance component) so config persists into every
	 * placed actor. Defaults bStartOnBeginPlay to 1 (on) because AI-authored slices nearly always
	 * want the SM to run on placement; pass FSMComponentConfig::bStartOnBeginPlay=false explicitly
	 * for manual Start() control.
	 * @param ActorBlueprint The actor blueprint hosting the component. Required.
	 * @param StateMachineBlueprint The FSM blueprint whose generated class becomes the
	 *        component template's StateMachineClass. Pass "None" (or empty) to leave
	 *        StateMachineClass alone, e.g. when only changing replication flags.
	 * @param ComponentName SCS variable name of the USMStateMachineComponent on the actor BP.
	 *        Required (no convention default; the AI client supplies it).
	 * @param Config Configuration bag. See FSMComponentConfig for field semantics and sentinels.
	 * @return JSON: { actor_blueprint, component_name, state_machine_class?, applied:[...], unknown_keys:[...] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ConfigureSMComponentOnActor(
		UBlueprint* ActorBlueprint,
		USMBlueprint* StateMachineBlueprint,
		const FString& ComponentName,
		const FSMComponentConfig& Config);

	/**
	 * Spawns a Logic Driver local-graph-read K2 node into a state's local graph or a transition's
	 * CanEnterTransition graph. The engine's create_node action menu does not expose these
	 * (they are spawned by LD based on the local state/transition scope); this endpoint
	 * routes through the LD core spawner so they are reachable to MCP authoring.
	 *
	 * Compatibility, by kind:
	 *  - TimeInState, HasStateUpdated, GetNodeInstance: state graphs and transition graphs.
	 *  - CanEvaluate, CanEvaluateFromEvent, GetTransitionInformation: transition graphs only.
	 *  - GetStateInformation: state graphs only.
	 *  - GetStateMachineReference: the bound graph of a state-machine-reference state
	 *    (USMGraphNode_StateMachineStateNode) — i.e. the intermediate graph inside the sub-SM
	 *    state. Pass that state's guid as NodeGuid.
	 *  - InEndState: a transition graph whose source state is a state-machine-reference state —
	 *    i.e. a transition leaving the sub-SM state. Pass that transition's guid as NodeGuid.
	 *
	 * Both intermediate-only kinds (GetStateMachineReference and InEndState) require an
	 * authored state-machine-reference state in the SM. If you haven't added one yet, call
	 * AddReference first, then pass either that state's guid (GetStateMachineReference) or the
	 * guid of a transition out of that state (InEndState). Spawning into the wrong graph kind is
	 * rejected with the standard "is not compatible with graph" envelope from LD core.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the state or transition whose bound graph receives the node.
	 *        Required.
	 * @param NodeType Local-graph-read type. Accepts PascalCase ("TimeInState") or snake_case
	 *        ("time_in_state"). Full list: TimeInState, HasStateUpdated, CanEvaluate,
	 *        CanEvaluateFromEvent, GetStateInformation, GetTransitionInformation,
	 *        GetStateMachineReference, GetNodeInstance, InEndState. Required.
	 * @param PositionX Local-graph X. Defaults to 0.
	 * @param PositionY Local-graph Y. Defaults to 0.
	 * @param NodeInstanceGuid Only used by GetNodeInstance for stack lookups. Empty = primary.
	 * @param NodeInstanceIndex Only used by GetNodeInstance for stack lookups. -1 = primary.
	 * @return JSON: { node_guid, type, target_graph_path }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SpawnLocalGraphReadNode(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& NodeType,
		double PositionX = 0.0,
		double PositionY = 0.0,
		const FString& NodeInstanceGuid = TEXT(""),
		int32 NodeInstanceIndex = -1);

	/**
	 * Spawns a Logic Driver local-graph-write K2 node into a transition's CanEnterTransition graph
	 * or a conduit's bound graph. The engine's create_node action menu does not expose these
	 * (they are spawned by LD based on the local transition/conduit scope); this endpoint routes
	 * through the LD core spawner so they are reachable to MCP authoring.
	 *
	 * Compatibility, by kind:
	 *  - CanEvaluate: transition graphs and conduit graphs. Sets bCanEvaluate on the enclosing edge.
	 *  - CanEvaluateFromEvent: transition graphs only. Sets bCanEvaluateFromEvent on the transition.
	 *
	 * Both kinds expose a single boolean input pin. Seed the pin's literal default via DefaultValue +
	 * bHasDefaultValue, or wire it to upstream K2 logic with BlueprintTools.connect_pins after spawn.
	 *
	 * TransitionEventReturn is intentionally NOT spawnable here. It is auto-placed as a side effect
	 * of binding a transition delegate. Use ConfigureTransitionEvent to bind, rebind, or clear that
	 * node.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the transition or conduit whose bound graph receives the node.
	 *        Required.
	 * @param NodeType Local-graph-write type. Accepts PascalCase ("CanEvaluate") or snake_case
	 *        ("can_evaluate"). Full list: CanEvaluate, CanEvaluateFromEvent. Required.
	 * @param PositionX Local-graph X. Defaults to 0.
	 * @param PositionY Local-graph Y. Defaults to 0.
	 * @param bHasDefaultValue When true, send bDefaultValue through to the op so the boolean input
	 *        pin's literal default is seeded. When false, the pin keeps its declared default (false).
	 * @param bDefaultValue Boolean literal to seed when bHasDefaultValue is true.
	 * @return JSON: { node_guid, type, target_graph_path }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SpawnLocalGraphWriteNode(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& NodeType,
		double PositionX = 0.0,
		double PositionY = 0.0,
		bool bHasDefaultValue = false,
		bool bDefaultValue = false);

	/**
	 * Spawns a Logic Driver lifecycle event-entry K2 node into a state's local graph or a transition's /
	 * conduit's bound graph. These are execution entry points that fire at a node lifecycle moment; wire
	 * caller logic from the node's output exec pin with BlueprintTools.connect_pins. Equivalent to the
	 * editor's right-click "Add Event On ..." menu and to what a node class auto-adds when assigned. The
	 * engine's create_node action menu does not expose these; this endpoint routes through the LD core
	 * spawner so they are reachable to MCP authoring.
	 *
	 * Compatibility, by kind:
	 *  - OnInitialized, OnShutdown: state, transition, and conduit graphs.
	 *  - OnTransitionEntered: transition and conduit graphs. Singleton (fails if already present).
	 *  - OnTransitionPreEvaluate, OnTransitionPostEvaluate: transition graphs only. Singleton.
	 *  - OnRootStateMachineStart, OnRootStateMachineStop: state, transition, and conduit graphs. Singleton.
	 *
	 * OnStateBegin, OnStateUpdate, and OnStateEnd are not spawnable: every state graph is created with all
	 * three entry nodes in it and they cannot be deleted. Read them back with GetLocalGraph and wire from
	 * the existing node.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the state, transition, or conduit whose bound graph receives the node.
	 *        Required.
	 * @param NodeType Event-entry type. Accepts PascalCase ("OnInitialized") or snake_case
	 *        ("on_initialized"). Full list: OnInitialized, OnShutdown, OnTransitionEntered,
	 *        OnTransitionPreEvaluate, OnTransitionPostEvaluate, OnRootStateMachineStart,
	 *        OnRootStateMachineStop. Required.
	 * @param PositionX Local-graph X. Defaults to 0.
	 * @param PositionY Local-graph Y. Defaults to 0.
	 * @return JSON: { node_guid, type, target_graph_path }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SpawnLocalGraphEventNode(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& NodeType,
		double PositionX = 0.0,
		double PositionY = 0.0);

	/**
	 * Reads the local (bound) graph of any Logic Driver SM graph node: a state's OnStateBegin/Update/End
	 * graph (a state shares one bound graph across those events), a transition's CanEnterTransition graph,
	 * or a conduit's graph. This is the primitive that makes bound-graph K2 authoring reachable: Logic
	 * Driver owns the node -> bound-graph resolution that generic blueprint tools cannot do, because their
	 * graph lookup does not recurse into node-owned bound graphs.
	 *
	 * Returns the graph's addressable identity (graph_name, graph_path, graph_guid) plus its nodes and
	 * pins. Each node "id" is the object name and each pin "connected_to" entry is "NodeName.PinName",
	 * matching the ids generic connect_pins expects, so callers read existing logic back and -- where the
	 * transport's graph resolver can reach a bound graph -- author into it with generic create_node /
	 * connect_pins.
	 *
	 * NodeGuid accepts a state, transition, conduit, or reroute node. Reroute waypoints and non-primary
	 * rerouted transition segments are normalized to the primary transition that owns the single compiled
	 * graph: "node_guid" in the response is the resolved node and "is_rerouted" flags the chain.
	 *
	 * For transition and conduit graphs the response also pins the wire-INTO anchor: result_node_name +
	 * result_pin_id + result_pin_name identify the evaluation pin a boolean condition connects to (the same
	 * pin SetTransitionCondition writes a literal to). State graphs expose their entry points as ordinary
	 * nodes in the list. Read-only.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the state, transition (any reroute segment), conduit, reroute waypoint, or
	 *        nested state machine whose local graph to read, at any nesting depth. Passing a nested state
	 *        machine's guid enumerates the states and transitions inside it. Reroutes resolve to the
	 *        primary transition. Required.
	 * @param bIncludePins Include each node's pin array. Defaults to true; pass false for a lighter
	 *        node-only listing.
	 * @return JSON: { asset_path, requested_node_guid, node_guid, node_class, node_kind, is_rerouted,
	 *                 graph_name, graph_path, graph_guid, [result_node_name, result_pin_id,
	 *                 result_pin_name], node_count, nodes:[ { id, node_guid, class, title, pos, [comment],
	 *                 [function], [is_result], [pins] } ] }
	 *         Each node reports both 'id' (the object name, which the local-graph write ops accept) and
	 *         'node_guid' (which every guid-addressed op takes).
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString GetLocalGraph(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		bool bIncludePins = true);

	/**
	 * Places any Blueprint K2 node into an SM node's bound (local) graph -- a state's
	 * OnStateBegin/Update/End graph, a transition's CanEnterTransition graph, or a conduit's graph. The
	 * graph is resolved from NodeGuid exactly like GetLocalGraph (reroutes normalize to the primary
	 * transition), then the node is spawned directly by Logic Driver Assist, so it does NOT depend on the
	 * transport's generic graph resolver.
	 *
	 * NodeClass accepts any UK2Node class: a class name (K2Node_IfThenElse, K2Node_MakeArray), a full
	 * class path (/Script/BlueprintGraph.K2Node_Knot), or a friendly alias (call_function, branch,
	 * get_variable, set_variable, sequence, cast, self). Reference-bearing nodes take config from the
	 * extra args: call_function needs FunctionName (+ FunctionClass when not on KismetMathLibrary /
	 * KismetSystemLibrary / the FSM class); get_variable/set_variable need VariableName (a member on the
	 * FSM blueprint -- compile first if just added); cast requires TargetClass. Any other node
	 * type spawns with its default pins. For Logic Driver's own read/write specials (TimeInState,
	 * CanEvaluate, ...) use SpawnLocalGraphReadNode / SpawnLocalGraphWriteNode. Wire with
	 * ConnectLocalGraphPins, seed literals with SetLocalGraphPinDefault, then Compile.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the state/transition/conduit/reroute node whose local graph receives the node. Required.
	 * @param NodeClass Any UK2Node class name, class path, or friendly alias. Required.
	 * @param FunctionName For call_function nodes: the UFunction name (e.g. "Greater_DoubleDouble").
	 * @param FunctionClass Optional owning class (path or name); empty searches the common libraries and FSM class.
	 * @param VariableName For get_variable/set_variable nodes: a member variable on the FSM blueprint.
	 * @param TargetClass For cast nodes (required): the class (path or name) to cast to.
	 * @param PositionX Local-graph X. Defaults to 0.
	 * @param PositionY Local-graph Y. Defaults to 0.
	 * @return JSON: { asset_path, id, node_guid, node_class, class, title, pos, pins, graph_name, graph_path }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddLocalGraphNode(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& NodeClass = TEXT(""),
		const FString& FunctionName = TEXT(""),
		const FString& FunctionClass = TEXT(""),
		const FString& VariableName = TEXT(""),
		const FString& TargetClass = TEXT(""),
		double PositionX = 0.0,
		double PositionY = 0.0);

	/**
	 * Connects two pins within an SM node's bound (local) graph. The graph is resolved from NodeGuid like
	 * GetLocalGraph. FromNodeId/ToNodeId accept the node "id" (object name) returned by GetLocalGraph or
	 * AddLocalGraphNode, or a node guid; FromPin/ToPin accept a pin name (e.g. "bCanEnterTransition",
	 * "ReturnValue", "A") or a pin id. FromPin resolves against the source node's OUTPUT pins, ToPin against
	 * the destination's INPUT pins. Routes through the K2 schema (type-promotion/conversion nodes inserted
	 * when the schema calls for it); a disallowed connection returns the schema's reason.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the node owning the local graph. Required.
	 * @param FromNodeId Source node id (object name) or node guid. Required.
	 * @param FromPin Source output pin name or pin id. Required.
	 * @param ToNodeId Destination node id (object name) or node guid; use result_node_name for the result node. Required.
	 * @param ToPin Destination input pin name or pin id; use result_pin_name (e.g. "bCanEnterTransition"). Required.
	 * @return JSON: { asset_path, graph_name, from_node_id, to_node_id, connected }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ConnectLocalGraphPins(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& FromNodeId,
		const FString& FromPin,
		const FString& ToNodeId,
		const FString& ToPin);

	/**
	 * Sets the literal default value of an input pin inside an SM node's bound (local) graph, mirroring
	 * typing a value into an unconnected pin. The graph is resolved from NodeGuid like GetLocalGraph;
	 * NodeId accepts the node "id" (object name) or a node guid; Pin accepts a pin name or pin id and
	 * resolves against the node's INPUT pins. Value is UE property-text ("2.5", "true", an enum name).
	 * Ignored by the compiler if the pin is wired.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the node owning the local graph. Required.
	 * @param NodeId Target node id (object name) or node guid. Required.
	 * @param Pin Input pin name or pin id. Required.
	 * @param Value Literal value in UE property-text form. Required.
	 * @return JSON: { asset_path, node_id, pin, value }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SetLocalGraphPinDefault(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& NodeId,
		const FString& Pin,
		const FString& Value);

	/**
	 * Removes a K2 node from an SM node's bound (local) graph. The graph is resolved from NodeGuid like
	 * GetLocalGraph; NodeId accepts the node "id" (object name) from GetLocalGraph / AddLocalGraphNode, or
	 * a node guid. Structural root nodes (the transition/conduit result node, state entry nodes) cannot be
	 * removed -- the op refuses them, matching the editor. Removing a node also breaks its pin links.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the node owning the local graph. Required.
	 * @param NodeId The node to remove: its id (object name) or a node guid. Required.
	 * @return JSON: { asset_path, graph_name, node_id, removed, node_count }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString RemoveLocalGraphNode(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& NodeId);

	/**
	 * Breaks a single connection between two pins in an SM node's bound (local) graph -- the inverse of
	 * ConnectLocalGraphPins, taking the same identifiers. The graph is resolved from NodeGuid like
	 * GetLocalGraph; node ids accept the GetLocalGraph "id" or a node guid; pins accept a name or a pin id.
	 * Only the link between the two named pins is broken. Re-connecting an already-wired input auto-breaks
	 * the old link, so this is mainly for detaching a wire without replacing it.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the node owning the local graph. Required.
	 * @param FromNodeId One endpoint node id (object name) or node guid. Required.
	 * @param FromPin Pin name or pin id on the from node. Required.
	 * @param ToNodeId Other endpoint node id (object name) or node guid. Required.
	 * @param ToPin Pin name or pin id on the to node. Required.
	 * @return JSON: { asset_path, graph_name, from_node_id, to_node_id, disconnected }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString DisconnectLocalGraphPins(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& FromNodeId,
		const FString& FromPin,
		const FString& ToNodeId,
		const FString& ToPin);

	/**
	 * Modifies an existing K2 node in an SM node's bound (local) graph in place. The graph is resolved
	 * from NodeGuid like GetLocalGraph; NodeId accepts the node "id" (object name) from GetLocalGraph /
	 * AddLocalGraphNode, or a node guid. Each change is gated by its bUpdate* flag (positions can be
	 * negative and booleans have no natural sentinel): set bUpdatePosition to reposition, bUpdateComment to
	 * set/clear the comment, bUpdateEnabled to enable/disable the node. Position and comment are cosmetic;
	 * enabled feeds compilation. Deep reconfiguration (e.g. changing a call node's function) is not done
	 * here -- use RemoveLocalGraphNode + AddLocalGraphNode.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the node owning the local graph. Required.
	 * @param NodeId The node to modify: its id (object name) or a node guid. Required.
	 * @param bUpdatePosition When true, apply PositionX/PositionY.
	 * @param PositionX New local-graph X coordinate.
	 * @param PositionY New local-graph Y coordinate.
	 * @param bUpdateComment When true, apply Comment.
	 * @param Comment Node comment text; empty string clears it.
	 * @param bUpdateEnabled When true, apply bEnabled.
	 * @param bEnabled false disables the node (excluded from compilation), true re-enables it.
	 * @return JSON: { asset_path, graph_name, id, class, title, pos, comment, enabled }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SetLocalGraphNode(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& NodeId,
		bool bUpdatePosition = false,
		double PositionX = 0.0,
		double PositionY = 0.0,
		bool bUpdateComment = false,
		const FString& Comment = TEXT(""),
		bool bUpdateEnabled = false,
		bool bEnabled = true);

	/**
	 * Binds, rebinds, or clears the auto-bound event on a transition edge, and/or updates its
	 * trigger flags. Mirrors a user edit in the transition's Details panel exactly: each set field
	 * is applied via PreEditChange/PostEditChangeProperty so cascading resets and downstream
	 * listeners (including an open Details panel auto-refresh) behave identically.
	 *
	 * Clear semantics: pass bUpdateDelegateName = true with DelegatePropertyName = "" to clear the
	 * binding. The auto-spawned event entry node is removed from the transition's bound graph; any
	 * TransitionEventReturn node the user wired downstream logic into is PRESERVED (matches the
	 * Details panel clear behavior so unbind/rebind cycles don't lose work).
	 *
	 * Sentinel disambiguation: empty string + null are natural "leave alone" defaults for
	 * DelegateOwnerInstance and DelegateOwnerClass. For DelegatePropertyName and the trigger flags,
	 * the matching bUpdate* boolean gates whether the value reaches the op. At least one field
	 * (after gating) must be supplied or the op fails.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param TransitionGuid Guid of the transition edge to reconfigure. Required.
	 * @param DelegateOwnerInstance Optional. Where the delegate lives: "This", "Context",
	 *        "PreviousState". Empty = leave alone. Changing this resets DelegateOwnerClass and
	 *        DelegatePropertyName as a side effect, so supply them together if you want to switch
	 *        both.
	 * @param DelegateOwnerClass Optional. Class owning the delegate property (required when
	 *        DelegateOwnerInstance is Context). Null = leave alone. This adapter cannot clear the
	 *        class; for that, call ld.configure_transition_event directly with
	 *        delegate_owner_class="".
	 * @param bUpdateDelegateName Gate for DelegatePropertyName. Must be true for the name field to
	 *        reach the op.
	 * @param DelegatePropertyName The multicast delegate property name to bind. Empty + gate=true
	 *        clears the binding.
	 * @param bUpdateTargetedUpdate Gate for bEventTriggersTargetedUpdate.
	 * @param bEventTriggersTargetedUpdate Trigger a targeted update of the SM limited to this
	 *        transition and destination state when the event fires. Propagates to TransitionEventReturn
	 *        nodes configured to follow their owning transition's settings.
	 * @param bUpdateFullUpdate Gate for bEventTriggersFullUpdate.
	 * @param bEventTriggersFullUpdate Trigger a full SM update when the event fires (legacy
	 *        behavior; applied after targeted update). Propagates to TransitionEventReturn nodes
	 *        configured to follow their owning transition's settings.
	 * @return JSON: { transition_guid, applied, applied_fields:[...] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ConfigureTransitionEvent(
		USMBlueprint* Blueprint,
		const FString& TransitionGuid,
		const FString& DelegateOwnerInstance = TEXT(""),
		UClass* DelegateOwnerClass = nullptr,
		bool bUpdateDelegateName = false,
		const FString& DelegatePropertyName = TEXT(""),
		bool bUpdateTargetedUpdate = false,
		bool bEventTriggersTargetedUpdate = false,
		bool bUpdateFullUpdate = false,
		bool bEventTriggersFullUpdate = false);

	/**
	 * Enumerates Logic Driver K2 read/write kinds spawnable into a target state/transition/conduit's
	 * bound graph. The Logic Driver companion to BlueprintTools.find_node_types; engine K2 nodes
	 * are NOT included here.
	 *
	 * Why this exists: the engine action menu filters LD K2 nodes out (USMGraphK2Node_*::IsActionFilteredOut
	 * rejects non-SM contexts), and LD spawns them through dedicated SMAssist ops, so even a
	 * fully-fixed upstream find_node_types would return zero LD kinds. This endpoint surfaces them
	 * in one call, with the matching spawn op and spawn-type string per entry.
	 *
	 * As of UE 5.8, BlueprintTools.find_node_types itself fails on SM transition/conduit bound
	 * graphs with "Cannot cast type ... to Blueprint" (the upstream code does a direct cast of
	 * Graph->GetOuter() to UBlueprint, which works for top-level BP graphs but not SM nested ones).
	 * The workaround is to query find_node_types against any non-SM UBlueprint's EventGraph and
	 * reuse the resulting type_id strings inside an SM nested graph via BlueprintTools.create_node;
	 * type_ids are universal across graphs.
	 *
	 * @param Blueprint The state-machine blueprint. Required.
	 * @param NodeGuid Guid of the state, transition, or conduit whose bound graph drives the
	 *        compatibility check. Required.
	 * @param TypeIdFilter Optional case-insensitive substring match against the LD kind name
	 *        (e.g. "evaluate" matches CanEvaluate and CanEvaluateFromEvent). Empty = return every
	 *        compatible kind.
	 * @return JSON: { asset_path, target_graph_path, read_kinds:[{kind, spawn_op, spawn_type}…],
	 *         write_kinds:[…], engine_nodes_hint }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString FindNodeTypes(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& TypeIdFilter = TEXT(""));

	/**
	 * Sets a conduit's condition result (always true / always false) by writing its evaluation pin's
	 * literal default. The conduit companion to SetTransitionCondition.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the conduit node. Required.
	 * @param bCondition Constant condition value. True = always pass; false = always block. Required.
	 * @return JSON: { node_guid, condition }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SetConduitCondition(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		bool bCondition);

	/**
	 * Spawns a "reach chain" of pure K2 nodes into a target graph that fetches a component off the
	 * state machine's context actor: GetContext -> Cast to the actor class -> GetComponentByClass.
	 * The three nodes are created and wired together; the returned component pin is left for the
	 * caller to wire downstream. If any wire fails the whole cluster is rolled back and the op
	 * errors, so a partial chain is never left behind.
	 * @param Blueprint The state-machine blueprint that owns the target graph. Required.
	 * @param TargetGraphPath Path or name of the graph on Blueprint to receive the nodes. Required.
	 * @param TargetActorClass Full object path of the AActor subclass to cast the context to. Required.
	 * @param ComponentClass Full object path of the UActorComponent subclass to fetch. Required.
	 * @param PositionX Graph X for the first node in the chain. Defaults to 0.
	 * @param PositionY Graph Y for the first node in the chain. Defaults to 0.
	 * @return JSON: { get_context_node_guid, cast_node_guid, get_component_node_guid, component_output_pin_id }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString SpawnActorContextComponent(
		USMBlueprint* Blueprint,
		const FString& TargetGraphPath,
		const FString& TargetActorClass,
		const FString& ComponentClass,
		double PositionX = 0.0,
		double PositionY = 0.0);

	/**
	 * Collapses a set of existing nodes into a nested state-machine container node, mirroring the
	 * editor's "Collapse to State Machine". Edges crossing the selection boundary are rewired onto
	 * the new container; fully-interior nodes and edges move inside it. The nodes may already live
	 * inside a nested state machine, so this works at any depth.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuidsJson JSON-encoded non-empty array of node GUID strings to collapse. Required. Every node must belong to the same graph.
	 * @return JSON: { state_guid, state_name, graph_path, node_guids:[...] }
	 *         'node_guids' lists what the container now holds, which is NOT the set you passed: boundary
	 *         transitions stay in the parent graph. Include every interior transition in NodeGuidsJson,
	 *         because one whose endpoints both moved is deleted rather than carried in. Collapse relocates
	 *         the same nodes rather than cloning them, so guids recorded beforehand stay valid afterwards.
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString CollapseToStateMachine(
		USMBlueprint* Blueprint,
		const FString& NodeGuidsJson);

	/**
	 * Merges source states' node-class templates into a destination state's stack, mirroring the
	 * editor's "Copy and Merge States" (bDestroyStates=false) / "Cut and Merge States"
	 * (bDestroyStates=true). New stack templates are minted with fresh guids and the owning blueprint
	 * is recompiled internally. The destination and all sources must be plain states.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param DestinationStateGuid GUID of the state receiving the merged templates. Required.
	 * @param SourceStateGuidsJson JSON-encoded non-empty array of source state GUID strings. Required.
	 * @param bDestroyStates When true, destroy the sources and rewire their transitions (cut); when false, leave the sources in place (copy). Default false.
	 * @return JSON: { destination_state_guid, merged_stack_template_guids:[...], b_destroy_states }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString MergeStates(
		USMBlueprint* Blueprint,
		const FString& DestinationStateGuid,
		const FString& SourceStateGuidsJson,
		bool bDestroyStates = false);

	/**
	 * Replaces a node with an equivalent node of a different kind, preserving existing transitions,
	 * mirroring the editor's "Replace With ..." entries. The original node is removed.
	 * @param Blueprint The blueprint to modify. Required.
	 * @param NodeGuid GUID of the node to replace. Required.
	 * @param Kind Target node kind. One of: "state", "conduit", "state_machine", "reference", "parent". Required.
	 * @return JSON: { node_guid, kind }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ReplaceNode(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& Kind);

	/**
	 * Extracts an inline nested state machine into its own state-machine reference asset and swaps
	 * the node to reference it, mirroring the editor's "Convert to Reference". Only operates on an
	 * inline nested state-machine node (USMGraphNode_StateMachineStateNode).
	 * @param Blueprint The blueprint that owns the nested state machine. Required.
	 * @param NodeGuid GUID of the inline nested state-machine node to convert. Required.
	 * @param Name Asset name for the newly minted reference. Empty = SMAssist default naming.
	 * @param Path Package path under /Game for the new asset. Empty = SMAssist default location.
	 * @param ParentClass Full object path of a USMInstance subclass to parent the new asset to. Empty = default.
	 * @return JSON: { node_guid, reference_asset_path, name }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ConvertToReference(
		USMBlueprint* Blueprint,
		const FString& NodeGuid,
		const FString& Name = TEXT(""),
		const FString& Path = TEXT(""),
		const FString& ParentClass = TEXT(""));

	/**
	 * Inspects the live runtime state of a state machine running on an actor in the active PIE
	 * session. Read-only: a PIE session must be active and the component's instance must be
	 * initialized.
	 * @param ActorIdentifier Name or label of the actor in the PIE world to inspect. Required.
	 * @param ComponentName Name of the USMStateMachineComponent to read. Empty = first USMStateMachineComponent found on the actor.
	 * @param bIncludeProperties Include each active state's exposed (editable) property values in the report. Default false.
	 * @param MaxDepth Recursion depth for struct/array member expansion when bIncludeProperties is true. Negative sentinel (e.g., -1) = SMAssist default (flat).
	 * @param PieInstance Which PIE instance to read. Negative sentinel (e.g., -1) = SMAssist default (0, the first instance).
	 * @return JSON: { actor, component, is_active, is_in_end_state, single_active_state?, active_states:[...], count }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString RuntimeGetState(
		const FString& ActorIdentifier,
		const FString& ComponentName = TEXT(""),
		bool bIncludeProperties = false,
		int32 MaxDepth = -1,
		int32 PieInstance = -1);

	// The following mirror the ld_ue.* generic-fallback ops: alternatives to the engine's own
	// blueprint.* / property MCP tools, for use only when those cannot express the operation (for
	// example TMap / TArray element access). They are not Logic Driver operations.

	/**
	 * Reads a property value off any resolvable object by property path. Fallback for the engine's
	 * own property-read tools.
	 * @param Object Object identifier: a full object path for an edit-time asset, or an actor name/label for a runtime (PIE) object. Required.
	 * @param PropertyPath Dot-separated property path; container element access via "Member[index]" or "Member[key]". Required.
	 * @param Target "edit" (or empty) resolves the edit-time object; "runtime" resolves in the active PIE world. Empty = edit.
	 * @param PieInstance Which PIE instance to resolve against when Target is runtime. Negative sentinel (e.g., -1) = default (0, the first instance).
	 * @return JSON: { object_resolved, target, property_path, property_type, value }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString ReadProperty(
		const FString& Object,
		const FString& PropertyPath,
		const FString& Target = TEXT(""),
		int32 PieInstance = -1);

	/**
	 * Writes a property value onto any resolvable object by property path. Fallback for the engine's
	 * own property-write tools.
	 * @param Object Object identifier: a full object path for an edit-time asset, or an actor name/label for a runtime (PIE) object. Required.
	 * @param PropertyPath Dot-separated property path; container element access via "Member[index]" or "Member[key]". Required.
	 * @param Value New value in UE property-text format. Required; may be an empty string to clear a string-like property.
	 * @param Target "edit" (or empty) resolves the edit-time object; "runtime" resolves in the active PIE world. Empty = edit.
	 * @param PieInstance Which PIE instance to resolve against when Target is runtime. Negative sentinel (e.g., -1) = default (0, the first instance).
	 * @return JSON: { object_resolved, target, property_path, property_type, value }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString WriteProperty(
		const FString& Object,
		const FString& PropertyPath,
		const FString& Value,
		const FString& Target = TEXT(""),
		int32 PieInstance = -1);

	/**
	 * Adds a multicast event dispatcher (delegate member variable + signature graph) to any
	 * blueprint. Fallback for the engine's own dispatcher tools; unlike some of them, this mints the
	 * member variable that survives compile.
	 * @param Blueprint The blueprint to modify (any UBlueprint). Required.
	 * @param Name Dispatcher name (FName-style; must be unique on the blueprint). Required.
	 * @param ParamsJson JSON-encoded array of { name, type } parameter descriptors for the delegate signature. Empty = no parameters.
	 * @return JSON: { asset_path, dispatcher_name, params_applied:[{ name, type }, ...] }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddDispatcher(
		UBlueprint* Blueprint,
		const FString& Name,
		const FString& ParamsJson = TEXT(""));
};
