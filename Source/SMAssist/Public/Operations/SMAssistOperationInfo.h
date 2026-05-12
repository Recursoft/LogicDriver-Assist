// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Operations/SMAssistOperationResult.h"

#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

DECLARE_DELEGATE_RetVal_OneParam(FSMAssistOperationResult, FSMAssistOperationHandler, const TSharedRef<FJsonObject>& /* InArgs */);

struct FSMAssistOperationInfo
{
	/** Stable identifier used to register, look up, and dispatch the operation. Must be unique across the subsystem. */
	FName Name;

	/** Human-readable summary of what the operation does. Surfaced through bridges to MCP clients. */
	FString Description;

	/** JSON Schema describing the expected input arguments. May be null when the operation takes no arguments. */
	TSharedPtr<FJsonObject> InputSchema;

	/** Callback invoked when ExecuteOperation runs this operation. Receives the raw JSON args and returns a structured result. */
	FSMAssistOperationHandler Handler;
};
