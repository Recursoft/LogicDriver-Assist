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

		// Off here so that every case about anchoring, layering, ordering and routing measures the layout
		// the algorithm computes. Most fixtures leave their nodes stacked on the origin, which the rule
		// scores as worse than anything and lays out anyway, but a fixture whose nodes already sit clear
		// of each other and of the entry node ties on all four measurements and would come back
		// untouched. Describe("declining") turns it on and owns it.
		Input.bOnlyIfImproved = false;
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

	// AddState leaves every node on the origin, which is a total tie on authored position and hands the
	// layer to the ranks below it. A case about authored order has to place its nodes itself.
	static FGuid AddStateAt(FLayoutInput& InOutInput, const TCHAR* InName, const FVector2f& InOldPosition, const TCHAR* InKind = TEXT("state"))
	{
		const FGuid Guid = AddState(InOutInput, InName, InKind);
		InOutInput.Nodes.Last().OldPosition = InOldPosition;
		return Guid;
	}

	// Priority is USMTransitionInstance::GetPriorityOrder, the order the source state evaluates its
	// transitions in. Lower is evaluated first.
	static FGuid AddEdgeWithPriority(FLayoutInput& InOutInput, const FGuid& InFrom, const FGuid& InTo, int32 InPriority)
	{
		const FGuid Guid = AddEdge(InOutInput, InFrom, InTo);
		InOutInput.Edges.Last().Priority = InPriority;
		return Guid;
	}

	// The names in one layer, in the order they were stacked. Left-to-right stacks along Y, which every
	// ordering case below uses.
	static TArray<FString> NamesInLayer(const FLayoutGraphResult& InResult, int32 InLayer)
	{
		TArray<const FLayoutNode*> Placed;
		for (const FLayoutNode& Node : InResult.Nodes)
		{
			if (Node.Layer == InLayer && Node.Lane == ELayoutLane::Main)
			{
				Placed.Add(&Node);
			}
		}
		Placed.Sort([](const FLayoutNode& A, const FLayoutNode& B)
		{
			return A.NewPosition.Y < B.NewPosition.Y;
		});

		TArray<FString> Names;
		Names.Reserve(Placed.Num());
		for (const FLayoutNode* Node : Placed)
		{
			Names.Add(Node->Name);
		}
		return Names;
	}

	// Where InName sits in one layer, or INDEX_NONE when it is not in it.
	static int32 IndexInLayer(const FLayoutGraphResult& InResult, int32 InLayer, const TCHAR* InName)
	{
		return NamesInLayer(InResult, InLayer).IndexOfByKey(FString(InName));
	}

	// A state as tall as InHeight. A fan only looks like a fan when its siblings stack into a column
	// taller than the gap the wires cross, which the 40 unit default state never does.
	static FGuid AddTallState(FLayoutInput& InOutInput, const TCHAR* InName, float InHeight)
	{
		const FGuid Guid = AddState(InOutInput, InName);
		InOutInput.Nodes.Last().WidgetSize = FVector2f(StateWidth, InHeight);
		return Guid;
	}

	// One hub feeding InCount tall siblings, which is the shape the trunk router exists for. The hub is
	// added first, so it is also the entry state and nothing is lifted out of the sibling layer.
	static FGuid AddFan(FLayoutInput& InOutInput, int32 InCount, TArray<FGuid>& OutTransitions)
	{
		const FGuid Hub = AddState(InOutInput, TEXT("Hub"));
		for (int32 SpokeIdx = 0; SpokeIdx < InCount; ++SpokeIdx)
		{
			const FString Name = FString::Printf(TEXT("Spoke%d"), SpokeIdx);
			const FGuid Spoke = AddTallState(InOutInput, *Name, 300.0f);
			OutTransitions.Add(AddEdge(InOutInput, Hub, Spoke));
		}
		return Hub;
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

	// Which node comes first inside a layer. A state machine's layers are usually one source state's
	// outgoing transitions, and nothing about that fan tells a barycenter sweep which order to put it in:
	// every spoke shares the one predecessor, so every spoke gets the same key. What fills that gap is
	// what these cases pin down.
	Describe("within-layer ordering", [this]()
	{
		It("keeps the authored order of a hub's spokes", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid Hub = AddState(Input, TEXT("Hub"));
			const FGuid Zulu = AddStateAt(Input, TEXT("Zulu"), FVector2f(600.0f, 0.0f));
			const FGuid Yankee = AddStateAt(Input, TEXT("Yankee"), FVector2f(600.0f, 200.0f));
			const FGuid Xray = AddStateAt(Input, TEXT("Xray"), FVector2f(600.0f, 400.0f));
			const FGuid Whiskey = AddStateAt(Input, TEXT("Whiskey"), FVector2f(600.0f, 600.0f));
			const FGuid Victor = AddStateAt(Input, TEXT("Victor"), FVector2f(600.0f, 800.0f));
			AddEdge(Input, Hub, Zulu);
			AddEdge(Input, Hub, Yankee);
			AddEdge(Input, Hub, Xray);
			AddEdge(Input, Hub, Whiskey);
			AddEdge(Input, Hub, Victor);

			// The authored order is the exact reverse of alphabetical, so a layer that comes back
			// alphabetical has thrown the author's arrangement away rather than merely failed to keep it.
			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const TArray<FString> Order = NamesInLayer(Result, 1);
			const TArray<FString> Expected = { TEXT("Zulu"), TEXT("Yankee"), TEXT("Xray"), TEXT("Whiskey"), TEXT("Victor") };
			TestEqual(TEXT("the spokes keep the order the author put them in"), Order, Expected);
		});

		It("reports the score the within-layer ordering chose by", [this]()
		{
			FLayoutInput ChainInput = MakeInput();
			AddChain(ChainInput, 3);
			const FLayoutGraphResult ChainResult = LD::Assist::Layout::ComputeLayout(ChainInput);

			// A chain has one node per layer, so no wire can be drawn through a third state and the
			// ordering has nothing to trade. A fan of five off one hub always draws the outer spokes past
			// the inner ones, whatever order they are in.
			TestEqual(TEXT("a chain scores zero"), ChainResult.OrderingScore, 0);

			FLayoutInput FanInput = MakeInput();
			const FGuid Hub = AddState(FanInput, TEXT("Hub"));
			for (int32 SpokeIdx = 0; SpokeIdx < 5; ++SpokeIdx)
			{
				const FString Name = FString::Printf(TEXT("Spoke%d"), SpokeIdx);
				AddEdge(FanInput, Hub, AddState(FanInput, *Name));
			}
			const FLayoutGraphResult FanResult = LD::Assist::Layout::ComputeLayout(FanInput);
			TestTrue(TEXT("a fan of five scores above zero"), FanResult.OrderingScore > 0);
		});

		It("orders one state's outgoing siblings by transition priority when nothing else does", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid Hub = AddState(Input, TEXT("Hub"));
			const FGuid Alpha = AddState(Input, TEXT("Alpha"));
			const FGuid Bravo = AddState(Input, TEXT("Bravo"));
			const FGuid Charlie = AddState(Input, TEXT("Charlie"));
			AddEdgeWithPriority(Input, Hub, Alpha, 2);
			AddEdgeWithPriority(Input, Hub, Bravo, 0);
			AddEdgeWithPriority(Input, Hub, Charlie, 1);

			// Every node is on the origin, so the authored position says nothing and the layer falls to
			// priority. The priorities are set against alphabetical order on purpose.
			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const TArray<FString> Order = NamesInLayer(Result, 1);
			const TArray<FString> Expected = { TEXT("Bravo"), TEXT("Charlie"), TEXT("Alpha") };
			TestEqual(TEXT("the fan reads in evaluation order"), Order, Expected);
		});

		It("does not compare priority across two different parents", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid Root = AddState(Input, TEXT("Root"));
			const FGuid Parent1 = AddState(Input, TEXT("Parent1"));
			const FGuid Parent2 = AddState(Input, TEXT("Parent2"));
			const FGuid Alpha = AddState(Input, TEXT("Alpha"));
			const FGuid Bravo = AddState(Input, TEXT("Bravo"));
			const FGuid Charlie = AddState(Input, TEXT("Charlie"));
			const FGuid Delta = AddState(Input, TEXT("Delta"));
			AddEdge(Input, Root, Parent1);
			AddEdge(Input, Root, Parent2);
			AddEdgeWithPriority(Input, Parent1, Alpha, 1);
			AddEdgeWithPriority(Input, Parent1, Delta, 0);
			AddEdgeWithPriority(Input, Parent2, Bravo, 1);
			AddEdgeWithPriority(Input, Parent2, Charlie, 0);

			// A priority is numbered per source state, so Parent1's 0 and Parent2's 0 mean nothing to
			// each other. Sorting the whole layer by a flat priority would interleave the two families.
			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const int32 AlphaIdx = IndexInLayer(Result, 2, TEXT("Alpha"));
			const int32 BravoIdx = IndexInLayer(Result, 2, TEXT("Bravo"));
			const int32 CharlieIdx = IndexInLayer(Result, 2, TEXT("Charlie"));
			const int32 DeltaIdx = IndexInLayer(Result, 2, TEXT("Delta"));
			if (AlphaIdx == INDEX_NONE || BravoIdx == INDEX_NONE || CharlieIdx == INDEX_NONE || DeltaIdx == INDEX_NONE)
			{
				AddError(TEXT("all four children should share one layer"));
				return;
			}

			TestEqual(TEXT("Parent1's children stay together"), FMath::Abs(AlphaIdx - DeltaIdx), 1);
			TestEqual(TEXT("Parent2's children stay together"), FMath::Abs(BravoIdx - CharlieIdx), 1);
			TestTrue(TEXT("Parent1's children read in evaluation order"), DeltaIdx < AlphaIdx);
			TestTrue(TEXT("Parent2's children read in evaluation order"), CharlieIdx < BravoIdx);
		});

		It("lets an authored position outrank a disagreeing priority", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid Hub = AddState(Input, TEXT("Hub"));
			const FGuid Alpha = AddStateAt(Input, TEXT("Alpha"), FVector2f(600.0f, 0.0f));
			const FGuid Bravo = AddStateAt(Input, TEXT("Bravo"), FVector2f(600.0f, 200.0f));
			const FGuid Charlie = AddStateAt(Input, TEXT("Charlie"), FVector2f(600.0f, 400.0f));
			AddEdgeWithPriority(Input, Hub, Alpha, 2);
			AddEdgeWithPriority(Input, Hub, Bravo, 1);
			AddEdgeWithPriority(Input, Hub, Charlie, 0);

			// The priorities are the exact reverse of the authored order. Only one of the two changes
			// behavior, and it is not the arrangement, so the arrangement is the one the layout may move
			// and the priorities are left where they are.
			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const TArray<FString> Order = NamesInLayer(Result, 1);
			const TArray<FString> Expected = { TEXT("Alpha"), TEXT("Bravo"), TEXT("Charlie") };
			TestEqual(TEXT("the authored order wins"), Order, Expected);
		});

		It("falls back to name when neither position nor priority decides", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid Hub = AddState(Input, TEXT("Hub"));
			const FGuid Charlie = AddState(Input, TEXT("Charlie"));
			const FGuid Alpha = AddState(Input, TEXT("Alpha"));
			const FGuid Bravo = AddState(Input, TEXT("Bravo"));
			AddEdge(Input, Hub, Charlie);
			AddEdge(Input, Hub, Alpha);
			AddEdge(Input, Hub, Bravo);

			// Every node is on the origin and every priority is the class default, which is what a graph
			// an agent has just built looks like. Name is what is left, and it has to decide, because the
			// alternative is UEdGraph::Nodes order and two runs of the same script would then differ.
			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const TArray<FString> Order = NamesInLayer(Result, 1);
			const TArray<FString> Expected = { TEXT("Alpha"), TEXT("Bravo"), TEXT("Charlie") };
			TestEqual(TEXT("the fan reads alphabetically"), Order, Expected);
		});

		It("produces the same order when a hub fan is fed its own result", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid Hub = AddState(Input, TEXT("Hub"));
			const FGuid Zulu = AddStateAt(Input, TEXT("Zulu"), FVector2f(600.0f, 0.0f));
			const FGuid Yankee = AddStateAt(Input, TEXT("Yankee"), FVector2f(600.0f, 200.0f));
			const FGuid Xray = AddStateAt(Input, TEXT("Xray"), FVector2f(600.0f, 400.0f));
			const FGuid Whiskey = AddStateAt(Input, TEXT("Whiskey"), FVector2f(600.0f, 600.0f));
			AddEdge(Input, Hub, Zulu);
			AddEdge(Input, Hub, Yankee);
			AddEdge(Input, Hub, Xray);
			AddEdge(Input, Hub, Whiskey);

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

			// The existing idempotence case is a chain, whose layers hold one node each, so it cannot see
			// a within-layer order at all.
			TestEqual(TEXT("the order is the same on a second run"), NamesInLayer(Second, 1), NamesInLayer(First, 1));
			TestEqual(TEXT("the score is the same on a second run"), Second.OrderingScore, First.OrderingScore);
		});
	});

	// Whether the layout should run at all. A graph someone arranged by hand is often already as good as
	// this algorithm can make it, and three of the state machines that prompted this were made worse by
	// running it. The comparison is overlapping node pairs, then transitions drawn through a state, then
	// transition markers drawn on a state, then reroute nodes needed, and only a strict win on the first
	// that differs counts.
	Describe("declining", [this]()
	{
		It("declines a layout that would only move a marker onto a state", [this]()
		{
			// Two states already clear of each other and of the entry node, so overlaps and crossings both
			// tie and the markers term is what decides. The layout can only equal it, so nothing moves.
			FLayoutInput Input = MakeInput();
			Input.bOnlyIfImproved = true;
			const FGuid First = AddStateAt(Input, TEXT("First"), FVector2f(400.0f, 0.0f));
			const FGuid Second = AddStateAt(Input, TEXT("Second"), FVector2f(700.0f, 0.0f));
			AddEdge(Input, First, Second);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestTrue(TEXT("nothing moved"), Result.bDeclined);
			TestEqual(TEXT("no rail is reported"), Result.Reroutes.Num(), 0);
			TestEqual(TEXT("no fan rail is reported"), Result.FanRails, 0);
			TestEqual(TEXT("the discarded ordering score is not reported"), Result.OrderingScore, 0);
			TestEqual(TEXT("the markers count repeats the input"), Result.MarkersOverNodes, Result.InputMarkersOverNodes);
			TestTrue(TEXT("the warning says why"), AnyWarningContains(Result, TEXT("transition markers drawn on")));
		});

		It("counts an overlap against the entry node", [this]()
		{
			FLayoutInput Input = MakeInput();
			AddState(Input, TEXT("Covering"));

			// The entry node is never moved and reaches the layout only as a rectangle, so a state parked
			// on top of it has to be counted here or nothing would ever move it off.
			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestEqual(TEXT("the state started on the entry node"), Result.InputNodeOverlaps, 1);
			TestEqual(TEXT("the layout moved it off"), Result.NodeOverlaps, 0);
		});

		It("declines when the only change would be to add a rail", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.bOnlyIfImproved = true;
			Input.EntryNodePosition = FVector2f(-300.0f, 0.0f);
			const FGuid Alpha = AddStateAt(Input, TEXT("Alpha"), FVector2f(0.0f, 0.0f));
			const FGuid Bravo = AddStateAt(Input, TEXT("Bravo"), FVector2f(400.0f, 0.0f));
			const FGuid Charlie = AddStateAt(Input, TEXT("Charlie"), FVector2f(200.0f, 400.0f));
			AddEdge(Input, Alpha, Bravo);
			AddEdge(Input, Bravo, Charlie);
			AddEdge(Input, Charlie, Alpha);

			// Three states in a cycle, placed as a triangle so the return edge runs clear of the state
			// between its ends. Laid out as a row that edge is drawn straight through the middle state and
			// needs two reroute nodes to lift it off. Both arrangements draw nothing through a state and
			// overlap nothing, so the triangle wins on the rails it does not need.
			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestTrue(TEXT("the layout declined"), Result.bDeclined);
			TestEqual(TEXT("no rail was planned"), Result.Reroutes.Num(), 0);
			TestTrue(TEXT("the reason is reported"), AnyWarningContains(Result, TEXT("Nothing was moved")));
			for (const FLayoutNode& Node : Result.Nodes)
			{
				TestEqual(FString::Printf(TEXT("'%s' did not move"), *Node.Name), Node.NewPosition, Node.OldPosition);
			}
		});

		It("reflows a graph whose node hides another even when the layout costs rails", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.bOnlyIfImproved = true;
			const FGuid Alpha = AddStateAt(Input, TEXT("Alpha"), FVector2f(0.0f, 0.0f));
			const FGuid Bravo = AddStateAt(Input, TEXT("Bravo"), FVector2f(400.0f, 0.0f));
			const FGuid Charlie = AddStateAt(Input, TEXT("Charlie"), FVector2f(200.0f, 400.0f));
			AddEdge(Input, Alpha, Bravo);
			AddEdge(Input, Bravo, Charlie);
			AddEdge(Input, Charlie, Alpha);

			// The same triangle, with the entry node left at the origin so Alpha covers it. The layout
			// costs two rails it did not need before, and it still runs, because a hidden node is worse
			// for a reader than any number of rails. That ordering of the four measurements is the whole
			// rule, and this is the case that proves the first one outranks the fourth.
			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestFalse(TEXT("the layout ran"), Result.bDeclined);
			TestEqual(TEXT("a node was hidden before"), Result.InputNodeOverlaps, 1);
			TestEqual(TEXT("nothing is hidden after"), Result.NodeOverlaps, 0);
			TestEqual(TEXT("the graph needed no rail before"), Result.InputReroutes, 0);
			TestEqual(TEXT("the layout paid two rails for it"), Result.Reroutes.Num(), 2);
		});

		It("reflows unconditionally when only_if_improved is false", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.EntryNodePosition = FVector2f(-300.0f, 0.0f);
			const FGuid Alpha = AddStateAt(Input, TEXT("Alpha"), FVector2f(0.0f, 0.0f));
			const FGuid Bravo = AddStateAt(Input, TEXT("Bravo"), FVector2f(400.0f, 0.0f));
			const FGuid Charlie = AddStateAt(Input, TEXT("Charlie"), FVector2f(200.0f, 400.0f));
			AddEdge(Input, Alpha, Bravo);
			AddEdge(Input, Bravo, Charlie);
			AddEdge(Input, Charlie, Alpha);

			// The fixture the case above declines. Turning the rule off has to lay it out anyway, because
			// that is the only way back to what the op did before the rule existed.
			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestFalse(TEXT("the layout ran"), Result.bDeclined);
			const FLayoutNode* Placed = FindNode(Result, Charlie);
			if (!TestNotNull(TEXT("Charlie placed"), Placed))
			{
				return;
			}
			TestNotEqual(TEXT("Charlie moved onto the flow"), Placed->NewPosition, FVector2f(200.0f, 400.0f));
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

	Describe("fanning out to a layer of siblings", [this]()
	{
		It("routes the trunk clear when another state stands on the hub's center line", [this]()
		{
			// A second state in the hub's own layer sits across the line the trunk would run down, so the
			// trunk has to move into the gap between the two layers instead. The bystander reaches layer 0
			// beside the hub by being fed from an any-state, which is what makes it a flow root without
			// giving it a predecessor in the flow.
			FLayoutInput Input = MakeInput();
			TArray<FGuid> Transitions;
			const FGuid Hub = AddFan(Input, 5, Transitions);
			const FGuid Bystander = AddTallState(Input, TEXT("Bystander"), 900.0f);
			const FGuid Any = AddState(Input, TEXT("Any"), TEXT("any_state"));
			AddEdge(Input, Any, Bystander);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* HubNode = FindNode(Result, Hub);
			const FLayoutNode* BystanderNode = FindNode(Result, Bystander);
			if (!TestNotNull(TEXT("hub placed"), HubNode) || !TestNotNull(TEXT("bystander placed"), BystanderNode))
			{
				return;
			}
			if (!TestEqual(TEXT("they share a layer"), BystanderNode->Layer, HubNode->Layer)
				|| !TestTrue(TEXT("the fan was railed"), Result.FanRails > 0))
			{
				return;
			}

			// The count is not asserted here. With the trunk in the gap, a spoke's first leg is a diagonal
			// from the hub rather than a vertical line, and it can still clip a layer-mate on the way out.
			// What this case pins is that the trunk moves rather than being drawn down through one.
			const float HubCenterX = HubNode->NewPosition.X + HubNode->WidgetSize.X * 0.5f;
			const float TrunkX = Result.Reroutes[0].Position.X + Input.RerouteSize * 0.5f;
			TestTrue(TEXT("the trunk left the hub's center line"), TrunkX > HubCenterX);
			TestTrue(
				TEXT("the trunk stops short of the sibling column"),
				TrunkX < FindNode(Result, Input.Nodes[1].NodeGuid)->NewPosition.X);
		});

		It("stops stretching a boundary when MaxExtraLayerGap is zero", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.bRouteEdges = false;
			Input.MaxExtraLayerGap = 0.0f;
			TArray<FGuid> Transitions;
			AddFan(Input, 5, Transitions);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* First = FindNode(Result, Input.Nodes[1].NodeGuid);
			if (!TestNotNull(TEXT("first spoke placed"), First))
			{
				return;
			}
			TestEqual(TEXT("the boundary stays at ColumnGap"), First->NewPosition.X, LayerX(1));
		});

		It("carries every steep wire of a fan on one trunk", [this]()
		{
			FLayoutInput Input = MakeInput();
			TArray<FGuid> Transitions;
			AddFan(Input, 5, Transitions);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);

			TSet<float> Lanes;
			float NearestLane = TNumericLimits<float>::Max();
			float FarthestLane = TNumericLimits<float>::Lowest();
			int32 Railed = 0;
			for (const FGuid& Transition : Transitions)
			{
				const TArray<const FLayoutReroute*> Rail = ReroutesFor(Result, Transition);
				if (Rail.Num() == 0)
				{
					continue;
				}
				if (!TestEqual(TEXT("a fan spoke takes one reroute, not a pair"), Rail.Num(), 1))
				{
					return;
				}
				Lanes.Add(Rail[0]->Position.X);
				NearestLane = FMath::Min(NearestLane, Rail[0]->Position.X);
				FarthestLane = FMath::Max(FarthestLane, Rail[0]->Position.X);
				++Railed;
			}

			TestEqual(TEXT("the four steep spokes are railed and the level one is not"), Railed, 4);
			TestEqual(TEXT("no two spokes share a lane"), Lanes.Num(), Railed);

			// Far enough apart to read as separate wires, close enough that the group still leaves the hub
			// as one bundle rather than spreading across the gap it has to cross.
			const FLayoutNode* HubNode = FindNode(Result, Input.Nodes[0].NodeGuid);
			const FLayoutNode* FirstSpoke = FindNode(Result, Input.Nodes[1].NodeGuid);
			if (!TestNotNull(TEXT("hub placed"), HubNode) || !TestNotNull(TEXT("spoke placed"), FirstSpoke))
			{
				return;
			}
			TestTrue(TEXT("the lanes stay clear of the sibling column"), FarthestLane < FirstSpoke->NewPosition.X);
			TestTrue(
				TEXT("the group is no wider than the gap it crosses"),
				FarthestLane - NearestLane < FirstSpoke->NewPosition.X - HubNode->NewPosition.X);
			TestEqual(TEXT("the reroutes are the only rails"), Result.FanRails, Result.Reroutes.Num());
			TestEqual(TEXT("no wire is left through a state"), Result.EdgesThroughStates, 0);
			TestEqual(TEXT("no marker is left on a state"), Result.MarkersOverNodes, 0);
		});

		It("leaves a fan below the threshold drawn straight", [this]()
		{
			FLayoutInput Input = MakeInput();
			TArray<FGuid> Transitions;
			AddFan(Input, 2, Transitions);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestEqual(TEXT("two siblings are not a fan"), Result.FanRails, 0);
		});

		It("widens the boundary instead when routing is off", [this]()
		{
			FLayoutInput Input = MakeInput();
			Input.bRouteEdges = false;
			TArray<FGuid> Transitions;
			AddFan(Input, 5, Transitions);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const TArray<FString> Spokes = NamesInLayer(Result, 1);
			if (!TestEqual(TEXT("five siblings in one layer"), Spokes.Num(), 5))
			{
				return;
			}
			const FLayoutNode* First = FindNode(Result, Input.Nodes[1].NodeGuid);
			if (!TestNotNull(TEXT("first spoke placed"), First))
			{
				return;
			}
			TestEqual(TEXT("no rail is planned"), Result.Reroutes.Num(), 0);
			TestTrue(TEXT("the boundary is stretched past ColumnGap"), First->NewPosition.X > LayerX(1));
		});
	});

	Describe("the entry state", [this]()
	{
		It("gets a column of its own when it is a dead end sharing a layer", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid Rest = AddState(Input, TEXT("Rest"));
			const FGuid Hub = AddState(Input, TEXT("Hub"));
			const FGuid Left = AddState(Input, TEXT("Left"));
			const FGuid Right = AddState(Input, TEXT("Right"));
			AddEdge(Input, Hub, Rest);
			AddEdge(Input, Hub, Left);
			AddEdge(Input, Hub, Right);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* RestNode = FindNode(Result, Rest);
			const FLayoutNode* HubNode = FindNode(Result, Hub);
			if (!TestNotNull(TEXT("entry state placed"), RestNode) || !TestNotNull(TEXT("hub placed"), HubNode))
			{
				return;
			}
			TestTrue(TEXT("the entry state is ahead of the state that feeds it"), RestNode->Layer < HubNode->Layer);
			TestEqual(TEXT("it has the column to itself"), NamesInLayer(Result, RestNode->Layer).Num(), 1);
			TestEqual(TEXT("the siblings it left keep one layer"), NamesInLayer(Result, HubNode->Layer + 1).Num(), 2);
		});

		It("stays in the flow when something outside the first layer feeds it", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid Rest = AddState(Input, TEXT("Rest"));
			const FGuid Start = AddState(Input, TEXT("Start"));
			const FGuid Middle = AddState(Input, TEXT("Middle"));
			const FGuid Other = AddState(Input, TEXT("Other"));
			AddEdge(Input, Start, Middle);
			AddEdge(Input, Start, Other);
			AddEdge(Input, Middle, Rest);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* RestNode = FindNode(Result, Rest);
			const FLayoutNode* MiddleNode = FindNode(Result, Middle);
			if (!TestNotNull(TEXT("entry state placed"), RestNode) || !TestNotNull(TEXT("feeder placed"), MiddleNode))
			{
				return;
			}
			TestEqual(TEXT("it stays one layer past the state that feeds it"), RestNode->Layer, MiddleNode->Layer + 1);
		});

		It("stays in the flow when it leads somewhere", [this]()
		{
			FLayoutInput Input = MakeInput();
			const FGuid Start = AddState(Input, TEXT("Start"));
			const FGuid Hub = AddState(Input, TEXT("Hub"));
			const FGuid Left = AddState(Input, TEXT("Left"));
			const FGuid Right = AddState(Input, TEXT("Right"));
			AddEdge(Input, Hub, Start);
			AddEdge(Input, Hub, Left);
			AddEdge(Input, Hub, Right);
			AddEdge(Input, Start, Left);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* StartNode = FindNode(Result, Start);
			const FLayoutNode* HubNode = FindNode(Result, Hub);
			if (!TestNotNull(TEXT("entry state placed"), StartNode) || !TestNotNull(TEXT("hub placed"), HubNode))
			{
				return;
			}
			TestEqual(TEXT("it stays one layer past the state that feeds it"), StartNode->Layer, HubNode->Layer + 1);
		});
	});

	Describe("markers", [this]()
	{
		It("counts a marker the arrangement leaves on a state and clears it", [this]()
		{
			FLayoutInput Input = MakeInput();
			TArray<FGuid> Transitions;
			AddFan(Input, 5, Transitions);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			TestTrue(TEXT("the stack every node arrived in puts markers on states"), Result.InputMarkersOverNodes > 0);
			TestEqual(TEXT("the computed layout leaves none"), Result.MarkersOverNodes, 0);
		});

		It("keeps a side-lane node clear of the rails above the flow", [this]()
		{
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 4);
			AddEdge(Input, Chain[0], Chain[2]);
			const FGuid Any = AddState(Input, TEXT("Any"), TEXT("any_state"));
			AddEdge(Input, Any, Chain[0]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* AnyNode = FindNode(Result, Any);
			if (!TestNotNull(TEXT("any-state placed"), AnyNode))
			{
				return;
			}
			TestEqual(TEXT("side lane"), static_cast<int32>(AnyNode->Lane), static_cast<int32>(ELayoutLane::Side));
			TestEqual(TEXT("nothing is drawn through a node"), Result.EdgesThroughStates, 0);
			TestEqual(TEXT("no marker is left on a state"), Result.MarkersOverNodes, 0);
		});

		It("puts a side-lane node beside the one state it feeds", [this]()
		{
			// The state it feeds is the last of a chain whose layers all sit on one row, so measuring the
			// lane against the whole graph and measuring it against that state give different answers
			// along the flow. Only the second keeps the wire short.
			FLayoutInput Input = MakeInput();
			const TArray<FGuid> Chain = AddChain(Input, 4);
			const FGuid Any = AddState(Input, TEXT("Any"), TEXT("any_state"));
			AddEdge(Input, Any, Chain[3]);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			const FLayoutNode* AnyNode = FindNode(Result, Any);
			const FLayoutNode* Target = FindNode(Result, Chain[3]);
			const FLayoutNode* First = FindNode(Result, Chain[0]);
			if (!TestNotNull(TEXT("any-state placed"), AnyNode) || !TestNotNull(TEXT("target placed"), Target)
				|| !TestNotNull(TEXT("first state placed"), First))
			{
				return;
			}
			TestTrue(
				TEXT("it is nearer the state it feeds than the start of the flow"),
				FMath::Abs(AnyNode->NewPosition.X - Target->NewPosition.X)
					< FMath::Abs(AnyNode->NewPosition.X - First->NewPosition.X));
			TestTrue(TEXT("it sits clear of the flow across it"), AnyNode->NewPosition.Y < Target->NewPosition.Y);
		});
	});

	Describe("grid snapping", [this]()
	{
		It("leaves every fan reroute level with the state it enters", [this]()
		{
			// A snap rounds a reroute and its state separately, so the leg into the state comes out as a
			// shallow slope unless something puts the reroute back. The grid is deliberately not a factor
			// of the row pitch, so the two round to different offsets.
			FLayoutInput Input = MakeInput();
			Input.bSnapToGrid = true;
			Input.SnapGridSize = 26.0f;
			TArray<FGuid> Transitions;
			AddFan(Input, 5, Transitions);

			const FLayoutGraphResult Result = LD::Assist::Layout::ComputeLayout(Input);
			if (!TestTrue(TEXT("the fan was railed"), Result.FanRails > 0))
			{
				return;
			}

			int32 Checked = 0;
			for (int32 SpokeIdx = 0; SpokeIdx < Transitions.Num(); ++SpokeIdx)
			{
				const TArray<const FLayoutReroute*> Rail = ReroutesFor(Result, Transitions[SpokeIdx]);
				if (Rail.Num() == 0)
				{
					continue;
				}
				const FLayoutNode* Target = FindNode(Result, Input.Nodes[SpokeIdx + 1].NodeGuid);
				if (!TestNotNull(TEXT("spoke target placed"), Target))
				{
					return;
				}
				const float RerouteCenter = Rail[0]->Position.Y + Input.RerouteSize * 0.5f;
				const float TargetCenter = Target->NewPosition.Y + Target->WidgetSize.Y * 0.5f;
				TestEqual(TEXT("the leg into the state is level"), RerouteCenter, TargetCenter);
				++Checked;
			}
			TestTrue(TEXT("at least one leg was checked"), Checked > 0);
		});
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
