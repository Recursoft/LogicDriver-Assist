// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"

struct FSMAssistOperationResult
{
	/** True when the operation completed successfully. False means the operation rejected the input or hit a runtime error. */
	bool bSuccess = false;

	/** Human-readable explanation when bSuccess is false. Empty on success. */
	FString ErrorMessage;

	/** Optional JSON result data. Populated on success when the operation has structured output; may also accompany an error to surface partial diagnostics. */
	TSharedPtr<FJsonObject> Payload;

	static FSMAssistOperationResult MakeSuccess(TSharedPtr<FJsonObject> InPayload = nullptr)
	{
		FSMAssistOperationResult Result;
		Result.bSuccess = true;
		Result.Payload = MoveTemp(InPayload);
		return Result;
	}

	static FSMAssistOperationResult MakeError(const FString& InMessage, TSharedPtr<FJsonObject> InPayload = nullptr)
	{
		FSMAssistOperationResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = InMessage;
		Result.Payload = MoveTemp(InPayload);
		return Result;
	}
};
