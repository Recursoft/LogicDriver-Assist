// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Math/Vector2D.h"
#include "Misc/Guid.h"

class UEdGraphNode;

namespace LD::Assist::Layout
{
	enum class ELayoutStrategy : uint8
	{
		LeftToRight,
		TopToBottom
	};

	enum class ELayoutLane : uint8
	{
		Main,
		Side
	};

	struct FLayoutNode
	{
		UEdGraphNode* Node = nullptr;
		FGuid NodeGuid;
		FString Name;
		FString Kind;
		FVector2f WidgetSize = FVector2f::ZeroVector;
		FVector2f OldPosition = FVector2f::ZeroVector;
		FVector2f NewPosition = FVector2f::ZeroVector;
		int32 Layer = INDEX_NONE;
		ELayoutLane Lane = ELayoutLane::Main;
		bool bPinned = false;
	};

	struct FLayoutEdge
	{
		FGuid FromGuid;
		FGuid ToGuid;

		// The transition node this edge came from. Carried so a routing plan can name the transition it
		// belongs to; the layering passes use only FromGuid and ToGuid.
		FGuid TransitionGuid;

		// USMTransitionInstance::GetPriorityOrder on the transition, which is the order the source state
		// evaluates its transitions in at runtime. Lower is evaluated first. The layout reads this to
		// order one state's outgoing siblings and never writes it, because it decides runtime behavior
		// and is the author's to set. It is numbered independently per source state, so two values are
		// only comparable when both edges leave the same state.
		int32 Priority = 0;
	};

	// Rendered size of a reroute node at 1:1 zoom. Its widget is a fixed-size spacer inside fixed
	// padding, so every reroute in every graph measures the same. This number stands in wherever a
	// measurement is unavailable.
	inline constexpr float DefaultRerouteSize = 36.0f;

	// Rendered size of the marker the editor draws on a transition wire, at 1:1 zoom. The body is a fixed
	// arrow box with the priority number above it, so it measures the same on every transition. This
	// number stands in wherever a measurement is unavailable.
	inline constexpr float DefaultTransitionMarkerWidth = 29.0f;
	inline constexpr float DefaultTransitionMarkerHeight = 44.0f;

	// One reroute node a routed edge needs, in the order it appears walking from the source state to the
	// destination. A transition with no entry here is drawn as a single straight segment.
	struct FLayoutReroute
	{
		FGuid TransitionGuid;
		int32 ChainIndex = 0;
		FVector2f Position = FVector2f::ZeroVector;
	};

	struct FLayoutInput
	{
		TArray<FLayoutNode> Nodes;
		TArray<FLayoutEdge> Edges;
		FGuid EntryGuid;
		ELayoutStrategy Strategy = ELayoutStrategy::LeftToRight;
		float ColumnGap = 80.0f;
		float RowGap = 40.0f;
		TSet<FGuid> PinnedGuids;
		bool bRespectExistingOrder = true;
		bool bSnapToGrid = true;
		float SnapGridSize = 16.0f;

		// Graph-space rectangle of the entry node, which is never moved and takes no part in the flow.
		// The first layer is placed one ColumnGap past this rectangle along the flow axis and centered on
		// it across. That keeps the first state off it. A size of zero means the caller found no entry
		// node to measure, and the flow then starts at Start instead.
		FVector2f EntryNodePosition = FVector2f::ZeroVector;
		FVector2f EntryNodeSize = FVector2f::ZeroVector;

		// Origin for the flow. Used when bStartExplicit is set, or when EntryNodeSize is zero because no
		// entry node was found. Otherwise the flow is anchored off the entry node. Callers set an explicit
		// origin to pin a layout to a chosen point.
		FVector2f Start = FVector2f::ZeroVector;
		bool bStartExplicit = false;

		// Rendered size of a reroute node, which the routing lanes are spaced by. See DefaultRerouteSize.
		float RerouteSize = DefaultRerouteSize;

		// Rendered size of a transition's marker. The editor draws one at the midpoint of every segment,
		// so a marker lands on a state whenever a wire's midpoint does. Spacing is chosen to keep them
		// off the states, which is why the layout has to know how big they are.
		FVector2f TransitionMarkerSize = FVector2f(DefaultTransitionMarkerWidth, DefaultTransitionMarkerHeight);

		// Route edges that would otherwise draw their marker over an intervening state: the back-edges of
		// a cycle, and any forward edge spanning more than one layer. Clearing that needs reroute nodes,
		// which the caller creates from the plan; spacing the states alone never fixes it.
		bool bRouteEdges = true;

		// The widest a single layer boundary may be stretched past ColumnGap to keep a wire off the states
		// it passes. A fan from one state to a tall column of siblings needs far more room than ColumnGap
		// gives it: the shallower the wire, the sooner it reaches its target, and stretching the boundary
		// is what makes it shallow. Zero turns the stretching off and leaves every boundary at ColumnGap.
		float MaxExtraLayerGap = 4000.0f;

		// How many siblings one state has to feed before its outgoing transitions are treated as a fan
		// and carried on a trunk. Below this a state's outgoing wires are drawn straight, because a
		// handful of wires leaving one state stay easy to follow and a rail would cost more than it
		// saves. Setting bRouteEdges to false turns the trunk off along with every other rail.
		int32 MinFanTargets = 3;

		// True (the default) measures the arrangement the nodes arrived in and returns it untouched unless
		// the computed layout is strictly better. A hand-arranged graph is often already as good as this
		// algorithm can make it, and re-flowing it then costs the reader an arrangement someone wrote on
		// purpose and buys nothing. False always returns the computed layout, whatever it measures.
		// FLayoutGraphResult::bDeclined says what "better" compares.
		bool bOnlyIfImproved = true;
	};

	struct FLayoutGraphResult
	{
		TArray<FLayoutNode> Nodes;
		TArray<FLayoutReroute> Reroutes;
		TArray<FString> Warnings;

		// Transitions still drawn through a state after routing, counted on the path the editor will
		// draw. Zero is the goal. It rises when bRouteEdges is off, which is the cost of turning routing
		// off and the reason the number is reported rather than kept internal.
		int32 EdgesThroughStates = 0;

		// Pairs of nodes whose boxes overlap, over the flow nodes plus the entry node rectangle. That is
		// the same set ld.get_graph_view counts overlaps for, so the two numbers agree.
		int32 NodeOverlaps = 0;

		// Transition markers drawn on top of a state box. A marker there cannot be read or clicked, and
		// it clutters the state it lands on, so this is a defect in its own right and not a restatement
		// of EdgesThroughStates: a wire can clear every state and still put its marker on one, and a wire
		// carried on a rail can cross a state while its marker sits in clear space.
		int32 MarkersOverNodes = 0;

		// How many of Reroutes came from carrying a fan on a trunk, as opposed to lifting a single edge
		// out of the flow. Reported so a caller can see that the fan rule fired, because it is the one
		// rule that adds reroute nodes to a graph whose wires cross nothing.
		int32 FanRails = 0;

		// The CountEdgesThroughNodes value the within-layer ordering settled on, or 0 when bDeclined,
		// because the order it scored was not applied and reporting its score would describe a graph
		// nobody has. It is a straight
		// center-to-center count taken before routing, over main-lane edges only, so it does not match
		// EdgesThroughStates and is not meant to. It is reported because it is the number that chose the
		// order. Both counts inflate with RowGap, so neither is comparable across two values of it.
		int32 OrderingScore = 0;

		// The same four measurements taken on the arrangement the nodes arrived in, with rails planned for
		// it on the same terms, and declared in the order they are compared. bOnlyIfImproved compares
		// NodeOverlaps, then EdgesThroughStates, then MarkersOverNodes, then the reroute count against
		// these, and keeps the computed layout only when it is strictly better on the first of the four
		// that differs.
		int32 InputNodeOverlaps = 0;
		int32 InputEdgesThroughStates = 0;
		int32 InputMarkersOverNodes = 0;
		int32 InputReroutes = 0;

		// True when bOnlyIfImproved was set and the computed layout did not beat the arrangement the nodes
		// arrived in. Every node's NewPosition then equals its OldPosition, no reroute is planned, and
		// Warnings says why. A caller must also skip whatever it would do off the back of a layout, such
		// as a second measure-and-place pass or settling transition markers, because nothing moved.
		bool bDeclined = false;
	};

	// Exported so SMAssistTests can drive the layout headlessly with hand-built inputs. Running the op
	// itself needs an open asset editor, which is fatal under -NullRHI.
	SMASSIST_API FLayoutGraphResult ComputeLayout(const FLayoutInput& In);

	const TCHAR* StrategyToString(ELayoutStrategy InStrategy);
	bool TryParseStrategy(const FString& InValue, ELayoutStrategy& OutStrategy);
	const TCHAR* LaneToString(ELayoutLane InLane);
}
