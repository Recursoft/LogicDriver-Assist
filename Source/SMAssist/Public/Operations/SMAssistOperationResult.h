// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"

struct FSMAssistOperationResult
{
	bool bSuccess = false;

	FString ErrorMessage;

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
