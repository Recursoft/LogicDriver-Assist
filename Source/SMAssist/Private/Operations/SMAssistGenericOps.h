// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Operations/SMAssistOperationResult.h"

class FJsonObject;

// Handlers for the ld_ue.* fallback namespace. See SMAssistGenericOpKeys.h for the fallback-only contract.
namespace LD::Assist::GenericOps
{
	FSMAssistOperationResult ReadProperty(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult WriteProperty(const TSharedRef<FJsonObject>& InArgs);

	FSMAssistOperationResult AddDispatcher(const TSharedRef<FJsonObject>& InArgs);
}
