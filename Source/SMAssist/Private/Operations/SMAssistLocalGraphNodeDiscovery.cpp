// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistLocalGraphNodeDiscovery.h"

#include "ISMAssetToolsModule.h"

namespace LD::Assist
{
	namespace Private
	{
		const TCHAR* ReadKindName(ISMGraphGeneration::ELocalGraphReadNodeType InKind)
		{
			switch (InKind)
			{
			case ISMGraphGeneration::ELocalGraphReadNodeType::TimeInState: return TEXT("TimeInState");
			case ISMGraphGeneration::ELocalGraphReadNodeType::HasStateUpdated: return TEXT("HasStateUpdated");
			case ISMGraphGeneration::ELocalGraphReadNodeType::CanEvaluate: return TEXT("CanEvaluate");
			case ISMGraphGeneration::ELocalGraphReadNodeType::CanEvaluateFromEvent: return TEXT("CanEvaluateFromEvent");
			case ISMGraphGeneration::ELocalGraphReadNodeType::GetStateInformation: return TEXT("GetStateInformation");
			case ISMGraphGeneration::ELocalGraphReadNodeType::GetTransitionInformation: return TEXT("GetTransitionInformation");
			case ISMGraphGeneration::ELocalGraphReadNodeType::GetStateMachineReference: return TEXT("GetStateMachineReference");
			case ISMGraphGeneration::ELocalGraphReadNodeType::GetNodeInstance: return TEXT("GetNodeInstance");
			case ISMGraphGeneration::ELocalGraphReadNodeType::InEndState: return TEXT("InEndState");
			}
			return TEXT("");
		}

		const TCHAR* WriteKindName(ISMGraphGeneration::ELocalGraphWriteNodeType InKind)
		{
			switch (InKind)
			{
			case ISMGraphGeneration::ELocalGraphWriteNodeType::CanEvaluate: return TEXT("CanEvaluate");
			case ISMGraphGeneration::ELocalGraphWriteNodeType::CanEvaluateFromEvent: return TEXT("CanEvaluateFromEvent");
			}
			return TEXT("");
		}

		const TCHAR* EventKindName(ISMGraphGeneration::ELocalGraphEventNodeType InKind)
		{
			switch (InKind)
			{
			case ISMGraphGeneration::ELocalGraphEventNodeType::OnInitialized: return TEXT("OnInitialized");
			case ISMGraphGeneration::ELocalGraphEventNodeType::OnShutdown: return TEXT("OnShutdown");
			case ISMGraphGeneration::ELocalGraphEventNodeType::OnStateUpdate: return TEXT("OnStateUpdate");
			case ISMGraphGeneration::ELocalGraphEventNodeType::OnStateEnd: return TEXT("OnStateEnd");
			case ISMGraphGeneration::ELocalGraphEventNodeType::OnTransitionEntered: return TEXT("OnTransitionEntered");
			case ISMGraphGeneration::ELocalGraphEventNodeType::OnTransitionPreEvaluate: return TEXT("OnTransitionPreEvaluate");
			case ISMGraphGeneration::ELocalGraphEventNodeType::OnTransitionPostEvaluate: return TEXT("OnTransitionPostEvaluate");
			case ISMGraphGeneration::ELocalGraphEventNodeType::OnRootStateMachineStart: return TEXT("OnRootStateMachineStart");
			case ISMGraphGeneration::ELocalGraphEventNodeType::OnRootStateMachineStop: return TEXT("OnRootStateMachineStop");
			}
			return TEXT("");
		}
	}

	FFindLocalGraphNodeTypesResult FindLocalGraphNodeTypes(USMBlueprint* InBlueprint, const FFindLocalGraphNodeTypesArgs& InArgs)
	{
		check(InBlueprint);

		FFindLocalGraphNodeTypesResult Result;
		if (!InArgs.TargetGraph)
		{
			return Result;
		}

		const TSharedPtr<ISMGraphGeneration> GraphGen = ISMAssetToolsModule::Get().GetGraphGenerationInterface();
		if (!GraphGen.IsValid())
		{
			return Result;
		}

		TArray<ISMGraphGeneration::ELocalGraphReadNodeType> ReadKinds;
		TArray<ISMGraphGeneration::ELocalGraphWriteNodeType> WriteKinds;
		GraphGen->GetCompatibleLocalGraphNodeTypes(InArgs.TargetGraph, ReadKinds, WriteKinds);

		TArray<ISMGraphGeneration::ELocalGraphEventNodeType> EventKinds;
		GraphGen->GetCompatibleLocalGraphEventNodeTypes(InArgs.TargetGraph, EventKinds);

		const FString FilterLower = InArgs.TypeIdFilter.ToLower();

		for (const ISMGraphGeneration::ELocalGraphReadNodeType Kind : ReadKinds)
		{
			if (FilterLower.IsEmpty() || FString(Private::ReadKindName(Kind)).ToLower().Contains(FilterLower))
			{
				Result.ReadKinds.Add(Kind);
			}
		}

		for (const ISMGraphGeneration::ELocalGraphWriteNodeType Kind : WriteKinds)
		{
			if (FilterLower.IsEmpty() || FString(Private::WriteKindName(Kind)).ToLower().Contains(FilterLower))
			{
				Result.WriteKinds.Add(Kind);
			}
		}

		for (const ISMGraphGeneration::ELocalGraphEventNodeType Kind : EventKinds)
		{
			if (FilterLower.IsEmpty() || FString(Private::EventKindName(Kind)).ToLower().Contains(FilterLower))
			{
				Result.EventKinds.Add(Kind);
			}
		}

		return Result;
	}
}
