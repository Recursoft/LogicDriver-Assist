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
	FName Name;

	FString Description;

	TSharedPtr<FJsonObject> InputSchema;

	FSMAssistOperationHandler Handler;
};
