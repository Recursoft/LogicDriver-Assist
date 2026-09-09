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
	};

	// Rendered size of a reroute node at 1:1 zoom. Its widget is a fixed-size spacer inside fixed
	// padding, so every reroute in every graph measures the same. This number stands in wherever a
	// measurement is unavailable.
	inline constexpr float DefaultRerouteSize = 36.0f;

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

		// Route edges that would otherwise draw their marker over an intervening state: the back-edges of
		// a cycle, and any forward edge spanning more than one layer. Clearing that needs reroute nodes,
		// which the caller creates from the plan; spacing the states alone never fixes it.
		bool bRouteEdges = true;
	};

	struct FLayoutGraphResult
	{
		TArray<FLayoutNode> Nodes;
		TArray<FLayoutReroute> Reroutes;
		TArray<FString> Warnings;
	};

	// Exported so SMAssistTests can drive the layout headlessly with hand-built inputs. Running the op
	// itself needs an open asset editor, which is fatal under -NullRHI.
	SMASSIST_API FLayoutGraphResult ComputeLayout(const FLayoutInput& In);

	const TCHAR* StrategyToString(ELayoutStrategy InStrategy);
	bool TryParseStrategy(const FString& InValue, ELayoutStrategy& OutStrategy);
	const TCHAR* LaneToString(ELayoutLane InLane);
}
