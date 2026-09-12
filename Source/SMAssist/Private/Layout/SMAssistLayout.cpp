// Copyright Recursoft LLC. All Rights Reserved.

#include "Layout/SMAssistLayout.h"

#include "Algo/StableSort.h"
#include "Containers/Map.h"
#include "EdGraph/EdGraphNode.h"
#include "Misc/AssertionMacros.h"

namespace LD::Assist::Layout
{
	const TCHAR* StrategyToString(ELayoutStrategy InStrategy)
	{
		return InStrategy == ELayoutStrategy::TopToBottom ? TEXT("top_to_bottom") : TEXT("left_to_right");
	}

	bool TryParseStrategy(const FString& InValue, ELayoutStrategy& OutStrategy)
	{
		if (InValue.Equals(TEXT("left_to_right"), ESearchCase::IgnoreCase) || InValue.Equals(TEXT("lr"), ESearchCase::IgnoreCase))
		{
			OutStrategy = ELayoutStrategy::LeftToRight;
			return true;
		}
		if (InValue.Equals(TEXT("top_to_bottom"), ESearchCase::IgnoreCase) || InValue.Equals(TEXT("tb"), ESearchCase::IgnoreCase))
		{
			OutStrategy = ELayoutStrategy::TopToBottom;
			return true;
		}
		return false;
	}

	const TCHAR* LaneToString(ELayoutLane InLane)
	{
		return InLane == ELayoutLane::Side ? TEXT("side") : TEXT("main");
	}
}

namespace LD::Assist::Layout::Private
{
	// Default per-kind widget sizes used when the handler couldn't measure a real Slate widget
	// (e.g., the asset just opened and the panel hasn't had a chance to construct widgets for
	// every node yet). Numbers tuned to match typical SGraphNode_State / Conduit / Reference
	// dimensions in UE 5.7. Close enough that layout collisions remain rare in practice.
	FVector2f DefaultSizeForKind(const FString& InKind)
	{
		if (InKind == TEXT("conduit"))           return FVector2f(180.0f, 70.0f);
		if (InKind == TEXT("state_machine_state")) return FVector2f(220.0f, 90.0f);
		if (InKind == TEXT("any_state"))         return FVector2f(160.0f, 60.0f);
		if (InKind == TEXT("link_state"))        return FVector2f(160.0f, 60.0f);
		return FVector2f(200.0f, 80.0f);
	}

	bool IsSideLaneKind(const FString& InKind)
	{
		return InKind == TEXT("any_state");
	}

	float GetPrimary(const FVector2f& InPos, ELayoutStrategy InStrategy)
	{
		return InStrategy == ELayoutStrategy::LeftToRight ? InPos.X : InPos.Y;
	}

	float GetSecondary(const FVector2f& InPos, ELayoutStrategy InStrategy)
	{
		return InStrategy == ELayoutStrategy::LeftToRight ? InPos.Y : InPos.X;
	}

	FVector2f MakePos(float InPrimary, float InSecondary, ELayoutStrategy InStrategy)
	{
		return InStrategy == ELayoutStrategy::LeftToRight
			? FVector2f(InPrimary, InSecondary)
			: FVector2f(InSecondary, InPrimary);
	}

	float GetPrimaryExtent(const FVector2f& InSize, ELayoutStrategy InStrategy)
	{
		return InStrategy == ELayoutStrategy::LeftToRight ? InSize.X : InSize.Y;
	}

	float GetSecondaryExtent(const FVector2f& InSize, ELayoutStrategy InStrategy)
	{
		return InStrategy == ELayoutStrategy::LeftToRight ? InSize.Y : InSize.X;
	}

	// Origin for the flow. The entry node is never moved and takes no part in the layering. The first
	// layer starts one ColumnGap past it, and every layer's stack is centered on it across the flow. That
	// keeps the first state off the entry node and keeps the wire between them straight. An explicit
	// origin from the caller replaces this, and a graph with no entry node falls back to Start.
	FVector2f ResolveStart(const FLayoutInput& InInput)
	{
		if (InInput.bStartExplicit || InInput.EntryNodeSize.X <= 0.0f || InInput.EntryNodeSize.Y <= 0.0f)
		{
			return InInput.Start;
		}

		const float Primary = GetPrimary(InInput.EntryNodePosition, InInput.Strategy)
			+ GetPrimaryExtent(InInput.EntryNodeSize, InInput.Strategy)
			+ InInput.ColumnGap;
		const float Secondary = GetSecondary(InInput.EntryNodePosition, InInput.Strategy)
			+ GetSecondaryExtent(InInput.EntryNodeSize, InInput.Strategy) * 0.5f;
		return MakePos(Primary, Secondary, InInput.Strategy);
	}

	// Categorize input nodes into main flow vs side lane, fill in any missing widget sizes,
	// and stamp each node's bPinned flag from PinnedGuids.
	// Whether the straight line between two points passes through any node's box, ignoring the two
	// nodes the line connects. Padding widens each box so a wire grazing an edge still counts.
	bool SegmentCrossesAnyNode(
		const FVector2f& InStart,
		const FVector2f& InEnd,
		const TArray<FLayoutNode>& InPlaced,
		const FGuid& InFromGuid,
		const FGuid& InToGuid,
		float InPadding)
	{
		const FVector2f Delta = InEnd - InStart;
		for (const FLayoutNode& Node : InPlaced)
		{
			if (Node.NodeGuid == InFromGuid || Node.NodeGuid == InToGuid)
			{
				continue;
			}

			const float MinX = Node.NewPosition.X - InPadding;
			const float MinY = Node.NewPosition.Y - InPadding;
			const float MaxX = Node.NewPosition.X + Node.WidgetSize.X + InPadding;
			const float MaxY = Node.NewPosition.Y + Node.WidgetSize.Y + InPadding;

			// Liang-Barsky: clip the segment against each of the four box edges in turn, narrowing the
			// portion that could still be inside. Anything left when all four are done is an overlap.
			const float Directions[4] = { -Delta.X, Delta.X, -Delta.Y, Delta.Y };
			const float Distances[4] = {
				InStart.X - MinX, MaxX - InStart.X, InStart.Y - MinY, MaxY - InStart.Y };

			float EnterT = 0.0f;
			float ExitT = 1.0f;
			bool bClippedOut = false;
			for (int32 EdgeIdx = 0; EdgeIdx < 4; ++EdgeIdx)
			{
				if (FMath::IsNearlyZero(Directions[EdgeIdx]))
				{
					if (Distances[EdgeIdx] < 0.0f)
					{
						bClippedOut = true;
						break;
					}
					continue;
				}

				const float Ratio = Distances[EdgeIdx] / Directions[EdgeIdx];
				if (Directions[EdgeIdx] < 0.0f)
				{
					if (Ratio > ExitT)
					{
						bClippedOut = true;
						break;
					}
					EnterT = FMath::Max(EnterT, Ratio);
				}
				else
				{
					if (Ratio < EnterT)
					{
						bClippedOut = true;
						break;
					}
					ExitT = FMath::Min(ExitT, Ratio);
				}
			}

			if (!bClippedOut && EnterT < ExitT)
			{
				return true;
			}
		}
		return false;
	}

	// The path the editor draws for one transition: the source center, every reroute on its rail in
	// order, then the destination center. Both the crossing count and the marker count read the drawn
	// path, so they build it the same way here rather than each deciding for itself what gets drawn.
	void BuildDrawnPolyline(
		const FLayoutEdge& InEdge,
		const TMap<FGuid, const FLayoutNode*>& InNodeByGuid,
		const TMap<FGuid, TArray<const FLayoutReroute*>>& InRailByTransition,
		float InRerouteSize,
		TArray<FVector2f>& OutPoints)
	{
		OutPoints.Reset();

		const FLayoutNode* const* FromPtr = InNodeByGuid.Find(InEdge.FromGuid);
		const FLayoutNode* const* ToPtr = InNodeByGuid.Find(InEdge.ToGuid);
		if (!FromPtr || !ToPtr)
		{
			return;
		}

		OutPoints.Add((*FromPtr)->NewPosition + (*FromPtr)->WidgetSize * 0.5f);
		if (const TArray<const FLayoutReroute*>* Rail = InRailByTransition.Find(InEdge.TransitionGuid))
		{
			for (const FLayoutReroute* Reroute : *Rail)
			{
				OutPoints.Add(Reroute->Position + FVector2f(InRerouteSize, InRerouteSize) * 0.5f);
			}
		}
		OutPoints.Add((*ToPtr)->NewPosition + (*ToPtr)->WidgetSize * 0.5f);
	}

	// Rails indexed by the transition they carry, each in the order it is walked from source to
	// destination.
	TMap<FGuid, TArray<const FLayoutReroute*>> BuildRailIndex(const TArray<FLayoutReroute>& InReroutes)
	{
		TMap<FGuid, TArray<const FLayoutReroute*>> RailByTransition;
		for (const FLayoutReroute& Reroute : InReroutes)
		{
			RailByTransition.FindOrAdd(Reroute.TransitionGuid).Add(&Reroute);
		}
		for (TPair<FGuid, TArray<const FLayoutReroute*>>& Rail : RailByTransition)
		{
			Rail.Value.Sort([](const FLayoutReroute& A, const FLayoutReroute& B)
			{
				return A.ChainIndex < B.ChainIndex;
			});
		}
		return RailByTransition;
	}

	// Whether a marker centered on InMidpoint would be drawn over any node's box.
	bool MarkerLandsOnNode(
		const FVector2f& InMidpoint,
		const TArray<FLayoutNode>& InPlaced,
		const FVector2f& InMarkerSize)
	{
		if (InMarkerSize.X <= 0.0f || InMarkerSize.Y <= 0.0f)
		{
			return false;
		}

		const FVector2f MarkerMin = InMidpoint - InMarkerSize * 0.5f;
		const FVector2f MarkerMax = InMidpoint + InMarkerSize * 0.5f;
		for (const FLayoutNode& Node : InPlaced)
		{
			const FVector2f NodeMin = Node.NewPosition;
			const FVector2f NodeMax = NodeMin + Node.WidgetSize;
			if (MarkerMin.X < NodeMax.X && NodeMin.X < MarkerMax.X
				&& MarkerMin.Y < NodeMax.Y && NodeMin.Y < MarkerMax.Y)
			{
				return true;
			}
		}
		return false;
	}

	// How many transition markers are drawn on top of a state. The editor puts a marker at the midpoint
	// of every segment it draws, so a railed transition contributes one per segment. A marker there
	// cannot be read or clicked and it clutters the state underneath, and no amount of state spacing
	// fixes it once the midpoint lands inside a box, because the midpoint moves with the states.
	int32 CountMarkersOverNodes(
		const TArray<FLayoutNode>& InPlaced,
		const TArray<FLayoutEdge>& InEdges,
		const TArray<FLayoutReroute>& InReroutes,
		const FVector2f& InMarkerSize,
		float InRerouteSize)
	{
		if (InMarkerSize.X <= 0.0f || InMarkerSize.Y <= 0.0f)
		{
			return 0;
		}

		TMap<FGuid, const FLayoutNode*> NodeByGuid;
		NodeByGuid.Reserve(InPlaced.Num());
		for (const FLayoutNode& Node : InPlaced)
		{
			NodeByGuid.Add(Node.NodeGuid, &Node);
		}
		const TMap<FGuid, TArray<const FLayoutReroute*>> RailByTransition = BuildRailIndex(InReroutes);

		int32 Total = 0;
		TArray<FVector2f> Points;
		for (const FLayoutEdge& Edge : InEdges)
		{
			if (Edge.FromGuid == Edge.ToGuid)
			{
				continue;
			}
			BuildDrawnPolyline(Edge, NodeByGuid, RailByTransition, InRerouteSize, Points);

			for (int32 PointIdx = 0; PointIdx + 1 < Points.Num(); ++PointIdx)
			{
				if (MarkerLandsOnNode((Points[PointIdx] + Points[PointIdx + 1]) * 0.5f, InPlaced, InMarkerSize))
				{
					++Total;
				}
			}
		}
		return Total;
	}

	// How many transitions are left drawn through a state once routing has run. Counted on the polyline
	// the editor will draw, so an edge carried on a rail is judged by the rail and not by the straight
	// line it would otherwise have taken. This is the number route_edges moves, so reporting it is what
	// lets a caller see what turning routing off just cost.
	int32 CountEdgesDrawnThroughNodes(
		const TArray<FLayoutNode>& InPlaced,
		const TArray<FLayoutEdge>& InEdges,
		const TArray<FLayoutReroute>& InReroutes,
		float InPadding,
		float InRerouteSize)
	{
		TMap<FGuid, const FLayoutNode*> NodeByGuid;
		NodeByGuid.Reserve(InPlaced.Num());
		for (const FLayoutNode& Node : InPlaced)
		{
			NodeByGuid.Add(Node.NodeGuid, &Node);
		}

		const TMap<FGuid, TArray<const FLayoutReroute*>> RailByTransition = BuildRailIndex(InReroutes);

		int32 Total = 0;
		TArray<FVector2f> Points;
		for (const FLayoutEdge& Edge : InEdges)
		{
			if (Edge.FromGuid == Edge.ToGuid)
			{
				continue;
			}
			BuildDrawnPolyline(Edge, NodeByGuid, RailByTransition, InRerouteSize, Points);

			for (int32 PointIdx = 0; PointIdx + 1 < Points.Num(); ++PointIdx)
			{
				if (SegmentCrossesAnyNode(
					Points[PointIdx], Points[PointIdx + 1], InPlaced, Edge.FromGuid, Edge.ToGuid, InPadding))
				{
					++Total;
					break;
				}
			}
		}
		return Total;
	}

	// How many transitions a reader would see drawn through a state box. This is the thing ordering is
	// trying to avoid, and it cannot be measured from the order alone, because a box has no position
	// until coordinates are assigned.
	int32 CountEdgesThroughNodes(
		const TArray<FLayoutNode>& InPlaced,
		const TArray<FLayoutEdge>& InEdges,
		float InPadding)
	{
		TMap<FGuid, const FLayoutNode*> NodeByGuid;
		NodeByGuid.Reserve(InPlaced.Num());
		for (const FLayoutNode& Node : InPlaced)
		{
			NodeByGuid.Add(Node.NodeGuid, &Node);
		}

		int32 Total = 0;
		for (const FLayoutEdge& Edge : InEdges)
		{
			if (Edge.FromGuid == Edge.ToGuid)
			{
				continue;
			}

			const FLayoutNode* const* FromPtr = NodeByGuid.Find(Edge.FromGuid);
			const FLayoutNode* const* ToPtr = NodeByGuid.Find(Edge.ToGuid);
			if (!FromPtr || !ToPtr)
			{
				continue;
			}

			const FLayoutNode& From = **FromPtr;
			const FLayoutNode& To = **ToPtr;
			if (From.Lane != ELayoutLane::Main || To.Lane != ELayoutLane::Main)
			{
				continue;
			}

			const FVector2f FromCenter = From.NewPosition + From.WidgetSize * 0.5f;
			const FVector2f ToCenter = To.NewPosition + To.WidgetSize * 0.5f;
			if (SegmentCrossesAnyNode(FromCenter, ToCenter, InPlaced, Edge.FromGuid, Edge.ToGuid, InPadding))
			{
				++Total;
			}
		}
		return Total;
	}

	// Ordering places each candidate before scoring it, so it needs the coordinate pass defined below.
	// InExtraGapAfterLayer widens single layer boundaries past ColumnGap; see WidenLayerGaps.
	void AssignCoordinates(
		TArray<FLayoutNode>& InOutMain,
		const FLayoutInput& InInput,
		const FVector2f& InStart,
		const TMap<int32, float>* InExtraGapAfterLayer = nullptr);

	void CategorizeAndPrepare(const FLayoutInput& InInput, TArray<FLayoutNode>& OutMain, TArray<FLayoutNode>& OutSide, TArray<FString>& OutWarnings)
	{
		OutMain.Reserve(InInput.Nodes.Num());

		TSet<FGuid> SeenGuids;
		for (const FLayoutNode& InNode : InInput.Nodes)
		{
			FLayoutNode Working = InNode;
			if (Working.WidgetSize.X <= 0.0f || Working.WidgetSize.Y <= 0.0f)
			{
				Working.WidgetSize = DefaultSizeForKind(Working.Kind);
			}
			Working.NewPosition = Working.OldPosition;
			Working.bPinned = InInput.PinnedGuids.Contains(Working.NodeGuid);
			Working.Layer = INDEX_NONE;

			if (IsSideLaneKind(Working.Kind))
			{
				Working.Lane = ELayoutLane::Side;
				OutSide.Add(MoveTemp(Working));
			}
			else
			{
				Working.Lane = ELayoutLane::Main;
				OutMain.Add(MoveTemp(Working));
			}
			SeenGuids.Add(InNode.NodeGuid);
		}

		for (const FGuid& PinnedGuid : InInput.PinnedGuids)
		{
			if (!SeenGuids.Contains(PinnedGuid))
			{
				OutWarnings.Add(FString::Printf(TEXT("pin_node_guid '%s' not found in graph; ignored."), *PinnedGuid.ToString()));
			}
		}
	}

	// Build directed adjacency over main-flow nodes. Drops self-loops and any edge whose source
	// is a side-lane node (AnyState fans out to every state at runtime; honoring those edges
	// would collapse the topological order to a single layer).
	//
	// OutSideLaneFed names every main node that a dropped side-lane edge pointed at. Such a node
	// has no predecessor left in the adjacency, so layering would otherwise call it unreachable
	// from the entry state. It is entered from an Any State, which is a real way in.
	void BuildAdjacency(
		const TArray<FLayoutNode>& InMain,
		const TArray<FLayoutNode>& InSide,
		const TArray<FLayoutEdge>& InEdges,
		TMap<FGuid, TArray<FGuid>>& OutSuccessors,
		TMap<FGuid, TArray<FGuid>>& OutPredecessors,
		TSet<FGuid>& OutSideLaneFed)
	{
		TSet<FGuid> MainSet;
		MainSet.Reserve(InMain.Num());
		for (const FLayoutNode& Node : InMain)
		{
			MainSet.Add(Node.NodeGuid);
			OutSuccessors.Add(Node.NodeGuid);
			OutPredecessors.Add(Node.NodeGuid);
		}

		TSet<FGuid> SideSet;
		SideSet.Reserve(InSide.Num());
		for (const FLayoutNode& Node : InSide)
		{
			SideSet.Add(Node.NodeGuid);
		}

		for (const FLayoutEdge& Edge : InEdges)
		{
			if (Edge.FromGuid == Edge.ToGuid)
			{
				continue;
			}
			if (SideSet.Contains(Edge.FromGuid))
			{
				if (MainSet.Contains(Edge.ToGuid))
				{
					OutSideLaneFed.Add(Edge.ToGuid);
				}
				continue;
			}
			if (!MainSet.Contains(Edge.FromGuid) || !MainSet.Contains(Edge.ToGuid))
			{
				continue;
			}
			OutSuccessors[Edge.FromGuid].Add(Edge.ToGuid);
			OutPredecessors[Edge.ToGuid].Add(Edge.FromGuid);
		}
	}

	// DFS for back-edges. An edge u -> v is a back-edge when v is currently on the recursion
	// stack (color == Gray). Back-edges are excluded from layer assignment so the topological
	// sort succeeds; they still render in the editor.
	TSet<TPair<FGuid, FGuid>> FindBackEdges(
		const TMap<FGuid, TArray<FGuid>>& InSuccessors,
		const TArray<FLayoutNode>& InMain,
		const FGuid& InEntryGuid,
		const TMap<FGuid, FString>& InNamesByGuid,
		TArray<FString>& OutWarnings)
	{
		enum class EColor : uint8 { White, Gray, Black };

		TMap<FGuid, EColor> Colors;
		Colors.Reserve(InMain.Num());
		for (const FLayoutNode& Node : InMain)
		{
			Colors.Add(Node.NodeGuid, EColor::White);
		}

		TSet<TPair<FGuid, FGuid>> BackEdges;

		TArray<FGuid> RootOrder;
		if (InEntryGuid.IsValid() && Colors.Contains(InEntryGuid))
		{
			RootOrder.Add(InEntryGuid);
		}
		for (const FLayoutNode& Node : InMain)
		{
			if (Node.NodeGuid != InEntryGuid)
			{
				RootOrder.Add(Node.NodeGuid);
			}
		}

		for (const FGuid& Root : RootOrder)
		{
			if (Colors[Root] != EColor::White)
			{
				continue;
			}

			TArray<TPair<FGuid, int32>> Stack;
			Stack.Add(TPair<FGuid, int32>(Root, 0));
			Colors[Root] = EColor::Gray;

			while (Stack.Num() > 0)
			{
				TPair<FGuid, int32>& Top = Stack.Last();
				const TArray<FGuid>* Children = InSuccessors.Find(Top.Key);
				if (!Children || Top.Value >= Children->Num())
				{
					Colors[Top.Key] = EColor::Black;
					Stack.Pop();
					continue;
				}

				const FGuid ChildGuid = (*Children)[Top.Value];
				++Top.Value;

				if (!Colors.Contains(ChildGuid))
				{
					continue;
				}

				EColor& ChildColor = Colors[ChildGuid];
				if (ChildColor == EColor::White)
				{
					ChildColor = EColor::Gray;
					Stack.Add(TPair<FGuid, int32>(ChildGuid, 0));
				}
				else if (ChildColor == EColor::Gray)
				{
					const TPair<FGuid, FGuid> BackEdge(Top.Key, ChildGuid);
					if (!BackEdges.Contains(BackEdge))
					{
						BackEdges.Add(BackEdge);
						const FString FromName = InNamesByGuid.Contains(BackEdge.Key) ? InNamesByGuid[BackEdge.Key] : BackEdge.Key.ToString();
						const FString ToName = InNamesByGuid.Contains(BackEdge.Value) ? InNamesByGuid[BackEdge.Value] : BackEdge.Value.ToString();
						OutWarnings.Add(FString::Printf(
							TEXT("Back-edge reversed for layering: '%s' -> '%s' (cycle preserved visually)."),
							*FromName, *ToName));
					}
				}
			}
		}

		return BackEdges;
	}

	// Topological order over the cycle-broken DAG, then layer[v] = max(layer[u]+1) over its
	// kept predecessors. Nodes reachable from the entry state, or entered from a side-lane node
	// listed in InSideLaneFed, are layered first. Unreachable nodes are placed at one layer past
	// the reachable maximum so they cluster at the right end of the flow with a warning naming them.
	void AssignLayers(
		TArray<FLayoutNode>& InOutMain,
		const TMap<FGuid, TArray<FGuid>>& InSuccessors,
		const TMap<FGuid, TArray<FGuid>>& InPredecessors,
		const TSet<TPair<FGuid, FGuid>>& InBackEdges,
		const FGuid& InEntryGuid,
		const TSet<FGuid>& InSideLaneFed,
		const TMap<FGuid, FString>& InNamesByGuid,
		TArray<FString>& OutWarnings)
	{
		auto IsKeptEdge = [&InBackEdges](const FGuid& From, const FGuid& To)
		{
			return !InBackEdges.Contains(TPair<FGuid, FGuid>(From, To));
		};

		// Compute effective in-degree (excluding back-edges).
		TMap<FGuid, int32> InDegree;
		InDegree.Reserve(InOutMain.Num());
		for (const FLayoutNode& Node : InOutMain)
		{
			int32 Count = 0;
			if (const TArray<FGuid>* Preds = InPredecessors.Find(Node.NodeGuid))
			{
				for (const FGuid& Pred : *Preds)
				{
					if (IsKeptEdge(Pred, Node.NodeGuid))
					{
						++Count;
					}
				}
			}
			InDegree.Add(Node.NodeGuid, Count);
		}

		// Kahn's algorithm seeded from entry first, then any other zero-in-degree nodes.
		TArray<FGuid> Queue;
		TSet<FGuid> Reachable;
		if (InEntryGuid.IsValid() && InDegree.Contains(InEntryGuid) && InDegree[InEntryGuid] == 0)
		{
			Queue.Add(InEntryGuid);
			Reachable.Add(InEntryGuid);
		}
		for (const FLayoutNode& Node : InOutMain)
		{
			if (Node.NodeGuid != InEntryGuid && InDegree[Node.NodeGuid] == 0)
			{
				Queue.Add(Node.NodeGuid);
				if (InSideLaneFed.Contains(Node.NodeGuid))
				{
					Reachable.Add(Node.NodeGuid);
				}
			}
		}

		TMap<FGuid, int32> LayerByGuid;
		LayerByGuid.Reserve(InOutMain.Num());
		for (const FGuid& Guid : Queue)
		{
			LayerByGuid.Add(Guid, 0);
		}

		TArray<FGuid> TopoOrder;
		TopoOrder.Reserve(InOutMain.Num());

		int32 QueueHead = 0;
		while (QueueHead < Queue.Num())
		{
			const FGuid Current = Queue[QueueHead++];
			TopoOrder.Add(Current);

			const TArray<FGuid>* Successors = InSuccessors.Find(Current);
			if (!Successors)
			{
				continue;
			}
			for (const FGuid& Successor : *Successors)
			{
				if (!IsKeptEdge(Current, Successor))
				{
					continue;
				}
				int32* RemainingIn = InDegree.Find(Successor);
				if (!RemainingIn)
				{
					continue;
				}
				if (Reachable.Contains(Current))
				{
					Reachable.Add(Successor);
				}
				const int32 SuccessorLayer = LayerByGuid.FindOrAdd(Successor, 0);
				const int32 ProposedLayer = LayerByGuid[Current] + 1;
				if (ProposedLayer > SuccessorLayer)
				{
					LayerByGuid[Successor] = ProposedLayer;
				}
				if (--(*RemainingIn) == 0)
				{
					Queue.Add(Successor);
				}
			}
		}

		int32 MaxReachableLayer = 0;
		for (const FGuid& Guid : Reachable)
		{
			if (const int32* Layer = LayerByGuid.Find(Guid))
			{
				MaxReachableLayer = FMath::Max(MaxReachableLayer, *Layer);
			}
		}

		TArray<FString> UnreachableNames;
		for (FLayoutNode& Node : InOutMain)
		{
			if (Reachable.Contains(Node.NodeGuid))
			{
				Node.Layer = LayerByGuid[Node.NodeGuid];
				continue;
			}
			if (LayerByGuid.Contains(Node.NodeGuid))
			{
				Node.Layer = MaxReachableLayer + 1 + LayerByGuid[Node.NodeGuid];
			}
			else
			{
				Node.Layer = MaxReachableLayer + 1;
			}
			UnreachableNames.Add(InNamesByGuid.Contains(Node.NodeGuid) ? InNamesByGuid[Node.NodeGuid] : Node.NodeGuid.ToString());
		}

		if (UnreachableNames.Num() > 0)
		{
			FString Joined;
			for (int32 NameIdx = 0; NameIdx < UnreachableNames.Num(); ++NameIdx)
			{
				if (NameIdx > 0)
				{
					Joined.Append(TEXT(", "));
				}
				Joined.Append(TEXT("'"));
				Joined.Append(UnreachableNames[NameIdx]);
				Joined.Append(TEXT("'"));
			}
			OutWarnings.Add(FString::Printf(
				TEXT("%d node%s unreachable from entry, placed at layer end: [%s]."),
				UnreachableNames.Num(),
				UnreachableNames.Num() == 1 ? TEXT("") : TEXT("s"),
				*Joined));
		}
	}

	// The entry state is where a reader starts, and the wire reaching it is drawn from an entry node the
	// layout never moves. A state with no outgoing transition takes no part in the flow past it, so
	// stacking it in a layer of states that do pushes those states apart, drags the entry wire across the
	// whole stack to reach it, and reads as one rung of a ladder it is not on. Give it a column of its
	// own ahead of the flow instead, next to the entry node, and move every other layer along by one.
	//
	// Its own incoming transition is then drawn back toward the start, which is what it means: the state
	// that feeds it is where the machine goes on to, and this is where it comes to rest. That only holds
	// when every transition into it comes from the first layer, so the rule asks for that. It also asks
	// for a state to have been sharing its layer, because a layer of one is already a column of its own.
	void LiftEntryLeafToOwnColumn(
		TArray<FLayoutNode>& InOutMain,
		const TMap<FGuid, TArray<FGuid>>& InSuccessors,
		const TMap<FGuid, TArray<FGuid>>& InPredecessors,
		const FGuid& InEntryGuid)
	{
		if (!InEntryGuid.IsValid() || InOutMain.Num() == 0)
		{
			return;
		}

		FLayoutNode* Entry = nullptr;
		int32 FirstLayer = TNumericLimits<int32>::Max();
		for (FLayoutNode& Node : InOutMain)
		{
			if (Node.Layer != INDEX_NONE)
			{
				FirstLayer = FMath::Min(FirstLayer, Node.Layer);
			}
			if (Node.NodeGuid == InEntryGuid)
			{
				Entry = &Node;
			}
		}

		if (!Entry || Entry->Layer == INDEX_NONE || Entry->Layer <= FirstLayer)
		{
			return;
		}

		const TArray<FGuid>* Successors = InSuccessors.Find(InEntryGuid);
		if (Successors && Successors->Num() > 0)
		{
			return;
		}

		const TArray<FGuid>* Predecessors = InPredecessors.Find(InEntryGuid);
		if (!Predecessors || Predecessors->Num() == 0)
		{
			return;
		}

		TMap<FGuid, int32> LayerByGuid;
		LayerByGuid.Reserve(InOutMain.Num());
		int32 LayerMates = 0;
		for (const FLayoutNode& Node : InOutMain)
		{
			LayerByGuid.Add(Node.NodeGuid, Node.Layer);
			if (Node.Layer == Entry->Layer && Node.NodeGuid != InEntryGuid)
			{
				++LayerMates;
			}
		}
		if (LayerMates == 0)
		{
			return;
		}

		for (const FGuid& Predecessor : *Predecessors)
		{
			const int32* PredecessorLayer = LayerByGuid.Find(Predecessor);
			if (!PredecessorLayer || *PredecessorLayer != FirstLayer)
			{
				return;
			}
		}

		// Every other layer moves along, rather than the entry state moving to FirstLayer - 1, because a
		// layer of INDEX_NONE means a node the layering could not reach and several passes test for it.
		for (FLayoutNode& Node : InOutMain)
		{
			if (Node.Layer != INDEX_NONE)
			{
				++Node.Layer;
			}
		}
		Entry->Layer = FirstLayer;
	}

	// Initial within-layer order = current secondary-axis position; one barycenter sweep
	// refines using neighbors in adjacent layers. Tiebreak on name then GUID for determinism.
	// Choose the within-layer order that draws fewest transitions through a state box. Each candidate
	// order is placed with the real coordinate pass and then measured, because the objective is about
	// geometry and the order by itself has none. Returns the score of the order it settled on.
	int32 OrderWithinLayers(
		TArray<FLayoutNode>& InOutMain,
		const TMap<FGuid, TArray<FGuid>>& InSuccessors,
		const TMap<FGuid, TArray<FGuid>>& InPredecessors,
		const TArray<FLayoutEdge>& InEdges,
		const FLayoutInput& InInput,
		const FVector2f& InStart,
		bool bRespectExistingOrder)
	{
		const ELayoutStrategy InStrategy = InInput.Strategy;
		// Group node indices by layer.
		TMap<int32, TArray<int32>> ByLayer;
		for (int32 NodeIdx = 0; NodeIdx < InOutMain.Num(); ++NodeIdx)
		{
			ByLayer.FindOrAdd(InOutMain[NodeIdx].Layer).Add(NodeIdx);
		}

		TMap<FGuid, int32> IndexByGuid;
		IndexByGuid.Reserve(InOutMain.Num());
		for (int32 NodeIdx = 0; NodeIdx < InOutMain.Num(); ++NodeIdx)
		{
			IndexByGuid.Add(InOutMain[NodeIdx].NodeGuid, NodeIdx);
		}

		// Initial order key: current secondary-axis position when respecting authored layout,
		// else fall back to alpha for fully deterministic re-flow.
		TMap<int32, double> InitialKey;
		InitialKey.Reserve(InOutMain.Num());
		for (int32 NodeIdx = 0; NodeIdx < InOutMain.Num(); ++NodeIdx)
		{
			const FLayoutNode& Node = InOutMain[NodeIdx];
			InitialKey.Add(NodeIdx, bRespectExistingOrder ? GetSecondary(Node.OldPosition, InStrategy) : 0.0);
		}

		// Parents in an earlier layer, and the lowest transition priority arriving from one. An edge that
		// leaves a later layer is a cycle's back-edge, and it says nothing about which sibling comes
		// first, so it is left out of both. A node reached by no such edge keeps an empty parent set and
		// MAX_int32, which sorts it after every node that does have a priority.
		TMap<int32, TSet<FGuid>> ForwardParents;
		TMap<int32, int32> LowestPriority;
		ForwardParents.Reserve(InOutMain.Num());
		LowestPriority.Reserve(InOutMain.Num());
		for (int32 NodeIdx = 0; NodeIdx < InOutMain.Num(); ++NodeIdx)
		{
			ForwardParents.Add(NodeIdx);
			LowestPriority.Add(NodeIdx, MAX_int32);
		}
		for (const FLayoutEdge& Edge : InEdges)
		{
			const int32* FromIdx = IndexByGuid.Find(Edge.FromGuid);
			const int32* ToIdx = IndexByGuid.Find(Edge.ToGuid);
			if (!FromIdx || !ToIdx || *FromIdx == *ToIdx)
			{
				continue;
			}
			if (InOutMain[*FromIdx].Layer >= InOutMain[*ToIdx].Layer)
			{
				continue;
			}
			ForwardParents[*ToIdx].Add(Edge.FromGuid);
			LowestPriority[*ToIdx] = FMath::Min(LowestPriority[*ToIdx], Edge.Priority);
		}

		// The initial order has to be total, because every sort after it is an Algo::StableSort and so
		// keeps whatever it was handed whenever the keys tie. Name and GUID are what make it total: with
		// bRespectExistingOrder off every key is 0, and a graph whose nodes were all added in one batch
		// has identical authored positions. Without them a layer would fall back to UEdGraph::Nodes
		// order, which is creation order mutated by every delete, paste and reroute splice, so two assets
		// built by the same script would lay out differently.
		auto SortLayerTotal = [&InOutMain, &InitialKey](TArray<int32>& InOutLayerIndices)
		{
			Algo::StableSort(InOutLayerIndices, [&InOutMain, &InitialKey](int32 A, int32 B)
			{
				const double KeyA = InitialKey[A];
				const double KeyB = InitialKey[B];
				if (KeyA != KeyB)
				{
					return KeyA < KeyB;
				}
				const int32 NameCmp = InOutMain[A].Name.Compare(InOutMain[B].Name);
				if (NameCmp != 0)
				{
					return NameCmp < 0;
				}
				return InOutMain[A].NodeGuid < InOutMain[B].NodeGuid;
			});
		};

		// The sweeps compare the barycenter key and nothing else. Every spoke of a hub shares one
		// predecessor, so a sweep hands them all the same key, and comparing the name here would throw
		// away the order the initial sort established and put the layer in alphabetical order instead.
		// Algo::StableSort keeps the incumbent when the keys tie, which is the right answer when the key
		// carries no information. The tie is exact rather than approximate: SweepKeys divides an exact
		// small-integer sum by a count and IEEE division is correctly rounded, so two neighbor-rank means
		// that are equal as real numbers produce the same double.
		auto SortLayerByKey = [](TArray<int32>& InOutLayerIndices, const TMap<int32, double>& InKey)
		{
			Algo::StableSort(InOutLayerIndices, [&InKey](int32 A, int32 B)
			{
				return InKey[A] < InKey[B];
			});
		};

		// Transition priority is the order the source state evaluates its transitions in at runtime, so
		// it is the one signal in the input that carries meaning rather than appearance. The layout reads
		// it and never writes it, because it decides behavior and is the author's to set.
		//
		// It applies in one place only. A priority is numbered per source state, so a number from one
		// parent means nothing beside a number from another, and a comparator that compared it only
		// between siblings would not be transitive and would trip Algo::StableSort. Reorder a run of
		// nodes a sort has left tied instead, and only when every node in the run has the same parents
		// and the same authored position. The same parents is what makes the numbers comparable. The same
		// authored position is what says the author has not already arranged these nodes, because an
		// arrangement someone wrote outranks an evaluation order.
		//
		// When a run shares more than one parent, each node's key is the lowest priority over the edges
		// reaching it, which mixes two independently numbered scales. That is a tie-break, not a claim
		// about evaluation order: it is applied only where nothing else distinguishes the nodes, and it
		// is stable, so the run keeps its incumbent order wherever the numbers are equal.
		auto RefineRunsByPriority = [&InitialKey, &ForwardParents, &LowestPriority](
			TArray<int32>& InOutLayerIndices, const TMap<int32, double>& InKey)
		{
			int32 RunStart = 0;
			while (RunStart < InOutLayerIndices.Num())
			{
				int32 RunEnd = RunStart + 1;
				while (RunEnd < InOutLayerIndices.Num()
					&& InKey[InOutLayerIndices[RunEnd]] == InKey[InOutLayerIndices[RunStart]])
				{
					++RunEnd;
				}

				const int32 RunLength = RunEnd - RunStart;
				if (RunLength > 1)
				{
					const int32 FirstIdx = InOutLayerIndices[RunStart];
					bool bComparable = true;
					for (int32 Offset = 1; Offset < RunLength && bComparable; ++Offset)
					{
						const int32 OtherIdx = InOutLayerIndices[RunStart + Offset];
						bComparable = InitialKey[OtherIdx] == InitialKey[FirstIdx]
							&& ForwardParents[OtherIdx].Num() == ForwardParents[FirstIdx].Num()
							&& ForwardParents[OtherIdx].Includes(ForwardParents[FirstIdx]);
					}

					if (bComparable)
					{
						TArray<int32> Run(InOutLayerIndices.GetData() + RunStart, RunLength);
						Algo::StableSort(Run, [&LowestPriority](int32 A, int32 B)
						{
							return LowestPriority[A] < LowestPriority[B];
						});
						for (int32 Offset = 0; Offset < RunLength; ++Offset)
						{
							InOutLayerIndices[RunStart + Offset] = Run[Offset];
						}
					}
				}

				RunStart = RunEnd;
			}
		};

		for (TPair<int32, TArray<int32>>& Layer : ByLayer)
		{
			SortLayerTotal(Layer.Value);
			RefineRunsByPriority(Layer.Value, InitialKey);
		}

		// Sweep ordering, keeping whichever arrangement measures fewest crossings. A single barycenter
		// pass improves each layer against its neighbors, but nothing in it measures the graph as a
		// whole, so a pass can trade one crossing for two. Alternating passes and scoring each one lets
		// placement remove the crossings placement can remove, leaving the router only what ordering
		// cannot fix.
		TArray<int32> SortedLayerKeys;
		ByLayer.GenerateKeyArray(SortedLayerKeys);
		SortedLayerKeys.Sort();

		auto RanksByIndex = [&ByLayer, &SortedLayerKeys]()
		{
			TMap<int32, int32> Ranks;
			for (int32 LayerKey : SortedLayerKeys)
			{
				const TArray<int32>& Indices = ByLayer[LayerKey];
				for (int32 PositionIdx = 0; PositionIdx < Indices.Num(); ++PositionIdx)
				{
					Ranks.Add(Indices[PositionIdx], PositionIdx);
				}
			}
			return Ranks;
		};

		// One pass: each node's key is the mean rank of its neighbors on one side, so sorting by that
		// key pulls it toward them. Passes alternate sides, which is what lets an order settle.
		auto SweepKeys = [&InOutMain, &InSuccessors, &InPredecessors, &IndexByGuid, &InitialKey](
			bool bTowardPredecessors, const TMap<int32, int32>& InRanks)
		{
			TMap<int32, double> Keys;
			Keys.Reserve(InOutMain.Num());
			for (int32 NodeIdx = 0; NodeIdx < InOutMain.Num(); ++NodeIdx)
			{
				const FLayoutNode& Node = InOutMain[NodeIdx];
				const TArray<FGuid>* Neighbors = bTowardPredecessors
					? InPredecessors.Find(Node.NodeGuid)
					: InSuccessors.Find(Node.NodeGuid);
				double Sum = 0.0;
				int32 Count = 0;
				if (Neighbors)
				{
					for (const FGuid& Neighbor : *Neighbors)
					{
						const int32* NeighborIdx = IndexByGuid.Find(Neighbor);
						if (!NeighborIdx)
						{
							continue;
						}
						if (const int32* NeighborRank = InRanks.Find(*NeighborIdx))
						{
							Sum += static_cast<double>(*NeighborRank);
							++Count;
						}
					}
				}
				if (Count > 0)
				{
					Keys.Add(NodeIdx, Sum / Count);
					continue;
				}
				const int32* OwnRank = InRanks.Find(NodeIdx);
				Keys.Add(NodeIdx, OwnRank ? static_cast<double>(*OwnRank) : InitialKey[NodeIdx]);
			}
			return Keys;
		};

		// Place a candidate order with the real coordinate pass and count the transitions it would draw
		// through a state. The placement is thrown away; only the number is kept.
		const float OverlapPadding = InInput.RowGap * 0.5f;
		auto ScoreOrder = [&InOutMain, &InEdges, &InInput, &InStart, &SortedLayerKeys, OverlapPadding](
			const TMap<int32, TArray<int32>>& InByLayer)
		{
			TArray<FLayoutNode> Candidate;
			Candidate.Reserve(InOutMain.Num());
			for (int32 LayerKey : SortedLayerKeys)
			{
				for (int32 NodeIdx : InByLayer[LayerKey])
				{
					Candidate.Add(InOutMain[NodeIdx]);
				}
			}
			AssignCoordinates(Candidate, InInput, InStart);
			return CountEdgesThroughNodes(Candidate, InEdges, OverlapPadding);
		};

		// Eight passes is past the point where the score stops improving on graphs this size. Only a
		// strictly lower score replaces the best, so equal scores keep the earlier order and the same
		// input always produces the same result.
		constexpr int32 SweepCount = 8;

		TMap<int32, TArray<int32>> BestByLayer = ByLayer;
		int32 BestScore = ScoreOrder(ByLayer);

		// Alphabetical order is deliberately not scored as a candidate arrangement. Name decides a layer
		// in the initial sort alone, and only once nothing else distinguishes the nodes. Scoring it here
		// as a whole arrangement would let one fewer crossing lift it above the order the author wrote,
		// which inverts the ranking the initial sort sets out.

		for (int32 SweepIdx = 0; SweepIdx < SweepCount && BestScore > 0; ++SweepIdx)
		{
			const TMap<int32, int32> Ranks = RanksByIndex();
			const TMap<int32, double> Keys = SweepKeys(SweepIdx % 2 == 0, Ranks);
			for (TPair<int32, TArray<int32>>& Layer : ByLayer)
			{
				SortLayerByKey(Layer.Value, Keys);
				RefineRunsByPriority(Layer.Value, Keys);
			}

			const int32 Score = ScoreOrder(ByLayer);
			if (Score < BestScore)
			{
				BestScore = Score;
				BestByLayer = ByLayer;
			}
		}

		ByLayer = BestByLayer;

		// Swapping two neighbors within a layer is the smallest move that can lift a wire off a state,
		// and the sweeps above cannot make it: they only pull a node toward the mean rank of its
		// neighbors, which is blind to what sits between two rows. Keep a swap only when it lowers the
		// score, and stop as soon as a whole pass finds nothing.
		constexpr int32 RefinePassCount = 4;
		for (int32 RefinePass = 0; RefinePass < RefinePassCount && BestScore > 0; ++RefinePass)
		{
			bool bImprovedThisPass = false;
			for (int32 LayerKey : SortedLayerKeys)
			{
				TArray<int32>& Layer = ByLayer[LayerKey];
				for (int32 PositionIdx = 0; PositionIdx + 1 < Layer.Num(); ++PositionIdx)
				{
					Layer.Swap(PositionIdx, PositionIdx + 1);
					const int32 Score = ScoreOrder(ByLayer);
					if (Score < BestScore)
					{
						BestScore = Score;
						bImprovedThisPass = true;
					}
					else
					{
						Layer.Swap(PositionIdx, PositionIdx + 1);
					}
				}
			}

			if (!bImprovedThisPass)
			{
				break;
			}
		}

		// Repack InOutMain in (layer, within-layer rank) order so AssignCoordinates can iterate
		// sequentially without consulting a separate rank map.
		TArray<FLayoutNode> Reordered;
		Reordered.Reserve(InOutMain.Num());
		for (int32 LayerKey : SortedLayerKeys)
		{
			const TArray<int32>& Indices = ByLayer[LayerKey];
			for (int32 NodeIdx : Indices)
			{
				Reordered.Add(InOutMain[NodeIdx]);
			}
		}
		InOutMain = MoveTemp(Reordered);
		return BestScore;
	}

	// Place each non-pinned main-flow node. Layer primary coordinate is the cumulative sum of
	// preceding layers' primary extents plus one ColumnGap per layer boundary. Within a layer,
	// nodes stack along the secondary axis centered around Start.secondary; total stack height
	// is the sum of widget secondary extents plus (n-1)*RowGap.
	void AssignCoordinates(
		TArray<FLayoutNode>& InOutMain,
		const FLayoutInput& InInput,
		const FVector2f& InStart,
		const TMap<int32, float>* InExtraGapAfterLayer)
	{
		if (InOutMain.Num() == 0)
		{
			return;
		}

		TMap<int32, TArray<int32>> ByLayer;
		for (int32 NodeIdx = 0; NodeIdx < InOutMain.Num(); ++NodeIdx)
		{
			ByLayer.FindOrAdd(InOutMain[NodeIdx].Layer).Add(NodeIdx);
		}

		TArray<int32> SortedLayers;
		ByLayer.GenerateKeyArray(SortedLayers);
		SortedLayers.Sort();

		TMap<int32, float> LayerPrimaryExtent;
		for (int32 LayerKey : SortedLayers)
		{
			float MaxExtent = 0.0f;
			for (int32 NodeIdx : ByLayer[LayerKey])
			{
				MaxExtent = FMath::Max(MaxExtent, GetPrimaryExtent(InOutMain[NodeIdx].WidgetSize, InInput.Strategy));
			}
			LayerPrimaryExtent.Add(LayerKey, MaxExtent);
		}

		float CumulativePrimary = GetPrimary(InStart, InInput.Strategy);
		const float StartSecondary = GetSecondary(InStart, InInput.Strategy);

		for (int32 LayerOrderIdx = 0; LayerOrderIdx < SortedLayers.Num(); ++LayerOrderIdx)
		{
			const int32 LayerKey = SortedLayers[LayerOrderIdx];
			const TArray<int32>& Indices = ByLayer[LayerKey];

			float TotalSecondary = 0.0f;
			for (int32 NodeIdx : Indices)
			{
				TotalSecondary += GetSecondaryExtent(InOutMain[NodeIdx].WidgetSize, InInput.Strategy);
			}
			if (Indices.Num() > 1)
			{
				TotalSecondary += static_cast<float>(Indices.Num() - 1) * InInput.RowGap;
			}

			float SecondaryCursor = StartSecondary - TotalSecondary * 0.5f;
			for (int32 NodeIdx : Indices)
			{
				FLayoutNode& Node = InOutMain[NodeIdx];
				if (!Node.bPinned)
				{
					Node.NewPosition = MakePos(CumulativePrimary, SecondaryCursor, InInput.Strategy);
				}
				SecondaryCursor += GetSecondaryExtent(Node.WidgetSize, InInput.Strategy) + InInput.RowGap;
			}

			float ExtraGap = 0.0f;
			if (InExtraGapAfterLayer)
			{
				if (const float* Found = InExtraGapAfterLayer->Find(LayerKey))
				{
					ExtraGap = *Found;
				}
			}
			CumulativePrimary += LayerPrimaryExtent[LayerKey] + InInput.ColumnGap + ExtraGap;
		}
	}

	// One state's outgoing transitions to a whole layer of siblings, taken as a group.
	struct FLayoutFan
	{
		FGuid HubGuid;
		int32 TargetLayer = INDEX_NONE;
		TArray<FGuid> TargetGuids;
		TArray<FGuid> TransitionGuids;
	};

	// Every state that feeds InMinTargets or more siblings sitting together in the next layer. Targets
	// spread over several layers are not a fan: those wires have different lengths and different rows to
	// reach, and the ordering pass can already separate them.
	TArray<FLayoutFan> FindFans(
		const TArray<FLayoutNode>& InMain,
		const TArray<FLayoutEdge>& InEdges,
		const TSet<TPair<FGuid, FGuid>>& InBackEdges,
		int32 InMinTargets)
	{
		TArray<FLayoutFan> Fans;
		if (InMinTargets < 2)
		{
			return Fans;
		}

		TMap<FGuid, const FLayoutNode*> NodeByGuid;
		NodeByGuid.Reserve(InMain.Num());
		for (const FLayoutNode& Node : InMain)
		{
			NodeByGuid.Add(Node.NodeGuid, &Node);
		}

		TMap<FGuid, FLayoutFan> ByHub;
		for (const FLayoutEdge& Edge : InEdges)
		{
			if (Edge.FromGuid == Edge.ToGuid || !Edge.TransitionGuid.IsValid()
				|| InBackEdges.Contains(TPair<FGuid, FGuid>(Edge.FromGuid, Edge.ToGuid)))
			{
				continue;
			}

			const FLayoutNode* const* FromPtr = NodeByGuid.Find(Edge.FromGuid);
			const FLayoutNode* const* ToPtr = NodeByGuid.Find(Edge.ToGuid);
			if (!FromPtr || !ToPtr)
			{
				continue;
			}

			const FLayoutNode& From = **FromPtr;
			const FLayoutNode& To = **ToPtr;
			if (From.Layer == INDEX_NONE || To.Layer != From.Layer + 1)
			{
				continue;
			}

			FLayoutFan& Fan = ByHub.FindOrAdd(Edge.FromGuid);
			Fan.HubGuid = Edge.FromGuid;
			Fan.TargetLayer = To.Layer;
			Fan.TargetGuids.Add(Edge.ToGuid);
			Fan.TransitionGuids.Add(Edge.TransitionGuid);
		}

		for (TPair<FGuid, FLayoutFan>& Pair : ByHub)
		{
			if (Pair.Value.TargetGuids.Num() >= InMinTargets)
			{
				Fans.Add(MoveTemp(Pair.Value));
			}
		}

		Fans.Sort([](const FLayoutFan& A, const FLayoutFan& B)
		{
			return A.HubGuid < B.HubGuid;
		});
		return Fans;
	}

	// Carry a fan on a trunk: one line leaving the hub across the gap to the sibling column, and one
	// reroute per sibling sitting on that line at the sibling's own row, so the last leg into a sibling
	// is a square corner arriving at exactly one state. That is the shape the straight lines cannot
	// reach at any spacing. A wire from the hub's center to an outer sibling's center enters the column
	// far from the row it wants, and the only cure without a rail is to push the two layers so far apart
	// that every wire is nearly flat, which spends a screen of space on seven wires.
	//
	// A sibling flat enough to reach with a clear straight wire keeps it, because a rail that avoids
	// nothing is one more node to account for. The trunk runs down the hub's own center line when nothing
	// in the hub's layer stands on it, so each wire leaves through the hub's top or bottom edge. When
	// something does stand on it, the trunk moves into the gap between the two layers, where no state
	// can be.
	void PlanFanRails(
		const TArray<FLayoutNode>& InPlaced,
		const TArray<FLayoutFan>& InFans,
		const FLayoutInput& InInput,
		TArray<FLayoutReroute>& OutReroutes,
		TSet<FGuid>& OutRailedTransitions,
		TMap<FGuid, FGuid>* OutTargetByTransition = nullptr)
	{
		if (InFans.Num() == 0 || InPlaced.Num() == 0)
		{
			return;
		}

		TMap<FGuid, const FLayoutNode*> NodeByGuid;
		NodeByGuid.Reserve(InPlaced.Num());
		for (const FLayoutNode& Node : InPlaced)
		{
			NodeByGuid.Add(Node.NodeGuid, &Node);
		}

		const ELayoutStrategy Strategy = InInput.Strategy;
		const float Padding = InInput.RowGap * 0.5f;
		const float HalfReroute = InInput.RerouteSize * 0.5f;

		auto SecondaryCenter = [Strategy](const FLayoutNode& InNode)
		{
			return GetSecondary(InNode.NewPosition, Strategy)
				+ GetSecondaryExtent(InNode.WidgetSize, Strategy) * 0.5f;
		};

		for (const FLayoutFan& Fan : InFans)
		{
			const FLayoutNode* const* HubPtr = NodeByGuid.Find(Fan.HubGuid);
			if (!HubPtr)
			{
				continue;
			}
			const FLayoutNode& Hub = **HubPtr;
			const FVector2f HubCenter = Hub.NewPosition + Hub.WidgetSize * 0.5f;
			const float HubPrimaryCenter = GetPrimary(Hub.NewPosition, Strategy)
				+ GetPrimaryExtent(Hub.WidgetSize, Strategy) * 0.5f;

			float LowestTarget = TNumericLimits<float>::Max();
			float HighestTarget = TNumericLimits<float>::Lowest();
			float TargetLayerNear = TNumericLimits<float>::Max();
			for (const FGuid& TargetGuid : Fan.TargetGuids)
			{
				const FLayoutNode* const* TargetPtr = NodeByGuid.Find(TargetGuid);
				if (!TargetPtr)
				{
					continue;
				}
				const float Center = SecondaryCenter(**TargetPtr);
				LowestTarget = FMath::Min(LowestTarget, Center);
				HighestTarget = FMath::Max(HighestTarget, Center);
				TargetLayerNear = FMath::Min(TargetLayerNear, GetPrimary((*TargetPtr)->NewPosition, Strategy));
			}
			if (LowestTarget > HighestTarget)
			{
				continue;
			}

			float HubLayerFar = GetPrimary(Hub.NewPosition, Strategy)
				+ GetPrimaryExtent(Hub.WidgetSize, Strategy);
			bool bCenterLineClear = true;
			for (const FLayoutNode& Node : InPlaced)
			{
				if (Node.Lane != ELayoutLane::Main || Node.Layer != Hub.Layer)
				{
					continue;
				}
				HubLayerFar = FMath::Max(
					HubLayerFar,
					GetPrimary(Node.NewPosition, Strategy) + GetPrimaryExtent(Node.WidgetSize, Strategy));

				if (Node.NodeGuid == Fan.HubGuid)
				{
					continue;
				}
				const float NodeNear = GetPrimary(Node.NewPosition, Strategy) - Padding;
				const float NodeFar = NodeNear + GetPrimaryExtent(Node.WidgetSize, Strategy) + Padding * 2.0f;
				const float NodeLow = GetSecondary(Node.NewPosition, Strategy) - Padding;
				const float NodeHigh = NodeLow + GetSecondaryExtent(Node.WidgetSize, Strategy) + Padding * 2.0f;
				if (HubPrimaryCenter > NodeNear && HubPrimaryCenter < NodeFar
					&& NodeHigh > LowestTarget && NodeLow < HighestTarget)
				{
					bCenterLineClear = false;
				}
			}

			const float TrunkCenter = bCenterLineClear
				? HubPrimaryCenter
				: FMath::Min((HubLayerFar + TargetLayerNear) * 0.5f, TargetLayerNear - HalfReroute - Padding);

			// Which spokes need carrying, decided before any of them is placed, because the lanes are
			// spread around the trunk and that needs the count.
			//
			// A wire that climbs further across the flow than it travels along it no longer reads as a
			// step forward, and several of them leaving one state converge on that state at angles a
			// reader has to untangle to see which sibling each one reaches. Carry those whether or not
			// they happen to miss every box. A flatter wire arrives at its sibling's row nearly level and
			// is easier to read as the straight line it is, so it stays one.
			TArray<int32> Carried;
			Carried.Reserve(Fan.TargetGuids.Num());
			for (int32 TargetIdx = 0; TargetIdx < Fan.TargetGuids.Num(); ++TargetIdx)
			{
				const FLayoutNode* const* TargetPtr = NodeByGuid.Find(Fan.TargetGuids[TargetIdx]);
				if (!TargetPtr)
				{
					continue;
				}
				const FLayoutNode& Target = **TargetPtr;
				const FVector2f TargetCenter = Target.NewPosition + Target.WidgetSize * 0.5f;

				const float PrimaryRun = FMath::Abs(
					GetPrimary(TargetCenter, Strategy) - GetPrimary(HubCenter, Strategy));
				const float SecondaryRise = FMath::Abs(
					GetSecondary(TargetCenter, Strategy) - GetSecondary(HubCenter, Strategy));

				if (SecondaryRise <= PrimaryRun
					&& !SegmentCrossesAnyNode(
						HubCenter, TargetCenter, InPlaced, Fan.HubGuid, Fan.TargetGuids[TargetIdx], Padding)
					&& !MarkerLandsOnNode(
						(HubCenter + TargetCenter) * 0.5f, InPlaced, InInput.TransitionMarkerSize))
				{
					continue;
				}
				Carried.Add(TargetIdx);
			}

			if (Carried.Num() == 0)
			{
				continue;
			}

			// Every carried spoke gets its own lane. Sharing one line would draw two wires on top of each
			// other, and a reader counting the transitions leaving a state would see one. Ordered by the
			// row each spoke arrives at, so the lanes fan out the same way the states do and no two of
			// the legs leaving the hub cross.
			Carried.Sort([&NodeByGuid, &Fan, &SecondaryCenter](int32 A, int32 B)
			{
				const float CenterA = SecondaryCenter(**NodeByGuid.Find(Fan.TargetGuids[A]));
				const float CenterB = SecondaryCenter(**NodeByGuid.Find(Fan.TargetGuids[B]));
				if (CenterA != CenterB)
				{
					return CenterA < CenterB;
				}
				return Fan.TransitionGuids[A] < Fan.TransitionGuids[B];
			});

			// One reroute width apart, which is the smallest gap that still reads as two lines at the
			// zoom a whole graph is viewed at. The lanes are centered on the trunk, so the group stays
			// balanced on the hub, and the span is capped to the room between the hub and the sibling
			// column. A lane closer than the snap grid would round onto its neighbor, so when there is
			// not room for that the whole fan shares one lane and the wires overlap as before.
			const float LaneLow = bCenterLineClear
				? GetPrimary(Hub.NewPosition, Strategy)
				: HubLayerFar + HalfReroute + Padding;
			const float LaneHigh = TargetLayerNear - HalfReroute - Padding;
			const float LaneRoom = FMath::Min(TrunkCenter - LaneLow, LaneHigh - TrunkCenter) * 2.0f;

			float LaneStep = InInput.RerouteSize;
			if (Carried.Num() > 1)
			{
				LaneStep = FMath::Min(LaneStep, LaneRoom / static_cast<float>(Carried.Num() - 1));
			}
			const float NarrowestLane = InInput.bSnapToGrid ? InInput.SnapGridSize : 1.0f;
			if (LaneStep < NarrowestLane)
			{
				LaneStep = 0.0f;
			}

			const float FirstLane = TrunkCenter - LaneStep * static_cast<float>(Carried.Num() - 1) * 0.5f;
			for (int32 LaneIdx = 0; LaneIdx < Carried.Num(); ++LaneIdx)
			{
				const int32 TargetIdx = Carried[LaneIdx];
				const FLayoutNode& Target = **NodeByGuid.Find(Fan.TargetGuids[TargetIdx]);

				FLayoutReroute Reroute;
				Reroute.TransitionGuid = Fan.TransitionGuids[TargetIdx];
				Reroute.ChainIndex = 0;
				Reroute.Position = MakePos(
					FirstLane + LaneStep * static_cast<float>(LaneIdx) - HalfReroute,
					SecondaryCenter(Target) - HalfReroute,
					Strategy);
				OutReroutes.Add(MoveTemp(Reroute));
				OutRailedTransitions.Add(Fan.TransitionGuids[TargetIdx]);
				if (OutTargetByTransition)
				{
					OutTargetByTransition->Add(Fan.TransitionGuids[TargetIdx], Fan.TargetGuids[TargetIdx]);
				}
			}
		}
	}

	// A wire is drawn from the source state's center to the target's center. When one state fans out to
	// a tall column of siblings, the wire to an outer sibling enters that column far from the row it is
	// going to and sweeps through every sibling in between. Stretching the boundary is the cure: the
	// further apart the two layers, the shallower every wire, and a shallow wire has almost reached its
	// own row by the time it arrives at the column.
	//
	// This is worth doing before routing rather than leaving it to the rails. A rail lifts the wire out
	// of the column, but it also adds two nodes and sends the wire out and back, which is harder to
	// follow than the straight line it replaced. Space costs a reader nothing; a rail costs attention.
	//
	// Widen one boundary at a time and keep the smallest widening that reaches the lowest count, so a
	// graph is never made wider than the crossings it removes justify. The candidates are fractions of
	// the taller of the two layers the boundary separates, because that height is what a wire has to
	// climb, and they are capped by MaxExtraLayerGap.
	void WidenLayerGaps(
		const TArray<FLayoutNode>& InMain,
		const TArray<FLayoutEdge>& InEdges,
		const FLayoutInput& InInput,
		const FVector2f& InStart,
		const TArray<FLayoutFan>& InFans,
		TMap<int32, float>& OutExtraGapAfterLayer)
	{
		if (InInput.MaxExtraLayerGap <= 0.0f || InMain.Num() == 0)
		{
			return;
		}

		TMap<int32, float> LayerSecondaryExtent;
		for (const FLayoutNode& Node : InMain)
		{
			if (Node.Layer == INDEX_NONE)
			{
				continue;
			}
			float& Extent = LayerSecondaryExtent.FindOrAdd(Node.Layer, 0.0f);
			Extent += GetSecondaryExtent(Node.WidgetSize, InInput.Strategy) + InInput.RowGap;
		}

		TArray<int32> SortedLayers;
		LayerSecondaryExtent.GenerateKeyArray(SortedLayers);
		SortedLayers.Sort();
		if (SortedLayers.Num() < 2)
		{
			return;
		}

		// Scored on the two things spacing can fix, in the order the decline rule ranks them: a wire drawn
		// through a state first, then a marker drawn on one. Each candidate is scored with its fan trunks
		// planned for it, because a fan carried on a trunk is already clear and widening the boundary for
		// it would buy nothing while spending the space anyway.
		const float OverlapPadding = InInput.RowGap * 0.5f;
		auto Score = [&InMain, &InEdges, &InInput, &InStart, &InFans, OverlapPadding](const TMap<int32, float>& InGaps)
		{
			TArray<FLayoutNode> Candidate = InMain;
			AssignCoordinates(Candidate, InInput, InStart, &InGaps);

			TArray<FLayoutReroute> Rails;
			TSet<FGuid> Railed;
			PlanFanRails(Candidate, InFans, InInput, Rails, Railed);

			const int32 Crossings = CountEdgesDrawnThroughNodes(
				Candidate, InEdges, Rails, OverlapPadding, InInput.RerouteSize);
			const int32 Markers = CountMarkersOverNodes(
				Candidate, InEdges, Rails, InInput.TransitionMarkerSize, InInput.RerouteSize);
			return TPair<int32, int32>(Crossings, Markers);
		};

		// Zero first, so a boundary is only widened when widening it actually removes a crossing.
		static constexpr float Fractions[] = { 0.0f, 0.125f, 0.25f, 0.5f, 1.0f };

		TPair<int32, int32> BestScore = Score(OutExtraGapAfterLayer);
		for (int32 LayerIdx = 0;
			LayerIdx + 1 < SortedLayers.Num() && (BestScore.Key > 0 || BestScore.Value > 0);
			++LayerIdx)
		{
			const int32 LayerKey = SortedLayers[LayerIdx];
			const float Reach = FMath::Max(
				LayerSecondaryExtent[LayerKey], LayerSecondaryExtent[SortedLayers[LayerIdx + 1]]);

			float BestGap = 0.0f;
			for (const float Fraction : Fractions)
			{
				const float Candidate = FMath::Min(Reach * Fraction, InInput.MaxExtraLayerGap);
				if (Fraction > 0.0f && Candidate <= BestGap)
				{
					continue;
				}

				OutExtraGapAfterLayer.Add(LayerKey, Candidate);
				const TPair<int32, int32> CandidateScore = Score(OutExtraGapAfterLayer);
				if (CandidateScore < BestScore)
				{
					BestScore = CandidateScore;
					BestGap = Candidate;
				}
			}

			OutExtraGapAfterLayer.Add(LayerKey, BestGap);
		}
	}

	// Place AnyState side-lane nodes above (LR) / left of (TB) the main flow. Each one sits two row gaps
	// clear of the highest state it is wired to, and starts at that state's own coordinate along the
	// flow, so the wire between them is as short as the lane allows. Measuring the lane against the whole
	// graph instead would hang a node that feeds one state at the top of the tallest column and stretch
	// its wire the full height of the graph for nothing. A side node wired to nothing falls back to the
	// flow's own start. Several of them stack along the flow axis, never closer than one column gap.
	void PlaceSideLane(
		TArray<FLayoutNode>& InOutSide,
		const TArray<FLayoutNode>& InMain,
		const TArray<FLayoutEdge>& InEdges,
		const FLayoutInput& InInput,
		const FVector2f& InStart)
	{
		if (InOutSide.Num() == 0)
		{
			return;
		}

		const ELayoutStrategy Strategy = InInput.Strategy;

		TMap<FGuid, const FLayoutNode*> MainByGuid;
		MainByGuid.Reserve(InMain.Num());
		float FlowTopSecondary = TNumericLimits<float>::Max();
		float FlowNearPrimary = TNumericLimits<float>::Max();
		for (const FLayoutNode& Node : InMain)
		{
			MainByGuid.Add(Node.NodeGuid, &Node);
			if (Node.bPinned)
			{
				continue;
			}
			FlowTopSecondary = FMath::Min(FlowTopSecondary, GetSecondary(Node.NewPosition, Strategy));
			FlowNearPrimary = FMath::Min(FlowNearPrimary, GetPrimary(Node.NewPosition, Strategy));
		}
		if (FlowTopSecondary == TNumericLimits<float>::Max())
		{
			FlowTopSecondary = GetSecondary(InStart, Strategy);
		}
		if (FlowNearPrimary == TNumericLimits<float>::Max())
		{
			FlowNearPrimary = GetPrimary(InStart, Strategy);
		}

		TSet<FGuid> SideGuids;
		SideGuids.Reserve(InOutSide.Num());
		for (const FLayoutNode& Node : InOutSide)
		{
			SideGuids.Add(Node.NodeGuid);
		}

		// Both directions, because a side node is reached as well as left from and either wire is one a
		// reader has to follow.
		TMap<FGuid, TSet<FGuid>> NeighborsBySide;
		for (const FLayoutEdge& Edge : InEdges)
		{
			if (SideGuids.Contains(Edge.FromGuid) && MainByGuid.Contains(Edge.ToGuid))
			{
				NeighborsBySide.FindOrAdd(Edge.FromGuid).Add(Edge.ToGuid);
			}
			if (SideGuids.Contains(Edge.ToGuid) && MainByGuid.Contains(Edge.FromGuid))
			{
				NeighborsBySide.FindOrAdd(Edge.ToGuid).Add(Edge.FromGuid);
			}
		}

		struct FSideSlot
		{
			int32 Index = 0;
			float DesiredPrimary = 0.0f;
			float Secondary = 0.0f;
		};

		TArray<FSideSlot> Slots;
		Slots.Reserve(InOutSide.Num());
		for (int32 SideIdx = 0; SideIdx < InOutSide.Num(); ++SideIdx)
		{
			const FLayoutNode& Node = InOutSide[SideIdx];

			float TopSecondary = TNumericLimits<float>::Max();
			float NearPrimary = TNumericLimits<float>::Max();
			if (const TSet<FGuid>* Neighbors = NeighborsBySide.Find(Node.NodeGuid))
			{
				for (const FGuid& Neighbor : *Neighbors)
				{
					const FLayoutNode* const* MainPtr = MainByGuid.Find(Neighbor);
					if (!MainPtr)
					{
						continue;
					}
					const FLayoutNode& MainNode = **MainPtr;
					TopSecondary = FMath::Min(TopSecondary, GetSecondary(MainNode.NewPosition, Strategy));
					NearPrimary = FMath::Min(NearPrimary, GetPrimary(MainNode.NewPosition, Strategy));
				}
			}
			if (TopSecondary == TNumericLimits<float>::Max())
			{
				TopSecondary = FlowTopSecondary;
				NearPrimary = FlowNearPrimary;
			}

			FSideSlot Slot;
			Slot.Index = SideIdx;
			Slot.DesiredPrimary = NearPrimary;
			Slot.Secondary = TopSecondary - InInput.RowGap * 2.0f
				- GetSecondaryExtent(Node.WidgetSize, Strategy);
			Slots.Add(Slot);
		}

		// Ordered by where each one wants to sit, then by where the author had it, so two side nodes that
		// want the same place keep the order someone gave them.
		Slots.StableSort([&InOutSide, Strategy](const FSideSlot& A, const FSideSlot& B)
		{
			if (A.DesiredPrimary != B.DesiredPrimary)
			{
				return A.DesiredPrimary < B.DesiredPrimary;
			}
			const float AuthoredA = GetPrimary(InOutSide[A.Index].OldPosition, Strategy);
			const float AuthoredB = GetPrimary(InOutSide[B.Index].OldPosition, Strategy);
			if (AuthoredA != AuthoredB)
			{
				return AuthoredA < AuthoredB;
			}
			const int32 NameCmp = InOutSide[A.Index].Name.Compare(InOutSide[B.Index].Name);
			if (NameCmp != 0)
			{
				return NameCmp < 0;
			}
			return InOutSide[A.Index].NodeGuid < InOutSide[B.Index].NodeGuid;
		});

		float PrimaryCursor = TNumericLimits<float>::Lowest();
		for (const FSideSlot& Slot : Slots)
		{
			FLayoutNode& Node = InOutSide[Slot.Index];
			const float Primary = FMath::Max(Slot.DesiredPrimary, PrimaryCursor);
			if (!Node.bPinned)
			{
				Node.NewPosition = MakePos(Primary, Slot.Secondary, Strategy);
			}
			PrimaryCursor = Primary + GetPrimaryExtent(Node.WidgetSize, Strategy) + InInput.ColumnGap;
		}
	}

	// A side-lane node sits above the flow, in the same band the near-side rails run through, so a rail
	// dropping back down to the flow can be drawn straight through it. Slide the whole lane along the
	// flow axis until nothing is, and no further: the smallest move that clears it keeps each node beside
	// the states it feeds. Both directions are tried, smallest first, because what is in the way can lie
	// on either side. Nothing is moved when the lane is already clear.
	void SettleSideLanePrimary(
		TArray<FLayoutNode>& InOutPlaced,
		const TArray<FLayoutEdge>& InEdges,
		const TArray<FLayoutReroute>& InReroutes,
		const FLayoutInput& InInput)
	{
		const ELayoutStrategy Strategy = InInput.Strategy;

		TArray<int32> SideIndices;
		TArray<FVector2f> BasePositions;
		float WidestSide = 0.0f;
		for (int32 NodeIdx = 0; NodeIdx < InOutPlaced.Num(); ++NodeIdx)
		{
			const FLayoutNode& Node = InOutPlaced[NodeIdx];
			if (Node.Lane != ELayoutLane::Side || Node.bPinned)
			{
				continue;
			}
			SideIndices.Add(NodeIdx);
			BasePositions.Add(Node.NewPosition);
			WidestSide = FMath::Max(WidestSide, GetPrimaryExtent(Node.WidgetSize, Strategy));
		}

		if (SideIndices.Num() == 0 || WidestSide <= 0.0f)
		{
			return;
		}

		const float OverlapPadding = InInput.RowGap * 0.5f;
		auto Score = [&InOutPlaced, &InEdges, &InReroutes, &InInput, OverlapPadding]()
		{
			return TPair<int32, int32>(
				CountEdgesDrawnThroughNodes(
					InOutPlaced, InEdges, InReroutes, OverlapPadding, InInput.RerouteSize),
				CountMarkersOverNodes(
					InOutPlaced, InEdges, InReroutes, InInput.TransitionMarkerSize, InInput.RerouteSize));
		};

		auto Apply = [&InOutPlaced, &SideIndices, &BasePositions, Strategy](float InOffset)
		{
			for (int32 Slot = 0; Slot < SideIndices.Num(); ++Slot)
			{
				InOutPlaced[SideIndices[Slot]].NewPosition = MakePos(
					GetPrimary(BasePositions[Slot], Strategy) + InOffset,
					GetSecondary(BasePositions[Slot], Strategy),
					Strategy);
			}
		};

		TPair<int32, int32> BestScore = Score();
		if (BestScore.Key == 0 && BestScore.Value == 0)
		{
			return;
		}

		static constexpr float Fractions[] =
			{ -0.25f, 0.25f, -0.5f, 0.5f, -0.75f, 0.75f, -1.0f, 1.0f, -1.5f, 1.5f };
		float BestOffset = 0.0f;
		for (const float Fraction : Fractions)
		{
			const float Offset = WidestSide * Fraction;
			Apply(Offset);
			const TPair<int32, int32> CandidateScore = Score();
			if (CandidateScore < BestScore)
			{
				BestScore = CandidateScore;
				BestOffset = Offset;
			}
			if (BestScore.Key == 0 && BestScore.Value == 0)
			{
				break;
			}
		}

		Apply(BestOffset);
	}

	// Half-open span along the primary axis, used to test whether two rails can share a lane.
	struct FLaneSpan
	{
		float Low = 0.0f;
		float High = 0.0f;

		bool Overlaps(const FLaneSpan& InOther) const
		{
			return Low < InOther.High && InOther.Low < High;
		}
	};

	// One edge the router carries on a rail (a pair of reroute nodes beside the flow) instead of drawing
	// it straight.
	struct FRoutedEdge
	{
		FGuid TransitionGuid;
		float SourceCenter = 0.0f;
		float DestinationCenter = 0.0f;
		bool bFarSide = false;

		float RangeMin() const { return FMath::Min(SourceCenter, DestinationCenter); }
		float RangeMax() const { return FMath::Max(SourceCenter, DestinationCenter); }
		float Span() const { return RangeMax() - RangeMin(); }
	};

	// Lowest lane on this side whose occupants leave room for InEdge, adding a lane when none does.
	int32 ClaimLane(TArray<TArray<FLaneSpan>>& InOutLanes, const FRoutedEdge& InEdge, float InClearance)
	{
		FLaneSpan Span;
		Span.Low = InEdge.RangeMin() - InClearance;
		Span.High = InEdge.RangeMax() + InClearance;

		for (int32 LaneIdx = 0; LaneIdx < InOutLanes.Num(); ++LaneIdx)
		{
			bool bFits = true;
			for (const FLaneSpan& Occupied : InOutLanes[LaneIdx])
			{
				if (Span.Overlaps(Occupied))
				{
					bFits = false;
					break;
				}
			}
			if (bFits)
			{
				InOutLanes[LaneIdx].Add(Span);
				return LaneIdx;
			}
		}

		InOutLanes.AddDefaulted();
		InOutLanes.Last().Add(Span);
		return InOutLanes.Num() - 1;
	}

	// Plan the reroute nodes for every edge whose marker would otherwise be drawn over a state. The
	// editor draws a transition's marker at the midpoint between the two states. An edge that skips a
	// layer puts its marker inside whatever sits between them. A cycle's back-edge puts it in the
	// middle of the row. Spacing the states further apart never fixes either, because the midpoint moves
	// with them.
	//
	// The fix is to carry the edge on a rail. One reroute sits beside the source and one beside the
	// destination, both the same distance out from the flow. The segment between them runs parallel to
	// the flow, so its marker lands in empty space. One reroute alone would leave two long diagonals
	// that still cross the row. Back-edges use the far side and forward skips the near side, so the two
	// never share a lane. Rails stack outward in lanes, widest first.
	void PlanReroutes(
		const TArray<FLayoutNode>& InPlaced,
		const TArray<FLayoutEdge>& InEdges,
		const TSet<TPair<FGuid, FGuid>>& InBackEdges,
		const TSet<FGuid>& InAlreadyRailed,
		const FLayoutInput& InInput,
		TArray<FLayoutReroute>& OutReroutes)
	{
		TMap<FGuid, const FLayoutNode*> NodeByGuid;
		NodeByGuid.Reserve(InPlaced.Num());
		float FlowMinSecondary = TNumericLimits<float>::Max();
		float FlowMaxSecondary = TNumericLimits<float>::Lowest();
		for (const FLayoutNode& Node : InPlaced)
		{
			NodeByGuid.Add(Node.NodeGuid, &Node);
			const float Secondary = GetSecondary(Node.NewPosition, InInput.Strategy);
			FlowMinSecondary = FMath::Min(FlowMinSecondary, Secondary);
			FlowMaxSecondary = FMath::Max(FlowMaxSecondary, Secondary + GetSecondaryExtent(Node.WidgetSize, InInput.Strategy));
		}
		if (FlowMinSecondary > FlowMaxSecondary)
		{
			return;
		}

		auto PrimaryCenter = [&InInput](const FLayoutNode& InNode)
		{
			return GetPrimary(InNode.NewPosition, InInput.Strategy)
				+ GetPrimaryExtent(InNode.WidgetSize, InInput.Strategy) * 0.5f;
		};

		TArray<FRoutedEdge> Routed;
		for (const FLayoutEdge& Edge : InEdges)
		{
			if (Edge.FromGuid == Edge.ToGuid || !Edge.TransitionGuid.IsValid()
				|| InAlreadyRailed.Contains(Edge.TransitionGuid))
			{
				continue;
			}

			const FLayoutNode* const* FromPtr = NodeByGuid.Find(Edge.FromGuid);
			const FLayoutNode* const* ToPtr = NodeByGuid.Find(Edge.ToGuid);
			if (!FromPtr || !ToPtr)
			{
				continue;
			}

			const FLayoutNode& From = **FromPtr;
			const FLayoutNode& To = **ToPtr;

			// A side-lane node fans out to the whole graph at runtime and has no layer. It has no span to
			// route and gets no rail.
			if (From.Lane != ELayoutLane::Main || To.Lane != ELayoutLane::Main
				|| From.Layer == INDEX_NONE || To.Layer == INDEX_NONE)
			{
				continue;
			}

			const bool bIsBackEdge = InBackEdges.Contains(TPair<FGuid, FGuid>(Edge.FromGuid, Edge.ToGuid));

			// Rail only what a straight wire would draw through. How many layers an edge spans says
			// nothing about whether anything is in its way: a long edge can run clear past an empty row,
			// and an edge between neighboring layers can still cut through a node ordering left between
			// them. A rail that avoids nothing is one more node for a reader to account for.
			const FVector2f FromCenter = From.NewPosition + From.WidgetSize * 0.5f;
			const FVector2f ToCenter = To.NewPosition + To.WidgetSize * 0.5f;
			if (!SegmentCrossesAnyNode(
				FromCenter, ToCenter, InPlaced, Edge.FromGuid, Edge.ToGuid, InInput.RowGap * 0.5f))
			{
				continue;
			}

			FRoutedEdge Entry;
			Entry.TransitionGuid = Edge.TransitionGuid;
			Entry.SourceCenter = PrimaryCenter(From);
			Entry.DestinationCenter = PrimaryCenter(To);
			Entry.bFarSide = bIsBackEdge;
			Routed.Add(MoveTemp(Entry));
		}

		if (Routed.Num() == 0)
		{
			return;
		}

		// Widest first, so a short rail lands in a lane a long one already opened instead of pushing
		// every later rail one lane further out.
		Routed.StableSort([](const FRoutedEdge& A, const FRoutedEdge& B)
		{
			if (A.Span() != B.Span())
			{
				return A.Span() > B.Span();
			}
			return A.TransitionGuid < B.TransitionGuid;
		});

		const float Clearance = InInput.RowGap * 2.0f;
		const float LaneStep = InInput.RerouteSize + InInput.RowGap;

		TArray<TArray<FLaneSpan>> NearLanes;
		TArray<TArray<FLaneSpan>> FarLanes;

		OutReroutes.Reserve(Routed.Num() * 2);
		for (const FRoutedEdge& Edge : Routed)
		{
			const int32 LaneIdx = ClaimLane(Edge.bFarSide ? FarLanes : NearLanes, Edge, InInput.RowGap);
			const float RailSecondary = Edge.bFarSide
				? FlowMaxSecondary + Clearance + LaneIdx * LaneStep
				: FlowMinSecondary - Clearance - InInput.RerouteSize - LaneIdx * LaneStep;

			const float HalfReroute = InInput.RerouteSize * 0.5f;
			for (int32 ChainIdx = 0; ChainIdx < 2; ++ChainIdx)
			{
				FLayoutReroute Reroute;
				Reroute.TransitionGuid = Edge.TransitionGuid;
				Reroute.ChainIndex = ChainIdx;
				Reroute.Position = MakePos(
					(ChainIdx == 0 ? Edge.SourceCenter : Edge.DestinationCenter) - HalfReroute,
					RailSecondary,
					InInput.Strategy);
				OutReroutes.Add(MoveTemp(Reroute));
			}
		}
	}

	// The entry node is never moved, so a node placed on top of it hides it and the graph appears to
	// have no entry point. The anchor keeps the flow clear of it. A caller that passed an explicit
	// origin, or pinned a node, can still put one there, and each case gets its own warning.
	void DetectEntryNodeOverlaps(const TArray<FLayoutNode>& InAll, const FLayoutInput& InInput, TArray<FString>& OutWarnings)
	{
		if (InInput.EntryNodeSize.X <= 0.0f || InInput.EntryNodeSize.Y <= 0.0f)
		{
			return;
		}

		const FVector2f EntryMin = InInput.EntryNodePosition;
		const FVector2f EntryMax = EntryMin + InInput.EntryNodeSize;
		for (const FLayoutNode& Node : InAll)
		{
			const FVector2f NodeMin = Node.NewPosition;
			const FVector2f NodeMax = NodeMin + Node.WidgetSize;
			if (EntryMin.X < NodeMax.X && NodeMin.X < EntryMax.X
				&& EntryMin.Y < NodeMax.Y && NodeMin.Y < EntryMax.Y)
			{
				OutWarnings.Add(FString::Printf(
					TEXT("'%s' is placed on top of the entry node, which hides it. %s"),
					*Node.Name,
					Node.bPinned
						? TEXT("The node is pinned, so the layout could not move it.")
						: TEXT("Drop start_x and start_y to anchor the flow past the entry node instead.")));
			}
		}
	}

	// How many pairs of nodes have overlapping boxes. The set counted is the flow nodes plus the entry
	// node rectangle, which is the set ld.get_graph_view reports overlaps over, so the two numbers agree.
	// The entry rectangle has to be in it: the entry node is never moved and reaches the layout only as a
	// position and a size, so leaving it out would score a state parked on top of it as no overlap at all
	// and then leave it there.
	int32 CountNodeOverlaps(const TArray<FLayoutNode>& InPlaced, const FLayoutInput& InInput)
	{
		TArray<FVector2f> Mins;
		TArray<FVector2f> Maxs;
		Mins.Reserve(InPlaced.Num() + 1);
		Maxs.Reserve(InPlaced.Num() + 1);
		for (const FLayoutNode& Node : InPlaced)
		{
			Mins.Add(Node.NewPosition);
			Maxs.Add(Node.NewPosition + Node.WidgetSize);
		}
		if (InInput.EntryNodeSize.X > 0.0f && InInput.EntryNodeSize.Y > 0.0f)
		{
			Mins.Add(InInput.EntryNodePosition);
			Maxs.Add(InInput.EntryNodePosition + InInput.EntryNodeSize);
		}

		int32 Total = 0;
		for (int32 FirstIdx = 0; FirstIdx < Mins.Num(); ++FirstIdx)
		{
			for (int32 SecondIdx = FirstIdx + 1; SecondIdx < Mins.Num(); ++SecondIdx)
			{
				const float OverlapX = FMath::Min(Maxs[FirstIdx].X, Maxs[SecondIdx].X) - FMath::Max(Mins[FirstIdx].X, Mins[SecondIdx].X);
				const float OverlapY = FMath::Min(Maxs[FirstIdx].Y, Maxs[SecondIdx].Y) - FMath::Max(Mins[FirstIdx].Y, Mins[SecondIdx].Y);
				if (OverlapX > 0.0f && OverlapY > 0.0f)
				{
					++Total;
				}
			}
		}
		return Total;
	}

	// What one arrangement of a graph costs a reader, as the four counts the layout compares, held
	// together so that a call site cannot pass them in the wrong order.
	//
	// The declaration order is the comparison order. Overlaps come first because a node hidden under
	// another cannot be read at all. A wire drawn through a state comes next: it crosses the whole box
	// and drags the reader off the path they were following. A marker on a state is smaller damage, one
	// icon that cannot be read or clicked, so it ranks below the wire. Reroutes come last but are
	// counted, because a rail is one more node to account for and the op adds rails without ever
	// removing one, so a marker cleared at the cost of eight rails leaves a graph that is harder to
	// follow than the one it replaced.
	struct FArrangementScore
	{
		int32 Overlaps = 0;
		int32 EdgesThroughStates = 0;
		int32 MarkersOverNodes = 0;
		int32 Reroutes = 0;

		bool operator<(const FArrangementScore& InOther) const
		{
			if (Overlaps != InOther.Overlaps)
			{
				return Overlaps < InOther.Overlaps;
			}
			if (EdgesThroughStates != InOther.EdgesThroughStates)
			{
				return EdgesThroughStates < InOther.EdgesThroughStates;
			}
			if (MarkersOverNodes != InOther.MarkersOverNodes)
			{
				return MarkersOverNodes < InOther.MarkersOverNodes;
			}
			return Reroutes < InOther.Reroutes;
		}
	};

	// Whether a reader is better off with the computed arrangement than with the one the nodes arrived
	// in. Only a strict win on the first count that differs is better, so a tie keeps whatever is
	// already in the graph.
	bool IsStrictlyBetterArrangement(const FArrangementScore& InProposed, const FArrangementScore& InCurrent)
	{
		return InProposed < InCurrent;
	}

	// Pinned nodes stay at their authored position. We don't try to push other nodes out of the
	// way (that would create cascading shifts). Instead, surface a warning when a pinned
	// position overlaps a non-pinned placement so the caller knows manual cleanup may be needed.
	void DetectPinnedOverlaps(const TArray<FLayoutNode>& InAll, TArray<FString>& OutWarnings)
	{
		for (int32 PinnedIdx = 0; PinnedIdx < InAll.Num(); ++PinnedIdx)
		{
			const FLayoutNode& Pinned = InAll[PinnedIdx];
			if (!Pinned.bPinned)
			{
				continue;
			}
			const FVector2f PinTL = Pinned.NewPosition;
			const FVector2f PinBR = PinTL + Pinned.WidgetSize;
			for (int32 OtherIdx = 0; OtherIdx < InAll.Num(); ++OtherIdx)
			{
				if (OtherIdx == PinnedIdx)
				{
					continue;
				}
				const FLayoutNode& Other = InAll[OtherIdx];
				if (Other.bPinned)
				{
					continue;
				}
				const FVector2f OtherTL = Other.NewPosition;
				const FVector2f OtherBR = OtherTL + Other.WidgetSize;
				const bool bOverlapX = PinTL.X < OtherBR.X && OtherTL.X < PinBR.X;
				const bool bOverlapY = PinTL.Y < OtherBR.Y && OtherTL.Y < PinBR.Y;
				if (bOverlapX && bOverlapY)
				{
					OutWarnings.Add(FString::Printf(
						TEXT("Pinned node '%s' overlaps proposed placement of '%s'; pinned position kept, manual cleanup may be needed."),
						*Pinned.Name, *Other.Name));
				}
			}
		}
	}

	// A grid snap rounds a reroute and the state it belongs to separately, so a reroute placed level with
	// its state's center can land up to half a grid cell off it, and the last leg into that state is
	// drawn as a shallow slope rather than the straight line the trunk is for. Put every fan reroute back
	// on its state's center across the flow once both have been snapped. Only that coordinate is
	// restored; the reroute stays on the grid along the flow, where every one of them shares a value.
	void LevelFanRails(
		const TArray<FLayoutNode>& InPlaced,
		const TMap<FGuid, FGuid>& InTargetByTransition,
		const FLayoutInput& InInput,
		TArray<FLayoutReroute>& InOutReroutes)
	{
		if (InTargetByTransition.Num() == 0)
		{
			return;
		}

		TMap<FGuid, const FLayoutNode*> NodeByGuid;
		NodeByGuid.Reserve(InPlaced.Num());
		for (const FLayoutNode& Node : InPlaced)
		{
			NodeByGuid.Add(Node.NodeGuid, &Node);
		}

		const ELayoutStrategy Strategy = InInput.Strategy;
		const float HalfReroute = InInput.RerouteSize * 0.5f;
		for (FLayoutReroute& Reroute : InOutReroutes)
		{
			const FGuid* TargetGuid = InTargetByTransition.Find(Reroute.TransitionGuid);
			if (!TargetGuid)
			{
				continue;
			}
			const FLayoutNode* const* TargetPtr = NodeByGuid.Find(*TargetGuid);
			if (!TargetPtr)
			{
				continue;
			}

			const FLayoutNode& Target = **TargetPtr;
			const float Secondary = GetSecondary(Target.NewPosition, Strategy)
				+ GetSecondaryExtent(Target.WidgetSize, Strategy) * 0.5f - HalfReroute;
			Reroute.Position = MakePos(GetPrimary(Reroute.Position, Strategy), Secondary, Strategy);
		}
	}

	void SnapAll(TArray<FLayoutNode>& InOutNodes, TArray<FLayoutReroute>& InOutReroutes, float InGridSize)
	{
		if (InGridSize <= 0.0f)
		{
			return;
		}
		auto Snap = [InGridSize](FVector2f& InOutPosition)
		{
			InOutPosition.X = FMath::RoundToFloat(InOutPosition.X / InGridSize) * InGridSize;
			InOutPosition.Y = FMath::RoundToFloat(InOutPosition.Y / InGridSize) * InGridSize;
		};

		for (FLayoutNode& Node : InOutNodes)
		{
			if (Node.bPinned)
			{
				continue;
			}
			Snap(Node.NewPosition);
		}
		for (FLayoutReroute& Reroute : InOutReroutes)
		{
			Snap(Reroute.Position);
		}
	}
}

namespace LD::Assist::Layout
{
	FLayoutGraphResult ComputeLayout(const FLayoutInput& In)
	{
		FLayoutGraphResult Result;

		TArray<FLayoutNode> Main;
		TArray<FLayoutNode> Side;
		Private::CategorizeAndPrepare(In, Main, Side, Result.Warnings);

		if (Main.Num() == 0 && Side.Num() == 0)
		{
			return Result;
		}

		TMap<FGuid, FString> NamesByGuid;
		NamesByGuid.Reserve(In.Nodes.Num());
		for (const FLayoutNode& Node : In.Nodes)
		{
			NamesByGuid.Add(Node.NodeGuid, Node.Name);
		}

		TMap<FGuid, TArray<FGuid>> Successors;
		TMap<FGuid, TArray<FGuid>> Predecessors;
		TSet<FGuid> SideLaneFed;
		Private::BuildAdjacency(Main, Side, In.Edges, Successors, Predecessors, SideLaneFed);

		const TSet<TPair<FGuid, FGuid>> BackEdges = Private::FindBackEdges(Successors, Main, In.EntryGuid, NamesByGuid, Result.Warnings);

		Private::AssignLayers(Main, Successors, Predecessors, BackEdges, In.EntryGuid, SideLaneFed, NamesByGuid, Result.Warnings);
		Private::LiftEntryLeafToOwnColumn(Main, Successors, Predecessors, In.EntryGuid);

		// The arrangement the nodes arrived in, taken before anything is placed and after the layers are
		// known, so a declined result still reports each node's layer. CategorizeAndPrepare has already
		// seeded NewPosition from OldPosition and filled in any widget size the caller could not measure,
		// so this is the graph as it stands, measured on the same terms as the computed layout.
		TArray<FLayoutNode> Current;
		Current.Reserve(Main.Num() + Side.Num());
		Current.Append(Main);
		Current.Append(Side);

		// Ordering places each candidate to score it, so the anchor has to be settled first.
		const FVector2f Start = Private::ResolveStart(In);

		// Found once, off the layers rather than any placement, because which states form a fan is a fact
		// about the graph. Every later pass that measures a candidate plans the same trunks for it.
		const TArray<Private::FLayoutFan> Fans = In.bRouteEdges
			? Private::FindFans(Main, In.Edges, BackEdges, In.MinFanTargets)
			: TArray<Private::FLayoutFan>();

		Result.OrderingScore = Private::OrderWithinLayers(
			Main, Successors, Predecessors, In.Edges, In, Start, In.bRespectExistingOrder);

		// Widened before the final placement, because every wire's slope depends on it and the crossing
		// count the widening minimises is measured on those wires.
		TMap<int32, float> ExtraGapAfterLayer;
		Private::WidenLayerGaps(Main, In.Edges, In, Start, Fans, ExtraGapAfterLayer);

		Private::AssignCoordinates(Main, In, Start, &ExtraGapAfterLayer);

		// The trunks go in before the side lane is placed and before any other rail is planned, because
		// both of those read the rails already in the plan to decide where they can go.
		TSet<FGuid> FanRailed;
		TMap<FGuid, FGuid> FanTargetByTransition;
		Private::PlanFanRails(Main, Fans, In, Result.Reroutes, FanRailed, &FanTargetByTransition);
		Result.FanRails = Result.Reroutes.Num();

		Private::PlaceSideLane(Side, Main, In.Edges, In, Start);

		TArray<FLayoutNode> Combined;
		Combined.Reserve(Main.Num() + Side.Num());
		Combined.Append(Main);
		Combined.Append(Side);

		if (In.bRouteEdges)
		{
			Private::PlanReroutes(Combined, In.Edges, BackEdges, FanRailed, In, Result.Reroutes);
		}

		Private::SettleSideLanePrimary(Combined, In.Edges, Result.Reroutes, In);

		Result.EdgesThroughStates = Private::CountEdgesDrawnThroughNodes(
			Combined, In.Edges, Result.Reroutes, In.RowGap * 0.5f, In.RerouteSize);
		Result.NodeOverlaps = Private::CountNodeOverlaps(Combined, In);
		Result.MarkersOverNodes = Private::CountMarkersOverNodes(
			Combined, In.Edges, Result.Reroutes, In.TransitionMarkerSize, In.RerouteSize);

		// The graph as it stands is measured with rails planned for it on the same terms, because the
		// caller strips the reroute nodes a graph already has out of the input and collapses a railed
		// transition back to one edge. Scoring it with an empty rail plan would charge it for rails it is
		// already carrying and hand the computed layout a win it did not earn.
		TArray<FLayoutReroute> CurrentReroutes;
		if (In.bRouteEdges)
		{
			TSet<FGuid> CurrentFanRailed;
			Private::PlanFanRails(Current, Fans, In, CurrentReroutes, CurrentFanRailed);
			Private::PlanReroutes(Current, In.Edges, BackEdges, CurrentFanRailed, In, CurrentReroutes);
		}
		Result.InputNodeOverlaps = Private::CountNodeOverlaps(Current, In);
		Result.InputMarkersOverNodes = Private::CountMarkersOverNodes(
			Current, In.Edges, CurrentReroutes, In.TransitionMarkerSize, In.RerouteSize);
		Result.InputEdgesThroughStates = Private::CountEdgesDrawnThroughNodes(
			Current, In.Edges, CurrentReroutes, In.RowGap * 0.5f, In.RerouteSize);
		Result.InputReroutes = CurrentReroutes.Num();

		Private::FArrangementScore Proposed;
		Proposed.Overlaps = Result.NodeOverlaps;
		Proposed.EdgesThroughStates = Result.EdgesThroughStates;
		Proposed.MarkersOverNodes = Result.MarkersOverNodes;
		Proposed.Reroutes = Result.Reroutes.Num();

		Private::FArrangementScore AsAuthored;
		AsAuthored.Overlaps = Result.InputNodeOverlaps;
		AsAuthored.EdgesThroughStates = Result.InputEdgesThroughStates;
		AsAuthored.MarkersOverNodes = Result.InputMarkersOverNodes;
		AsAuthored.Reroutes = Result.InputReroutes;

		if (In.bOnlyIfImproved && !Private::IsStrictlyBetterArrangement(Proposed, AsAuthored))
		{
			Result.bDeclined = true;
			Result.Reroutes.Reset();
			Result.FanRails = 0;

			// The order the ordering pass settled on was not applied either, and reporting its score
			// would describe a graph nobody is looking at.
			Result.OrderingScore = 0;

			Result.NodeOverlaps = AsAuthored.Overlaps;
			Result.EdgesThroughStates = AsAuthored.EdgesThroughStates;
			Result.MarkersOverNodes = AsAuthored.MarkersOverNodes;
			Combined = MoveTemp(Current);
			Result.Warnings.Add(FString::Printf(
				TEXT("Nothing was moved: the graph is already at least as good as this layout can make it. ")
				TEXT("Overlapping node pairs, transitions drawn through a state, transition markers drawn on ")
				TEXT("a state, then reroute nodes needed, compared in that order: the graph has %d, %d, %d and ")
				TEXT("%d, and the computed layout has %d, %d, %d and %d. The computed layout is used only when ")
				TEXT("it wins the first of the four that differs. Pass only_if_improved=false to lay the graph ")
				TEXT("out anyway."),
				AsAuthored.Overlaps,
				AsAuthored.EdgesThroughStates,
				AsAuthored.MarkersOverNodes,
				AsAuthored.Reroutes,
				Proposed.Overlaps,
				Proposed.EdgesThroughStates,
				Proposed.MarkersOverNodes,
				Proposed.Reroutes));
		}

		Private::DetectPinnedOverlaps(Combined, Result.Warnings);
		Private::DetectEntryNodeOverlaps(Combined, In, Result.Warnings);

		// Snapping moves a node, so a declined layout must not run it. The repro for that is a node
		// authored at a coordinate the grid does not land on: rounding it would move the node and dirty
		// the package for a run that reported it had changed nothing.
		if (In.bSnapToGrid && !Result.bDeclined)
		{
			Private::SnapAll(Combined, Result.Reroutes, In.SnapGridSize);
		}

		if (!Result.bDeclined)
		{
			Private::LevelFanRails(Combined, FanTargetByTransition, In, Result.Reroutes);
		}

		Result.Nodes = MoveTemp(Combined);
		return Result;
	}
}
