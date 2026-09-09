// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationResult.h"

#include "Helpers/SMTestHelpers.h"

#include "Blueprints/SMBlueprint.h"
#include "Graph/SMGraph.h"
#include "ISMAssetToolsModule.h"
#include "ISMGraphGeneration.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditor.h"
#include "Misc/App.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "Widgets/SWindow.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FSMGraphMeasureSpec, "LogicDriver.Assist.GraphMeasure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	FSMAssistOperationResult Run(const TCHAR* InOp, const TSharedRef<FJsonObject>& InArgs)
	{
		return GetSubsystem()->ExecuteOperation(FName(InOp), InArgs);
	}

	static FString Str(const FSMAssistOperationResult& InResult, const TCHAR* InField)
	{
		FString Value;
		if (InResult.Payload.IsValid())
		{
			InResult.Payload->TryGetStringField(InField, Value);
		}
		return Value;
	}

	FString CreateBlueprint()
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), FGuid::NewGuid().ToString());
		Args->SetStringField(TEXT("path"), FAssetHandler::DefaultGamePath());
		return Str(Run(TEXT("ld.create_blueprint"), Args), TEXT("asset_path"));
	}

	FString AddStateAt(const FString& InAsset, const FString& InName, double InX, double InY)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetStringField(TEXT("state_name"), InName);
		Args->SetNumberField(TEXT("position_x"), InX);
		Args->SetNumberField(TEXT("position_y"), InY);
		return Str(Run(TEXT("ld.add_state"), Args), TEXT("state_guid"));
	}

	FSMAssistOperationResult GetGraphView(const FString& InAsset)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		return Run(TEXT("ld.get_graph_view"), Args);
	}

	static int32 OverlapCount(const FSMAssistOperationResult& InResult)
	{
		const TArray<TSharedPtr<FJsonValue>>* Overlaps = nullptr;
		if (!InResult.Payload.IsValid() || !InResult.Payload->TryGetArrayField(TEXT("overlaps"), Overlaps))
		{
			return INDEX_NONE;
		}
		return Overlaps->Num();
	}

	static int32 TransitionOverlapCount(const FSMAssistOperationResult& InResult)
	{
		const TArray<TSharedPtr<FJsonValue>>* Overlaps = nullptr;
		if (!InResult.Payload.IsValid() || !InResult.Payload->TryGetArrayField(TEXT("transition_overlaps"), Overlaps))
		{
			return INDEX_NONE;
		}
		return Overlaps->Num();
	}

	FString AddTransition(const FString& InAsset, const FString& InFrom, const FString& InTo)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAsset);
		Args->SetStringField(TEXT("from_state_guid"), InFrom);
		Args->SetStringField(TEXT("to_state_guid"), InTo);
		return Str(Run(TEXT("ld.add_transition"), Args), TEXT("transition_guid"));
	}

	// How far the two boxes intersect, which is what lets a caller tell a graze from a full stack.
	static bool FirstOverlapExtent(const FSMAssistOperationResult& InResult, FVector2f& OutExtent)
	{
		const TArray<TSharedPtr<FJsonValue>>* Overlaps = nullptr;
		if (!InResult.Payload.IsValid() || !InResult.Payload->TryGetArrayField(TEXT("overlaps"), Overlaps) || Overlaps->Num() == 0)
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* Entry = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Extent = nullptr;
		if (!(*Overlaps)[0]->TryGetObject(Entry) || !(*Entry)->TryGetArrayField(TEXT("overlap_extent"), Extent) || Extent->Num() != 2)
		{
			return false;
		}

		OutExtent = FVector2f(static_cast<float>((*Extent)[0]->AsNumber()), static_cast<float>((*Extent)[1]->AsNumber()));
		return true;
	}

	static bool FindNodeWidgetSize(const FSMAssistOperationResult& InResult, const FString& InNodeGuid, FVector2f& OutSize)
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!InResult.Payload.IsValid() || !InResult.Payload->TryGetArrayField(TEXT("nodes"), Nodes))
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Nodes)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			FString Guid;
			if (!Value->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("node_guid"), Guid) || Guid != InNodeGuid)
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* Size = nullptr;
			if (!(*Entry)->TryGetArrayField(TEXT("widget_size"), Size) || Size->Num() != 2)
			{
				return false;
			}
			OutSize = FVector2f(static_cast<float>((*Size)[0]->AsNumber()), static_cast<float>((*Size)[1]->AsNumber()));
			return true;
		}
		return false;
	}

	// Reaches the live graph editor so the view can be read straight off it, rather than through the
	// panel_view the op reports, which would leave the op grading its own homework.
	static TSharedPtr<SGraphEditor> GetRootGraphEditor(const FString& InAssetPath)
	{
		USMBlueprint* Blueprint = Cast<USMBlueprint>(StaticLoadObject(USMBlueprint::StaticClass(), nullptr, *InAssetPath));
		if (!Blueprint)
		{
			return nullptr;
		}

		// The op frames this graph, not the ubergraph an ordinary blueprint would put first, and it resolves
		// it through this same interface.
		const TSharedPtr<ISMGraphGeneration> GraphGeneration = ISMAssetToolsModule::Get().GetGraphGenerationInterface();
		USMGraph* RootGraph = GraphGeneration.IsValid() ? GraphGeneration->GetRootStateMachineGraph(Blueprint) : nullptr;
		return RootGraph ? SGraphEditor::FindGraphEditorForGraph(RootGraph) : nullptr;
	}

	static SGraphPanel* GetRootGraphPanel(const FString& InAssetPath)
	{
		const TSharedPtr<SGraphEditor> GraphEditor = GetRootGraphEditor(InAssetPath);
		return GraphEditor.IsValid() ? GraphEditor->GetGraphPanel() : nullptr;
	}

	// Measures the two widgets here rather than reading the op's own overlap array, so a fault in the
	// overlap scan cannot report a graph as spaced while the boxes still intersect.
	static bool WidgetRectsIntersect(SGraphPanel* InPanel, const FString& InFirstGuid, const FString& InSecondGuid, bool& bOutResolved)
	{
		bOutResolved = false;

		FGuid FirstGuid;
		FGuid SecondGuid;
		if (!InPanel || !FGuid::Parse(InFirstGuid, FirstGuid) || !FGuid::Parse(InSecondGuid, SecondGuid))
		{
			return false;
		}

		// A panel drops its node widgets on a graph change and rebuilds them from a paint-time timer, so
		// reading straight after the op resolves them most of the time and intermittently does not. This
		// mirrors EnsureGraphPanelDrawn in SMAssistGraphView.cpp deliberately: the point of measuring here
		// is to be independent of the code under test, so it cannot borrow that helper's paints. Keep the
		// two in step if the production paint recipe changes.
		static constexpr int32 MaxResolveAttempts = 8;

		TSharedPtr<SGraphNode> First;
		TSharedPtr<SGraphNode> Second;
		for (int32 AttemptIdx = 0; AttemptIdx < MaxResolveAttempts && (!First.IsValid() || !Second.IsValid()); ++AttemptIdx)
		{
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication& App = FSlateApplication::Get();
				App.Tick(ESlateTickType::All);
				if (const TSharedPtr<SWindow> Window = App.FindWidgetWindow(InPanel->AsShared()))
				{
					App.ForceRedrawWindow(Window.ToSharedRef());
				}
			}
			First = InPanel->GetNodeWidgetFromGuid(FirstGuid);
			Second = InPanel->GetNodeWidgetFromGuid(SecondGuid);
		}

		if (!First.IsValid() || !Second.IsValid())
		{
			return false;
		}
		bOutResolved = true;

		const FVector2f FirstMin = First->GetPosition2f();
		const FVector2f FirstMax = FirstMin + First->GetDesiredSizeForMarquee2f();
		const FVector2f SecondMin = Second->GetPosition2f();
		const FVector2f SecondMax = SecondMin + Second->GetDesiredSizeForMarquee2f();

		return FirstMax.X > SecondMin.X && SecondMax.X > FirstMin.X
			&& FirstMax.Y > SecondMin.Y && SecondMax.Y > FirstMin.Y;
	}

	// Opening an asset editor headless fatals in FGenericWindow::GetRestoredDimensions via the deferred
	// RequestSavePersistentLayout ticker, so every op below that opens one is gated the same way
	// SMCaptureLocalGraph.spec.cpp gates its render half. Measuring node widgets additionally needs a panel
	// that can paint, which is the whole subject of these tests.
	bool CanMeasure()
	{
		if (FApp::CanEverRender() && FSlateApplication::IsInitialized())
		{
			return true;
		}
		AddInfo(TEXT("Rendering disabled (-NullRHI); skipping the measurement checks. They are exercised over MCP against a live editor."));
		return false;
	}

END_DEFINE_SPEC(FSMGraphMeasureSpec)

void FSMGraphMeasureSpec::Define()
{
	It("reports two states stacked on one position as overlapping", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		// Identical positions rather than a near-miss, so the assertion holds whatever the nodes measure.
		const FString First = AddStateAt(Asset, TEXT("Stacked1"), 400.0, 0.0);
		const FString Second = AddStateAt(Asset, TEXT("Stacked2"), 400.0, 0.0);
		if (!TestTrue(TEXT("states created"), !First.IsEmpty() && !Second.IsEmpty()))
		{
			return;
		}

		if (!CanMeasure())
		{
			return;
		}

		const FSMAssistOperationResult View = GetGraphView(Asset);
		if (!TestTrue(FString::Printf(TEXT("get_graph_view succeeded (%s)"), *View.ErrorMessage), View.bSuccess))
		{
			return;
		}
		TestTrue(TEXT("stacked states are reported as overlapping"), OverlapCount(View) > 0);
		TestEqual(TEXT("the transition array is present and empty with no transitions"), TransitionOverlapCount(View), 0);

		FVector2f Extent = FVector2f::ZeroVector;
		if (TestTrue(TEXT("the overlap carries a two-element extent"), FirstOverlapExtent(View, Extent)))
		{
			TestTrue(TEXT("the extent measures a real intersection"), Extent.X > 0.0f && Extent.Y > 0.0f);
		}

		bool bResolved = false;
		const bool bIntersect = WidgetRectsIntersect(GetRootGraphPanel(Asset), First, Second, bResolved);
		if (TestTrue(TEXT("both node widgets resolved"), bResolved))
		{
			TestTrue(TEXT("the widgets measured here intersect too"), bIntersect);
		}
	});

	It("reports no overlaps once layout_states has spaced the graph", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		const FString First = AddStateAt(Asset, TEXT("Stacked1"), 400.0, 0.0);
		const FString Second = AddStateAt(Asset, TEXT("Stacked2"), 400.0, 0.0);
		if (!TestTrue(TEXT("states created"), !First.IsEmpty() && !Second.IsEmpty()))
		{
			return;
		}

		const TSharedRef<FJsonObject> TransitionArgs = MakeShared<FJsonObject>();
		TransitionArgs->SetStringField(TEXT("asset_path"), Asset);
		TransitionArgs->SetStringField(TEXT("from_state_guid"), First);
		TransitionArgs->SetStringField(TEXT("to_state_guid"), Second);
		TestTrue(TEXT("transition created"), Run(TEXT("ld.add_transition"), TransitionArgs).bSuccess);

		if (!CanMeasure())
		{
			return;
		}

		const TSharedRef<FJsonObject> LayoutArgs = MakeShared<FJsonObject>();
		LayoutArgs->SetStringField(TEXT("asset_path"), Asset);
		LayoutArgs->SetBoolField(TEXT("apply"), true);
		const FSMAssistOperationResult Layout = Run(TEXT("ld.layout_states"), LayoutArgs);
		if (!TestTrue(FString::Printf(TEXT("layout_states succeeded (%s)"), *Layout.ErrorMessage), Layout.bSuccess))
		{
			return;
		}

		// The layout spaces by measured node size, so an overlap surviving it means the measurement
		// collapsed back to placeholder sizes.
		TestEqual(TEXT("layout leaves no overlapping states"), OverlapCount(GetGraphView(Asset)), 0);

		bool bResolved = false;
		const bool bIntersect = WidgetRectsIntersect(GetRootGraphPanel(Asset), First, Second, bResolved);
		if (TestTrue(TEXT("both node widgets resolved"), bResolved))
		{
			TestFalse(TEXT("the widgets measured here are spaced apart too"), bIntersect);
		}
	});

	It("leaves the panel view untouched, as a read-only op must", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}
		TestTrue(TEXT("state created"), !AddStateAt(Asset, TEXT("Only"), 400.0, 0.0).IsEmpty());

		if (!CanMeasure())
		{
			return;
		}

		// The first call is only here to open the editor.
		if (!TestTrue(TEXT("opening get_graph_view succeeded"), GetGraphView(Asset).bSuccess))
		{
			return;
		}

		const TSharedPtr<SGraphEditor> GraphEditor = GetRootGraphEditor(Asset);
		if (!TestTrue(TEXT("root graph editor resolved"), GraphEditor.IsValid()))
		{
			return;
		}

		// Park the view somewhere measuring would never leave it, and read the baseline back from the panel
		// so zoom snapping and offset clamping are already accounted for. Taking the baseline from a prior
		// identical call instead would pass even with the restore deleted: the sweep is deterministic, so
		// both calls would end on the same tile and the comparison would hold for the wrong reason.
		GraphEditor->SetViewLocation(FVector2f(-1234.0f, 567.0f), 0.5f);

		FVector2f BeforeOffset = FVector2f::ZeroVector;
		float BeforeZoom = 0.0f;
		GraphEditor->GetViewLocation(BeforeOffset, BeforeZoom);

		if (!TestTrue(TEXT("second get_graph_view succeeded"), GetGraphView(Asset).bSuccess))
		{
			return;
		}

		// Measuring pans and zooms the panel across the graph to reach every node, so an unrestored view
		// is the expected regression, and view_offset is what moves furthest.
		FVector2f AfterOffset = FVector2f::ZeroVector;
		float AfterZoom = 0.0f;
		GraphEditor->GetViewLocation(AfterOffset, AfterZoom);

		TestEqual(TEXT("zoom is unchanged by a read"), AfterZoom, BeforeZoom);
		TestTrue(TEXT("view offset is unchanged by a read"), AfterOffset.Equals(BeforeOffset));
	});

	It("reports stacked transition markers separately from stacked states", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		// Two long parallel edges close enough in Y that their markers, which sit at the midpoint of each
		// edge, land on each other. This is the defect the node scan is blind to by design: no amount of
		// respacing the states moves a marker off another marker.
		const FString FromA = AddStateAt(Asset, TEXT("FromA"), 0.0, 0.0);
		const FString ToA = AddStateAt(Asset, TEXT("ToA"), 900.0, 0.0);
		const FString FromB = AddStateAt(Asset, TEXT("FromB"), 0.0, 25.0);
		const FString ToB = AddStateAt(Asset, TEXT("ToB"), 900.0, 25.0);
		if (!TestTrue(TEXT("states created"), !FromA.IsEmpty() && !ToA.IsEmpty() && !FromB.IsEmpty() && !ToB.IsEmpty()))
		{
			return;
		}
		if (!TestTrue(TEXT("transitions created"),
			!AddTransition(Asset, FromA, ToA).IsEmpty() && !AddTransition(Asset, FromB, ToB).IsEmpty()))
		{
			return;
		}

		if (!CanMeasure())
		{
			return;
		}

		const FSMAssistOperationResult View = GetGraphView(Asset);
		if (!TestTrue(FString::Printf(TEXT("get_graph_view succeeded (%s)"), *View.ErrorMessage), View.bSuccess))
		{
			return;
		}

		AddInfo(FString::Printf(TEXT("overlaps=%d transition_overlaps=%d"),
			OverlapCount(View), TransitionOverlapCount(View)));
		TestTrue(TEXT("the stacked transition markers are reported"), TransitionOverlapCount(View) > 0);
	});

	It("does not report a reroute as colliding with the transition it was spliced into", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		const FString From = AddStateAt(Asset, TEXT("From"), 0.0, 0.0);
		const FString To = AddStateAt(Asset, TEXT("To"), 900.0, 0.0);
		if (!TestTrue(TEXT("states created"), !From.IsEmpty() && !To.IsEmpty()))
		{
			return;
		}

		const FString Transition = AddTransition(Asset, From, To);
		if (!TestTrue(TEXT("transition created"), !Transition.IsEmpty()))
		{
			return;
		}

		// A reroute is spliced onto the transition's own path, so it sits near that transition's marker by
		// construction. Scanning reroutes is only worth doing if that proximity does not read as a
		// collision, which is what this pins: adding one reroute the recommended way must stay quiet.
		const TSharedRef<FJsonObject> RerouteArgs = MakeShared<FJsonObject>();
		RerouteArgs->SetStringField(TEXT("asset_path"), Asset);
		RerouteArgs->SetStringField(TEXT("transition_guid"), Transition);
		RerouteArgs->SetNumberField(TEXT("position_x"), 450.0);
		RerouteArgs->SetNumberField(TEXT("position_y"), 200.0);
		if (!TestTrue(TEXT("reroute created"), Run(TEXT("ld.add_transition_reroute"), RerouteArgs).bSuccess))
		{
			return;
		}

		if (!CanMeasure())
		{
			return;
		}

		const FSMAssistOperationResult View = GetGraphView(Asset);
		if (!TestTrue(FString::Printf(TEXT("get_graph_view succeeded (%s)"), *View.ErrorMessage), View.bSuccess))
		{
			return;
		}

		AddInfo(FString::Printf(TEXT("with reroute: overlaps=%d transition_overlaps=%d"),
			OverlapCount(View), TransitionOverlapCount(View)));
		TestEqual(TEXT("a spliced reroute reports no marker collision"), TransitionOverlapCount(View), 0);
	});

	It("describes a rerouted transition by its segments and its rail order", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		const FString From = AddStateAt(Asset, TEXT("From"), 0.0, 0.0);
		const FString To = AddStateAt(Asset, TEXT("To"), 900.0, 0.0);
		if (!TestTrue(TEXT("states created"), !From.IsEmpty() && !To.IsEmpty()))
		{
			return;
		}

		const FString Transition = AddTransition(Asset, From, To);
		if (!TestTrue(TEXT("transition created"), !Transition.IsEmpty()))
		{
			return;
		}

		const TSharedRef<FJsonObject> RerouteArgs = MakeShared<FJsonObject>();
		RerouteArgs->SetStringField(TEXT("asset_path"), Asset);
		RerouteArgs->SetStringField(TEXT("transition_guid"), Transition);
		RerouteArgs->SetNumberField(TEXT("position_x"), 450.0);
		RerouteArgs->SetNumberField(TEXT("position_y"), 200.0);
		if (!TestTrue(TEXT("reroute created"), Run(TEXT("ld.add_transition_reroute"), RerouteArgs).bSuccess))
		{
			return;
		}

		if (!CanMeasure())
		{
			return;
		}

		const FSMAssistOperationResult View = GetGraphView(Asset);
		if (!TestTrue(FString::Printf(TEXT("get_graph_view succeeded (%s)"), *View.ErrorMessage), View.bSuccess))
		{
			return;
		}

		// One reroute splits the transition into two drawn segments. Both report the same two states, so
		// only primary_transition_guid says they are one transition, and only the segment fields say where
		// each piece runs.
		const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
		if (!TestTrue(TEXT("payload has 'transitions'"), View.Payload->TryGetArrayField(TEXT("transitions"), Transitions)))
		{
			return;
		}
		TestEqual(TEXT("the rail draws two segments"), Transitions->Num(), 2);

		TSet<FString> PrimaryGuids;
		TSet<FString> SegmentEnds;
		for (const TSharedPtr<FJsonValue>& Value : *Transitions)
		{
			const TSharedPtr<FJsonObject>& Entry = Value->AsObject();
			FString Primary;
			if (Entry->TryGetStringField(TEXT("primary_transition_guid"), Primary))
			{
				PrimaryGuids.Add(Primary);
			}
			FString SegmentFrom;
			FString SegmentTo;
			Entry->TryGetStringField(TEXT("segment_from_guid"), SegmentFrom);
			Entry->TryGetStringField(TEXT("segment_to_guid"), SegmentTo);
			SegmentEnds.Add(SegmentFrom);
			SegmentEnds.Add(SegmentTo);
		}
		TestEqual(TEXT("both segments name one transition"), PrimaryGuids.Num(), 1);

		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!TestTrue(TEXT("payload has 'nodes'"), View.Payload->TryGetArrayField(TEXT("nodes"), Nodes)))
		{
			return;
		}

		int32 RerouteCount = 0;
		for (const TSharedPtr<FJsonValue>& Value : *Nodes)
		{
			const TSharedPtr<FJsonObject>& Entry = Value->AsObject();
			FString Kind;
			Entry->TryGetStringField(TEXT("kind"), Kind);
			if (Kind != TEXT("reroute"))
			{
				continue;
			}

			++RerouteCount;

			FString OwningTransition;
			TestTrue(TEXT("the reroute names its transition"),
				Entry->TryGetStringField(TEXT("transition_guid"), OwningTransition));
			TestTrue(TEXT("the reroute names the transition its segments report"),
				PrimaryGuids.Contains(OwningTransition));

			double ChainIndex = -1.0;
			TestTrue(TEXT("the reroute reports its place on the rail"),
				Entry->TryGetNumberField(TEXT("chain_index"), ChainIndex));
			TestEqual(TEXT("the only reroute is first on the rail"), static_cast<int32>(ChainIndex), 0);

			FString RerouteGuid;
			Entry->TryGetStringField(TEXT("node_guid"), RerouteGuid);
			TestTrue(TEXT("a segment is drawn to the reroute"), SegmentEnds.Contains(RerouteGuid));
		}
		TestEqual(TEXT("one reroute is reported"), RerouteCount, 1);

		TestTrue(TEXT("the rail still runs between the two states"),
			SegmentEnds.Contains(From) && SegmentEnds.Contains(To));
	});

	It("measures a bare state at its rendered size rather than a placeholder", [this]()
	{
		const FString Asset = CreateBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !Asset.IsEmpty()))
		{
			return;
		}

		// A node's width tracks its display name, which is the property the authoring guidance tells agents
		// to budget for. Two names far apart in length pin that down without hardcoding a rendered width.
		const FString Short = AddStateAt(Asset, TEXT("Ok"), 400.0, 0.0);
		const FString Long = AddStateAt(Asset, TEXT("AStateWithAVeryMuchLongerDisplayName"), 400.0, 400.0);
		if (!TestTrue(TEXT("states created"), !Short.IsEmpty() && !Long.IsEmpty()))
		{
			return;
		}

		if (!CanMeasure())
		{
			return;
		}

		const FSMAssistOperationResult View = GetGraphView(Asset);
		if (!TestTrue(FString::Printf(TEXT("get_graph_view succeeded (%s)"), *View.ErrorMessage), View.bSuccess))
		{
			return;
		}

		FVector2f ShortSize = FVector2f::ZeroVector;
		FVector2f LongSize = FVector2f::ZeroVector;
		if (!TestTrue(TEXT("both states report a widget_size"),
			FindNodeWidgetSize(View, Short, ShortSize) && FindNodeWidgetSize(View, Long, LongSize)))
		{
			return;
		}

		AddInfo(FString::Printf(TEXT("widget_size short=%.1fx%.1f long=%.1fx%.1f"),
			ShortSize.X, ShortSize.Y, LongSize.X, LongSize.Y));

		// The suite's only absolute check. Everything else here is relative, so a measurement that
		// collapsed to placeholder sizes uniformly would still report no overlaps and pass. Placeholder
		// widths do not track the title, so a long name measuring far wider than a short one is the
		// evidence that these are rendered sizes.
		TestTrue(FString::Printf(TEXT("the long name measures wider (%.1f vs %.1f)"), LongSize.X, ShortSize.X),
			LongSize.X > ShortSize.X + 100.0f);
		TestTrue(FString::Printf(TEXT("heights are rendered (%.1f, %.1f)"), ShortSize.Y, LongSize.Y),
			ShortSize.Y >= 30.0f && LongSize.Y >= 30.0f);
	});

	It("errors when asset_path is missing", [this]()
	{
		TestFalse(TEXT("missing asset_path fails"), Run(TEXT("ld.get_graph_view"), MakeShared<FJsonObject>()).bSuccess);
	});

	It("errors on an asset that does not exist", [this]()
	{
		TestFalse(TEXT("unknown asset fails"), GetGraphView(TEXT("/Game/SMGraphMeasureSpec/DoesNotExist")).bSuccess);
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
