// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Operations/SMAssistOperationResult.h"

class FJsonObject;

namespace LD::Assist
{
	FSMAssistOperationResult CreateBlueprint(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddState(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddTransition(const TSharedRef<FJsonObject>& InArgs);

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

	FSMAssistOperationResult ResetNodeProperty(const TSharedRef<FJsonObject>& InArgs);
}
