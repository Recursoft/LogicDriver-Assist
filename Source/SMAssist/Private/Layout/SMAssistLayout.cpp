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

	bool IsLinkStateKind(const FString& InKind)
	{
		return InKind == TEXT("link_state");
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

	// Categorize input nodes into main flow vs side lane, fill in any missing widget sizes,
	// and stamp each node's bPinned flag from PinnedGuids.
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
	void BuildAdjacency(
		const TArray<FLayoutNode>& InMain,
		const TArray<FLayoutNode>& InSide,
		const TArray<FLayoutEdge>& InEdges,
		TMap<FGuid, TArray<FGuid>>& OutSuccessors,
		TMap<FGuid, TArray<FGuid>>& OutPredecessors)
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
	// kept predecessors. Reachable-from-entry nodes are layered first. Unreachable nodes are
	// placed at one layer past the reachable maximum so they cluster at the right end of the
	// flow with a warning identifying them.
	void AssignLayers(
		TArray<FLayoutNode>& InOutMain,
		const TMap<FGuid, TArray<FGuid>>& InSuccessors,
		const TMap<FGuid, TArray<FGuid>>& InPredecessors,
		const TSet<TPair<FGuid, FGuid>>& InBackEdges,
		const FGuid& InEntryGuid,
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

		// LinkStates with predecessors get their natural topological layer (one past the
		// deepest predecessor) so the inbound transition stays short. Orphan LinkStates with
		// no predecessors still land at the unreachable-end layer via the same path the
		// "unreachable from entry" logic above takes for any other orphan node, which keeps
		// the "forwarder lives at the end" intuition in the case where it actually applies.

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
	void OrderWithinLayers(
		TArray<FLayoutNode>& InOutMain,
		const TMap<FGuid, TArray<FGuid>>& InSuccessors,
		const TMap<FGuid, TArray<FGuid>>& InPredecessors,
		ELayoutStrategy InStrategy,
		bool bRespectExistingOrder)
	{
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

		// Position-rank within the current order (used as input to the barycenter pass).
		TMap<int32, int32> RankByIndex;
		RankByIndex.Reserve(InOutMain.Num());
		for (const TPair<int32, TArray<int32>>& Layer : ByLayer)
		{
			for (int32 PositionIdx = 0; PositionIdx < Layer.Value.Num(); ++PositionIdx)
			{
				RankByIndex.Add(Layer.Value[PositionIdx], PositionIdx);
			}
		}

		// One barycenter sweep: each node's new key = mean rank of neighbors in adjacent layers.
		TMap<int32, double> BaryKey;
		BaryKey.Reserve(InOutMain.Num());
		for (int32 NodeIdx = 0; NodeIdx < InOutMain.Num(); ++NodeIdx)
		{
			const FLayoutNode& Node = InOutMain[NodeIdx];
			double Sum = 0.0;
			int32 Count = 0;
			if (const TArray<FGuid>* Preds = InPredecessors.Find(Node.NodeGuid))
			{
				for (const FGuid& Pred : *Preds)
				{
					if (const int32* PredIdx = IndexByGuid.Find(Pred))
					{
						if (const int32* PredRank = RankByIndex.Find(*PredIdx))
						{
							Sum += static_cast<double>(*PredRank);
							++Count;
						}
					}
				}
			}
			if (const TArray<FGuid>* Succs = InSuccessors.Find(Node.NodeGuid))
			{
				for (const FGuid& Succ : *Succs)
				{
					if (const int32* SuccIdx = IndexByGuid.Find(Succ))
					{
						if (const int32* SuccRank = RankByIndex.Find(*SuccIdx))
						{
							Sum += static_cast<double>(*SuccRank);
							++Count;
						}
					}
				}
			}
			BaryKey.Add(NodeIdx, Count > 0 ? (Sum / Count) : InitialKey[NodeIdx]);
		}

		for (TPair<int32, TArray<int32>>& Layer : ByLayer)
		{
			SortLayer(Layer.Value, BaryKey);
		}

		// Repack InOutMain in (layer, within-layer rank) order so AssignCoordinates can iterate
		// sequentially without consulting a separate rank map.
		TArray<FLayoutNode> Reordered;
		Reordered.Reserve(InOutMain.Num());
		TArray<int32> SortedLayerKeys;
		ByLayer.GenerateKeyArray(SortedLayerKeys);
		SortedLayerKeys.Sort();
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
	void AssignCoordinates(TArray<FLayoutNode>& InOutMain, const FLayoutInput& InInput)
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

		float CumulativePrimary = GetPrimary(InInput.Start, InInput.Strategy);
		const float StartSecondary = GetSecondary(InInput.Start, InInput.Strategy);

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
	void PlaceSideLane(TArray<FLayoutNode>& InOutSide, const TArray<FLayoutNode>& InMain, const FLayoutInput& InInput)
	{
		if (InOutSide.Num() == 0)
		{
			return;
		}

		float MainMinSecondary = GetSecondary(InInput.Start, InInput.Strategy);
		float SideHeightTallest = 0.0f;
		float MainStartPrimary = GetPrimary(InInput.Start, InInput.Strategy);
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
				MainMinSecondary = GetSecondary(InInput.Start, InInput.Strategy);
			}
			if (MainStartPrimary == TNumericLimits<float>::Max())
			{
				MainStartPrimary = GetPrimary(InInput.Start, InInput.Strategy);
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

	void SnapAll(TArray<FLayoutNode>& InOutNodes, float InGridSize)
	{
		if (InGridSize <= 0.0f)
		{
			return;
		}
		for (FLayoutNode& Node : InOutNodes)
		{
			if (Node.bPinned)
			{
				continue;
			}
			Node.NewPosition.X = FMath::RoundToFloat(Node.NewPosition.X / InGridSize) * InGridSize;
			Node.NewPosition.Y = FMath::RoundToFloat(Node.NewPosition.Y / InGridSize) * InGridSize;
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
		Private::BuildAdjacency(Main, Side, In.Edges, Successors, Predecessors);

		const TSet<TPair<FGuid, FGuid>> BackEdges = Private::FindBackEdges(Successors, Main, In.EntryGuid, NamesByGuid, Result.Warnings);

		Private::AssignLayers(Main, Successors, Predecessors, BackEdges, In.EntryGuid, NamesByGuid, Result.Warnings);

		Private::OrderWithinLayers(Main, Successors, Predecessors, In.Strategy, In.bRespectExistingOrder);

		Private::AssignCoordinates(Main, In);

		Private::PlaceSideLane(Side, Main, In);

		TArray<FLayoutNode> Combined;
		Combined.Reserve(Main.Num() + Side.Num());
		Combined.Append(Main);
		Combined.Append(Side);

		Private::DetectPinnedOverlaps(Combined, Result.Warnings);

		if (In.bSnapToGrid)
		{
			Private::SnapAll(Combined, In.SnapGridSize);
		}

		Result.Nodes = MoveTemp(Combined);
		return Result;
	}
}
