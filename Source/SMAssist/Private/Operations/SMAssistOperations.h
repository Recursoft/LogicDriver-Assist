// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Operations/SMAssistOperationResult.h"

class FJsonObject;

namespace LD::Assist
{
	FSMAssistOperationResult CreateBlueprint(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddState(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddTransition(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddTransitionReroute(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult ListAssets(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult GetAsset(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult RemoveNode(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult SetNodeProperty(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult Compile(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult RenameState(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult SetInitialState(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddStateStack(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddTransitionStack(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddConduit(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddReference(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddAnyState(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddLinkState(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult GetNodeProperties(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult SetTransitionCondition(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult GetGraphView(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult CaptureGraphView(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult ClearScreenshots(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult LayoutStates(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult GetPropertyPins(const TSharedRef<FJsonObject>& InArgs);
	FSMAssistOperationResult GetPropertyGraph(const TSharedRef<FJsonObject>& InArgs);
	FSMAssistOperationResult SetPropertyGraphEditMode(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult SplitPin(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult RecombinePin(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult ResetNodeProperty(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddSMVariable(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult ConfigureSMComponentOnActor(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult SpawnLocalGraphReadNode(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult ConfigureReference(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult SpawnLocalGraphWriteNode(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult ConfigureTransitionEvent(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult FindNodeTypes(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddNodeVariable(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult ConfigureNodeVariable(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult ConnectNodeVariableOutput(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult DisconnectNodeVariableOutput(const TSharedRef<FJsonObject>& InArgs);
}
