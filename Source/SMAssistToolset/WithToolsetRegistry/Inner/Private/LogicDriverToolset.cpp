// Copyright Recursoft LLC. All Rights Reserved.

#include "LogicDriverToolset.h"

#include "SMAssistToolsetMarshal.h"

#include "Blueprints/SMBlueprint.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LogicDriverToolset)

namespace LDA = LD::Assist::Toolset::Marshal;

FString ULogicDriverToolset::CreateBlueprint(const FString& Name, const FString& Path)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("name"), Name);
	LDA::AddIfNonEmpty(*Args, TEXT("path"), Path);
	return LDA::Execute(TEXT("ld.create_blueprint"), Args);
}

FString ULogicDriverToolset::ListAssets(const FString& PathPrefix)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddIfNonEmpty(*Args, TEXT("path_prefix"), PathPrefix);
	return LDA::Execute(TEXT("ld.list_assets"), Args);
}

FString ULogicDriverToolset::GetAsset(USMBlueprint* Blueprint)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	return LDA::Execute(TEXT("ld.get_asset"), Args);
}

FString ULogicDriverToolset::Compile(UBlueprint* Blueprint)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	return LDA::Execute(TEXT("ld.compile"), Args);
}

FString ULogicDriverToolset::AddState(
	USMBlueprint* Blueprint,
	const FString& StateName,
	bool bIsEntry,
	bool bAutoPosition,
	double PositionX,
	double PositionY,
	const FString& StateClass)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	LDA::AddIfNonEmpty(*Args, TEXT("state_name"), StateName);
	LDA::AddBool(*Args, TEXT("is_entry"), bIsEntry);
	LDA::AddPosition(*Args, bAutoPosition, TEXT("position_x"), TEXT("position_y"), PositionX, PositionY);
	LDA::AddIfNonEmpty(*Args, TEXT("state_class"), StateClass);
	return LDA::Execute(TEXT("ld.add_state"), Args);
}

FString ULogicDriverToolset::AddConduit(
	USMBlueprint* Blueprint,
	const FString& StateName,
	bool bIsEntry,
	bool bAutoPosition,
	double PositionX,
	double PositionY,
	const FString& StateClass,
	bool bEvalWithTransitions)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	LDA::AddIfNonEmpty(*Args, TEXT("state_name"), StateName);
	LDA::AddBool(*Args, TEXT("is_entry"), bIsEntry);
	LDA::AddPosition(*Args, bAutoPosition, TEXT("position_x"), TEXT("position_y"), PositionX, PositionY);
	LDA::AddIfNonEmpty(*Args, TEXT("state_class"), StateClass);
	LDA::AddBool(*Args, TEXT("eval_with_transitions"), bEvalWithTransitions);
	return LDA::Execute(TEXT("ld.add_conduit"), Args);
}

FString ULogicDriverToolset::AddAnyState(
	USMBlueprint* Blueprint,
	const FString& StateName,
	bool bAutoPosition,
	double PositionX,
	double PositionY)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	LDA::AddIfNonEmpty(*Args, TEXT("state_name"), StateName);
	LDA::AddPosition(*Args, bAutoPosition, TEXT("position_x"), TEXT("position_y"), PositionX, PositionY);
	return LDA::Execute(TEXT("ld.add_any_state"), Args);
}

FString ULogicDriverToolset::AddLinkState(
	USMBlueprint* Blueprint,
	const FString& LinkToStateName,
	bool bAutoPosition,
	double PositionX,
	double PositionY)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("link_to_state_name"), LinkToStateName);
	LDA::AddPosition(*Args, bAutoPosition, TEXT("position_x"), TEXT("position_y"), PositionX, PositionY);
	return LDA::Execute(TEXT("ld.add_link_state"), Args);
}

FString ULogicDriverToolset::AddReference(
	USMBlueprint* Blueprint,
	USMBlueprint* ReferenceBlueprint,
	const FString& StateName,
	bool bIsEntry,
	bool bAutoPosition,
	double PositionX,
	double PositionY,
	bool bUseIntermediateGraph)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	if (ReferenceBlueprint)
	{
		LDA::AddObjectPath(*Args, TEXT("reference_asset_path"), ReferenceBlueprint);
	}
	LDA::AddIfNonEmpty(*Args, TEXT("state_name"), StateName);
	LDA::AddBool(*Args, TEXT("is_entry"), bIsEntry);
	LDA::AddPosition(*Args, bAutoPosition, TEXT("position_x"), TEXT("position_y"), PositionX, PositionY);
	LDA::AddBool(*Args, TEXT("use_intermediate_graph"), bUseIntermediateGraph);
	return LDA::Execute(TEXT("ld.add_reference"), Args);
}

FString ULogicDriverToolset::ConfigureReference(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	USMBlueprint* ReferenceBlueprint,
	bool bUpdateIntermediateGraph,
	bool bUseIntermediateGraph)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	if (ReferenceBlueprint)
	{
		LDA::AddObjectPath(*Args, TEXT("reference_asset_path"), ReferenceBlueprint);
	}
	if (bUpdateIntermediateGraph)
	{
		LDA::AddBool(*Args, TEXT("use_intermediate_graph"), bUseIntermediateGraph);
	}
	return LDA::Execute(TEXT("ld.configure_reference"), Args);
}

FString ULogicDriverToolset::AddTransition(
	USMBlueprint* Blueprint,
	const FString& FromStateGuid,
	const FString& ToStateGuid,
	const FString& TransitionClass)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("from_state_guid"), FromStateGuid);
	Args->SetStringField(TEXT("to_state_guid"), ToStateGuid);
	LDA::AddIfNonEmpty(*Args, TEXT("transition_class"), TransitionClass);
	return LDA::Execute(TEXT("ld.add_transition"), Args);
}

FString ULogicDriverToolset::AddTransitionReroute(
	USMBlueprint* Blueprint,
	const FString& TransitionGuid,
	double PositionX,
	double PositionY)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	LDA::AddIfNonEmpty(*Args, TEXT("transition_guid"), TransitionGuid);
	Args->SetNumberField(TEXT("position_x"), PositionX);
	Args->SetNumberField(TEXT("position_y"), PositionY);
	return LDA::Execute(TEXT("ld.add_transition_reroute"), Args);
}

FString ULogicDriverToolset::AddStateStack(
	USMBlueprint* Blueprint,
	const FString& StateGuid,
	const FString& StateClass,
	int32 StackIndex)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("state_guid"), StateGuid);
	Args->SetStringField(TEXT("state_class"), StateClass);
	LDA::AddIfNotIndexNone(*Args, TEXT("stack_index"), StackIndex);
	return LDA::Execute(TEXT("ld.add_state_stack"), Args);
}

FString ULogicDriverToolset::AddTransitionStack(
	USMBlueprint* Blueprint,
	const FString& TransitionGuid,
	const FString& TransitionClass,
	int32 StackIndex)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("transition_guid"), TransitionGuid);
	Args->SetStringField(TEXT("transition_class"), TransitionClass);
	LDA::AddIfNotIndexNone(*Args, TEXT("stack_index"), StackIndex);
	return LDA::Execute(TEXT("ld.add_transition_stack"), Args);
}

FString ULogicDriverToolset::SetTransitionCondition(
	USMBlueprint* Blueprint,
	const FString& TransitionGuid,
	bool bCondition)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("transition_guid"), TransitionGuid);
	LDA::AddBool(*Args, TEXT("condition"), bCondition);
	return LDA::Execute(TEXT("ld.set_transition_condition"), Args);
}

FString ULogicDriverToolset::RemoveNode(USMBlueprint* Blueprint, const FString& NodeGuid)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	return LDA::Execute(TEXT("ld.remove_node"), Args);
}

FString ULogicDriverToolset::RenameState(
	USMBlueprint* Blueprint,
	const FString& StateGuid,
	const FString& NewName)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("state_guid"), StateGuid);
	Args->SetStringField(TEXT("new_name"), NewName);
	return LDA::Execute(TEXT("ld.rename_state"), Args);
}

FString ULogicDriverToolset::SetInitialState(USMBlueprint* Blueprint, const FString& StateGuid)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("state_guid"), StateGuid);
	return LDA::Execute(TEXT("ld.set_initial_state"), Args);
}

FString ULogicDriverToolset::SetNodeClass(USMBlueprint* Blueprint, const FString& NodeGuid, const FString& NodeClass)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("node_class"), NodeClass);
	return LDA::Execute(TEXT("ld.set_node_class"), Args);
}

FString ULogicDriverToolset::SetNodeProperty(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& PropertyName,
	const FString& Value,
	int32 ArrayIndex,
	int32 TargetIndex,
	const FString& ArrayAction,
	int32 StackIndex,
	const FString& PropertyPath)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("property_name"), PropertyName);
	if (!LDA::AddJsonValue(*Args, TEXT("value"), Value))
	{
		return FString();
	}
	LDA::AddIfNotIndexNone(*Args, TEXT("array_index"), ArrayIndex);
	LDA::AddIfNotIndexNone(*Args, TEXT("target_index"), TargetIndex);
	LDA::AddIfNonEmpty(*Args, TEXT("array_action"), ArrayAction);
	LDA::AddIfNotIndexNone(*Args, TEXT("stack_index"), StackIndex);
	LDA::AddIfNonEmpty(*Args, TEXT("property_path"), PropertyPath);
	return LDA::Execute(TEXT("ld.set_node_property"), Args);
}

FString ULogicDriverToolset::ResetNodeProperty(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& PropertyName,
	int32 ArrayIndex,
	int32 StackIndex)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("property_name"), PropertyName);
	LDA::AddIfNotIndexNone(*Args, TEXT("array_index"), ArrayIndex);
	LDA::AddIfNotIndexNone(*Args, TEXT("stack_index"), StackIndex);
	return LDA::Execute(TEXT("ld.reset_node_property"), Args);
}

FString ULogicDriverToolset::GetNodeProperties(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	int32 StackIndex,
	int32 MaxDepth)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	LDA::AddIfNotIndexNone(*Args, TEXT("stack_index"), StackIndex);
	LDA::AddIfNotIndexNone(*Args, TEXT("max_depth"), MaxDepth);
	return LDA::Execute(TEXT("ld.get_node_properties"), Args);
}

FString ULogicDriverToolset::GetPropertyPins(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& VariableName)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	LDA::AddIfNonEmpty(*Args, TEXT("variable_name"), VariableName);
	return LDA::Execute(TEXT("ld.get_property_pins"), Args);
}

FString ULogicDriverToolset::GetPropertyGraph(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& VariableName,
	const FString& PropertyPath,
	bool bIncludePinTree)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("variable_name"), VariableName);
	LDA::AddIfNonEmpty(*Args, TEXT("property_path"), PropertyPath);
	Args->SetBoolField(TEXT("include_pin_tree"), bIncludePinTree);
	return LDA::Execute(TEXT("ld.get_property_graph"), Args);
}

FString ULogicDriverToolset::SetPropertyGraphEditMode(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& VariableName,
	bool bEnable,
	const FString& PropertyPath)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("variable_name"), VariableName);
	LDA::AddIfNonEmpty(*Args, TEXT("property_path"), PropertyPath);
	Args->SetBoolField(TEXT("b_enable"), bEnable);
	return LDA::Execute(TEXT("ld.set_property_graph_edit_mode"), Args);
}

FString ULogicDriverToolset::SplitPin(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& VariableName,
	const FString& PinId)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("variable_name"), VariableName);
	LDA::AddIfNonEmpty(*Args, TEXT("pin_id"), PinId);
	return LDA::Execute(TEXT("ld.split_pin"), Args);
}

FString ULogicDriverToolset::RecombinePin(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& VariableName,
	const FString& PinId)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("variable_name"), VariableName);
	LDA::AddIfNonEmpty(*Args, TEXT("pin_id"), PinId);
	return LDA::Execute(TEXT("ld.recombine_pin"), Args);
}

FString ULogicDriverToolset::GetGraphView(
	USMBlueprint* Blueprint,
	bool bIncludeTransitions,
	bool bIncludePins)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	LDA::AddBool(*Args, TEXT("include_transitions"), bIncludeTransitions);
	LDA::AddBool(*Args, TEXT("include_pins"), bIncludePins);
	return LDA::Execute(TEXT("ld.get_graph_view"), Args);
}

FString ULogicDriverToolset::CaptureGraphView(
	USMBlueprint* Blueprint,
	bool bClipToPanel,
	bool bFitToContent,
	const FString& NodeGuid,
	const FString& OutputSubdir,
	const FString& Prefix)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	LDA::AddBool(*Args, TEXT("clip_to_panel"), bClipToPanel);
	LDA::AddBool(*Args, TEXT("fit_to_content"), bFitToContent);
	LDA::AddIfNonEmpty(*Args, TEXT("node_guid"), NodeGuid);
	LDA::AddIfNonEmpty(*Args, TEXT("output_subdir"), OutputSubdir);
	LDA::AddIfNonEmpty(*Args, TEXT("prefix"), Prefix);
	return LDA::Execute(TEXT("ld.capture_graph_view"), Args);
}

FString ULogicDriverToolset::CaptureLocalGraph(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	bool bClipToPanel,
	bool bFitToContent,
	const FString& OutputSubdir,
	const FString& Prefix)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	LDA::AddBool(*Args, TEXT("clip_to_panel"), bClipToPanel);
	LDA::AddBool(*Args, TEXT("fit_to_content"), bFitToContent);
	LDA::AddIfNonEmpty(*Args, TEXT("output_subdir"), OutputSubdir);
	LDA::AddIfNonEmpty(*Args, TEXT("prefix"), Prefix);
	return LDA::Execute(TEXT("ld.capture_local_graph"), Args);
}

FString ULogicDriverToolset::ClearScreenshots(
	const FString& OutputSubdir,
	double OlderThanSeconds,
	bool bDryRun)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddIfNonEmpty(*Args, TEXT("output_subdir"), OutputSubdir);
	LDA::AddIfNonNegative(*Args, TEXT("older_than_seconds"), OlderThanSeconds);
	LDA::AddBool(*Args, TEXT("dry_run"), bDryRun);
	return LDA::Execute(TEXT("ld.clear_screenshots"), Args);
}

FString ULogicDriverToolset::LayoutStates(
	USMBlueprint* Blueprint,
	const FString& Strategy,
	bool bApply,
	const FString& Scope,
	double ColumnGap,
	double RowGap,
	bool bDefaultOrigin,
	double StartX,
	double StartY,
	const FString& PinNodeGuidsJson,
	bool bRespectExistingOrder,
	bool bSnapToGrid)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	LDA::AddIfNonEmpty(*Args, TEXT("strategy"), Strategy);
	LDA::AddBool(*Args, TEXT("apply"), bApply);
	LDA::AddIfNonEmpty(*Args, TEXT("scope"), Scope);
	LDA::AddIfNonNegative(*Args, TEXT("column_gap"), ColumnGap);
	LDA::AddIfNonNegative(*Args, TEXT("row_gap"), RowGap);
	LDA::AddPosition(*Args, bDefaultOrigin, TEXT("start_x"), TEXT("start_y"), StartX, StartY);
	if (!LDA::AddJsonValue(*Args, TEXT("pin_node_guids"), PinNodeGuidsJson))
	{
		return FString();
	}
	LDA::AddBool(*Args, TEXT("respect_existing_order"), bRespectExistingOrder);
	LDA::AddBool(*Args, TEXT("snap_to_grid"), bSnapToGrid);
	return LDA::Execute(TEXT("ld.layout_states"), Args);
}

namespace LD::Assist::Toolset::Private
{
	static bool IsContainerTypeSentinel(const FString& Value)
	{
		return Value.IsEmpty() || Value.Equals(TEXT("None"), ESearchCase::IgnoreCase);
	}
}

FString ULogicDriverToolset::AddSMVariable(
	USMBlueprint* Blueprint,
	const FString& VarName,
	const FString& VarType,
	const FString& DefaultValue,
	const FString& ContainerType,
	const FString& KeyType)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("variable_name"), VarName);
	Args->SetStringField(TEXT("var_type"), VarType);
	LDA::AddIfNonEmpty(*Args, TEXT("default_value"), DefaultValue);
	if (!LD::Assist::Toolset::Private::IsContainerTypeSentinel(ContainerType))
	{
		Args->SetStringField(TEXT("container_type"), ContainerType);
	}
	LDA::AddIfNonEmpty(*Args, TEXT("key_type"), KeyType);
	return LDA::Execute(TEXT("ld.add_sm_variable"), Args);
}

FString ULogicDriverToolset::ConfigureSMComponentOnActor(
	UBlueprint* ActorBlueprint,
	USMBlueprint* StateMachineBlueprint,
	const FString& ComponentName,
	const FSMComponentConfig& Config)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("actor_blueprint"), ActorBlueprint);
	LDA::AddObjectPath(*Args, TEXT("state_machine_class"), StateMachineBlueprint);
	Args->SetStringField(TEXT("component_name"), ComponentName);

	auto AddOptionalBool = [&](const TCHAR* Field, const TOptional<bool>& Value)
	{
		if (Value.IsSet())
		{
			Args->SetBoolField(Field, Value.GetValue());
		}
	};

	AddOptionalBool(TEXT("b_start_on_begin_play"), Config.bStartOnBeginPlay);
	AddOptionalBool(TEXT("b_initialize_on_begin_play"), Config.bInitializeOnBeginPlay);
	AddOptionalBool(TEXT("b_stop_on_end_play"), Config.bStopOnEndPlay);
	AddOptionalBool(TEXT("b_reuse_instance_after_shutdown"), Config.bReuseInstanceAfterShutdown);
	AddOptionalBool(TEXT("b_replicates"), Config.bReplicates);
	AddOptionalBool(TEXT("b_include_simulated_proxies"), Config.bIncludeSimulatedProxies);
	AddOptionalBool(TEXT("b_wait_for_transactions_from_server"), Config.bWaitForTransactionsFromServer);
	AddOptionalBool(TEXT("b_handle_controller_change"), Config.bHandleControllerChange);

	LDA::AddIfNonEmpty(*Args, TEXT("state_change_authority"), Config.StateChangeAuthority);
	LDA::AddIfNonEmpty(*Args, TEXT("network_tick_configuration"), Config.NetworkTickConfiguration);
	LDA::AddIfNonEmpty(*Args, TEXT("network_state_execution"), Config.NetworkStateExecution);
	LDA::AddIfNonEmpty(*Args, TEXT("network_transition_entered_configuration"), Config.NetworkTransitionEnteredConfiguration);

	LDA::AddIfNonEmpty(*Args, TEXT("extra_config_json"), Config.ExtraConfigJson);

	return LDA::Execute(TEXT("ld.configure_sm_component_on_actor"), Args);
}

FString ULogicDriverToolset::SpawnLocalGraphReadNode(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& NodeType,
	double PositionX,
	double PositionY,
	const FString& NodeInstanceGuid,
	int32 NodeInstanceIndex)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("type"), NodeType);
	Args->SetNumberField(TEXT("position_x"), PositionX);
	Args->SetNumberField(TEXT("position_y"), PositionY);
	LDA::AddIfNonEmpty(*Args, TEXT("node_instance_guid"), NodeInstanceGuid);
	LDA::AddIfNotIndexNone(*Args, TEXT("node_instance_index"), NodeInstanceIndex);
	return LDA::Execute(TEXT("ld.spawn_local_graph_read_node"), Args);
}

FString ULogicDriverToolset::SpawnLocalGraphWriteNode(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& NodeType,
	double PositionX,
	double PositionY,
	bool bHasDefaultValue,
	bool bDefaultValue)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("type"), NodeType);
	Args->SetNumberField(TEXT("position_x"), PositionX);
	Args->SetNumberField(TEXT("position_y"), PositionY);
	if (bHasDefaultValue)
	{
		LDA::AddBool(*Args, TEXT("default_value"), bDefaultValue);
	}
	return LDA::Execute(TEXT("ld.spawn_local_graph_write_node"), Args);
}

FString ULogicDriverToolset::SpawnLocalGraphEventNode(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& NodeType,
	double PositionX,
	double PositionY)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("type"), NodeType);
	Args->SetNumberField(TEXT("position_x"), PositionX);
	Args->SetNumberField(TEXT("position_y"), PositionY);
	return LDA::Execute(TEXT("ld.spawn_local_graph_event_node"), Args);
}

FString ULogicDriverToolset::GetLocalGraph(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	bool bIncludePins)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	LDA::AddBool(*Args, TEXT("include_pins"), bIncludePins);
	return LDA::Execute(TEXT("ld.get_local_graph"), Args);
}

FString ULogicDriverToolset::AddLocalGraphNode(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& NodeClass,
	const FString& FunctionName,
	const FString& FunctionClass,
	const FString& VariableName,
	const FString& TargetClass,
	double PositionX,
	double PositionY)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	LDA::AddIfNonEmpty(*Args, TEXT("node_class"), NodeClass);
	LDA::AddIfNonEmpty(*Args, TEXT("function_name"), FunctionName);
	LDA::AddIfNonEmpty(*Args, TEXT("function_class"), FunctionClass);
	LDA::AddIfNonEmpty(*Args, TEXT("variable_name"), VariableName);
	LDA::AddIfNonEmpty(*Args, TEXT("target_class"), TargetClass);
	Args->SetNumberField(TEXT("position_x"), PositionX);
	Args->SetNumberField(TEXT("position_y"), PositionY);
	return LDA::Execute(TEXT("ld.add_local_graph_node"), Args);
}

FString ULogicDriverToolset::ConnectLocalGraphPins(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& FromNodeId,
	const FString& FromPin,
	const FString& ToNodeId,
	const FString& ToPin)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("from_node_id"), FromNodeId);
	Args->SetStringField(TEXT("from_pin"), FromPin);
	Args->SetStringField(TEXT("to_node_id"), ToNodeId);
	Args->SetStringField(TEXT("to_pin"), ToPin);
	return LDA::Execute(TEXT("ld.connect_local_graph_pins"), Args);
}

FString ULogicDriverToolset::SetLocalGraphPinDefault(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& NodeId,
	const FString& Pin,
	const FString& Value)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("node_id"), NodeId);
	Args->SetStringField(TEXT("pin"), Pin);
	Args->SetStringField(TEXT("value"), Value);
	return LDA::Execute(TEXT("ld.set_local_graph_pin_default"), Args);
}

FString ULogicDriverToolset::RemoveLocalGraphNode(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& NodeId)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("node_id"), NodeId);
	return LDA::Execute(TEXT("ld.remove_local_graph_node"), Args);
}

FString ULogicDriverToolset::DisconnectLocalGraphPins(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& FromNodeId,
	const FString& FromPin,
	const FString& ToNodeId,
	const FString& ToPin)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("from_node_id"), FromNodeId);
	Args->SetStringField(TEXT("from_pin"), FromPin);
	Args->SetStringField(TEXT("to_node_id"), ToNodeId);
	Args->SetStringField(TEXT("to_pin"), ToPin);
	return LDA::Execute(TEXT("ld.disconnect_local_graph_pins"), Args);
}

FString ULogicDriverToolset::SetLocalGraphNode(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& NodeId,
	bool bUpdatePosition,
	double PositionX,
	double PositionY,
	bool bUpdateComment,
	const FString& Comment,
	bool bUpdateEnabled,
	bool bEnabled)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("node_id"), NodeId);
	if (bUpdatePosition)
	{
		Args->SetNumberField(TEXT("position_x"), PositionX);
		Args->SetNumberField(TEXT("position_y"), PositionY);
	}
	if (bUpdateComment)
	{
		Args->SetStringField(TEXT("comment"), Comment);
	}
	if (bUpdateEnabled)
	{
		LDA::AddBool(*Args, TEXT("enabled"), bEnabled);
	}
	return LDA::Execute(TEXT("ld.set_local_graph_node"), Args);
}

FString ULogicDriverToolset::ConfigureTransitionEvent(
	USMBlueprint* Blueprint,
	const FString& TransitionGuid,
	const FString& DelegateOwnerInstance,
	UClass* DelegateOwnerClass,
	bool bUpdateDelegateName,
	const FString& DelegatePropertyName,
	bool bUpdateTargetedUpdate,
	bool bEventTriggersTargetedUpdate,
	bool bUpdateFullUpdate,
	bool bEventTriggersFullUpdate)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("transition_guid"), TransitionGuid);
	LDA::AddIfNonEmpty(*Args, TEXT("delegate_owner_instance"), DelegateOwnerInstance);
	if (DelegateOwnerClass)
	{
		LDA::AddObjectPath(*Args, TEXT("delegate_owner_class"), DelegateOwnerClass);
	}
	if (bUpdateDelegateName)
	{
		Args->SetStringField(TEXT("delegate_property_name"), DelegatePropertyName);
	}
	if (bUpdateTargetedUpdate)
	{
		LDA::AddBool(*Args, TEXT("event_triggers_targeted_update"), bEventTriggersTargetedUpdate);
	}
	if (bUpdateFullUpdate)
	{
		LDA::AddBool(*Args, TEXT("event_triggers_full_update"), bEventTriggersFullUpdate);
	}
	return LDA::Execute(TEXT("ld.configure_transition_event"), Args);
}

FString ULogicDriverToolset::FindNodeTypes(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& TypeIdFilter)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	LDA::AddIfNonEmpty(*Args, TEXT("type_id_filter"), TypeIdFilter);
	return LDA::Execute(TEXT("ld.find_node_types"), Args);
}

FString ULogicDriverToolset::AddNodeVariable(
	UBlueprint* NodeClassBlueprint,
	const FString& VarName,
	const FString& VarType,
	const FString& DefaultValue,
	const FString& Direction,
	bool bHidden,
	bool bReadOnly,
	const FString& ContainerType,
	const FString& KeyType)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), NodeClassBlueprint);
	Args->SetStringField(TEXT("variable_name"), VarName);
	Args->SetStringField(TEXT("var_type"), VarType);
	LDA::AddIfNonEmpty(*Args, TEXT("default_value"), DefaultValue);
	LDA::AddIfNonEmpty(*Args, TEXT("direction"), Direction);
	if (bHidden)
	{
		LDA::AddBool(*Args, TEXT("b_hidden"), bHidden);
	}
	if (bReadOnly)
	{
		LDA::AddBool(*Args, TEXT("b_read_only"), bReadOnly);
	}
	if (!LD::Assist::Toolset::Private::IsContainerTypeSentinel(ContainerType))
	{
		Args->SetStringField(TEXT("container_type"), ContainerType);
	}
	LDA::AddIfNonEmpty(*Args, TEXT("key_type"), KeyType);
	return LDA::Execute(TEXT("ld.add_node_variable"), Args);
}

FString ULogicDriverToolset::AddBlueprintVariable(
	UBlueprint* Blueprint,
	const FString& VarName,
	const FString& VarType,
	const FString& DefaultValue,
	const FString& ContainerType,
	const FString& KeyType)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("variable_name"), VarName);
	Args->SetStringField(TEXT("var_type"), VarType);
	LDA::AddIfNonEmpty(*Args, TEXT("default_value"), DefaultValue);
	if (!LD::Assist::Toolset::Private::IsContainerTypeSentinel(ContainerType))
	{
		Args->SetStringField(TEXT("container_type"), ContainerType);
	}
	LDA::AddIfNonEmpty(*Args, TEXT("key_type"), KeyType);
	return LDA::Execute(TEXT("ld.add_blueprint_variable"), Args);
}

FString ULogicDriverToolset::ConfigureNodeVariable(
	UBlueprint* NodeClassBlueprint,
	const FString& VarName,
	bool bUpdateDirection,
	const FString& Direction,
	bool bUpdateHidden,
	bool bHidden,
	bool bUpdateReadOnly,
	bool bReadOnly)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), NodeClassBlueprint);
	Args->SetStringField(TEXT("variable_name"), VarName);
	if (bUpdateDirection)
	{
		LDA::AddBool(*Args, TEXT("b_update_direction"), true);
		Args->SetStringField(TEXT("direction"), Direction);
	}
	if (bUpdateHidden)
	{
		LDA::AddBool(*Args, TEXT("b_update_hidden"), true);
		LDA::AddBool(*Args, TEXT("b_hidden"), bHidden);
	}
	if (bUpdateReadOnly)
	{
		LDA::AddBool(*Args, TEXT("b_update_read_only"), true);
		LDA::AddBool(*Args, TEXT("b_read_only"), bReadOnly);
	}
	return LDA::Execute(TEXT("ld.configure_node_variable"), Args);
}

FString ULogicDriverToolset::ConnectNodeVariableOutput(
	USMBlueprint* Blueprint,
	const FString& FromStateGuid,
	int32 FromStackIndex,
	const FString& FromVarName,
	const FString& ToStateGuid,
	int32 ToStackIndex,
	const FString& ToVarName,
	const FString& ToOwningBlueprintVariable)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("from_state_guid"), FromStateGuid);
	LDA::AddIfNotIndexNone(*Args, TEXT("from_stack_index"), FromStackIndex);
	Args->SetStringField(TEXT("from_variable_name"), FromVarName);
	LDA::AddIfNonEmpty(*Args, TEXT("to_state_guid"), ToStateGuid);
	LDA::AddIfNotIndexNone(*Args, TEXT("to_stack_index"), ToStackIndex);
	LDA::AddIfNonEmpty(*Args, TEXT("to_variable_name"), ToVarName);
	LDA::AddIfNonEmpty(*Args, TEXT("to_owning_blueprint_variable"), ToOwningBlueprintVariable);
	return LDA::Execute(TEXT("ld.connect_node_variable_output"), Args);
}

FString ULogicDriverToolset::DisconnectNodeVariableOutput(
	USMBlueprint* Blueprint,
	const FString& FromStateGuid,
	int32 FromStackIndex,
	const FString& FromVarName,
	const FString& ToStateGuid,
	int32 ToStackIndex,
	const FString& ToVarName,
	const FString& ToOwningBlueprintVariable)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("from_state_guid"), FromStateGuid);
	LDA::AddIfNotIndexNone(*Args, TEXT("from_stack_index"), FromStackIndex);
	Args->SetStringField(TEXT("from_variable_name"), FromVarName);
	LDA::AddIfNonEmpty(*Args, TEXT("to_state_guid"), ToStateGuid);
	LDA::AddIfNotIndexNone(*Args, TEXT("to_stack_index"), ToStackIndex);
	LDA::AddIfNonEmpty(*Args, TEXT("to_variable_name"), ToVarName);
	LDA::AddIfNonEmpty(*Args, TEXT("to_owning_blueprint_variable"), ToOwningBlueprintVariable);
	return LDA::Execute(TEXT("ld.disconnect_node_variable_output"), Args);
}

FString ULogicDriverToolset::SetConduitCondition(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	bool bCondition)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	LDA::AddBool(*Args, TEXT("condition"), bCondition);
	return LDA::Execute(TEXT("ld.set_conduit_condition"), Args);
}

FString ULogicDriverToolset::SpawnActorContextComponent(
	USMBlueprint* Blueprint,
	const FString& TargetGraphPath,
	const FString& TargetActorClass,
	const FString& ComponentClass,
	double PositionX,
	double PositionY)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("target_graph_path"), TargetGraphPath);
	Args->SetStringField(TEXT("target_actor_class"), TargetActorClass);
	Args->SetStringField(TEXT("component_class"), ComponentClass);
	Args->SetNumberField(TEXT("position_x"), PositionX);
	Args->SetNumberField(TEXT("position_y"), PositionY);
	return LDA::Execute(TEXT("ld.spawn_actor_context_component"), Args);
}

FString ULogicDriverToolset::CollapseToStateMachine(
	USMBlueprint* Blueprint,
	const FString& NodeGuidsJson)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	if (!LDA::AddJsonValue(*Args, TEXT("node_guids"), NodeGuidsJson))
	{
		return FString();
	}
	return LDA::Execute(TEXT("ld.collapse_to_state_machine"), Args);
}

FString ULogicDriverToolset::MergeStates(
	USMBlueprint* Blueprint,
	const FString& DestinationStateGuid,
	const FString& SourceStateGuidsJson,
	bool bDestroyStates)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("destination_state_guid"), DestinationStateGuid);
	if (!LDA::AddJsonValue(*Args, TEXT("source_state_guids"), SourceStateGuidsJson))
	{
		return FString();
	}
	LDA::AddBool(*Args, TEXT("b_destroy_states"), bDestroyStates);
	return LDA::Execute(TEXT("ld.merge_states"), Args);
}

FString ULogicDriverToolset::ReplaceNode(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& Kind)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	Args->SetStringField(TEXT("kind"), Kind);
	return LDA::Execute(TEXT("ld.replace_node"), Args);
}

FString ULogicDriverToolset::ConvertToReference(
	USMBlueprint* Blueprint,
	const FString& NodeGuid,
	const FString& Name,
	const FString& Path,
	const FString& ParentClass)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("node_guid"), NodeGuid);
	LDA::AddIfNonEmpty(*Args, TEXT("name"), Name);
	LDA::AddIfNonEmpty(*Args, TEXT("path"), Path);
	LDA::AddIfNonEmpty(*Args, TEXT("parent_class"), ParentClass);
	return LDA::Execute(TEXT("ld.convert_to_reference"), Args);
}

FString ULogicDriverToolset::RuntimeGetState(
	const FString& ActorIdentifier,
	const FString& ComponentName,
	bool bIncludeProperties,
	int32 MaxDepth,
	int32 PieInstance)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("actor_identifier"), ActorIdentifier);
	LDA::AddIfNonEmpty(*Args, TEXT("component_name"), ComponentName);
	LDA::AddBool(*Args, TEXT("b_include_properties"), bIncludeProperties);
	LDA::AddIfNotIndexNone(*Args, TEXT("max_depth"), MaxDepth);
	LDA::AddIfNotIndexNone(*Args, TEXT("pie_instance"), PieInstance);
	return LDA::Execute(TEXT("ld.runtime_get_state"), Args);
}

FString ULogicDriverToolset::ReadProperty(
	const FString& Object,
	const FString& PropertyPath,
	const FString& Target,
	int32 PieInstance)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("object"), Object);
	Args->SetStringField(TEXT("property_path"), PropertyPath);
	LDA::AddIfNonEmpty(*Args, TEXT("target"), Target);
	LDA::AddIfNotIndexNone(*Args, TEXT("pie_instance"), PieInstance);
	return LDA::Execute(TEXT("ld_ue.read_property"), Args);
}

FString ULogicDriverToolset::WriteProperty(
	const FString& Object,
	const FString& PropertyPath,
	const FString& Value,
	const FString& Target,
	int32 PieInstance)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("object"), Object);
	Args->SetStringField(TEXT("property_path"), PropertyPath);
	Args->SetStringField(TEXT("value"), Value);
	LDA::AddIfNonEmpty(*Args, TEXT("target"), Target);
	LDA::AddIfNotIndexNone(*Args, TEXT("pie_instance"), PieInstance);
	return LDA::Execute(TEXT("ld_ue.write_property"), Args);
}

FString ULogicDriverToolset::AddDispatcher(
	UBlueprint* Blueprint,
	const FString& Name,
	const FString& ParamsJson)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	LDA::AddObjectPath(*Args, TEXT("asset_path"), Blueprint);
	Args->SetStringField(TEXT("name"), Name);
	if (!LDA::AddJsonValue(*Args, TEXT("params"), ParamsJson))
	{
		return FString();
	}
	return LDA::Execute(TEXT("ld_ue.add_dispatcher"), Args);
}
