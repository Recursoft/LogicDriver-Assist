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
	void AssignCoordinates(TArray<FLayoutNode>& InOutMain, const FLayoutInput& InInput, const FVector2f& InStart);

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

	// Initial within-layer order = current secondary-axis position; one barycenter sweep
	// refines using neighbors in adjacent layers. Tiebreak on name then GUID for determinism.
	// Choose the within-layer order that draws fewest transitions through a state box. Each candidate
	// order is placed with the real coordinate pass and then measured, because the objective is about
	// geometry and the order by itself has none.
	void OrderWithinLayers(
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

		auto SortLayer = [&InOutMain, &InitialKey](TArray<int32>& InOutLayerIndices, const TMap<int32, double>& InKey)
		{
			Algo::StableSort(InOutLayerIndices, [&InOutMain, &InKey](int32 A, int32 B)
			{
				const double KeyA = InKey[A];
				const double KeyB = InKey[B];
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

		for (TPair<int32, TArray<int32>>& Layer : ByLayer)
		{
			SortLayer(Layer.Value, InitialKey);
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

		for (int32 SweepIdx = 0; SweepIdx < SweepCount && BestScore > 0; ++SweepIdx)
		{
			const TMap<int32, int32> Ranks = RanksByIndex();
			const TMap<int32, double> Keys = SweepKeys(SweepIdx % 2 == 0, Ranks);
			for (TPair<int32, TArray<int32>>& Layer : ByLayer)
			{
				SortLayer(Layer.Value, Keys);
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
	}

	// Place each non-pinned main-flow node. Layer primary coordinate is the cumulative sum of
	// preceding layers' primary extents plus one ColumnGap per layer boundary. Within a layer,
	// nodes stack along the secondary axis centered around Start.secondary; total stack height
	// is the sum of widget secondary extents plus (n-1)*RowGap.
	void AssignCoordinates(TArray<FLayoutNode>& InOutMain, const FLayoutInput& InInput, const FVector2f& InStart)
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

			CumulativePrimary += LayerPrimaryExtent[LayerKey] + InInput.ColumnGap;
		}
	}

	// Place AnyState side-lane nodes above (LR) / left of (TB) the main flow. Multiple
	// AnyStates stack along the primary axis ordered by their current primary coordinate so
	// authored intent is preserved when present.
	void PlaceSideLane(TArray<FLayoutNode>& InOutSide, const TArray<FLayoutNode>& InMain, const FLayoutInput& InInput, const FVector2f& InStart)
	{
		if (InOutSide.Num() == 0)
		{
			return;
		}

		float MainMinSecondary = GetSecondary(InStart, InInput.Strategy);
		float SideHeightTallest = 0.0f;
		float MainStartPrimary = GetPrimary(InStart, InInput.Strategy);
		if (InMain.Num() > 0)
		{
			MainMinSecondary = TNumericLimits<float>::Max();
			MainStartPrimary = TNumericLimits<float>::Max();
			for (const FLayoutNode& Node : InMain)
			{
				if (Node.bPinned)
				{
					continue;
				}
				MainMinSecondary = FMath::Min(MainMinSecondary, GetSecondary(Node.NewPosition, InInput.Strategy));
				MainStartPrimary = FMath::Min(MainStartPrimary, GetPrimary(Node.NewPosition, InInput.Strategy));
			}
			if (MainMinSecondary == TNumericLimits<float>::Max())
			{
				MainMinSecondary = GetSecondary(InStart, InInput.Strategy);
			}
			if (MainStartPrimary == TNumericLimits<float>::Max())
			{
				MainStartPrimary = GetPrimary(InStart, InInput.Strategy);
			}
		}

		for (const FLayoutNode& Node : InOutSide)
		{
			SideHeightTallest = FMath::Max(SideHeightTallest, GetSecondaryExtent(Node.WidgetSize, InInput.Strategy));
		}

		TArray<int32> Ordering;
		Ordering.Reserve(InOutSide.Num());
		for (int32 SideIdx = 0; SideIdx < InOutSide.Num(); ++SideIdx)
		{
			Ordering.Add(SideIdx);
		}
		Ordering.StableSort([&InOutSide, &InInput](int32 A, int32 B)
		{
			const float PrimaryA = GetPrimary(InOutSide[A].OldPosition, InInput.Strategy);
			const float PrimaryB = GetPrimary(InOutSide[B].OldPosition, InInput.Strategy);
			if (PrimaryA != PrimaryB)
			{
				return PrimaryA < PrimaryB;
			}
			const int32 NameCmp = InOutSide[A].Name.Compare(InOutSide[B].Name);
			if (NameCmp != 0)
			{
				return NameCmp < 0;
			}
			return InOutSide[A].NodeGuid < InOutSide[B].NodeGuid;
		});

		const float SideSecondary = MainMinSecondary - InInput.RowGap * 2.0f - SideHeightTallest;
		float PrimaryCursor = MainStartPrimary;
		for (int32 OrderIdx : Ordering)
		{
			FLayoutNode& Node = InOutSide[OrderIdx];
			if (!Node.bPinned)
			{
				Node.NewPosition = MakePos(PrimaryCursor, SideSecondary, InInput.Strategy);
			}
			PrimaryCursor += GetPrimaryExtent(Node.WidgetSize, InInput.Strategy) + InInput.ColumnGap;
		}
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
			if (Edge.FromGuid == Edge.ToGuid || !Edge.TransitionGuid.IsValid())
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

		// Ordering places each candidate to score it, so the anchor has to be settled first.
		const FVector2f Start = Private::ResolveStart(In);

		Private::OrderWithinLayers(Main, Successors, Predecessors, In.Edges, In, Start, In.bRespectExistingOrder);

		Private::AssignCoordinates(Main, In, Start);

		Private::PlaceSideLane(Side, Main, In, Start);

		TArray<FLayoutNode> Combined;
		Combined.Reserve(Main.Num() + Side.Num());
		Combined.Append(Main);
		Combined.Append(Side);

		if (In.bRouteEdges)
		{
			Private::PlanReroutes(Combined, In.Edges, BackEdges, In, Result.Reroutes);
		}

		Private::DetectPinnedOverlaps(Combined, Result.Warnings);
		Private::DetectEntryNodeOverlaps(Combined, In, Result.Warnings);

		if (In.bSnapToGrid)
		{
			Private::SnapAll(Combined, Result.Reroutes, In.SnapGridSize);
		}

		Result.Nodes = MoveTemp(Combined);
		return Result;
	}
}
