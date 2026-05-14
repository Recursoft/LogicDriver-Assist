// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistLocalGraphNodeDiscovery.h"

#include "Blueprints/SMBlueprint.h"

#include "Graph/Nodes/FunctionNodes/SMGraphK2Node_StateReadNodes.h"
#include "Graph/Nodes/FunctionNodes/SMGraphK2Node_StateWriteNodes.h"

#include "EdGraph/EdGraph.h"

namespace LD::Assist
{
	FFindLocalGraphNodeTypesResult FindLocalGraphNodeTypes(USMBlueprint* InBlueprint, const FFindLocalGraphNodeTypesArgs& InArgs)
	{
		check(InBlueprint);

		FFindLocalGraphNodeTypesResult Result;
		if (!InArgs.TargetGraph)
		{
			return Result;
		}

		const FString FilterLower = InArgs.TypeIdFilter.ToLower();

		struct FReadEntry { ISMGraphGeneration::ELocalGraphReadNodeType Kind; UClass* Class; const TCHAR* Name; };
		const FReadEntry ReadEntries[] =
		{
			{ ISMGraphGeneration::ELocalGraphReadNodeType::TimeInState, USMGraphK2Node_StateReadNode_TimeInState::StaticClass(), TEXT("TimeInState") },
			{ ISMGraphGeneration::ELocalGraphReadNodeType::HasStateUpdated, USMGraphK2Node_StateReadNode_HasStateUpdated::StaticClass(), TEXT("HasStateUpdated") },
			{ ISMGraphGeneration::ELocalGraphReadNodeType::CanEvaluate, USMGraphK2Node_StateReadNode_CanEvaluate::StaticClass(), TEXT("CanEvaluate") },
			{ ISMGraphGeneration::ELocalGraphReadNodeType::CanEvaluateFromEvent, USMGraphK2Node_StateReadNode_CanEvaluateFromEvent::StaticClass(), TEXT("CanEvaluateFromEvent") },
			{ ISMGraphGeneration::ELocalGraphReadNodeType::GetStateInformation, USMGraphK2Node_StateReadNode_GetStateInformation::StaticClass(), TEXT("GetStateInformation") },
			{ ISMGraphGeneration::ELocalGraphReadNodeType::GetTransitionInformation, USMGraphK2Node_StateReadNode_GetTransitionInformation::StaticClass(), TEXT("GetTransitionInformation") },
			{ ISMGraphGeneration::ELocalGraphReadNodeType::GetStateMachineReference, USMGraphK2Node_StateReadNode_GetStateMachineReference::StaticClass(), TEXT("GetStateMachineReference") },
			{ ISMGraphGeneration::ELocalGraphReadNodeType::GetNodeInstance, USMGraphK2Node_StateReadNode_GetNodeInstance::StaticClass(), TEXT("GetNodeInstance") },
			{ ISMGraphGeneration::ELocalGraphReadNodeType::InEndState, USMGraphK2Node_StateMachineReadNode_InEndState::StaticClass(), TEXT("InEndState") },
		};

		for (const FReadEntry& Entry : ReadEntries)
		{
			if (!FilterLower.IsEmpty() && !FString(Entry.Name).ToLower().Contains(FilterLower))
			{
				continue;
			}
			const USMGraphK2Node_StateReadNode* CDO = Cast<USMGraphK2Node_StateReadNode>(Entry.Class->GetDefaultObject());
			if (CDO && CDO->IsCompatibleWithGraph(InArgs.TargetGraph))
			{
				Result.ReadKinds.Add(Entry.Kind);
			}
		}

		struct FWriteEntry { ISMGraphGeneration::ELocalGraphWriteNodeType Kind; UClass* Class; const TCHAR* Name; };
		const FWriteEntry WriteEntries[] =
		{
			{ ISMGraphGeneration::ELocalGraphWriteNodeType::CanEvaluate, USMGraphK2Node_StateWriteNode_CanEvaluate::StaticClass(), TEXT("CanEvaluate") },
			{ ISMGraphGeneration::ELocalGraphWriteNodeType::CanEvaluateFromEvent, USMGraphK2Node_StateWriteNode_CanEvaluateFromEvent::StaticClass(), TEXT("CanEvaluateFromEvent") },
		};

		for (const FWriteEntry& Entry : WriteEntries)
		{
			if (!FilterLower.IsEmpty() && !FString(Entry.Name).ToLower().Contains(FilterLower))
			{
				continue;
			}
			const USMGraphK2Node_StateWriteNode* CDO = Cast<USMGraphK2Node_StateWriteNode>(Entry.Class->GetDefaultObject());
			if (CDO && CDO->IsCompatibleWithGraph(InArgs.TargetGraph))
			{
				Result.WriteKinds.Add(Entry.Kind);
			}
		}

		return Result;
	}
}
