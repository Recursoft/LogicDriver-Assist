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
 *
 * K2 self-binding inside nested state and transition graphs: when authoring K2 nodes in a
 * state's OnStateBegin / OnStateUpdate / OnStateEnd graph or a transition's CanEnterTransition
 * graph, the implicit K2 self resolves to the FSM's USMInstance, not a USMNodeInstance. Calls
 * into UFUNCTIONs on USMNodeInstance fail to compile with "(self) is not a SMNodeInstance". Use
 * the USMStateMachineInstances function library (StateMachineInstances::GetContext and friends),
 * which binds self to the SM instance automatically, or wire an explicit Target pin from a
 * node-instance retrieval node.
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
	 * @param bUseIntermediateGraph Enable the intermediate K2 graph on the new reference state. Default false (matches the LD runtime default). Required true so SpawnLocalGraphReadNode kinds (GetStateMachineReference, InEndState) are visible/editable inside the reference state; without it, double-clicking the reference state enters the sub-SM directly. Toggle after creation via ConfigureReference.
	 * @return JSON: { state_guid, state_name, reference_asset_path, use_intermediate_graph? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddReference(
		USMBlueprint* Blueprint,
		USMBlueprint* ReferenceBlueprint,
		const FString& StateName = TEXT(""),
		bool bIsEntry = false,
		double PositionX = -1.0,
		double PositionY = -1.0,
		bool bUseIntermediateGraph = false);

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
	 * Adds a transition reroute node. Reroutes are cosmetic graph nodes that let a transition
	 * curve bend around obstructions; they have no runtime effect and the primary transition
	 * retains all configuration.
	 *
	 * Two modes:
	 *  - Inline-insert: supply TransitionGuid to splice the reroute into that transition's
	 *    outgoing pin chain. Useful for V-shaping a long back-edge in a cyclic FSM so its curve
	 *    bows around the state row.
	 *  - Standalone: leave TransitionGuid empty. The reroute is placed on the root state machine
	 *    graph at PositionX/PositionY. Connect transitions to/from it later via AddTransition
	 *    (reroute GUIDs are valid from/to endpoints).
	 *
	 * @param Blueprint The blueprint to modify. Required.
	 * @param TransitionGuid Optional. Empty = standalone reroute.
	 * @param PositionX Graph X coordinate for the reroute. Defaults to 0.
	 * @param PositionY Graph Y coordinate for the reroute. Defaults to 0. To V-shape a back-edge below a row of states, set positive Y (state row sits around y=-43).
	 * @return JSON: { reroute_guid, transition_guid? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddTransitionReroute(
		USMBlueprint* Blueprint,
		const FString& TransitionGuid = TEXT(""),
		double PositionX = 0.0,
		double PositionY = 0.0);

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
	 * @param VarType Type token. Accepted short names: bool, int, int64, byte, float, single,
	 *                string, name, text, vector, vector2d, rotator, transform, linearcolor,
	 *                color, guid. Or a class/struct object path such as /Script/Engine.Actor
	 *                or /Game/MyBP.MyBP_C. Required.
	 * @param DefaultValue Default value as a string in UE property-text format. Empty = engine
	 *                     default for the type. Examples: "true" for bool, "1.25" for float,
	 *                     "(R=1.0,G=0.0,B=0.0,A=1.0)" for FLinearColor.
	 * @return JSON: { asset_path, variable_name, var_type, default_value? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddSMVariable(
		USMBlueprint* Blueprint,
		const FString& VarName,
		const FString& VarType,
		const FString& DefaultValue = TEXT(""));

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
	 * Compile behavior: when any of Direction / bHidden / bReadOnly is set, the blueprint is
	 * compiled in-call so the override can be stamped on the CDO and subsequent ops see the new
	 * FProperty immediately. When all three are unset (plain variable add), the blueprint is
	 * NOT compiled -- batch multiple adds and call Compile once at the end for best performance.
	 *
	 * @param NodeClassBlueprint The node-class Blueprint to modify. Required.
	 * @param VarName Variable name (FName-style; no spaces). Required.
	 * @param VarType Type token. Same forms as AddSMVariable. Required.
	 * @param DefaultValue Default value in UE property-text format. Empty = engine default.
	 * @param Direction One of "Input", "Output", "Both". Empty = no graph-pin exposure.
	 *                  Transition-class BPs reject non-empty values.
	 * @param bHidden Hide from on-node display. The property graph is still compiled and
	 *                evaluated; only the on-node display is suppressed. Transition-class BPs
	 *                reject true.
	 * @param bReadOnly Display as read-only on the placed node. Transition-class BPs reject true.
	 * @return JSON: { asset_path, variable_name, var_type, default_value?, direction?, b_hidden?, b_read_only? }
	 */
	UFUNCTION(meta = (AICallable), Category = "LogicDriver")
	static FString AddNodeVariable(
		UBlueprint* NodeClassBlueprint,
		const FString& VarName,
		const FString& VarType,
		const FString& DefaultValue = TEXT(""),
		const FString& Direction = TEXT(""),
		bool bHidden = false,
		bool bReadOnly = false);

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
	 *        component template's StateMachineClass. Null/unset = leave StateMachineClass alone.
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
	 *        class; for that, call sm.configure_transition_event directly with
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
};
