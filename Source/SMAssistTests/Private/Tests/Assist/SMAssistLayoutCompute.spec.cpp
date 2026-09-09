// Copyright Recursoft LLC. All Rights Reserved.

#include "Layout/SMAssistLayout.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

// Drives LD::Assist::Layout::ComputeLayout directly. It is a pure function of FLayoutInput, so every
// rule about anchoring, layering, routing, and lanes is checked here without an editor, which the
// ld.layout_states op needs and which is fatal to open under -NullRHI.
BEGIN_DEFINE_SPEC(FSMAssistLayoutComputeSpec, "LogicDriver.Assist.LayoutCompute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	using FLayoutInput = LD::Assist::Layout::FLayoutInput;
	using FLayoutNode = LD::Assist::Layout::FLayoutNode;
	using FLayoutEdge = LD::Assist::Layout::FLayoutEdge;
	using FLayoutReroute = LD::Assist::Layout::FLayoutReroute;
	using FLayoutGraphResult = LD::Assist::Layout::FLayoutGraphResult;
	using ELayoutStrategy = LD::Assist::Layout::ELayoutStrategy;
	using ELayoutLane = LD::Assist::Layout::ELayoutLane;

	static constexpr float StateWidth = 100.0f;
	static constexpr float StateHeight = 40.0f;
	static constexpr float EntryWidth = 48.0f;
	static constexpr float EntryHeight = 40.0f;
	static constexpr float ColumnGap = 80.0f;
	static constexpr float RowGap = 40.0f;

	// Every state is 100 by 40, the entry node sits at the origin, and snapping is off so positions
	// can be asserted exactly. The first layer therefore starts at X 128 and is centered on Y 20.
	static FLayoutInput MakeInput()
	{
		FLayoutInput Input;
		Input.Strategy = ELayoutStrategy::LeftToRight;
		Input.ColumnGap = ColumnGap;
		Input.RowGap = RowGap;
		Input.bSnapToGrid = false;
		Input.EntryNodePosition = FVector2f::ZeroVector;
		Input.EntryNodeSize = FVector2f(EntryWidth, EntryHeight);
		return Input;
	}

	static FGuid AddState(FLayoutInput& InOutInput, const TCHAR* InName, const TCHAR* InKind = TEXT("state"))
	{
		FLayoutNode Node;
		Node.NodeGuid = FGuid::NewGuid();
		Node.Name = InName;
		Node.Kind = InKind;
		Node.WidgetSize = FVector2f(StateWidth, StateHeight);
		InOutInput.Nodes.Add(Node);
		if (!InOutInput.EntryGuid.IsValid())
		{
			InOutInput.EntryGuid = Node.NodeGuid;
		}
		return Node.NodeGuid;
	}

	static FGuid AddEdge(FLayoutInput& InOutInput, const FGuid& InFrom, const FGuid& InTo)
	{
		FLayoutEdge Edge;
		Edge.FromGuid = InFrom;
		Edge.ToGuid = InTo;
		Edge.TransitionGuid = FGuid::NewGuid();
		InOutInput.Edges.Add(Edge);
		return Edge.TransitionGuid;
	}

	// A chain of InCount states joined in order, returning their guids.
	static TArray<FGuid> AddChain(FLayoutInput& InOutInput, int32 InCount)
	{
		TArray<FGuid> Guids;
		for (int32 StateIdx = 0; StateIdx < InCount; ++StateIdx)
		{
			const FString Name = FString::Printf(TEXT("S%d"), StateIdx);
			Guids.Add(AddState(InOutInput, *Name));
			if (StateIdx > 0)
			{
				AddEdge(InOutInput, Guids[StateIdx - 1], Guids[StateIdx]);
			}
		}
		return Guids;
	}

	static const FLayoutNode* FindNode(const FLayoutGraphResult& InResult, const FGuid& InGuid)
	{
		return InResult.Nodes.FindByPredicate([&InGuid](const FLayoutNode& InNode)
		{
			return InNode.NodeGuid == InGuid;
		});
	}

	static TArray<const FLayoutReroute*> ReroutesFor(const FLayoutGraphResult& InResult, const FGuid& InTransition)
	{
		TArray<const FLayoutReroute*> Found;
		for (const FLayoutReroute& Reroute : InResult.Reroutes)
		{
			if (Reroute.TransitionGuid == InTransition)
			{
				Found.Add(&Reroute);
			}
		}
		Found.Sort([](const FLayoutReroute& A, const FLayoutReroute& B)
		{
			return A.ChainIndex < B.ChainIndex;
		});
		return Found;
	}

	static bool AnyWarningContains(const FLayoutGraphResult& InResult, const TCHAR* InText)
	{
		for (const FString& Warning : InResult.Warnings)
		{
			if (Warning.Contains(InText))
			{
				return true;
			}
		}
		return false;
	}

	// The expected X of layer InLayer when every state is StateWidth wide.
	static float LayerX(int32 InLayer)
	{
		return EntryWidth + ColumnGap + InLayer * (StateWidth + ColumnGap);
	}

END_DEFINE_SPEC(FSMAssistLayoutComputeSpec)

void FSMAssistLayoutComputeSpec::Define()
{
	Describe("anchoring", [this]()
	{
		It("starts the first layer one column gap past the entry node and centered on it", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid First = AddState(Input, TEXT("First"));

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* Node = FindNode(Result, First);
			if (!TestNotNull(TEXT("state placed"), Node))
			{
				return;
			}
			TestEqual(TEXT("X is one column gap past the entry node"), Node->NewPosition.X, LayerX(0));
			TestEqual(TEXT("Y centers the state on the entry node"), Node->NewPosition.Y, EntryHeight * 0.5f - StateHeight * 0.5f);
		});

		It("follows an entry node that is not at the origin", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.EntryNodePosition = FVector2f(-300.0f, 120.0f);
			const FGuid First = AddState(Input, TEXT("First"));

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* Node = FindNode(Result, First);
			if (!TestNotNull(TEXT("state placed"), Node))
			{
				return;
			}
			TestEqual(TEXT("X follows the entry node"), Node->NewPosition.X, -300.0f + EntryWidth + ColumnGap);
			TestEqual(TEXT("Y follows the entry node"), Node->NewPosition.Y, 120.0f + EntryHeight * 0.5f - StateHeight * 0.5f);
		});

		It("uses an explicit origin instead of the entry node", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.bStartExplicit = true;
			Input.Start = FVector2f(640.0f, 0.0f);
			const FGuid First = AddState(Input, TEXT("First"));

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* Node = FindNode(Result, First);
			if (!TestNotNull(TEXT("state placed"), Node))
			{
				return;
			}
			TestEqual(TEXT("X is the explicit origin"), Node->NewPosition.X, 640.0f);
			TestEqual(TEXT("Y centers the state on the explicit origin"), Node->NewPosition.Y, -StateHeight * 0.5f);
		});

		It("falls back to Start when there is no entry node to measure", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.EntryNodeSize = FVector2f::ZeroVector;
			const FGuid First = AddState(Input, TEXT("First"));

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* Node = FindNode(Result, First);
			if (!TestNotNull(TEXT("state placed"), Node))
			{
				return;
			}
			TestEqual(TEXT("X is Start.X"), Node->NewPosition.X, 0.0f);
		});

		It("anchors along Y for top_to_bottom", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.Strategy = ELayoutStrategy::TopToBottom;
			const FGuid First = AddState(Input, TEXT("First"));

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* Node = FindNode(Result, First);
			if (!TestNotNull(TEXT("state placed"), Node))
			{
				return;
			}
			TestEqual(TEXT("Y is one column gap below the entry node"), Node->NewPosition.Y, EntryHeight + ColumnGap);
			TestEqual(TEXT("X centers the state on the entry node"), Node->NewPosition.X, EntryWidth * 0.5f - StateWidth * 0.5f);
		});

		It("warns when an explicit origin puts a state on the entry node", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.bStartExplicit = true;
			Input.Start = FVector2f::ZeroVector;
			AddState(Input, TEXT("Covering"));

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestTrue(TEXT("the covered entry node is reported"), AnyWarningContains(Result, TEXT("'Covering' is placed on top of the entry node")));
			TestTrue(TEXT("the warning says how to fix it"), AnyWarningContains(Result, TEXT("Drop start_x and start_y")));
		});

		It("snaps states and reroutes to the grid when asked", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.bSnapToGrid = true;
			Input.SnapGridSize = 16.0f;
			Input.ColumnGap = 70.0f;
			const TArray<FGuid> Chain = AddChain(Input, 3);
			AddEdge(Input, Chain[2], Chain[0]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			for (const FLayoutNode& Node : Result.Nodes)
			{
				TestEqual(FString::Printf(TEXT("'%s' X is on the grid"), *Node.Name), FMath::Fmod(Node.NewPosition.X, 16.0f), 0.0f);
				TestEqual(FString::Printf(TEXT("'%s' Y is on the grid"), *Node.Name), FMath::Fmod(Node.NewPosition.Y, 16.0f), 0.0f);
			}
			TestEqual(TEXT("the back-edge got its two reroutes"), Result.Reroutes.Num(), 2);
			for (const FLayoutReroute& Reroute : Result.Reroutes)
			{
				TestEqual(TEXT("reroute X is on the grid"), FMath::Fmod(Reroute.Position.X, 16.0f), 0.0f);
				TestEqual(TEXT("reroute Y is on the grid"), FMath::Fmod(Reroute.Position.Y, 16.0f), 0.0f);
			}
		});
	});

	Describe("layering", [this]()
	{
		It("places a chain one layer per state, each a column gap apart", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 3);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			for (int32 StateIdx = 0; StateIdx < Chain.Num(); ++StateIdx)
			{
				const FLayoutNode* Node = FindNode(Result, Chain[StateIdx]);
				if (!TestNotNull(FString::Printf(TEXT("S%d placed"), StateIdx), Node))
				{
					return;
				}
				TestEqual(FString::Printf(TEXT("S%d layer"), StateIdx), Node->Layer, StateIdx);
				TestEqual(FString::Printf(TEXT("S%d X"), StateIdx), Node->NewPosition.X, LayerX(StateIdx));
				TestEqual(FString::Printf(TEXT("S%d Y stays on the row"), StateIdx), Node->NewPosition.Y, 0.0f);
			}
			TestEqual(TEXT("no warnings on a plain chain"), Result.Warnings.Num(), 0);
		});

		It("reverses a back-edge for layering and names it", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 3);
			AddEdge(Input, Chain[2], Chain[0]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* Last = FindNode(Result, Chain[2]);
			if (!TestNotNull(TEXT("S2 placed"), Last))
			{
				return;
			}
			TestEqual(TEXT("the cycle does not disturb the chain's layers"), Last->Layer, 2);
			TestTrue(TEXT("the reversed edge is named"), AnyWarningContains(Result, TEXT("Back-edge reversed for layering: 'S2' -> 'S0'")));
		});

		It("reports a node the entry cannot reach and places it at the end", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 2);
			const FGuid Orphan = AddState(Input, TEXT("Orphan"));

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* OrphanNode = FindNode(Result, Orphan);
			const FLayoutNode* Last = FindNode(Result, Chain[1]);
			if (!TestNotNull(TEXT("orphan placed"), OrphanNode) || !TestNotNull(TEXT("S1 placed"), Last))
			{
				return;
			}
			TestTrue(TEXT("the orphan is past the reachable flow"), OrphanNode->Layer > Last->Layer);
			TestTrue(TEXT("the orphan is named"), AnyWarningContains(Result, TEXT("1 node unreachable from entry, placed at layer end: ['Orphan']")));
		});

		It("keeps a pinned node where it was", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 2);
			Input.Nodes[1].OldPosition = FVector2f(1000.0f, 1000.0f);
			Input.PinnedGuids.Add(Chain[1]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* Pinned = FindNode(Result, Chain[1]);
			if (!TestNotNull(TEXT("pinned node present"), Pinned))
			{
				return;
			}
			TestTrue(TEXT("flagged as pinned"), Pinned->bPinned);
			TestEqual(TEXT("position untouched"), Pinned->NewPosition, FVector2f(1000.0f, 1000.0f));
		});

		It("places an any-state in a side lane above the flow", [this]()
		{
			FLayoutInput Input = MakeInput();
			AddChain(Input, 2);
			const FGuid Any = AddState(Input, TEXT("Any"), TEXT("any_state"));

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* AnyNode = FindNode(Result, Any);
			if (!TestNotNull(TEXT("any-state placed"), AnyNode))
			{
				return;
			}
			TestEqual(TEXT("lane"), AnyNode->Lane, ELayoutLane::Side);
			TestTrue(TEXT("sits above the main row"), AnyNode->NewPosition.Y + AnyNode->WidgetSize.Y <= 0.0f);
		});

		It("treats a state reached only through an any-state as part of the flow", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 3);
			const FGuid Any = AddState(Input, TEXT("Any"), TEXT("any_state"));
			const FGuid Dead = AddState(Input, TEXT("Dead"));
			AddEdge(Input, Any, Dead);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* DeadNode = FindNode(Result, Dead);
			const FLayoutNode* Last = FindNode(Result, Chain[2]);
			if (!TestNotNull(TEXT("Dead placed"), DeadNode) || !TestNotNull(TEXT("S2 placed"), Last))
			{
				return;
			}

			// An any-state's outgoing edges are dropped from the adjacency, because honoring them would
			// collapse every layer into one. Dead then has no predecessor left, but it is entered from
			// the any-state, so it belongs in the flow. Before this was handled it landed one layer past
			// the whole flow and drew a transition across the graph to reach it.
			TestTrue(TEXT("Dead sits inside the flow"), DeadNode->Layer <= Last->Layer);
			TestFalse(TEXT("nothing reported unreachable"),
				AnyWarningContains(Result, TEXT("unreachable from entry")));
		});

		It("produces the same layout when fed its own result", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 4);
			AddEdge(Input, Chain[0], Chain[2]);
			AddEdge(Input, Chain[3], Chain[0]);

			const FLayoutGraphResult First = LD::Assist::Layout::ComputeLayout(Input);
			for (FLayoutNode& Node : Input.Nodes)
			{
				const FLayoutNode* Placed = FindNode(First, Node.NodeGuid);
				if (Placed)
				{
					Node.OldPosition = Placed->NewPosition;
				}
			}
			const FLayoutGraphResult Second = LD::Assist::Layout::ComputeLayout(Input);

			for (const FLayoutNode& Node : First.Nodes)
			{
				const FLayoutNode* Again = FindNode(Second, Node.NodeGuid);
				if (!TestNotNull(FString::Printf(TEXT("'%s' placed again"), *Node.Name), Again))
				{
					return;
				}
				TestEqual(FString::Printf(TEXT("'%s' does not move on a second run"), *Node.Name), Again->NewPosition, Node.NewPosition);
			}
			TestEqual(TEXT("the same reroutes are planned"), Second.Reroutes.Num(), First.Reroutes.Num());
		});
	});

	Describe("routing", [this]()
	{
		// The router exists to lift a wire off a state, so what decides a rail is whether the wire would
		// draw through one. How many layers the edge spans decides nothing on its own.
		It("leaves a back-edge alone when nothing sits between its ends", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 2);
			const FGuid Back = AddEdge(Input, Chain[1], Chain[0]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);

			TestEqual(TEXT("the back-edge is not routed"), ReroutesFor(Result, Back).Num(), 0);
		});

		It("routes a back-edge that would draw through the state between its ends", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 3);
			const FGuid Back = AddEdge(Input, Chain[2], Chain[0]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);

			TestEqual(TEXT("the back-edge is routed on a two-reroute rail"), ReroutesFor(Result, Back).Num(), 2);
		});

		It("plans no reroute for an edge between adjacent layers", [this]()
		{
			FLayoutInput Input = MakeInput();
			AddChain(Input, 3);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestEqual(TEXT("no reroutes"), Result.Reroutes.Num(), 0);
		});

		It("carries a back-edge on two reroutes below the flow, source first", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 3);
			const FGuid BackEdge = AddEdge(Input, Chain[2], Chain[0]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const TArray<const FLayoutReroute*> Rail = ReroutesFor(Result, BackEdge);
			if (!TestEqual(TEXT("two reroutes on the back-edge"), Rail.Num(), 2))
			{
				return;
			}
			TestEqual(TEXT("only the back-edge is routed"), Result.Reroutes.Num(), 2);
			TestTrue(TEXT("the rail is below the row"), Rail[0]->Position.Y >= StateHeight && Rail[1]->Position.Y >= StateHeight);
			TestEqual(TEXT("both reroutes share a Y"), Rail[0]->Position.Y, Rail[1]->Position.Y);
			TestTrue(TEXT("chain index 0 sits by the source, which is the rightmost state"), Rail[0]->Position.X > Rail[1]->Position.X);
		});

		It("carries an edge that skips a layer on two reroutes above the flow", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 3);
			const FGuid Skip = AddEdge(Input, Chain[0], Chain[2]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const TArray<const FLayoutReroute*> Rail = ReroutesFor(Result, Skip);
			if (!TestEqual(TEXT("two reroutes on the skip edge"), Rail.Num(), 2))
			{
				return;
			}
			TestEqual(TEXT("only the skip edge is routed"), Result.Reroutes.Num(), 2);
			TestTrue(TEXT("the rail is above the row"), Rail[0]->Position.Y + Input.RerouteSize <= 0.0f);
			TestTrue(TEXT("chain index 0 sits by the source, which is the leftmost state"), Rail[0]->Position.X < Rail[1]->Position.X);
		});

		It("plans nothing when routing is off", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.bRouteEdges = false;
			const TArray<FGuid> Chain = AddChain(Input, 3);
			AddEdge(Input, Chain[2], Chain[0]);
			AddEdge(Input, Chain[0], Chain[2]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestEqual(TEXT("no reroutes"), Result.Reroutes.Num(), 0);
		});

		It("shares a lane between rails whose spans do not overlap and separates ones that do", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 6);
			const FGuid EarlySkip = AddEdge(Input, Chain[0], Chain[2]);
			const FGuid LateSkip = AddEdge(Input, Chain[3], Chain[5]);
			const FGuid OverlappingSkip = AddEdge(Input, Chain[1], Chain[3]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const TArray<const FLayoutReroute*> Early = ReroutesFor(Result, EarlySkip);
			const TArray<const FLayoutReroute*> Late = ReroutesFor(Result, LateSkip);
			const TArray<const FLayoutReroute*> Overlapping = ReroutesFor(Result, OverlappingSkip);
			if (!TestEqual(TEXT("early rail"), Early.Num(), 2) || !TestEqual(TEXT("late rail"), Late.Num(), 2)
				|| !TestEqual(TEXT("overlapping rail"), Overlapping.Num(), 2))
			{
				return;
			}
			TestEqual(TEXT("rails that do not overlap share a lane"), Early[0]->Position.Y, Late[0]->Position.Y);
			TestNotEqual(TEXT("a rail that overlaps another takes its own lane"), Early[0]->Position.Y, Overlapping[0]->Position.Y);
		});
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
