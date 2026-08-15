// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace LD::Assist::Ops
{
	inline constexpr const TCHAR* CreateBlueprint = TEXT("ld.create_blueprint");
	inline constexpr const TCHAR* AddState = TEXT("ld.add_state");
	inline constexpr const TCHAR* AddTransition = TEXT("ld.add_transition");
	inline constexpr const TCHAR* AddTransitionReroute = TEXT("ld.add_transition_reroute");
	inline constexpr const TCHAR* ListAssets = TEXT("ld.list_assets");
	inline constexpr const TCHAR* GetAsset = TEXT("ld.get_asset");
	inline constexpr const TCHAR* RemoveNode = TEXT("ld.remove_node");
	inline constexpr const TCHAR* SetNodeProperty = TEXT("ld.set_node_property");
	inline constexpr const TCHAR* SetNodeClass = TEXT("ld.set_node_class");
	inline constexpr const TCHAR* Compile = TEXT("ld.compile");
	inline constexpr const TCHAR* RenameState = TEXT("ld.rename_state");
	inline constexpr const TCHAR* SetInitialState = TEXT("ld.set_initial_state");
	inline constexpr const TCHAR* AddStateStack = TEXT("ld.add_state_stack");
	inline constexpr const TCHAR* AddTransitionStack = TEXT("ld.add_transition_stack");
	inline constexpr const TCHAR* AddConduit = TEXT("ld.add_conduit");
	inline constexpr const TCHAR* AddReference = TEXT("ld.add_reference");
	inline constexpr const TCHAR* AddAnyState = TEXT("ld.add_any_state");
	inline constexpr const TCHAR* AddLinkState = TEXT("ld.add_link_state");
	inline constexpr const TCHAR* GetNodeProperties = TEXT("ld.get_node_properties");
	inline constexpr const TCHAR* SetTransitionCondition = TEXT("ld.set_transition_condition");
	inline constexpr const TCHAR* GetGraphView = TEXT("ld.get_graph_view");
	inline constexpr const TCHAR* CaptureGraphView = TEXT("ld.capture_graph_view");
	inline constexpr const TCHAR* CaptureLocalGraph = TEXT("ld.capture_local_graph");
	inline constexpr const TCHAR* ClearScreenshots = TEXT("ld.clear_screenshots");
	inline constexpr const TCHAR* LayoutStates = TEXT("ld.layout_states");
	inline constexpr const TCHAR* GetPropertyPins = TEXT("ld.get_property_pins");
	inline constexpr const TCHAR* GetPropertyGraph = TEXT("ld.get_property_graph");
	inline constexpr const TCHAR* SetPropertyGraphEditMode = TEXT("ld.set_property_graph_edit_mode");
	inline constexpr const TCHAR* ResetNodeProperty = TEXT("ld.reset_node_property");
	inline constexpr const TCHAR* AddSMVariable = TEXT("ld.add_sm_variable");
	inline constexpr const TCHAR* ConfigureSMComponentOnActor = TEXT("ld.configure_sm_component_on_actor");
	inline constexpr const TCHAR* SpawnLocalGraphReadNode = TEXT("ld.spawn_local_graph_read_node");
	inline constexpr const TCHAR* ConfigureReference = TEXT("ld.configure_reference");
	inline constexpr const TCHAR* SpawnLocalGraphWriteNode = TEXT("ld.spawn_local_graph_write_node");
	inline constexpr const TCHAR* SpawnLocalGraphEventNode = TEXT("ld.spawn_local_graph_event_node");
	inline constexpr const TCHAR* ConfigureTransitionEvent = TEXT("ld.configure_transition_event");
	inline constexpr const TCHAR* FindNodeTypes = TEXT("ld.find_node_types");
	inline constexpr const TCHAR* AddNodeVariable = TEXT("ld.add_node_variable");
	inline constexpr const TCHAR* AddBlueprintVariable = TEXT("ld.add_blueprint_variable");
	inline constexpr const TCHAR* ConfigureNodeVariable = TEXT("ld.configure_node_variable");
	inline constexpr const TCHAR* ConnectNodeVariableOutput = TEXT("ld.connect_node_variable_output");
	inline constexpr const TCHAR* DisconnectNodeVariableOutput = TEXT("ld.disconnect_node_variable_output");
	inline constexpr const TCHAR* SplitPin = TEXT("ld.split_pin");
	inline constexpr const TCHAR* RecombinePin = TEXT("ld.recombine_pin");
	inline constexpr const TCHAR* RuntimeGetState = TEXT("ld.runtime_get_state");
	inline constexpr const TCHAR* SetConduitCondition = TEXT("ld.set_conduit_condition");
	inline constexpr const TCHAR* SpawnActorContextComponent = TEXT("ld.spawn_actor_context_component");
	inline constexpr const TCHAR* CollapseToStateMachine = TEXT("ld.collapse_to_state_machine");
	inline constexpr const TCHAR* MergeStates = TEXT("ld.merge_states");
	inline constexpr const TCHAR* ReplaceNode = TEXT("ld.replace_node");
	inline constexpr const TCHAR* ConvertToReference = TEXT("ld.convert_to_reference");
	inline constexpr const TCHAR* GetLocalGraph = TEXT("ld.get_local_graph");
	inline constexpr const TCHAR* AddLocalGraphNode = TEXT("ld.add_local_graph_node");
	inline constexpr const TCHAR* ConnectLocalGraphPins = TEXT("ld.connect_local_graph_pins");
	inline constexpr const TCHAR* SetLocalGraphPinDefault = TEXT("ld.set_local_graph_pin_default");
	inline constexpr const TCHAR* RemoveLocalGraphNode = TEXT("ld.remove_local_graph_node");
	inline constexpr const TCHAR* DisconnectLocalGraphPins = TEXT("ld.disconnect_local_graph_pins");
	inline constexpr const TCHAR* SetLocalGraphNode = TEXT("ld.set_local_graph_node");
}

namespace LD::Assist::Args
{
	inline constexpr const TCHAR* AssetPath = TEXT("asset_path");
	inline constexpr const TCHAR* Name = TEXT("name");
	inline constexpr const TCHAR* Path = TEXT("path");
	inline constexpr const TCHAR* PathPrefix = TEXT("path_prefix");
	inline constexpr const TCHAR* ParentClass = TEXT("parent_class");

	inline constexpr const TCHAR* StateName = TEXT("state_name");
	inline constexpr const TCHAR* StateGuid = TEXT("state_guid");
	inline constexpr const TCHAR* StateClass = TEXT("state_class");
	inline constexpr const TCHAR* IsEntry = TEXT("is_entry");
	inline constexpr const TCHAR* PositionX = TEXT("position_x");
	inline constexpr const TCHAR* PositionY = TEXT("position_y");
	inline constexpr const TCHAR* NewName = TEXT("new_name");

	inline constexpr const TCHAR* FromStateGuid = TEXT("from_state_guid");
	inline constexpr const TCHAR* ToStateGuid = TEXT("to_state_guid");
	inline constexpr const TCHAR* TransitionGuid = TEXT("transition_guid");
	inline constexpr const TCHAR* TransitionClass = TEXT("transition_class");
	inline constexpr const TCHAR* Gate = TEXT("gate");
	inline constexpr const TCHAR* Evaluation = TEXT("evaluation");
	inline constexpr const TCHAR* Event = TEXT("event");
	inline constexpr const TCHAR* RerouteGuid = TEXT("reroute_guid");

	inline constexpr const TCHAR* NodeGuid = TEXT("node_guid");
	inline constexpr const TCHAR* PropertyName = TEXT("property_name");
	inline constexpr const TCHAR* Value = TEXT("value");
	inline constexpr const TCHAR* ArrayIndex = TEXT("array_index");
	inline constexpr const TCHAR* TargetIndex = TEXT("target_index");
	inline constexpr const TCHAR* ArrayAction = TEXT("array_action");
	inline constexpr const TCHAR* StackIndex = TEXT("stack_index");
	inline constexpr const TCHAR* TemplateGuid = TEXT("template_guid");
	inline constexpr const TCHAR* ElementCount = TEXT("element_count");

	inline constexpr const TCHAR* States = TEXT("states");
	inline constexpr const TCHAR* Transitions = TEXT("transitions");
	inline constexpr const TCHAR* Assets = TEXT("assets");
	inline constexpr const TCHAR* Count = TEXT("count");
	inline constexpr const TCHAR* EntryStateGuids = TEXT("entry_state_guids");

	inline constexpr const TCHAR* UpToDate = TEXT("up_to_date");
	inline constexpr const TCHAR* HasWarnings = TEXT("has_warnings");
	inline constexpr const TCHAR* HasErrors = TEXT("has_errors");
	inline constexpr const TCHAR* Status = TEXT("status");

	inline constexpr const TCHAR* ReferenceAssetPath = TEXT("reference_asset_path");
	inline constexpr const TCHAR* UseIntermediateGraph = TEXT("use_intermediate_graph");
	inline constexpr const TCHAR* EvalWithTransitions = TEXT("eval_with_transitions");
	inline constexpr const TCHAR* Properties = TEXT("properties");
	inline constexpr const TCHAR* PropertyPath = TEXT("property_path");
	inline constexpr const TCHAR* Type = TEXT("type");
	inline constexpr const TCHAR* Category = TEXT("category");
	inline constexpr const TCHAR* MaxDepth = TEXT("max_depth");
	inline constexpr const TCHAR* Members = TEXT("members");
	inline constexpr const TCHAR* Elements = TEXT("elements");
	inline constexpr const TCHAR* Condition = TEXT("condition");

	inline constexpr const TCHAR* Kind = TEXT("kind");
	inline constexpr const TCHAR* LinkToStateName = TEXT("link_to_state_name");
	inline constexpr const TCHAR* LinkedStateGuid = TEXT("linked_state_guid");

	inline constexpr const TCHAR* IncludeTransitions = TEXT("include_transitions");
	inline constexpr const TCHAR* IncludePins = TEXT("include_pins");
	inline constexpr const TCHAR* Nodes = TEXT("nodes");
	inline constexpr const TCHAR* PanelView = TEXT("panel_view");
	inline constexpr const TCHAR* Zoom = TEXT("zoom");
	inline constexpr const TCHAR* ViewOffset = TEXT("view_offset");
	inline constexpr const TCHAR* LogicalPosition = TEXT("logical_position");
	inline constexpr const TCHAR* WidgetPosition = TEXT("widget_position");
	inline constexpr const TCHAR* WidgetSize = TEXT("widget_size");
	inline constexpr const TCHAR* TitleText = TEXT("title_text");
	inline constexpr const TCHAR* BodyColor = TEXT("body_color");
	inline constexpr const TCHAR* TitleColor = TEXT("title_color");
	inline constexpr const TCHAR* Comment = TEXT("comment");
	inline constexpr const TCHAR* IsSelected = TEXT("is_selected");
	inline constexpr const TCHAR* Overlaps = TEXT("overlaps");
	inline constexpr const TCHAR* TransitionOverlaps = TEXT("transition_overlaps");
	inline constexpr const TCHAR* MeasurementWarnings = TEXT("measurement_warnings");
	inline constexpr const TCHAR* FirstNodeGuid = TEXT("first_node_guid");
	inline constexpr const TCHAR* FirstTitleText = TEXT("first_title_text");
	inline constexpr const TCHAR* SecondNodeGuid = TEXT("second_node_guid");
	inline constexpr const TCHAR* SecondTitleText = TEXT("second_title_text");
	inline constexpr const TCHAR* OverlapExtent = TEXT("overlap_extent");
	inline constexpr const TCHAR* Pins = TEXT("pins");
	inline constexpr const TCHAR* PinId = TEXT("pin_id");
	inline constexpr const TCHAR* PinName = TEXT("pin_name");
	inline constexpr const TCHAR* PinDirection = TEXT("pin_direction");
	inline constexpr const TCHAR* DisplayedText = TEXT("displayed_text");

	inline constexpr const TCHAR* ClipToPanel = TEXT("clip_to_panel");
	inline constexpr const TCHAR* FitToContent = TEXT("fit_to_content");
	inline constexpr const TCHAR* OutputSubdir = TEXT("output_subdir");
	inline constexpr const TCHAR* Prefix = TEXT("prefix");
	inline constexpr const TCHAR* Width = TEXT("width");
	inline constexpr const TCHAR* Height = TEXT("height");
	inline constexpr const TCHAR* Bytes = TEXT("bytes");
	inline constexpr const TCHAR* Mime = TEXT("mime");

	inline constexpr const TCHAR* OlderThanSeconds = TEXT("older_than_seconds");
	inline constexpr const TCHAR* DryRun = TEXT("dry_run");
	inline constexpr const TCHAR* DeletedCount = TEXT("deleted_count");
	inline constexpr const TCHAR* FreedBytes = TEXT("freed_bytes");
	inline constexpr const TCHAR* Paths = TEXT("paths");
	inline constexpr const TCHAR* Directory = TEXT("directory");

	inline constexpr const TCHAR* Strategy = TEXT("strategy");
	inline constexpr const TCHAR* Apply = TEXT("apply");
	inline constexpr const TCHAR* Scope = TEXT("scope");
	inline constexpr const TCHAR* ColumnGap = TEXT("column_gap");
	inline constexpr const TCHAR* RowGap = TEXT("row_gap");
	inline constexpr const TCHAR* StartX = TEXT("start_x");
	inline constexpr const TCHAR* StartY = TEXT("start_y");
	inline constexpr const TCHAR* PinNodeGuids = TEXT("pin_node_guids");
	inline constexpr const TCHAR* RespectExistingOrder = TEXT("respect_existing_order");
	inline constexpr const TCHAR* SnapToGrid = TEXT("snap_to_grid");
	inline constexpr const TCHAR* Graphs = TEXT("graphs");
	inline constexpr const TCHAR* GraphPath = TEXT("graph_path");
	inline constexpr const TCHAR* NodeLayout = TEXT("node_layout");
	inline constexpr const TCHAR* ProposedPosition = TEXT("proposed_position");
	inline constexpr const TCHAR* Layer = TEXT("layer");
	inline constexpr const TCHAR* Lane = TEXT("lane");
	inline constexpr const TCHAR* Delta = TEXT("delta");
	inline constexpr const TCHAR* Warnings = TEXT("warnings");
	inline constexpr const TCHAR* Applied = TEXT("applied");

	inline constexpr const TCHAR* VariableName = TEXT("variable_name");
	inline constexpr const TCHAR* VarType = TEXT("var_type");
	inline constexpr const TCHAR* ContainerType = TEXT("container_type");
	inline constexpr const TCHAR* KeyType = TEXT("key_type");
	inline constexpr const TCHAR* DefaultValue = TEXT("default_value");

	inline constexpr const TCHAR* ActorBlueprint = TEXT("actor_blueprint");
	inline constexpr const TCHAR* StateMachineClass = TEXT("state_machine_class");
	inline constexpr const TCHAR* ComponentName = TEXT("component_name");
	inline constexpr const TCHAR* StartOnBeginPlay = TEXT("b_start_on_begin_play");
	inline constexpr const TCHAR* InitializeOnBeginPlay = TEXT("b_initialize_on_begin_play");
	inline constexpr const TCHAR* StopOnEndPlay = TEXT("b_stop_on_end_play");
	inline constexpr const TCHAR* ReuseInstanceAfterShutdown = TEXT("b_reuse_instance_after_shutdown");
	inline constexpr const TCHAR* Replicates = TEXT("b_replicates");
	inline constexpr const TCHAR* IncludeSimulatedProxies = TEXT("b_include_simulated_proxies");
	inline constexpr const TCHAR* WaitForTransactionsFromServer = TEXT("b_wait_for_transactions_from_server");
	inline constexpr const TCHAR* HandleControllerChange = TEXT("b_handle_controller_change");
	inline constexpr const TCHAR* StateChangeAuthority = TEXT("state_change_authority");
	inline constexpr const TCHAR* NetworkTickConfiguration = TEXT("network_tick_configuration");
	inline constexpr const TCHAR* NetworkStateExecution = TEXT("network_state_execution");
	inline constexpr const TCHAR* NetworkTransitionEnteredConfiguration = TEXT("network_transition_entered_configuration");
	inline constexpr const TCHAR* ExtraConfigJson = TEXT("extra_config_json");
	inline constexpr const TCHAR* UnknownKeys = TEXT("unknown_keys");

	inline constexpr const TCHAR* NodeInstanceGuid = TEXT("node_instance_guid");
	inline constexpr const TCHAR* NodeInstanceIndex = TEXT("node_instance_index");
	inline constexpr const TCHAR* TargetGraphPath = TEXT("target_graph_path");

	inline constexpr const TCHAR* DelegateOwnerInstance = TEXT("delegate_owner_instance");
	inline constexpr const TCHAR* DelegateOwnerClass = TEXT("delegate_owner_class");
	inline constexpr const TCHAR* DelegatePropertyName = TEXT("delegate_property_name");
	inline constexpr const TCHAR* EventTriggersTargetedUpdate = TEXT("event_triggers_targeted_update");
	inline constexpr const TCHAR* EventTriggersFullUpdate = TEXT("event_triggers_full_update");

	inline constexpr const TCHAR* TypeIdFilter = TEXT("type_id_filter");
	inline constexpr const TCHAR* AppliedFields = TEXT("applied_fields");
	inline constexpr const TCHAR* ReadKinds = TEXT("read_kinds");
	inline constexpr const TCHAR* WriteKinds = TEXT("write_kinds");
	inline constexpr const TCHAR* EventKinds = TEXT("event_kinds");
	inline constexpr const TCHAR* EngineNodesHint = TEXT("engine_nodes_hint");
	inline constexpr const TCHAR* SpawnOp = TEXT("spawn_op");
	inline constexpr const TCHAR* SpawnType = TEXT("spawn_type");

	inline constexpr const TCHAR* Direction = TEXT("direction");
	inline constexpr const TCHAR* Hidden = TEXT("b_hidden");
	inline constexpr const TCHAR* ReadOnly = TEXT("b_read_only");
	inline constexpr const TCHAR* UpdateDirection = TEXT("b_update_direction");
	inline constexpr const TCHAR* UpdateHidden = TEXT("b_update_hidden");
	inline constexpr const TCHAR* UpdateReadOnly = TEXT("b_update_read_only");

	inline constexpr const TCHAR* GraphName = TEXT("graph_name");
	inline constexpr const TCHAR* GraphGuid = TEXT("graph_guid");
	inline constexpr const TCHAR* ResultNodeName = TEXT("result_node_name");
	inline constexpr const TCHAR* ResultPinName = TEXT("result_pin_name");
	inline constexpr const TCHAR* BucketIndex = TEXT("bucket_index");
	inline constexpr const TCHAR* ElementType = TEXT("element_type");
	inline constexpr const TCHAR* ResultPin = TEXT("result_pin");
	inline constexpr const TCHAR* ResultPinId = TEXT("result_pin_id");
	inline constexpr const TCHAR* IncludePinTree = TEXT("include_pin_tree");
	inline constexpr const TCHAR* Enable = TEXT("b_enable");

	inline constexpr const TCHAR* RequestedNodeGuid = TEXT("requested_node_guid");
	inline constexpr const TCHAR* NodeClass = TEXT("node_class");
	inline constexpr const TCHAR* NodeKind = TEXT("node_kind");
	inline constexpr const TCHAR* IsRerouted = TEXT("is_rerouted");
	inline constexpr const TCHAR* NodeCount = TEXT("node_count");
	inline constexpr const TCHAR* NodeId = TEXT("node_id");
	inline constexpr const TCHAR* FunctionName = TEXT("function_name");
	inline constexpr const TCHAR* FunctionClass = TEXT("function_class");
	inline constexpr const TCHAR* FromNodeId = TEXT("from_node_id");
	inline constexpr const TCHAR* FromPin = TEXT("from_pin");
	inline constexpr const TCHAR* ToNodeId = TEXT("to_node_id");
	inline constexpr const TCHAR* ToPin = TEXT("to_pin");
	inline constexpr const TCHAR* Pin = TEXT("pin");
	inline constexpr const TCHAR* Connected = TEXT("connected");
	inline constexpr const TCHAR* TargetClass = TEXT("target_class");
	inline constexpr const TCHAR* Enabled = TEXT("enabled");

	inline constexpr const TCHAR* FromStackIndex = TEXT("from_stack_index");
	inline constexpr const TCHAR* FromVariableName = TEXT("from_variable_name");
	inline constexpr const TCHAR* ToStackIndex = TEXT("to_stack_index");
	inline constexpr const TCHAR* ToVariableName = TEXT("to_variable_name");
	inline constexpr const TCHAR* ToOwningBlueprintVariable = TEXT("to_owning_blueprint_variable");

	inline constexpr const TCHAR* ActorIdentifier = TEXT("actor_identifier");
	inline constexpr const TCHAR* PieInstance = TEXT("pie_instance");
	inline constexpr const TCHAR* IncludeProperties = TEXT("b_include_properties");
	inline constexpr const TCHAR* Actor = TEXT("actor");
	inline constexpr const TCHAR* Component = TEXT("component");
	inline constexpr const TCHAR* IsActive = TEXT("is_active");
	inline constexpr const TCHAR* IsInEndState = TEXT("is_in_end_state");
	inline constexpr const TCHAR* SingleActiveState = TEXT("single_active_state");
	inline constexpr const TCHAR* ActiveStates = TEXT("active_states");

	inline constexpr const TCHAR* TargetActorClass = TEXT("target_actor_class");
	inline constexpr const TCHAR* ComponentClass = TEXT("component_class");
	inline constexpr const TCHAR* GetContextNodeGuid = TEXT("get_context_node_guid");
	inline constexpr const TCHAR* CastNodeGuid = TEXT("cast_node_guid");
	inline constexpr const TCHAR* GetComponentNodeGuid = TEXT("get_component_node_guid");
	inline constexpr const TCHAR* ComponentOutputPinId = TEXT("component_output_pin_id");

	inline constexpr const TCHAR* NodeGuids = TEXT("node_guids");
	inline constexpr const TCHAR* DestinationStateGuid = TEXT("destination_state_guid");
	inline constexpr const TCHAR* SourceStateGuids = TEXT("source_state_guids");
	inline constexpr const TCHAR* DestroyStates = TEXT("b_destroy_states");
	inline constexpr const TCHAR* MergedStackTemplateGuids = TEXT("merged_stack_template_guids");
}
