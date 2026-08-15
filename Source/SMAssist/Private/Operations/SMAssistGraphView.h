// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Operations/SMAssistOperationResult.h"

#include "Math/Color.h"
#include "Math/Vector2D.h"

class FBlueprintEditor;
class FJsonValue;
class SGraphEditor;
class SGraphNode;
class SGraphPanel;
class UEdGraph;
class UEdGraphNode;
class USMBlueprint;

/**
 * Drives the live graph editor for the ops that read, measure, frame, or capture a graph:
 * ld.get_graph_view, ld.capture_graph_view, ld.capture_local_graph and ld.layout_states.
 * Every entry point here requires the editor to be running and must be called on the game thread.
 */
namespace LD::Assist::GraphView
{
	/** Ticks Slate so geometry authored this frame is laid out before anything reads it. */
	void EnsureSlateLayoutReady();

	/** Opens InBlueprint's asset editor if it is not already open. Null with OutError set on failure. */
	FBlueprintEditor* FindOrOpenBlueprintEditor(USMBlueprint* InBlueprint, FString& OutError);

	/** Focuses InBlueprint's root state machine graph. Null with OutError set on failure. */
	TSharedPtr<SGraphEditor> OpenAndFocusRootGraph(FBlueprintEditor* InEditor, USMBlueprint* InBlueprint, FString& OutError);

	/**
	 * Sizes every node widget in InGraph, then restores the view. Anything that could not be measured
	 * lands in OutWarnings, because a caller reading sizes or overlaps has no other way to tell an
	 * accurate answer from a partial one.
	 */
	void MeasureGraphNodeWidgets(
		const TSharedRef<SGraphEditor>& InGraphEditor,
		SGraphPanel* InPanel,
		const UEdGraph* InGraph,
		TArray<FString>& OutWarnings);

	/**
	 * Shared body of the capture ops: the only thing capture_graph_view and capture_local_graph differ on
	 * is which graph they pass in. Returns the {asset_path, path, width, height, bytes, mime} payload.
	 */
	FSMAssistOperationResult CaptureGraphToPng(
		FBlueprintEditor* InEditor,
		UEdGraph* InGraph,
		USMBlueprint* InBlueprint,
		const UEdGraphNode* InFocusNode,
		bool bClipToPanel,
		bool bFitToContent,
		const FString& InOutputSubdir,
		FString InPrefix,
		const FString& InDefaultPrefixBase);

	/**
	 * Pairs of rendered boxes that intersect, which is the failure a caller would otherwise have to
	 * capture a PNG to see. OutNodeOverlaps carries the flow nodes a layout can reposition;
	 * OutTransitionOverlaps carries transition markers and reroutes stacked on each other, which state
	 * spacing does not fix. Run MeasureGraphNodeWidgets first or both come back empty.
	 */
	void BuildOverlapJson(
		const UEdGraph* InGraph,
		const TMap<const UEdGraphNode*, TSharedRef<SGraphNode>>& InNodeToWidget,
		TArray<TSharedPtr<FJsonValue>>& OutNodeOverlaps,
		TArray<TSharedPtr<FJsonValue>>& OutTransitionOverlaps);

	/** Payload token for InNode's type, such as "state", "transition" or "conduit". */
	const TCHAR* ResolveNodeKind(const UEdGraphNode* InNode);

	/** Appends a {pin_id, pin_name, pin_direction} entry for each of InNode's pins. */
	void BuildPinJson(const UEdGraphNode* InNode, TArray<TSharedPtr<FJsonValue>>& OutPins);

	TArray<TSharedPtr<FJsonValue>> Vec2fToJsonArray(const FVector2f& InVec);

	TArray<TSharedPtr<FJsonValue>> ColorToJsonArray(const FLinearColor& InColor);
}
