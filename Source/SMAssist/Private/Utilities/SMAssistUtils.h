// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class FProperty;
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

	/**
	 * Resolve a caller-supplied screenshots subdirectory to an absolute path confined under
	 * <Project>/Saved/Screenshots. Returns false with OutError set when the subdir escapes that root
	 * (e.g. contains '..' or an absolute path), so file writes and deletes cannot leave the directory.
	 */
	bool ResolveContainedScreenshotsDir(const FString& InSubdir, FString& OutDir, FString& OutError);

	/** True when InStem is safe as a bare filename stem: non-empty and free of path separators or parent refs. */
	bool IsSafeFileStem(const FString& InStem);

	/**
	 * Returns false only when InProperty is a non-enum integer property and InValue is not integer
	 * text: optional sign plus decimal digits, or a 0x hex literal (both forms the engine importer
	 * accepts). The engine's integer text import can accept arbitrary alpha-leading text depending
	 * on process state: the token falls into UEnum::ParseEnum, where an un-interned string collapses
	 * to NAME_None and can match a loaded enum's bare "None" value, silently importing 0 (observed
	 * on UE 5.8; rejected on UE 5.7). Integer writes pre-validate so garbage is rejected regardless
	 * of which enums the process happens to have loaded. Float, enum-backed, and non-numeric
	 * properties return true: their import behavior is sound and their text formats (e.g. "1e5", an
	 * enum name) must stay acceptable.
	 */
	bool IntegerPropertyTextParses(const FProperty* InProperty, const FString& InValue);
}
