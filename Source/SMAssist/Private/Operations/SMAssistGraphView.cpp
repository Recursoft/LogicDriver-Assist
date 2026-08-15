// Copyright Recursoft LLC. All Rights Reserved.

#include "Operations/SMAssistGraphView.h"

#include "Operations/SMAssistOpKeys.h"
#include "Utilities/SMAssistUtils.h"

#include "Blueprints/SMBlueprint.h"
#include "Graph/Nodes/SMGraphNode_AnyStateNode.h"
#include "Graph/Nodes/SMGraphNode_ConduitNode.h"
#include "Graph/Nodes/SMGraphNode_LinkStateNode.h"
#include "Graph/Nodes/SMGraphNode_RerouteNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineEntryNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/Nodes/SMGraphNode_TransitionEdge.h"
#include "Graph/SMGraph.h"

#include "BlueprintEditor.h"
#include "Containers/ArrayView.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "SNodePanel.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

namespace LD::Assist::GraphView
{
	FBlueprintEditor* FindOrOpenBlueprintEditor(USMBlueprint* InBlueprint, FString& OutError)
	{
		if (!GEditor)
		{
			OutError = TEXT("GEditor unavailable; this op requires the editor to be running.");
			return nullptr;
		}

		UAssetEditorSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!AssetSubsystem)
		{
			OutError = TEXT("AssetEditorSubsystem unavailable.");
			return nullptr;
		}

		if (!AssetSubsystem->OpenEditorForAsset(InBlueprint))
		{
			OutError = FString::Printf(TEXT("Failed to open asset editor for '%s'."), *InBlueprint->GetPathName());
			return nullptr;
		}

		IAssetEditorInstance* EditorInstance = AssetSubsystem->FindEditorForAsset(InBlueprint, /*bFocusIfOpen=*/false);
		if (!EditorInstance)
		{
			OutError = FString::Printf(TEXT("Asset editor instance not found for '%s' after open."), *InBlueprint->GetPathName());
			return nullptr;
		}

		// LD's SM blueprint editor is FSMStateMachineBlueprintEditor : ISMStateMachineBlueprintEditor : FBlueprintEditor.
		// FBlueprintEditor : FAssetEditorToolkit : IAssetEditorInstance, so the static_cast back to FBlueprintEditor is
		// safe whenever the asset is a USMBlueprint (the only path this helper services).
		return static_cast<FBlueprintEditor*>(EditorInstance);
	}

	// OpenGraphAndBringToFront accepts a bound local graph exactly as double-clicking a transition does, so
	// opening one instead of the root graph is the only difference between the two capture callers.
	static TSharedPtr<SGraphEditor> OpenAndFocusGraph(FBlueprintEditor* InEditor, UEdGraph* InGraph, FString& OutError)
	{
		if (!InGraph)
		{
			OutError = TEXT("No graph to open.");
			return nullptr;
		}

		TSharedPtr<SGraphEditor> GraphEditor = InEditor->OpenGraphAndBringToFront(InGraph, /*bSetFocus=*/true);
		if (!GraphEditor.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to focus graph '%s' in the blueprint editor."), *InGraph->GetName());
			return nullptr;
		}
		return GraphEditor;
	}

	TSharedPtr<SGraphEditor> OpenAndFocusRootGraph(FBlueprintEditor* InEditor, USMBlueprint* InBlueprint, FString& OutError)
	{
		USMGraph* RootGraph = LD::Assist::Utils::GetRootStateMachineGraph(InBlueprint);
		if (!RootGraph)
		{
			OutError = TEXT("Blueprint has no root state machine graph.");
			return nullptr;
		}
		return OpenAndFocusGraph(InEditor, RootGraph, OutError);
	}

	void EnsureSlateLayoutReady()
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick(ESlateTickType::All);
		}
	}

	// A node widget reports a placeholder size, several times smaller than it renders at, until the panel
	// has painted it. A tick alone is not enough to guarantee that: FSlateApplication::Tick only reaches
	// DrawWindows while something keeps Slate awake, and skips the draw once it has gone idle. Every
	// measurement of a node widget goes through here first.
	static void EnsureGraphPanelDrawn(SGraphPanel* InPanel)
	{
		EnsureSlateLayoutReady();
		if (!InPanel || !FSlateApplication::IsInitialized())
		{
			return;
		}
		FSlateApplication& App = FSlateApplication::Get();
		if (const TSharedPtr<SWindow> Window = App.FindWidgetWindow(InPanel->AsShared()))
		{
			App.ForceRedrawWindow(Window.ToSharedRef());
		}
	}

	// Panel-space margin left clear on every side so framed content never touches the capture edge.
	static constexpr float GraphCaptureMarginPx = 24.0f;

	// Top of SNodePanel's fixed zoom table. Requesting above it falls back to 1:1 instead of clamping,
	// which is one of the two ways an unframed capture ends up at 1:1 with an empty frame.
	static constexpr float GraphCaptureMaxZoom = 2.0f;

	// A focused node fills the frame at the fit zoom, which loses the neighbors that give it meaning.
	static constexpr float GraphCaptureFocusMaxZoom = 1.0f;

	// Grid the measuring sweep divides a graph into. Only tiles holding nodes are visited, so the real
	// cost tracks the node count and this bound just has to be past any graph anyone would author: 32
	// tiles is roughly 35000 by 30000 graph units. A node outside the grid would measure as a placeholder.
	static constexpr int32 GraphMeasureMaxTilesPerAxis = 32;

	// Draws a tile is given to settle before the sweep moves on. Nodes that draw property rows take a few.
	static constexpr int32 GraphMeasureMaxPassesPerTile = 8;

	// Ceiling on the sweep's draws across all tiles. The per-tile cap alone bounds nothing useful, because
	// occupied tiles scale with the node count: a few hundred spread-out nodes would otherwise sit on a
	// redraw loop for minutes with nothing reported to the caller.
	static constexpr int32 GraphMeasureMaxTotalDraws = 256;

	// Steps the zoom walk is allowed, comfortably past SNodePanel's 20-entry table.
	static constexpr int32 GraphZoomWalkMaxSteps = 32;

	// Framing passes before the fit is taken as good enough. A pass is a draw, a measure, and a reframe.
	static constexpr int32 GraphFramingMaxPasses = 12;

	// Graph-space bounds of the node widgets the panel has realized. Mirrors SNodePanel::GetBoundsForNodes,
	// which is protected. The public GetBoundsForSelectedNodes would mean selecting every node and each one
	// then draws a selection highlight into the capture.
	static bool GetGraphNodeBounds(const SGraphPanel* InPanel, TConstArrayView<const UEdGraphNode*> InNodes, FVector2f& OutMin, FVector2f& OutMax)
	{
		OutMin = FVector2f(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
		OutMax = FVector2f(TNumericLimits<float>::Lowest(), TNumericLimits<float>::Lowest());

		bool bValid = false;
		for (const UEdGraphNode* Node : InNodes)
		{
			if (!Node)
			{
				continue;
			}
			const TSharedPtr<SGraphNode> Widget = InPanel->GetNodeWidgetFromGuid(Node->NodeGuid);
			if (!Widget.IsValid())
			{
				continue;
			}
			const FVector2f Lower = Widget->GetPosition2f();
			const FVector2f Upper = Lower + Widget->GetDesiredSize();
			OutMin.X = FMath::Min(OutMin.X, Lower.X);
			OutMin.Y = FMath::Min(OutMin.Y, Lower.Y);
			OutMax.X = FMath::Max(OutMax.X, Upper.X);
			OutMax.Y = FMath::Max(OutMax.Y, Upper.Y);
			bValid = true;
		}
		return bValid;
	}

	// Draws until the given nodes stop resizing under the current view. One paint only gets a node part of
	// the way, because its property rows keep expanding it over the next few. Reports the draws it spent so
	// a caller sweeping many tiles can hold a budget across all of them, and whether the sizes actually
	// stopped moving: running out of passes leaves them part-grown, which reads as a smaller node.
	static int32 SettleNodeBounds(SGraphPanel* InPanel, TConstArrayView<const UEdGraphNode*> InNodes, int32 InMaxPasses, bool& bOutSettled)
	{
		FVector2f LastMin = FVector2f::ZeroVector;
		FVector2f LastMax = FVector2f::ZeroVector;
		bool bMeasured = false;

		bOutSettled = false;
		int32 DrawCount = 0;
		for (int32 PassIdx = 0; PassIdx < InMaxPasses; ++PassIdx)
		{
			EnsureGraphPanelDrawn(InPanel);
			++DrawCount;

			FVector2f Min = FVector2f::ZeroVector;
			FVector2f Max = FVector2f::ZeroVector;
			if (!GetGraphNodeBounds(InPanel, InNodes, Min, Max))
			{
				// SGraphPanel drops every node widget the moment the graph changes and rebuilds them from
				// an active timer, which only runs inside a paint. So an empty read means the panel has not
				// caught up yet, not that there is nothing to measure, and drawing again is what fixes it.
				// Only an empty node list is genuinely settled; running out of passes here leaves
				// bOutSettled false so the caller reports it rather than passing off unmeasured nodes.
				if (InNodes.IsEmpty())
				{
					bOutSettled = true;
					break;
				}
				continue;
			}
			if (bMeasured && Min.Equals(LastMin) && Max.Equals(LastMax))
			{
				bOutSettled = true;
				break;
			}

			LastMin = Min;
			LastMax = Max;
			bMeasured = true;
		}
		return DrawCount;
	}

	static bool SettleGraphNodeSizes(SGraphPanel* InPanel, const UEdGraph* InGraph)
	{
		if (!InPanel || !InGraph)
		{
			return true;
		}

		TArray<const UEdGraphNode*> Nodes;
		Nodes.Reserve(InGraph->Nodes.Num());
		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			Nodes.Add(Node);
		}

		bool bSettled = false;
		SettleNodeBounds(InPanel, Nodes, GraphMeasureMaxPassesPerTile, bSettled);
		return bSettled;
	}

	// The panel's zoom table is fixed and private, and it resolves a request up to the next level, which
	// overflows the frame the caller just sized. Walk the table upward instead: adding an epsilon to a
	// level's own amount always resolves to the next level, so the last one at or under the target is the
	// one that fits. The walk also never trips the above-the-table fallback to 1:1. The epsilon on the
	// opening request matters too: a literal zero means "first display" to RestoreViewSettings and queues
	// a deferred zoom to extents instead of selecting the widest level.
	// Returns the table's widest level when the target is under it, which is above the target rather than
	// at or under it. The graph is then wider than any available zoom and cropping is the only outcome.
	static float ApplyLargestZoomAtOrUnder(const TSharedRef<SGraphEditor>& InGraphEditor, SGraphPanel* InPanel, float InTargetZoom)
	{
		const FVector2f ViewOffset = InPanel->GetViewOffset();
		InGraphEditor->SetViewLocation(ViewOffset, UE_KINDA_SMALL_NUMBER);

		float Applied = InPanel->GetZoomAmount();
		for (int32 StepIdx = 0; StepIdx < GraphZoomWalkMaxSteps; ++StepIdx)
		{
			InGraphEditor->SetViewLocation(ViewOffset, Applied + UE_KINDA_SMALL_NUMBER);
			const float Next = InPanel->GetZoomAmount();
			if (Next <= Applied || Next > InTargetZoom)
			{
				break;
			}
			Applied = Next;
		}

		InGraphEditor->SetViewLocation(ViewOffset, Applied);
		return Applied;
	}

	// Frames a graph-space rect without SGraphEditor::ZoomToFit, which cannot serve either half of this.
	// ZoomToFit refuses to zoom in past 1:1 because SNodePanel::ZoomToLocation only searches zoom-out
	// levels, so a small graph in a large panel keeps most of the frame empty. Its scroll also runs on an
	// active timer interpolating against real delta time, which a tight Slate pump starves, leaving the
	// view wherever it started. Setting offset and zoom directly makes the view final on return.
	static void FrameGraphRect(
		const TSharedRef<SGraphEditor>& InGraphEditor,
		SGraphPanel* InPanel,
		const FVector2f& InMin,
		const FVector2f& InMax,
		float InMaxZoom)
	{
		const FVector2f PanelSize = InPanel->GetTickSpaceGeometry().GetLocalSize();
		if (PanelSize.X <= 0.0f || PanelSize.Y <= 0.0f)
		{
			return;
		}

		// The breadcrumb title bar is an overlay sibling covering the top of the panel, so the area it
		// hides has to come out of the fit or the topmost nodes land behind it.
		float TopInset = 0.0f;
		if (const TSharedPtr<SWidget> TitleBar = InGraphEditor->GetTitleBar())
		{
			TopInset = TitleBar->GetTickSpaceGeometry().GetLocalSize().Y;
		}
		TopInset = FMath::Clamp(TopInset, 0.0f, PanelSize.Y * 0.5f);

		const FVector2f Available(
			PanelSize.X - 2.0f * GraphCaptureMarginPx,
			PanelSize.Y - TopInset - 2.0f * GraphCaptureMarginPx);
		if (Available.X <= 0.0f || Available.Y <= 0.0f)
		{
			return;
		}

		const float ExtentX = FMath::Max(InMax.X - InMin.X, 1.0f);
		const float ExtentY = FMath::Max(InMax.Y - InMin.Y, 1.0f);
		const float TargetZoom = FMath::Min3(Available.X / ExtentX, Available.Y / ExtentY, FMath::Min(InMaxZoom, GraphCaptureMaxZoom));
		const float AppliedZoom = ApplyLargestZoomAtOrUnder(InGraphEditor, InPanel, TargetZoom);
		if (AppliedZoom <= 0.0f)
		{
			return;
		}

		// SNodePanel maps graph to panel space as (Graph - ViewOffset) * Zoom, so solving that for the
		// offset puts the content's center on the center of what the title bar leaves visible.
		const FVector2f VisibleCenter(PanelSize.X * 0.5f, TopInset + (PanelSize.Y - TopInset) * 0.5f);
		const FVector2f GraphCenter = (InMin + InMax) * 0.5f;
		InGraphEditor->SetViewLocation(GraphCenter - VisibleCenter / AppliedZoom, AppliedZoom);
	}

	// Fitting a graph is a fixed point rather than one calculation: a node's rendered size depends on the
	// panel's detail level, the detail level depends on the zoom, and the zoom is what fitting picks.
	static void FrameGraphPanel(
		const TSharedRef<SGraphEditor>& InGraphEditor,
		SGraphPanel* InPanel,
		const UEdGraph* InGraph,
		const UEdGraphNode* InFocusNode)
	{
		if (!InPanel || !InGraph)
		{
			return;
		}

		TArray<const UEdGraphNode*> Nodes;
		float MaxZoom = GraphCaptureMaxZoom;
		if (InFocusNode)
		{
			Nodes.Add(InFocusNode);
			MaxZoom = GraphCaptureFocusMaxZoom;
		}
		else
		{
			Nodes.Reserve(InGraph->Nodes.Num());
			for (const UEdGraphNode* Node : InGraph->Nodes)
			{
				Nodes.Add(Node);
			}
		}

		// Two passes back as well as one, because the loop can reach a stable alternation rather than a
		// stable view: crossing a detail-level boundary resizes the nodes, which moves the fit back across
		// it. Settling on the wider of the two ends the capture on the zoom that frames every node, where
		// the tighter one crops.
		FVector2f LastOffset = FVector2f::ZeroVector;
		FVector2f PreviousOffset = FVector2f::ZeroVector;
		float LastZoom = 0.0f;
		float PreviousZoom = 0.0f;
		int32 AppliedCount = 0;

		for (int32 PassIdx = 0; PassIdx < GraphFramingMaxPasses; ++PassIdx)
		{
			EnsureGraphPanelDrawn(InPanel);

			FVector2f Min = FVector2f::ZeroVector;
			FVector2f Max = FVector2f::ZeroVector;
			if (!GetGraphNodeBounds(InPanel, Nodes, Min, Max))
			{
				break;
			}
			FrameGraphRect(InGraphEditor, InPanel, Min, Max, MaxZoom);

			const FVector2f Offset = InPanel->GetViewOffset();
			const float Zoom = InPanel->GetZoomAmount();
			if (AppliedCount >= 1 && Offset.Equals(LastOffset) && FMath::IsNearlyEqual(Zoom, LastZoom))
			{
				break;
			}
			if (AppliedCount >= 2 && Offset.Equals(PreviousOffset) && FMath::IsNearlyEqual(Zoom, PreviousZoom))
			{
				if (LastZoom < Zoom)
				{
					InGraphEditor->SetViewLocation(LastOffset, LastZoom);
				}
				break;
			}

			PreviousOffset = LastOffset;
			PreviousZoom = LastZoom;
			LastOffset = Offset;
			LastZoom = Zoom;
			++AppliedCount;
		}

		EnsureGraphPanelDrawn(InPanel);
	}

	// Sizes are quoted at 1:1 throughout the op descriptions, and a node renders a collapsed box at the
	// panel's lower detail levels, so this is both the documented reference and the safe measuring zoom.
	static constexpr float GraphMeasureZoom = 1.0f;

	// Bounds of the authored node positions. Reads UEdGraphNode rather than the widgets, which is the
	// point: this runs to decide where to look before any widget can be trusted to report a size.
	static bool GetGraphLogicalBounds(const UEdGraph* InGraph, FVector2f& OutMin, FVector2f& OutMax)
	{
		OutMin = FVector2f(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
		OutMax = FVector2f(TNumericLimits<float>::Lowest(), TNumericLimits<float>::Lowest());

		bool bValid = false;
		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			const FVector2f Position(static_cast<float>(Node->NodePosX), static_cast<float>(Node->NodePosY));
			OutMin.X = FMath::Min(OutMin.X, Position.X);
			OutMin.Y = FMath::Min(OutMin.Y, Position.Y);
			OutMax.X = FMath::Max(OutMax.X, Position.X);
			OutMax.Y = FMath::Max(OutMax.Y, Position.Y);
			bValid = true;
		}
		return bValid;
	}

	// The panel paints only what the view intersects, so a graph too large for one screen leaves its
	// off-screen nodes unpainted and unmeasurable. Walking the authored positions in panel-sized tiles
	// reaches all of them, and tiling on the positions alone is enough: a node whose corner is inside a
	// tile intersects it, so it paints. Empty tiles are skipped, which is most of them on a sparse graph.
	static void SweepGraphPanelForNodeSizes(
		const TSharedRef<SGraphEditor>& InGraphEditor,
		SGraphPanel* InPanel,
		const UEdGraph* InGraph,
		TArray<FString>& OutWarnings)
	{
		FVector2f Min = FVector2f::ZeroVector;
		FVector2f Max = FVector2f::ZeroVector;
		if (!GetGraphLogicalBounds(InGraph, Min, Max))
		{
			return;
		}

		const FVector2f PanelSize = InPanel->GetTickSpaceGeometry().GetLocalSize();
		if (PanelSize.X <= 0.0f || PanelSize.Y <= 0.0f)
		{
			OutWarnings.Add(TEXT("The graph panel reported no size, so node widgets could not be measured. Reported sizes and overlaps are unreliable."));
			return;
		}

		InGraphEditor->SetViewLocation(InPanel->GetViewOffset(), GraphMeasureZoom);
		const float MeasureZoom = InPanel->GetZoomAmount();
		if (MeasureZoom <= 0.0f)
		{
			OutWarnings.Add(TEXT("The graph panel reported no zoom, so node widgets could not be measured. Reported sizes and overlaps are unreliable."));
			return;
		}

		const FVector2f TileSpan = PanelSize / MeasureZoom;
		const int32 SpannedTilesX = FMath::CeilToInt((Max.X - Min.X) / TileSpan.X);
		const int32 SpannedTilesY = FMath::CeilToInt((Max.Y - Min.Y) / TileSpan.Y);
		const int32 TilesX = FMath::Clamp(SpannedTilesX, 1, GraphMeasureMaxTilesPerAxis);
		const int32 TilesY = FMath::Clamp(SpannedTilesY, 1, GraphMeasureMaxTilesPerAxis);

		// Nodes past the grid are clamped into an edge tile below, and that tile's view does not reach them,
		// so they are never painted. Every other way this sweep can under-measure reports itself; without
		// this one the caller is handed placeholder sizes and an empty warnings list.
		if (SpannedTilesX > TilesX || SpannedTilesY > TilesY)
		{
			OutWarnings.Add(FString::Printf(
				TEXT("The graph is larger than the %dx%d screens measuring covers, so nodes past that keep placeholder sizes: their reported sizes are too small and overlaps involving them are not reported at all. Run ld.layout_states to pack the graph back down, or split it into referenced sub-state-machines."),
				GraphMeasureMaxTilesPerAxis, GraphMeasureMaxTilesPerAxis));
		}

		TMap<int32, TArray<const UEdGraphNode*>> TileToNodes;
		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			const int32 TileX = FMath::Clamp(FMath::FloorToInt((static_cast<float>(Node->NodePosX) - Min.X) / TileSpan.X), 0, TilesX - 1);
			const int32 TileY = FMath::Clamp(FMath::FloorToInt((static_cast<float>(Node->NodePosY) - Min.Y) / TileSpan.Y), 0, TilesY - 1);
			TileToNodes.FindOrAdd(TileY * TilesX + TileX).Add(Node);
		}

		// Each tile settles against its own nodes rather than the whole graph, which would stop as soon as
		// the tiles already visited stopped moving and leave the rest at placeholder sizes.
		int32 DrawBudget = GraphMeasureMaxTotalDraws;
		int32 TilesVisited = 0;
		int32 UnsettledTiles = 0;

		for (const TPair<int32, TArray<const UEdGraphNode*>>& Tile : TileToNodes)
		{
			if (DrawBudget <= 0)
			{
				OutWarnings.Add(FString::Printf(
					TEXT("Measuring stopped at the %d draw budget with %d of %d areas of the graph unvisited. Nodes there keep placeholder sizes, so their reported sizes are too small and overlaps involving them are not reported at all. Run ld.layout_states to pack the graph back down, or split it into referenced sub-state-machines."),
					GraphMeasureMaxTotalDraws, TileToNodes.Num() - TilesVisited, TileToNodes.Num()));
				break;
			}

			const FVector2f TileMin(
				Min.X + static_cast<float>(Tile.Key % TilesX) * TileSpan.X,
				Min.Y + static_cast<float>(Tile.Key / TilesX) * TileSpan.Y);
			InGraphEditor->SetViewLocation(TileMin, MeasureZoom);

			bool bSettled = false;
			DrawBudget -= SettleNodeBounds(InPanel, Tile.Value, FMath::Min(GraphMeasureMaxPassesPerTile, DrawBudget), bSettled);
			++TilesVisited;
			UnsettledTiles += bSettled ? 0 : 1;
		}

		// A tile the budget cut short mid-growth is the quieter failure: it was visited, so the loop above
		// has nothing to report, but its nodes measure smaller than they render.
		if (UnsettledTiles > 0)
		{
			OutWarnings.Add(FString::Printf(
				TEXT("%d of %d areas of the graph were still resizing when measuring stopped, so nodes there may report sizes smaller than they render at."),
				UnsettledTiles, TileToNodes.Num()));
		}
	}

	struct FMeasuredNode
	{
		const UEdGraphNode* Node = nullptr;
		FString Title;
		FVector2f Min = FVector2f::ZeroVector;
		FVector2f Max = FVector2f::ZeroVector;
	};

	static void CollectMeasuredNodes(
		const UEdGraph* InGraph,
		const TMap<const UEdGraphNode*, TSharedRef<SGraphNode>>& InNodeToWidget,
		TFunctionRef<bool(const UEdGraphNode*)> InAccept,
		TArray<FMeasuredNode>& OutMeasured)
	{
		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			if (!Node || !InAccept(Node))
			{
				continue;
			}
			const TSharedRef<SGraphNode>* WidgetPtr = InNodeToWidget.Find(Node);
			if (!WidgetPtr)
			{
				continue;
			}

			FMeasuredNode Entry;
			Entry.Node = Node;
			Entry.Title = (*WidgetPtr)->GetEditableNodeTitleAsText().ToString();
			Entry.Min = (*WidgetPtr)->GetPosition2f();
			Entry.Max = Entry.Min + (*WidgetPtr)->GetDesiredSizeForMarquee2f();
			OutMeasured.Add(MoveTemp(Entry));
		}
	}

	static void BuildOverlapPairs(const TArray<FMeasuredNode>& InMeasured, TArray<TSharedPtr<FJsonValue>>& OutOverlaps)
	{
		for (int32 FirstIdx = 0; FirstIdx < InMeasured.Num(); ++FirstIdx)
		{
			for (int32 SecondIdx = FirstIdx + 1; SecondIdx < InMeasured.Num(); ++SecondIdx)
			{
				const FMeasuredNode& First = InMeasured[FirstIdx];
				const FMeasuredNode& Second = InMeasured[SecondIdx];

				const float OverlapX = FMath::Min(First.Max.X, Second.Max.X) - FMath::Max(First.Min.X, Second.Min.X);
				const float OverlapY = FMath::Min(First.Max.Y, Second.Max.Y) - FMath::Max(First.Min.Y, Second.Min.Y);
				if (OverlapX <= 0.0f || OverlapY <= 0.0f)
				{
					continue;
				}

				const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(Args::FirstNodeGuid, First.Node->NodeGuid.ToString());
				Entry->SetStringField(Args::FirstTitleText, First.Title);
				Entry->SetStringField(Args::SecondNodeGuid, Second.Node->NodeGuid.ToString());
				Entry->SetStringField(Args::SecondTitleText, Second.Title);
				Entry->SetArrayField(Args::OverlapExtent, Vec2fToJsonArray(FVector2f(OverlapX, OverlapY)));
				OutOverlaps.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
	}

	void BuildOverlapJson(
		const UEdGraph* InGraph,
		const TMap<const UEdGraphNode*, TSharedRef<SGraphNode>>& InNodeToWidget,
		TArray<TSharedPtr<FJsonValue>>& OutNodeOverlaps,
		TArray<TSharedPtr<FJsonValue>>& OutTransitionOverlaps)
	{
		// Exactly the set BuildLayoutInputForGraph positions, so every pair reported here is one a layout
		// can actually clear. A comment is drawn to enclose the states it groups, so it overlaps every one
		// of them by design, and the entry node is never moved.
		TArray<FMeasuredNode> FlowNodes;
		CollectMeasuredNodes(InGraph, InNodeToWidget,
			[](const UEdGraphNode* InNode)
			{
				return !InNode->IsA<USMGraphNode_TransitionEdge>()
					&& !InNode->IsA<USMGraphNode_RerouteNode>()
					&& !InNode->IsA<USMGraphNode_StateMachineEntryNode>()
					&& !InNode->IsA<UEdGraphNode_Comment>();
			},
			FlowNodes);
		BuildOverlapPairs(FlowNodes, OutNodeOverlaps);

		// Reported apart from the flow nodes because it is a different defect with a different remedy. A
		// marker brushing a state box is normal and goes unreported, but two markers stacked on each other
		// hide a transition outright, and no amount of state spacing fixes it: the cure is a reroute or a
		// different route for one of the edges. Reroutes are measured alongside the transition markers
		// rather than skipped, because adding one is the remedy this reports, and a remedy that lands on
		// another marker has to be visible too.
		TArray<FMeasuredNode> EdgeMarkers;
		CollectMeasuredNodes(InGraph, InNodeToWidget,
			[](const UEdGraphNode* InNode)
			{
				return InNode->IsA<USMGraphNode_TransitionEdge>() || InNode->IsA<USMGraphNode_RerouteNode>();
			},
			EdgeMarkers);
		BuildOverlapPairs(EdgeMarkers, OutTransitionOverlaps);
	}

	void MeasureGraphNodeWidgets(
		const TSharedRef<SGraphEditor>& InGraphEditor,
		SGraphPanel* InPanel,
		const UEdGraph* InGraph,
		TArray<FString>& OutWarnings)
	{
		if (!InPanel || !InGraph)
		{
			return;
		}

		// Snapshotted before anything ticks. A graph editor opened this frame has a zoom-to-extents queued
		// for its first display, and the draw below would consume that flag and start the fit interpolating,
		// so a snapshot taken afterwards would capture a half-finished view and restore the graph to it.
		// HasDeferredZoomDestination only covers a fit still queued, so an already-running one is caught by
		// its target rect. Restoring a concrete zoom cancels either, hence re-arming rather than restoring.
		FVector2f ZoomTargetMin = FVector2f::ZeroVector;
		FVector2f ZoomTargetMax = FVector2f::ZeroVector;
		const bool bRestoreDeferredZoom = InPanel->HasDeferredZoomDestination()
			|| InPanel->GetZoomTargetRect(ZoomTargetMin, ZoomTargetMax);
		const FVector2f RestoreOffset = InPanel->GetViewOffset();
		const float RestoreZoom = InPanel->GetZoomAmount();

		// The sweep sizes its tiles off the panel's geometry, and a panel carries none until it has
		// painted: a graph opened this frame would measure zero, skip the sweep, and silently report every
		// off-screen node at its placeholder size.
		EnsureGraphPanelDrawn(InPanel);

		SweepGraphPanelForNodeSizes(InGraphEditor, InPanel, InGraph, OutWarnings);
		if (!SettleGraphNodeSizes(InPanel, InGraph))
		{
			OutWarnings.Add(TEXT("Node widgets were still resizing when measuring stopped, so some may report sizes smaller than they render at."));
		}

		InGraphEditor->SetViewLocation(RestoreOffset, bRestoreDeferredZoom ? -1.0f : RestoreZoom);
	}

	FSMAssistOperationResult CaptureGraphToPng(
		FBlueprintEditor* InEditor,
		UEdGraph* InGraph,
		USMBlueprint* InBlueprint,
		const UEdGraphNode* InFocusNode,
		bool bClipToPanel,
		bool bFitToContent,
		const FString& InOutputSubdir,
		FString InPrefix,
		const FString& InDefaultPrefixBase)
	{
		FString GraphError;
		TSharedPtr<SGraphEditor> GraphEditor = OpenAndFocusGraph(InEditor, InGraph, GraphError);
		if (!GraphEditor.IsValid())
		{
			return FSMAssistOperationResult::MakeError(GraphError);
		}

		EnsureSlateLayoutReady();

		SGraphPanel* Panel = GraphEditor->GetGraphPanel();
		if (!Panel)
		{
			return FSMAssistOperationResult::MakeError(TEXT("Graph editor has no panel."));
		}

		// Focusing a single node and fitting to all content are mutually exclusive framing intents; a
		// requested focus node wins. Both paths leave the panel on its final view before capture.
		if (InFocusNode)
		{
			// Selected rather than jumped to, because JumpToNode's deferred pan would override the
			// framing below on the next tick. The highlight is what marks the node in the capture.
			// The cast is to reach SGraphEditor::SetNodeSelection, which takes a mutable node and only
			// records it in the panel's selection set.
			GraphEditor->ClearSelectionSet();
			GraphEditor->SetNodeSelection(const_cast<UEdGraphNode*>(InFocusNode), /*bSelect=*/true);
			FrameGraphPanel(GraphEditor.ToSharedRef(), Panel, InGraph, InFocusNode);
		}
		else if (bFitToContent)
		{
			FrameGraphPanel(GraphEditor.ToSharedRef(), Panel, InGraph, /*InFocusNode=*/nullptr);
		}

		TSharedPtr<SWidget> TargetWidget;
		if (bClipToPanel)
		{
			TargetWidget = Panel->AsShared();
		}
		else
		{
			// Capture the entire blueprint editor window the panel is parented in.
			TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(Panel->AsShared());
			if (!Window.IsValid())
			{
				return FSMAssistOperationResult::MakeError(TEXT("Could not locate the editor window for capture."));
			}
			TargetWidget = Window;
		}

		TArray<FColor> ColorData;
		FIntVector OutSize(0, 0, 0);
		if (!FSlateApplication::Get().TakeScreenshot(TargetWidget.ToSharedRef(), ColorData, OutSize))
		{
			return FSMAssistOperationResult::MakeError(TEXT("Slate screenshot capture failed."));
		}
		if (OutSize.X <= 0 || OutSize.Y <= 0 || ColorData.Num() == 0)
		{
			return FSMAssistOperationResult::MakeError(TEXT("Screenshot returned an empty image."));
		}

		TArray64<uint8> PngBytes;
		FImageUtils::PNGCompressImageArray(
			OutSize.X, OutSize.Y,
			TArrayView64<const FColor>(ColorData.GetData(), ColorData.Num()),
			PngBytes);

		if (PngBytes.Num() == 0)
		{
			return FSMAssistOperationResult::MakeError(TEXT("PNG encoding produced zero bytes."));
		}

		if (!InPrefix.IsEmpty() && !LD::Assist::Utils::IsSafeFileStem(InPrefix))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'prefix' must be a bare filename with no path separators or '..'."));
		}

		if (InPrefix.IsEmpty())
		{
			InPrefix = FString::Printf(TEXT("%s_%s"), *InDefaultPrefixBase, *FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S")));
		}

		const FString FileName = InPrefix + TEXT(".png");
		FString TargetDir;
		FString PathError;
		if (!LD::Assist::Utils::ResolveContainedScreenshotsDir(InOutputSubdir, TargetDir, PathError))
		{
			return FSMAssistOperationResult::MakeError(PathError);
		}
		IFileManager::Get().MakeDirectory(*TargetDir, /*Tree=*/true);
		const FString TargetPath = FPaths::Combine(TargetDir, FileName);

		if (!FFileHelper::SaveArrayToFile(PngBytes, *TargetPath))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to write PNG to '%s'."), *TargetPath));
		}

		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(Args::AssetPath, InBlueprint->GetPathName());
		Payload->SetStringField(Args::Path, TargetPath);
		Payload->SetNumberField(Args::Width, OutSize.X);
		Payload->SetNumberField(Args::Height, OutSize.Y);
		Payload->SetNumberField(Args::Bytes, PngBytes.Num());
		Payload->SetStringField(Args::Mime, TEXT("image/png"));
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	TArray<TSharedPtr<FJsonValue>> Vec2fToJsonArray(const FVector2f& InVec)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Add(MakeShared<FJsonValueNumber>(InVec.X));
		Array.Add(MakeShared<FJsonValueNumber>(InVec.Y));
		return Array;
	}

	TArray<TSharedPtr<FJsonValue>> ColorToJsonArray(const FLinearColor& InColor)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Add(MakeShared<FJsonValueNumber>(InColor.R));
		Array.Add(MakeShared<FJsonValueNumber>(InColor.G));
		Array.Add(MakeShared<FJsonValueNumber>(InColor.B));
		Array.Add(MakeShared<FJsonValueNumber>(InColor.A));
		return Array;
	}

	const TCHAR* ResolveNodeKind(const UEdGraphNode* InNode)
	{
		if (InNode->IsA<USMGraphNode_StateMachineEntryNode>())
		{
			return TEXT("entry");
		}
		if (InNode->IsA<USMGraphNode_TransitionEdge>())
		{
			return TEXT("transition");
		}
		if (InNode->IsA<USMGraphNode_AnyStateNode>())
		{
			return TEXT("any_state");
		}
		if (InNode->IsA<USMGraphNode_LinkStateNode>())
		{
			return TEXT("link_state");
		}
		if (InNode->IsA<USMGraphNode_RerouteNode>())
		{
			return TEXT("reroute");
		}
		if (InNode->IsA<USMGraphNode_StateMachineStateNode>())
		{
			return TEXT("state_machine_state");
		}
		if (InNode->IsA<USMGraphNode_ConduitNode>())
		{
			return TEXT("conduit");
		}
		if (InNode->IsA<USMGraphNode_StateNodeBase>())
		{
			return TEXT("state");
		}
		return TEXT("unknown");
	}

	void BuildPinJson(const UEdGraphNode* InNode, TArray<TSharedPtr<FJsonValue>>& OutPins)
	{
		for (const UEdGraphPin* Pin : InNode->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(Args::PinId, Pin->PinId.ToString());
			Entry->SetStringField(Args::PinName, Pin->PinName.ToString());
			Entry->SetStringField(Args::PinDirection, Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			OutPins.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
}
