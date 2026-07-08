// Copyright Recursoft LLC. All Rights Reserved.

#include "Utilities/SMAssistUtils.h"

#include "Blueprints/SMBlueprint.h"
#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/SMGraph.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Editor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

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

UWorld* LD::Assist::Utils::GetActivePIEWorld(int32 InPieInstance, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Editor is unavailable.");
		return nullptr;
	}

	int32 PieIndex = 0;
	for (const FWorldContext& Context : GEditor->GetWorldContexts())
	{
		if (Context.WorldType != EWorldType::PIE || !Context.World())
		{
			continue;
		}

		if (PieIndex == InPieInstance)
		{
			return Context.World();
		}
		++PieIndex;
	}

	OutError = PieIndex > 0
		? FString::Printf(TEXT("No PIE world at instance %d (found %d running)."), InPieInstance, PieIndex)
		: FString(TEXT("No Play-In-Editor session is running. Start PIE first."));
	return nullptr;
}

AActor* LD::Assist::Utils::FindActorByIdentifier(UWorld* InWorld, const FString& InIdentifier)
{
	if (!InWorld || InIdentifier.IsEmpty())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(InWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (Actor->GetName().Equals(InIdentifier, ESearchCase::IgnoreCase)
			|| Actor->GetActorNameOrLabel().Equals(InIdentifier, ESearchCase::IgnoreCase))
		{
			return Actor;
		}
	}

	return nullptr;
}

bool LD::Assist::Utils::ResolveTerminalType(const FString& InTypeStr, FName& OutCategory, FName& OutSubCategory, UObject*& OutSubCategoryObject)
{
	OutCategory = NAME_None;
	OutSubCategory = NAME_None;
	OutSubCategoryObject = nullptr;

	const FString TypeStr = InTypeStr.ToLower();

	if (TypeStr == TEXT("bool") || TypeStr == TEXT("boolean"))
	{
		OutCategory = UEdGraphSchema_K2::PC_Boolean;
		return true;
	}
	if (TypeStr == TEXT("byte") || TypeStr == TEXT("uint8"))
	{
		OutCategory = UEdGraphSchema_K2::PC_Byte;
		return true;
	}
	if (TypeStr == TEXT("int") || TypeStr == TEXT("int32") || TypeStr == TEXT("integer"))
	{
		OutCategory = UEdGraphSchema_K2::PC_Int;
		return true;
	}
	if (TypeStr == TEXT("int64") || TypeStr == TEXT("long"))
	{
		OutCategory = UEdGraphSchema_K2::PC_Int64;
		return true;
	}
	// BP UI's "Float" maps to PC_Real + PC_Double in UE 5.x.
	if (TypeStr == TEXT("float") || TypeStr == TEXT("real") || TypeStr == TEXT("double"))
	{
		OutCategory = UEdGraphSchema_K2::PC_Real;
		OutSubCategory = UEdGraphSchema_K2::PC_Double;
		return true;
	}
	if (TypeStr == TEXT("single") || TypeStr == TEXT("single_precision_float"))
	{
		OutCategory = UEdGraphSchema_K2::PC_Real;
		OutSubCategory = UEdGraphSchema_K2::PC_Float;
		return true;
	}
	if (TypeStr == TEXT("string") || TypeStr == TEXT("fstring"))
	{
		OutCategory = UEdGraphSchema_K2::PC_String;
		return true;
	}
	if (TypeStr == TEXT("name") || TypeStr == TEXT("fname"))
	{
		OutCategory = UEdGraphSchema_K2::PC_Name;
		return true;
	}
	if (TypeStr == TEXT("text") || TypeStr == TEXT("ftext"))
	{
		OutCategory = UEdGraphSchema_K2::PC_Text;
		return true;
	}

	UScriptStruct* StructType = nullptr;
	if (TypeStr == TEXT("vector") || TypeStr == TEXT("fvector"))
	{
		StructType = TBaseStructure<FVector>::Get();
	}
	else if (TypeStr == TEXT("vector2d") || TypeStr == TEXT("fvector2d"))
	{
		StructType = TBaseStructure<FVector2D>::Get();
	}
	else if (TypeStr == TEXT("rotator") || TypeStr == TEXT("frotator"))
	{
		StructType = TBaseStructure<FRotator>::Get();
	}
	else if (TypeStr == TEXT("transform") || TypeStr == TEXT("ftransform"))
	{
		StructType = TBaseStructure<FTransform>::Get();
	}
	else if (TypeStr == TEXT("linearcolor") || TypeStr == TEXT("flinearcolor"))
	{
		StructType = TBaseStructure<FLinearColor>::Get();
	}
	else if (TypeStr == TEXT("color") || TypeStr == TEXT("fcolor"))
	{
		StructType = TBaseStructure<FColor>::Get();
	}
	else if (TypeStr == TEXT("guid") || TypeStr == TEXT("fguid"))
	{
		StructType = TBaseStructure<FGuid>::Get();
	}
	if (StructType)
	{
		OutCategory = UEdGraphSchema_K2::PC_Struct;
		OutSubCategoryObject = StructType;
		return true;
	}

	if (InTypeStr.Contains(TEXT(".")) || InTypeStr.StartsWith(TEXT("/")))
	{
		if (UClass* ObjectClass = LoadClass<UObject>(nullptr, *InTypeStr))
		{
			OutCategory = UEdGraphSchema_K2::PC_Object;
			OutSubCategoryObject = ObjectClass;
			return true;
		}
		if (UScriptStruct* ArbitraryStruct = LoadObject<UScriptStruct>(nullptr, *InTypeStr))
		{
			OutCategory = UEdGraphSchema_K2::PC_Struct;
			OutSubCategoryObject = ArbitraryStruct;
			return true;
		}
	}

	return false;
}

bool LD::Assist::Utils::ResolveContainedScreenshotsDir(const FString& InSubdir, FString& OutDir, FString& OutError)
{
	const FString Root = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots")));

	// ConvertRelativePathToFull collapses any '..' segments, so a comparison against the normalized root
	// catches every escape (parent traversal or an absolute output_subdir) after normalization.
	const FString Resolved = FPaths::ConvertRelativePathToFull(FPaths::Combine(Root, InSubdir));

	FString NormRoot = Root;
	FString NormResolved = Resolved;
	FPaths::NormalizeDirectoryName(NormRoot);
	FPaths::NormalizeDirectoryName(NormResolved);

	if (!NormResolved.Equals(NormRoot, ESearchCase::IgnoreCase)
		&& !NormResolved.StartsWith(NormRoot + TEXT("/"), ESearchCase::IgnoreCase))
	{
		OutError = FString::Printf(
			TEXT("'output_subdir' ('%s') resolves outside the screenshots directory and was rejected."), *InSubdir);
		return false;
	}

	OutDir = NormResolved;
	return true;
}

bool LD::Assist::Utils::IsSafeFileStem(const FString& InStem)
{
	if (InStem.IsEmpty())
	{
		return false;
	}
	return !InStem.Contains(TEXT("/")) && !InStem.Contains(TEXT("\\")) && !InStem.Contains(TEXT(".."));
}

bool LD::Assist::Utils::IntegerPropertyTextParses(const FProperty* InProperty, const FString& InValue)
{
	const FNumericProperty* NumericProp = CastField<FNumericProperty>(InProperty);
	if (!NumericProp || NumericProp->IsFloatingPoint() || NumericProp->GetIntPropertyEnum() != nullptr)
	{
		return true;
	}

	const FString Trimmed = InValue.TrimStartAndEnd();
	const TCHAR* Ch = *Trimmed;

	if (Trimmed.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase))
	{
		Ch += 2;
		if (*Ch == TCHAR('\0'))
		{
			return false;
		}
		for (; *Ch != TCHAR('\0'); ++Ch)
		{
			if (!FChar::IsHexDigit(*Ch))
			{
				return false;
			}
		}
		return true;
	}

	if (*Ch == TCHAR('+') || *Ch == TCHAR('-'))
	{
		++Ch;
	}
	if (*Ch == TCHAR('\0'))
	{
		return false;
	}
	for (; *Ch != TCHAR('\0'); ++Ch)
	{
		if (!FChar::IsDigit(*Ch))
		{
			return false;
		}
	}
	return true;
}
