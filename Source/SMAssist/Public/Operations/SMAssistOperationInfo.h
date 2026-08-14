// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Operations/SMAssistOperationResult.h"

#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

DECLARE_DELEGATE_RetVal_OneParam(FSMAssistOperationResult, FSMAssistOperationHandler, const TSharedRef<FJsonObject>& /* InArgs */);

/**
 * Whether an operation is safe to run without asking the user first. Bridges map this onto their
 * transport's vocabulary. For MCP, ReadOnly is readOnlyHint and Destructive is destructiveHint.
 *
 * The split is deliberately coarse: anything that changes project state is Destructive, including
 * purely additive changes, because permission gating only ever asks the binary question.
 *
 * A value describes the operation's WORST CASE across all argument combinations, matching the "may"
 * wording of the MCP definitions, so an operation whose damage is argument-gated (a dry-run flag, an
 * opt-in destroy flag) is still Destructive.
 *
 * Ordered most-dangerous first so a value-initialized enum requires approval.
 */
enum class ESMAssistOperationImpact : uint8
{
	/** Changes asset, project, or filesystem state in any way. */
	Destructive,

	/**
	 * Reads without changing anything the user would recognize as their work. Clients may auto-approve
	 * these, so an operation belongs here only if nothing in its call graph edits a graph, compiles,
	 * dirties a package, or writes to disk. Incidental editor bookkeeping that any normal interaction
	 * with the asset performs anyway does not disqualify an operation; ld.get_graph_view is the
	 * precedent, and its registration carries the argument.
	 */
	ReadOnly
};

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

	/** Permission-gating classification. Registrants outside this module classify their own operations. */
	ESMAssistOperationImpact Impact = ESMAssistOperationImpact::Destructive;
};
