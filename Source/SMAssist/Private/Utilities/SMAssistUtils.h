// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UBlueprint;
class USMBlueprint;
class USMGraphNode_Base;
class USMGraphNode_StateNodeBase;
class UWorld;

namespace LD::Assist::Utils
{
	UBlueprint* LoadBlueprint(const FString& InAssetPath, FString& OutError);

	USMBlueprint* LoadStateMachineBlueprint(const FString& InAssetPath, FString& OutError);

	USMGraphNode_Base* FindNodeByGuid(USMBlueprint* InBlueprint, const FGuid& InGuid);

	USMGraphNode_StateNodeBase* FindStateNodeByGuid(USMBlueprint* InBlueprint, const FGuid& InGuid);

	/** Resolve a live Play-In-Editor world. InPieInstance selects among multiple PIE worlds (0 = first). Returns null with OutError set when no matching PIE world is running. */
	UWorld* GetActivePIEWorld(int32 InPieInstance, FString& OutError);

	/** Find an actor in InWorld whose object name or display label matches InIdentifier (case-insensitive). Returns null when none match. */
	AActor* FindActorByIdentifier(UWorld* InWorld, const FString& InIdentifier);

	/** Map a terminal type token (e.g. "int", "Text", "Vector", or an object/struct path) to K2 pin-type fields. Returns false for an unrecognized token. */
	bool ResolveTerminalType(const FString& InTypeStr, FName& OutCategory, FName& OutSubCategory, UObject*& OutSubCategoryObject);
}
