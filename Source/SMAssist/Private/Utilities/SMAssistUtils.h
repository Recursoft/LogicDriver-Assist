// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USMBlueprint;
class USMGraphNode_Base;
class USMGraphNode_StateNodeBase;

namespace LD::Assist::Utils
{
	USMBlueprint* LoadStateMachineBlueprint(const FString& InAssetPath, FString& OutError);

	USMGraphNode_Base* FindNodeByGuid(USMBlueprint* InBlueprint, const FGuid& InGuid);

	USMGraphNode_StateNodeBase* FindStateNodeByGuid(USMBlueprint* InBlueprint, const FGuid& InGuid);
}
