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
	};

	struct FLayoutInput
	{
		TArray<FLayoutNode> Nodes;
		TArray<FLayoutEdge> Edges;
		FGuid EntryGuid;
		ELayoutStrategy Strategy = ELayoutStrategy::LeftToRight;
		float ColumnGap = 80.0f;
		float RowGap = 40.0f;
		FVector2f Start = FVector2f::ZeroVector;
		TSet<FGuid> PinnedGuids;
		bool bRespectExistingOrder = true;
		bool bSnapToGrid = true;
		float SnapGridSize = 16.0f;
	};

	struct FLayoutGraphResult
	{
		TArray<FLayoutNode> Nodes;
		TArray<FString> Warnings;
	};

	FLayoutGraphResult ComputeLayout(const FLayoutInput& In);

	const TCHAR* StrategyToString(ELayoutStrategy InStrategy);
	bool TryParseStrategy(const FString& InValue, ELayoutStrategy& OutStrategy);
	const TCHAR* LaneToString(ELayoutLane InLane);
}
