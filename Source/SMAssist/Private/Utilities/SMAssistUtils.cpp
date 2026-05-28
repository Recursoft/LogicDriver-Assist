// Copyright Recursoft LLC. All Rights Reserved.

#include "Utilities/SMAssistUtils.h"

#include "Blueprints/SMBlueprint.h"
#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/SMGraph.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

UBlueprint* LD::Assist::Utils::LoadBlueprint(const FString& InAssetPath, FString& OutError)
{
	if (InAssetPath.IsEmpty())
	{
		OutError = TEXT("Missing 'asset_path'.");
		return nullptr;
	}

	const FSoftObjectPath ObjectPath(InAssetPath);
	const FString PackageName = ObjectPath.GetLongPackageName();
	if (PackageName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Could not load asset '%s'."), *InAssetPath);
		return nullptr;
	}

	const bool bPackageInMemory = FindPackage(nullptr, *PackageName) != nullptr;
	if (!bPackageInMemory && !FPackageName::DoesPackageExist(PackageName))
	{
		OutError = FString::Printf(TEXT("Could not load asset '%s'."), *InAssetPath);
		return nullptr;
	}

	UObject* Loaded = ObjectPath.TryLoad();
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Could not load asset '%s'."), *InAssetPath);
		return nullptr;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(Loaded);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Asset '%s' is not a blueprint."), *InAssetPath);
		return nullptr;
	}

	return Blueprint;
}

USMBlueprint* LD::Assist::Utils::LoadStateMachineBlueprint(const FString& InAssetPath, FString& OutError)
{
	UBlueprint* Blueprint = LoadBlueprint(InAssetPath, OutError);
	if (!Blueprint)
	{
		return nullptr;
	}

	USMBlueprint* SMBlueprint = Cast<USMBlueprint>(Blueprint);
	if (!SMBlueprint)
	{
		OutError = FString::Printf(TEXT("Asset '%s' is not a state machine blueprint."), *InAssetPath);
		return nullptr;
	}

	return SMBlueprint;
}

USMGraphNode_Base* LD::Assist::Utils::FindNodeByGuid(USMBlueprint* InBlueprint, const FGuid& InGuid)
{
	if (!InBlueprint || !InGuid.IsValid())
	{
		return nullptr;
	}

	USMGraph* RootGraph = FSMBlueprintEditorUtils::GetRootStateMachineGraph(InBlueprint);
	if (!RootGraph)
	{
		return nullptr;
	}

	TArray<USMGraph*> GraphsToSearch;
	GraphsToSearch.Add(RootGraph);

	while (GraphsToSearch.Num() > 0)
	{
		USMGraph* Graph = GraphsToSearch.Pop();
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			if (Node->NodeGuid == InGuid)
			{
				if (USMGraphNode_Base* GraphNode = Cast<USMGraphNode_Base>(Node))
				{
					return GraphNode;
				}
			}

			for (UEdGraph* SubGraph : Node->GetSubGraphs())
			{
				if (USMGraph* SubSMGraph = Cast<USMGraph>(SubGraph))
				{
					GraphsToSearch.Add(SubSMGraph);
				}
			}
		}
	}

	return nullptr;
}

USMGraphNode_StateNodeBase* LD::Assist::Utils::FindStateNodeByGuid(USMBlueprint* InBlueprint, const FGuid& InGuid)
{
	return Cast<USMGraphNode_StateNodeBase>(FindNodeByGuid(InBlueprint, InGuid));
}
