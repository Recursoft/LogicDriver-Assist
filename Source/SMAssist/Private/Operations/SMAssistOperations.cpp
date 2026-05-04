// Copyright Recursoft LLC. All Rights Reserved.

#include "Operations/SMAssistOperations.h"

#include "Operations/SMAssistOpKeys.h"
#include "Layout/SMAssistLayout.h"
#include "SMAssistLog.h"
#include "Utilities/SMAssistUtils.h"

#include "ISMAssetManager.h"
#include "ISMAssetToolsModule.h"
#include "ISMGraphGeneration.h"
#include "Blueprints/SMBlueprint.h"
#include "Blueprints/SMBlueprintGeneratedClass.h"
#include "Graph/Nodes/SMGraphNode_AnyStateNode.h"
#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/Nodes/SMGraphNode_ConduitNode.h"
#include "Graph/Nodes/SMGraphNode_LinkStateNode.h"
#include "Graph/Nodes/SMGraphNode_RerouteNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineEntryNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/Nodes/SMGraphNode_TransitionEdge.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/RootNodes/SMGraphK2Node_TransitionResultNode.h"
#include "Graph/SMGraph.h"
#include "Graph/SMTransitionGraph.h"
#include "NodeStack/NodeStackContainer.h"
#include "Properties/SMEditorPropertyUtils.h"
#include "Properties/SMGraphProperty_Base.h"
#include "SMConduitInstance.h"
#include "SMStateInstance.h"
#include "SMStateMachineInstance.h"
#include "SMTransitionInstance.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Algo/Reverse.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraphNode_Comment.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditor.h"
#include "ImageUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/Children.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "SNodePanel.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWindow.h"

namespace LD::Assist::Private
{
	static ISMGraphGeneration* GetGraphGeneration(FString& OutError)
	{
		ISMAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<ISMAssetToolsModule>(
			LOGICDRIVER_ASSET_TOOLS_MODULE_NAME);

		const TSharedPtr<ISMGraphGeneration> GraphGen = AssetTools.GetGraphGenerationInterface();
		if (!GraphGen.IsValid())
		{
			OutError = TEXT("Graph generation interface unavailable.");
			return nullptr;
		}
		return GraphGen.Get();
	}
}

FSMAssistOperationResult LD::Assist::CreateBlueprint(const TSharedRef<FJsonObject>& InArgs)
{
	FString Name;
	if (!InArgs->TryGetStringField(Args::Name, Name) || Name.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'name'."));
	}

	FString Path;
	InArgs->TryGetStringField(Args::Path, Path);

	ISMAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<ISMAssetToolsModule>(
		LOGICDRIVER_ASSET_TOOLS_MODULE_NAME);

	const TSharedPtr<ISMAssetManager> AssetManager = AssetToolsModule.GetAssetManagerInterface();
	if (!AssetManager.IsValid())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Asset manager interface unavailable."));
	}

	ISMAssetManager::FCreateStateMachineBlueprintArgs CreateArgs;
	CreateArgs.Name = *Name;
	CreateArgs.Path = Path;

	USMBlueprint* NewBlueprint = AssetManager->CreateStateMachineBlueprint(CreateArgs);
	if (!NewBlueprint)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create state machine blueprint."));
	}

	LDASSIST_LOG_INFO(TEXT("Created state machine blueprint %s."), *NewBlueprint->GetPathName());

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, NewBlueprint->GetPathName());
	Payload->SetStringField(Args::Name, NewBlueprint->GetName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;

	FString StateName;
	if (InArgs->TryGetStringField(Args::StateName, StateName))
	{
		CreateArgs.StateName = StateName;
	}

	bool bIsEntry = false;
	if (InArgs->TryGetBoolField(Args::IsEntry, bIsEntry))
	{
		CreateArgs.bIsEntryState = bIsEntry;
	}

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	FString StateClassPath;
	if (InArgs->TryGetStringField(Args::StateClass, StateClassPath) && !StateClassPath.IsEmpty())
	{
		UClass* StateClass = LoadClass<USMStateInstance_Base>(nullptr, *StateClassPath);
		if (!StateClass)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not load 'state_class' '%s'."), *StateClassPath));
		}
		CreateArgs.StateInstanceClass = StateClass;
	}

	USMGraphNode_StateNodeBase* StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create state node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddTransition(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString FromGuidStr;
	if (!InArgs->TryGetStringField(Args::FromStateGuid, FromGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'from_state_guid'."));
	}

	FString ToGuidStr;
	if (!InArgs->TryGetStringField(Args::ToStateGuid, ToGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'to_state_guid'."));
	}

	FGuid FromGuid;
	if (!FGuid::Parse(FromGuidStr, FromGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'from_state_guid' '%s'."), *FromGuidStr));
	}

	FGuid ToGuid;
	if (!FGuid::Parse(ToGuidStr, ToGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'to_state_guid' '%s'."), *ToGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_StateNodeBase* FromState = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, FromGuid);
	if (!FromState)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find 'from' state with guid '%s'."), *FromGuidStr));
	}

	USMGraphNode_StateNodeBase* ToState = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, ToGuid);
	if (!ToState)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find 'to' state with guid '%s'."), *ToGuidStr));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateTransitionEdgeArgs CreateArgs;
	CreateArgs.FromStateNode = FromState;
	CreateArgs.ToStateNode = ToState;

	FString TransitionClassPath;
	if (InArgs->TryGetStringField(Args::TransitionClass, TransitionClassPath) && !TransitionClassPath.IsEmpty())
	{
		UClass* TransitionClass = LoadClass<USMTransitionInstance>(nullptr, *TransitionClassPath);
		if (!TransitionClass)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not load 'transition_class' '%s'."), *TransitionClassPath));
		}
		CreateArgs.TransitionInstanceClass = TransitionClass;
	}

	USMGraphNode_TransitionEdge* TransitionEdge = GraphGen->CreateTransitionEdge(Blueprint, CreateArgs);
	if (!TransitionEdge)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create transition edge."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::TransitionGuid, TransitionEdge->NodeGuid.ToString());
	Payload->SetStringField(Args::FromStateGuid, FromState->NodeGuid.ToString());
	Payload->SetStringField(Args::ToStateGuid, ToState->NodeGuid.ToString());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::ListAssets(const TSharedRef<FJsonObject>& InArgs)
{
	FString PathPrefix;
	InArgs->TryGetStringField(Args::PathPrefix, PathPrefix);

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry"));
	const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(USMBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	if (!PathPrefix.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathPrefix));
	}

	TArray<FAssetData> AssetData;
	AssetRegistry.GetAssets(Filter, AssetData);

	TArray<TSharedPtr<FJsonValue>> AssetArray;
	AssetArray.Reserve(AssetData.Num());

	for (const FAssetData& Data : AssetData)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(Args::AssetPath, Data.GetObjectPathString());
		Entry->SetStringField(Args::Name, Data.AssetName.ToString());

		const FString ParentClassTag = Data.GetTagValueRef<FString>(FBlueprintTags::ParentClassPath);
		if (!ParentClassTag.IsEmpty())
		{
			Entry->SetStringField(Args::ParentClass, ParentClassTag);
		}

		AssetArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(Args::Assets, AssetArray);
	Payload->SetNumberField(Args::Count, AssetArray.Num());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::GetAsset(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraph* RootGraph = FSMBlueprintEditorUtils::GetRootStateMachineGraph(Blueprint);
	if (!RootGraph)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Blueprint has no root state machine graph."));
	}

	TSet<FGuid> EntryStateGuids;
	if (const USMGraphNode_StateMachineEntryNode* EntryNode = RootGraph->GetEntryNode())
	{
		if (const UEdGraphPin* EntryOutputPin = EntryNode->GetOutputPin())
		{
			for (const UEdGraphPin* LinkedPin : EntryOutputPin->LinkedTo)
			{
				if (LinkedPin)
				{
					if (const USMGraphNode_StateNodeBase* LinkedState = Cast<USMGraphNode_StateNodeBase>(LinkedPin->GetOwningNode()))
					{
						EntryStateGuids.Add(LinkedState->NodeGuid);
					}
				}
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> States;
	TArray<TSharedPtr<FJsonValue>> Transitions;

	for (UEdGraphNode* Node : RootGraph->Nodes)
	{
		if (Node->IsA<USMGraphNode_StateMachineEntryNode>())
		{
			continue;
		}

		if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Node))
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
			Entry->SetStringField(Args::StateName, StateNode->GetStateName());
			if (const UClass* NodeClass = StateNode->GetNodeClass())
			{
				Entry->SetStringField(Args::StateClass, NodeClass->GetPathName());
			}
			Entry->SetNumberField(Args::PositionX, StateNode->NodePosX);
			Entry->SetNumberField(Args::PositionY, StateNode->NodePosY);
			Entry->SetBoolField(Args::IsEntry, EntryStateGuids.Contains(StateNode->NodeGuid));

			const TCHAR* Kind = TEXT("state");
			if (StateNode->IsA<USMGraphNode_AnyStateNode>())
			{
				Kind = TEXT("any_state");
			}
			else if (const USMGraphNode_LinkStateNode* LinkNode = Cast<USMGraphNode_LinkStateNode>(StateNode))
			{
				Kind = TEXT("link_state");
				if (const USMGraphNode_StateNodeBase* LinkedState = LinkNode->GetLinkedState())
				{
					Entry->SetStringField(Args::LinkedStateGuid, LinkedState->NodeGuid.ToString());
					Entry->SetStringField(Args::LinkToStateName, LinkedState->GetStateName());
				}
			}
			else if (StateNode->IsA<USMGraphNode_RerouteNode>())
			{
				Kind = TEXT("reroute");
			}
			else if (StateNode->IsA<USMGraphNode_StateMachineStateNode>())
			{
				Kind = TEXT("state_machine_state");
			}
			else if (StateNode->IsA<USMGraphNode_ConduitNode>())
			{
				Kind = TEXT("conduit");
			}
			Entry->SetStringField(Args::Kind, Kind);

			States.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		if (const USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node))
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(Args::TransitionGuid, TransitionEdge->NodeGuid.ToString());
			if (const USMGraphNode_StateNodeBase* FromState = TransitionEdge->GetFromState())
			{
				Entry->SetStringField(Args::FromStateGuid, FromState->NodeGuid.ToString());
			}
			if (const USMGraphNode_StateNodeBase* ToState = TransitionEdge->GetToState())
			{
				Entry->SetStringField(Args::ToStateGuid, ToState->NodeGuid.ToString());
			}
			if (const UClass* NodeClass = TransitionEdge->GetNodeClass())
			{
				Entry->SetStringField(Args::TransitionClass, NodeClass->GetPathName());
			}
			Transitions.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TArray<TSharedPtr<FJsonValue>> EntryGuidArray;
	for (const FGuid& EntryGuid : EntryStateGuids)
	{
		EntryGuidArray.Add(MakeShared<FJsonValueString>(EntryGuid.ToString()));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::Name, Blueprint->GetName());
	if (const UClass* ParentClass = Blueprint->ParentClass)
	{
		Payload->SetStringField(Args::ParentClass, ParentClass->GetPathName());
	}
	Payload->SetArrayField(Args::EntryStateGuids, EntryGuidArray);
	Payload->SetArrayField(Args::States, States);
	Payload->SetArrayField(Args::Transitions, Transitions);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::RemoveNode(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	if (!GraphGen->RemoveNode(Node))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to remove node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static bool IsPropertyHiddenOnInstanceTemplate(const FProperty* InProperty, const USMNodeInstance* InTemplate)
	{
		if (!InProperty || !InTemplate)
		{
			return false;
		}
		if (InProperty->HasMetaData(TEXT("InstancedTemplate")))
		{
			return true;
		}
		if (InTemplate->GetTemplateGuid().IsValid() && InProperty->HasMetaData(TEXT("NodeBaseOnly")))
		{
			return true;
		}
		return false;
	}

	static bool JsonScalarToDefaultString(const TSharedPtr<FJsonValue>& InValue, FString& OutString, FString& OutError)
	{
		if (!InValue.IsValid())
		{
			OutError = TEXT("Missing value.");
			return false;
		}

		switch (InValue->Type)
		{
			case EJson::String:
				OutString = InValue->AsString();
				return true;
			case EJson::Number:
				OutString = FString::SanitizeFloat(InValue->AsNumber());
				return true;
			case EJson::Boolean:
				OutString = InValue->AsBool() ? TEXT("true") : TEXT("false");
				return true;
			case EJson::Null:
				OutString.Reset();
				return true;
			default:
				OutError = TEXT("Value must be a string, number, boolean, or null.");
				return false;
		}
	}

	static bool JsonValueToDefaultStrings(const TSharedPtr<FJsonValue>& InValue, TArray<FString>& OutStrings, bool& bOutIsArray, FString& OutError)
	{
		bOutIsArray = false;
		OutStrings.Reset();

		if (!InValue.IsValid())
		{
			OutError = TEXT("Missing 'value'.");
			return false;
		}

		if (InValue->Type == EJson::Array)
		{
			bOutIsArray = true;
			const TArray<TSharedPtr<FJsonValue>>& Array = InValue->AsArray();
			OutStrings.Reserve(Array.Num());
			for (int32 Idx = 0; Idx < Array.Num(); ++Idx)
			{
				FString Element;
				FString ElementError;
				if (!JsonScalarToDefaultString(Array[Idx], Element, ElementError))
				{
					OutError = FString::Printf(TEXT("'value[%d]': %s"), Idx, *ElementError);
					return false;
				}
				OutStrings.Add(MoveTemp(Element));
			}
			return true;
		}

		FString Scalar;
		if (!JsonScalarToDefaultString(InValue, Scalar, OutError))
		{
			return false;
		}
		OutStrings.Add(MoveTemp(Scalar));
		return true;
	}
}

FSMAssistOperationResult LD::Assist::SetNodeProperty(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FString PropertyName;
	if (!InArgs->TryGetStringField(Args::PropertyName, PropertyName) || PropertyName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'property_name'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	USMNodeInstance* TargetTemplate = nullptr;
	int32 StackIndex = INDEX_NONE;
	const bool bStackIndexProvided = InArgs->TryGetNumberField(Args::StackIndex, StackIndex) && StackIndex >= 0;
	if (bStackIndexProvided)
	{
		TargetTemplate = Node->GetTemplateFromIndex(StackIndex);
		if (!TargetTemplate)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("No stack template at index %d on node '%s'."), StackIndex, *NodeGuidStr));
		}
	}

	FString ArrayAction;
	InArgs->TryGetStringField(Args::ArrayAction, ArrayAction);
	const FString NormalizedAction = ArrayAction.ToLower();

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	Payload->SetStringField(Args::PropertyName, PropertyName);
	if (bStackIndexProvided)
	{
		Payload->SetNumberField(Args::StackIndex, StackIndex);
	}

	// Target UEdGraphNode fields (NodePosX/NodePosY/NodeComment, etc.) when the caller didn't pin a stack template.
	// Skip deprecated graph-node fields. UE reflection strips the _DEPRECATED suffix during name lookup, so a
	// template-side property name (e.g. bDisableTickTransitionEvaluation) could otherwise match a deprecated
	// graph-node mirror and silently write to the wrong object.
	if (!bStackIndexProvided)
	{
		FProperty* GraphNodeProperty = FindFProperty<FProperty>(Node->GetClass(), *PropertyName);
		if (GraphNodeProperty && GraphNodeProperty->HasAnyPropertyFlags(CPF_Deprecated))
		{
			GraphNodeProperty = nullptr;
		}
		if (GraphNodeProperty)
		{
			if (!NormalizedAction.IsEmpty() && NormalizedAction != TEXT("set"))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("'%s' resolves to graph-node property '%s'; 'array_action=%s' is only supported on node-template properties (template array_action values: 'set', 'remove', 'clear', 'add', 'insert', 'duplicate', 'move')."),
						*PropertyName, *PropertyName, *ArrayAction));
			}

			TArray<FString> ValueStrings;
			bool bValueIsArray = false;
			FString ValueError;
			const TSharedPtr<FJsonValue> ValueField = InArgs->TryGetField(Args::Value);
			if (!LD::Assist::Private::JsonValueToDefaultStrings(ValueField, ValueStrings, bValueIsArray, ValueError))
			{
				return FSMAssistOperationResult::MakeError(ValueError);
			}

			int32 StartIndex = 0;
			const bool bHasExplicitIndex = InArgs->TryGetNumberField(Args::ArrayIndex, StartIndex);
			if (bValueIsArray && bHasExplicitIndex)
			{
				return FSMAssistOperationResult::MakeError(
					TEXT("'array_index' cannot be combined with an array 'value'; elements are written starting at index 0."));
			}

			Node->Modify();
			for (int32 Idx = 0; Idx < ValueStrings.Num(); ++Idx)
			{
				const int32 WriteIndex = bValueIsArray ? Idx : StartIndex;
				LD::Editor::PropertyUtils::SetPropertyValue(GraphNodeProperty, ValueStrings[Idx], Node, WriteIndex);
			}

			FPropertyChangedEvent PropertyChangedEvent(GraphNodeProperty, EPropertyChangeType::ValueSet);
			Node->PostEditChangeProperty(PropertyChangedEvent);

			if (UEdGraph* OwnerGraph = Node->GetGraph())
			{
				OwnerGraph->NotifyGraphChanged();
			}
			Node->GetPackage()->MarkPackageDirty();

			if (bValueIsArray)
			{
				Payload->SetNumberField(Args::ElementCount, ValueStrings.Num());
			}
			else if (bHasExplicitIndex)
			{
				Payload->SetNumberField(Args::ArrayIndex, StartIndex);
			}
			return FSMAssistOperationResult::MakeSuccess(Payload);
		}
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	USMNodeInstance* ResolvedTemplate = TargetTemplate ? TargetTemplate : Node->GetNodeTemplate();
	if (ResolvedTemplate)
	{
		if (const FProperty* TemplateProperty = ResolvedTemplate->GetClass()->FindPropertyByName(*PropertyName))
		{
			if (LD::Assist::Private::IsPropertyHiddenOnInstanceTemplate(TemplateProperty, ResolvedTemplate))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Property '%s' is base-class-only and cannot be written on an instance template. Edit the class default (on the owning USMNodeInstance subclass) instead."),
						*PropertyName));
			}
		}
	}

	ISMGraphGeneration::FSetNodePropertyArgs PropertyArgs;
	PropertyArgs.PropertyName = *PropertyName;
	PropertyArgs.NodeInstance = TargetTemplate;

	const bool bIsStructuralAction =
		NormalizedAction == TEXT("add") ||
		NormalizedAction == TEXT("insert") ||
		NormalizedAction == TEXT("duplicate") ||
		NormalizedAction == TEXT("move") ||
		NormalizedAction == TEXT("remove") ||
		NormalizedAction == TEXT("clear");

	// Structural actions never accept a 'value' payload, since they change the array's shape, not cell contents.
	if (bIsStructuralAction)
	{
		if (InArgs->HasField(Args::Value))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("'array_action=%s' does not accept 'value'. Use 'set' to write element values."), *NormalizedAction));
		}
	}

	// Pre-flight: distinguish "not an array" from "array but locked" so the gate error below stays accurate.
	if (bIsStructuralAction && ResolvedTemplate)
	{
		if (const FProperty* TargetProperty = ResolvedTemplate->GetClass()->FindPropertyByName(*PropertyName))
		{
			if (!CastField<FArrayProperty>(TargetProperty))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Property '%s' is not an array; structural 'array_action=%s' requires an array property."),
						*PropertyName, *NormalizedAction));
			}
		}
	}

	// Pre-flight gate: surface EditFixedSize / BlueprintReadOnly as an explicit error rather than a silent no-op.
	auto CanMutateArray = [&](bool bStructural) -> bool
	{
		return bStructural
			? Node->CanModifyArraySize(*PropertyName, PropertyArgs.NodeInstance)
			: Node->CanModifyArrayContents(*PropertyName, PropertyArgs.NodeInstance);
	};
	auto MakeReadOnlyError = [&]() -> FSMAssistOperationResult
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Array property '%s' rejects '%s' (EditFixedSize, BlueprintReadOnly, or ExposedPropertyOverrides.bReadOnly)."),
				*PropertyName, *NormalizedAction));
	};

	if (NormalizedAction == TEXT("clear"))
	{
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::Clear;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to clear array property '%s' on node."), *PropertyName));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("clear"));
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("remove"))
	{
		int32 RemoveIndex = 0;
		if (!InArgs->TryGetNumberField(Args::ArrayIndex, RemoveIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=remove' requires 'array_index'."));
		}
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::RemoveElement;
		PropertyArgs.PropertyIndex = RemoveIndex;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to remove element %d from array property '%s'."), RemoveIndex, *PropertyName));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("remove"));
		Payload->SetNumberField(Args::ArrayIndex, RemoveIndex);
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("add"))
	{
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::AddElement;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to add element to array property '%s'."), *PropertyName));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("add"));
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("insert"))
	{
		int32 InsertIndex = 0;
		if (!InArgs->TryGetNumberField(Args::ArrayIndex, InsertIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=insert' requires 'array_index'."));
		}
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::InsertElement;
		PropertyArgs.PropertyIndex = InsertIndex;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to insert element at %d into array property '%s'."), InsertIndex, *PropertyName));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("insert"));
		Payload->SetNumberField(Args::ArrayIndex, InsertIndex);
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("duplicate"))
	{
		int32 SourceIndex = 0;
		if (!InArgs->TryGetNumberField(Args::ArrayIndex, SourceIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=duplicate' requires 'array_index' (source element)."));
		}
		if (!CanMutateArray(/*bStructural*/ true))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::DuplicateElement;
		PropertyArgs.PropertyIndex = SourceIndex;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to duplicate element %d in array property '%s'."), SourceIndex, *PropertyName));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("duplicate"));
		Payload->SetNumberField(Args::ArrayIndex, SourceIndex);
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (NormalizedAction == TEXT("move"))
	{
		int32 SourceIndex = 0;
		if (!InArgs->TryGetNumberField(Args::ArrayIndex, SourceIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=move' requires 'array_index' (source element)."));
		}
		int32 DestIndex = 0;
		if (!InArgs->TryGetNumberField(Args::TargetIndex, DestIndex))
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=move' requires 'target_index' (destination element)."));
		}
		if (SourceIndex == DestIndex)
		{
			return FSMAssistOperationResult::MakeError(
				TEXT("'array_action=move' requires 'array_index' and 'target_index' to differ."));
		}
		if (!CanMutateArray(/*bStructural*/ false))
		{
			return MakeReadOnlyError();
		}
		PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::MoveElement;
		PropertyArgs.PropertyIndex = SourceIndex;
		PropertyArgs.TargetIndex = DestIndex;
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to move element %d to %d in array property '%s'."), SourceIndex, DestIndex, *PropertyName));
		}
		Payload->SetStringField(Args::ArrayAction, TEXT("move"));
		Payload->SetNumberField(Args::ArrayIndex, SourceIndex);
		Payload->SetNumberField(Args::TargetIndex, DestIndex);
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	if (!NormalizedAction.IsEmpty() && NormalizedAction != TEXT("set"))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Unknown 'array_action' '%s'. Expected 'set', 'add', 'insert', 'duplicate', 'move', 'remove', or 'clear'."), *ArrayAction));
	}

	TArray<FString> ValueStrings;
	bool bValueIsArray = false;
	FString ValueError;
	const TSharedPtr<FJsonValue> ValueField = InArgs->TryGetField(Args::Value);
	if (!LD::Assist::Private::JsonValueToDefaultStrings(ValueField, ValueStrings, bValueIsArray, ValueError))
	{
		return FSMAssistOperationResult::MakeError(ValueError);
	}

	int32 StartIndex = 0;
	const bool bHasExplicitIndex = InArgs->TryGetNumberField(Args::ArrayIndex, StartIndex);
	if (bValueIsArray && bHasExplicitIndex)
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("'array_index' cannot be combined with an array 'value'; elements are written starting at index 0."));
	}

	PropertyArgs.ArrayChangeType = ISMGraphGeneration::EArrayChangeType::SetElement;
	for (int32 Idx = 0; Idx < ValueStrings.Num(); ++Idx)
	{
		PropertyArgs.PropertyIndex = bValueIsArray ? Idx : StartIndex;
		PropertyArgs.PropertyDefaultValue = ValueStrings[Idx];
		if (!GraphGen->SetNodePropertyValue(Node, PropertyArgs))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Failed to set property '%s' at index %d on node."), *PropertyName, PropertyArgs.PropertyIndex));
		}
	}

	if (bValueIsArray)
	{
		Payload->SetNumberField(Args::ElementCount, ValueStrings.Num());
	}
	else if (bHasExplicitIndex)
	{
		Payload->SetNumberField(Args::ArrayIndex, StartIndex);
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::Compile(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	const EBlueprintStatus Status = Blueprint->Status;
	const bool bSuccess = Status == BS_UpToDate || Status == BS_UpToDateWithWarnings;

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetBoolField(Args::UpToDate, Status == BS_UpToDate);
	Payload->SetBoolField(Args::HasWarnings, Status == BS_UpToDateWithWarnings);
	Payload->SetBoolField(Args::HasErrors, Status == BS_Error);
	Payload->SetNumberField(Args::Status, static_cast<int32>(Status));

	if (!bSuccess)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Blueprint compile reported errors."), Payload);
	}

	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::RenameState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::StateGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'state_guid'."));
	}

	FString NewName;
	if (!InArgs->TryGetStringField(Args::NewName, NewName) || NewName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'new_name'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'state_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_StateNodeBase* StateNode = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, NodeGuid);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find state node with guid '%s'."), *NodeGuidStr));
	}

	FText RenameError;
	if (!StateNode->SetNodeName(NewName, RenameError))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Rename rejected: %s"), *RenameError.ToString()));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, NodeGuidStr);
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SetInitialState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString StateGuidStr;
	if (!InArgs->TryGetStringField(Args::StateGuid, StateGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'state_guid'."));
	}

	FGuid StateGuid;
	if (!FGuid::Parse(StateGuidStr, StateGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'state_guid' '%s'."), *StateGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_StateNodeBase* StateNode = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, StateGuid);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find state node with guid '%s'."), *StateGuidStr));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	if (!GraphGen->SetInitialState(StateNode))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Failed to set initial state for node '%s'."), *StateGuidStr));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateGuidStr);
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddStateStack(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString StateGuidStr;
	if (!InArgs->TryGetStringField(Args::StateGuid, StateGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'state_guid'."));
	}

	FString StateClassPath;
	if (!InArgs->TryGetStringField(Args::StateClass, StateClassPath) || StateClassPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'state_class'."));
	}

	FGuid StateGuid;
	if (!FGuid::Parse(StateGuidStr, StateGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'state_guid' '%s'."), *StateGuidStr));
	}

	UClass* StackClass = LoadClass<USMStateInstance>(nullptr, *StateClassPath);
	if (!StackClass)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not load 'state_class' '%s'."), *StateClassPath));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_StateNodeBase* StateNodeBase = LD::Assist::Utils::FindStateNodeByGuid(Blueprint, StateGuid);
	USMGraphNode_StateNode* StateNode = Cast<USMGraphNode_StateNode>(StateNodeBase);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' is not a state node that supports a state stack."), *StateGuidStr));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateStateStackArgs StackArgs;
	StackArgs.StateStackInstanceClass = StackClass;
	StackArgs.StateStackIndex = INDEX_NONE;

	int32 RequestedIndex = 0;
	if (InArgs->TryGetNumberField(Args::StackIndex, RequestedIndex))
	{
		StackArgs.StateStackIndex = RequestedIndex;
	}

	USMStateInstance* StackInstance = GraphGen->CreateStateStackInstance(StateNode, StackArgs);
	if (!StackInstance)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Failed to add state stack '%s' to state '%s'."), *StateClassPath, *StateGuidStr));
	}

	const TArray<FStateStackContainer>& Stack = StateNode->GetAllNodeStackTemplates();
	int32 ResolvedIndex = INDEX_NONE;
	FGuid TemplateGuid;
	for (int32 Idx = 0; Idx < Stack.Num(); ++Idx)
	{
		if (Stack[Idx].NodeStackInstanceTemplate == StackInstance)
		{
			ResolvedIndex = Idx;
			TemplateGuid = Stack[Idx].TemplateGuid;
			break;
		}
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateGuidStr);
	Payload->SetStringField(Args::StateClass, StackClass->GetPathName());
	Payload->SetNumberField(Args::StackIndex, ResolvedIndex);
	Payload->SetStringField(Args::TemplateGuid, TemplateGuid.ToString());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddTransitionStack(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString TransitionGuidStr;
	if (!InArgs->TryGetStringField(Args::TransitionGuid, TransitionGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'transition_guid'."));
	}

	FString TransitionClassPath;
	if (!InArgs->TryGetStringField(Args::TransitionClass, TransitionClassPath) || TransitionClassPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'transition_class'."));
	}

	FGuid TransitionGuid;
	if (!FGuid::Parse(TransitionGuidStr, TransitionGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'transition_guid' '%s'."), *TransitionGuidStr));
	}

	UClass* StackClass = LoadClass<USMTransitionInstance>(nullptr, *TransitionClassPath);
	if (!StackClass)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not load 'transition_class' '%s'."), *TransitionClassPath));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, TransitionGuid);
	USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node);
	if (!TransitionEdge)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' is not a transition edge."), *TransitionGuidStr));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateTransitionStackArgs StackArgs;
	StackArgs.TransitionStackInstanceClass = StackClass;
	StackArgs.TransitionStackIndex = INDEX_NONE;

	int32 RequestedIndex = 0;
	if (InArgs->TryGetNumberField(Args::StackIndex, RequestedIndex))
	{
		StackArgs.TransitionStackIndex = RequestedIndex;
	}

	USMTransitionInstance* StackInstance = GraphGen->CreateTransitionStackInstance(TransitionEdge, StackArgs);
	if (!StackInstance)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Failed to add transition stack '%s' to transition '%s'."), *TransitionClassPath, *TransitionGuidStr));
	}

	const TArray<FTransitionStackContainer>& Stack = TransitionEdge->GetAllNodeStackTemplates();
	int32 ResolvedIndex = INDEX_NONE;
	FGuid TemplateGuid;
	for (int32 Idx = 0; Idx < Stack.Num(); ++Idx)
	{
		if (Stack[Idx].NodeStackInstanceTemplate == StackInstance)
		{
			ResolvedIndex = Idx;
			TemplateGuid = Stack[Idx].TemplateGuid;
			break;
		}
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::TransitionGuid, TransitionGuidStr);
	Payload->SetStringField(Args::TransitionClass, StackClass->GetPathName());
	Payload->SetNumberField(Args::StackIndex, ResolvedIndex);
	Payload->SetStringField(Args::TemplateGuid, TemplateGuid.ToString());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddConduit(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;
	CreateArgs.StateInstanceClass = USMConduitInstance::StaticClass();

	FString StateName;
	if (InArgs->TryGetStringField(Args::StateName, StateName))
	{
		CreateArgs.StateName = StateName;
	}

	bool bIsEntry = false;
	if (InArgs->TryGetBoolField(Args::IsEntry, bIsEntry))
	{
		CreateArgs.bIsEntryState = bIsEntry;
	}

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	FString StateClassPath;
	if (InArgs->TryGetStringField(Args::StateClass, StateClassPath) && !StateClassPath.IsEmpty())
	{
		UClass* ConduitClass = LoadClass<USMConduitInstance>(nullptr, *StateClassPath);
		if (!ConduitClass)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not load 'state_class' '%s' as a USMConduitInstance subclass."), *StateClassPath));
		}
		CreateArgs.StateInstanceClass = ConduitClass;
	}

	bool bEvalWithTransitions = false;
	if (InArgs->TryGetBoolField(Args::EvalWithTransitions, bEvalWithTransitions))
	{
		CreateArgs.bConduitEvalWithTransitions = bEvalWithTransitions;
	}

	USMGraphNode_StateNodeBase* StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create conduit node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	if (const UClass* NodeClass = StateNode->GetNodeClass())
	{
		Payload->SetStringField(Args::StateClass, NodeClass->GetPathName());
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddReference(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString ReferencedPath;
	if (!InArgs->TryGetStringField(Args::ReferenceAssetPath, ReferencedPath) || ReferencedPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'reference_asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString ReferencedLoadError;
	USMBlueprint* ReferencedBlueprint = LD::Assist::Utils::LoadStateMachineBlueprint(ReferencedPath, ReferencedLoadError);
	if (!ReferencedBlueprint)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not load 'reference_asset_path': %s"), *ReferencedLoadError));
	}

	if (ReferencedBlueprint == Blueprint)
	{
		return FSMAssistOperationResult::MakeError(
			TEXT("A state machine blueprint cannot reference itself."));
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;
	CreateArgs.StateInstanceClass = USMStateMachineInstance::StaticClass();
	CreateArgs.ReferencedBlueprint = ReferencedBlueprint;

	FString StateName;
	if (InArgs->TryGetStringField(Args::StateName, StateName))
	{
		CreateArgs.StateName = StateName;
	}

	bool bIsEntry = false;
	if (InArgs->TryGetBoolField(Args::IsEntry, bIsEntry))
	{
		CreateArgs.bIsEntryState = bIsEntry;
	}

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	USMGraphNode_StateNodeBase* StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create reference node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	Payload->SetStringField(Args::ReferenceAssetPath, ReferencedBlueprint->GetPathName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddAnyState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;
	CreateArgs.GraphNodeClass = USMGraphNode_AnyStateNode::StaticClass();

	FString StateName;
	if (InArgs->TryGetStringField(Args::StateName, StateName))
	{
		CreateArgs.StateName = StateName;
	}

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	USMGraphNode_StateNodeBase* StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create any state node."));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, StateNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, StateNode->GetStateName());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::AddLinkState(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString LinkToName;
	if (!InArgs->TryGetStringField(Args::LinkToStateName, LinkToName) || LinkToName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'link_to_state_name'."));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FCreateStateNodeArgs CreateArgs;
	CreateArgs.GraphNodeClass = USMGraphNode_LinkStateNode::StaticClass();

	if (InArgs->HasField(Args::PositionX) || InArgs->HasField(Args::PositionY))
	{
		double PosX = 0.0;
		double PosY = 0.0;
		InArgs->TryGetNumberField(Args::PositionX, PosX);
		InArgs->TryGetNumberField(Args::PositionY, PosY);
		CreateArgs.NodePosition = FVector2D(PosX, PosY);
	}

	USMGraphNode_StateNodeBase* StateNode = GraphGen->CreateStateNode(Blueprint, CreateArgs);
	if (!StateNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Failed to create link state node."));
	}

	USMGraphNode_LinkStateNode* LinkNode = Cast<USMGraphNode_LinkStateNode>(StateNode);
	if (!LinkNode)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Created node was not a link state node."));
	}

	TArray<USMGraphNode_StateNodeBase*> Available;
	LinkNode->GetAvailableStatesToLink(Available);
	const bool bTargetExists = Available.ContainsByPredicate(
		[&LinkToName](const USMGraphNode_StateNodeBase* InState)
		{
			return InState && InState->GetStateName() == LinkToName;
		});

	if (!bTargetExists)
	{
		GraphGen->RemoveNode(LinkNode);
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("No state named '%s' is available to link in this graph."), *LinkToName));
	}

	LinkNode->LinkToState(LinkToName);

	if (!LinkNode->IsLinkedStateValid())
	{
		GraphGen->RemoveNode(LinkNode);
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Failed to link to state '%s'."), *LinkToName));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::StateGuid, LinkNode->NodeGuid.ToString());
	Payload->SetStringField(Args::StateName, LinkNode->GetStateName());
	if (const USMGraphNode_StateNodeBase* LinkedState = LinkNode->GetLinkedState())
	{
		Payload->SetStringField(Args::LinkedStateGuid, LinkedState->NodeGuid.ToString());
		Payload->SetStringField(Args::LinkToStateName, LinkedState->GetStateName());
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static TSharedRef<FJsonObject> DescribeProperty(const FProperty* InProperty, const void* InContainer, const UObject* InOwner)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(Args::Name, InProperty->GetName());
		Entry->SetStringField(Args::Type, InProperty->GetCPPType());

		const FString Category = InProperty->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty())
		{
			Entry->SetStringField(Args::Category, Category);
		}

		FString ValueString;
		const void* ValuePtr = InProperty->ContainerPtrToValuePtr<void>(InContainer);
		InProperty->ExportTextItem_Direct(ValueString, ValuePtr, nullptr, const_cast<UObject*>(InOwner), PPF_None);
		Entry->SetStringField(Args::Value, ValueString);
		return Entry;
	}
}

FSMAssistOperationResult LD::Assist::GetNodeProperties(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	int32 StackIndex = INDEX_NONE;
	const bool bHasStackIndex = InArgs->TryGetNumberField(Args::StackIndex, StackIndex) && StackIndex >= 0;

	USMNodeInstance* Template = bHasStackIndex ? Node->GetTemplateFromIndex(StackIndex) : Node->GetNodeTemplate();
	if (!Template)
	{
		if (bHasStackIndex)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("No stack template at index %d on node '%s'."), StackIndex, *NodeGuidStr));
		}
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' has no template instance."), *NodeGuidStr));
	}

	TArray<TSharedPtr<FJsonValue>> Properties;
	for (TFieldIterator<FProperty> It(Template->GetClass(), EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		if (LD::Assist::Private::IsPropertyHiddenOnInstanceTemplate(Property, Template))
		{
			continue;
		}
		Properties.Add(MakeShared<FJsonValueObject>(LD::Assist::Private::DescribeProperty(Property, Template, Template)));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	Payload->SetStringField(Args::StateClass, Template->GetClass()->GetPathName());
	if (bHasStackIndex)
	{
		Payload->SetNumberField(Args::StackIndex, StackIndex);
	}
	Payload->SetArrayField(Args::Properties, Properties);
	Payload->SetNumberField(Args::Count, Properties.Num());
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::SetTransitionCondition(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString TransitionGuidStr;
	if (!InArgs->TryGetStringField(Args::TransitionGuid, TransitionGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'transition_guid'."));
	}

	bool bCondition = false;
	if (!InArgs->TryGetBoolField(Args::Condition, bCondition))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'condition' (boolean)."));
	}

	FGuid TransitionGuid;
	if (!FGuid::Parse(TransitionGuidStr, TransitionGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'transition_guid' '%s'."), *TransitionGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, TransitionGuid);
	USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node);
	if (!TransitionEdge)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Node '%s' is not a transition edge."), *TransitionGuidStr));
	}

	USMTransitionGraph* TransitionGraph = TransitionEdge->GetTransitionGraph();
	if (!TransitionGraph || !TransitionGraph->ResultNode)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Transition '%s' has no result node to configure."), *TransitionGuidStr));
	}

	UEdGraphPin* EvaluationPin = TransitionGraph->ResultNode->GetTransitionEvaluationPin();
	if (!EvaluationPin)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Transition '%s' has no evaluation pin."), *TransitionGuidStr));
	}

	const UEdGraphSchema* Schema = TransitionGraph->GetSchema();
	if (!Schema)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Transition '%s' graph has no schema."), *TransitionGuidStr));
	}

	Schema->TrySetDefaultValue(*EvaluationPin, bCondition ? TEXT("True") : TEXT("False"));

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::TransitionGuid, TransitionGuidStr);
	Payload->SetBoolField(Args::Condition, bCondition);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static FBlueprintEditor* FindOrOpenBlueprintEditor(USMBlueprint* InBlueprint, FString& OutError)
	{
		if (!GEditor)
		{
			OutError = TEXT("GEditor unavailable; this op requires the editor to be running.");
			return nullptr;
		}

		UAssetEditorSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!AssetSubsystem)
		{
			OutError = TEXT("AssetEditorSubsystem unavailable.");
			return nullptr;
		}

		if (!AssetSubsystem->OpenEditorForAsset(InBlueprint))
		{
			OutError = FString::Printf(TEXT("Failed to open asset editor for '%s'."), *InBlueprint->GetPathName());
			return nullptr;
		}

		IAssetEditorInstance* EditorInstance = AssetSubsystem->FindEditorForAsset(InBlueprint, /*bFocusIfOpen=*/false);
		if (!EditorInstance)
		{
			OutError = FString::Printf(TEXT("Asset editor instance not found for '%s' after open."), *InBlueprint->GetPathName());
			return nullptr;
		}

		// LD's SM blueprint editor is FSMStateMachineBlueprintEditor : ISMStateMachineBlueprintEditor : FBlueprintEditor.
		// FBlueprintEditor : FAssetEditorToolkit : IAssetEditorInstance, so the static_cast back to FBlueprintEditor is
		// safe whenever the asset is a USMBlueprint (the only path this helper services).
		return static_cast<FBlueprintEditor*>(EditorInstance);
	}

	static TSharedPtr<SGraphEditor> OpenAndFocusRootGraph(FBlueprintEditor* InEditor, USMBlueprint* InBlueprint, FString& OutError)
	{
		USMGraph* RootGraph = FSMBlueprintEditorUtils::GetRootStateMachineGraph(InBlueprint);
		if (!RootGraph)
		{
			OutError = TEXT("Blueprint has no root state machine graph.");
			return nullptr;
		}

		TSharedPtr<SGraphEditor> GraphEditor = InEditor->OpenGraphAndBringToFront(RootGraph, /*bSetFocus=*/true);
		if (!GraphEditor.IsValid())
		{
			OutError = TEXT("Failed to focus the root graph in the blueprint editor.");
			return nullptr;
		}
		return GraphEditor;
	}

	// Force a Slate tick so newly-opened panels compute their desired sizes before we read geometry.
	static void EnsureSlateLayoutReady()
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick(ESlateTickType::All);
		}
	}

	// True while any of the panel's deferred view changes are still pending: a queued movement-to-target,
	// a queued zoom-to-extents, or an in-flight scroll/zoom interpolation driven by the active timer.
	// HasDeferredObjectFocus and HasDeferredZoomDestination both clear after the first tick that consumes
	// the deferred request, but the actual scrolling and zooming runs on an active timer that interpolates
	// over many frames; ZoomTargetTopLeft/BottomRight are zeroed only when that timer reports it is done.
	// All three checks together are needed to know the view has truly settled before screenshotting.
	static bool IsGraphPanelViewSettled(const SGraphPanel* InPanel)
	{
		if (!InPanel)
		{
			return true;
		}
		FVector2f ZoomTopLeft = FVector2f::ZeroVector;
		FVector2f ZoomBottomRight = FVector2f::ZeroVector;
		const bool bHasZoomTarget = InPanel->GetZoomTargetRect(ZoomTopLeft, ZoomBottomRight);
		return !InPanel->HasDeferredObjectFocus()
			&& !InPanel->HasDeferredZoomDestination()
			&& !bHasZoomTarget;
	}

	static void PumpSlateUntilGraphPanelSettles(SGraphPanel* InPanel, int32 InMaxTicks)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		FSlateApplication& App = FSlateApplication::Get();
		for (int32 TickIdx = 0; TickIdx < InMaxTicks && !IsGraphPanelViewSettled(InPanel); ++TickIdx)
		{
			App.Tick(ESlateTickType::All);
		}
	}

	// SGraphEditor::ZoomToFit defers the zoom to the next paint cycle and animates over multiple frames.
	// Pump Slate until the panel's deferred state and the active-timer interpolation have both resolved
	// so the screenshot reflects the final framed view, not an in-flight scroll or zoom.
	static void FitGraphPanelToContent(const TSharedRef<SGraphEditor>& InGraphEditor, SGraphPanel* InPanel)
	{
		if (!InPanel)
		{
			return;
		}
		InGraphEditor->ZoomToFit(/*bOnlySelection=*/false);
		PumpSlateUntilGraphPanelSettles(InPanel, /*MaxTicks=*/64);
	}

	// SGraphEditor::JumpToNode is more deferred than ZoomToFit: the first tick consumes the selection
	// and movement-target state and schedules a scroll-and-zoom active timer; subsequent ticks
	// interpolate the view toward the node. The settle check waits for the interpolation to finish.
	static void FocusGraphPanelOnNode(const TSharedRef<SGraphEditor>& InGraphEditor, SGraphPanel* InPanel, const UEdGraphNode* InNode)
	{
		if (!InPanel || !InNode)
		{
			return;
		}
		InGraphEditor->JumpToNode(InNode, /*bRequestRename=*/false, /*bSelectNode=*/true);
		PumpSlateUntilGraphPanelSettles(InPanel, /*MaxTicks=*/64);
	}

	static TArray<TSharedPtr<FJsonValue>> Vec2fToJsonArray(const FVector2f& InVec)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Add(MakeShared<FJsonValueNumber>(InVec.X));
		Array.Add(MakeShared<FJsonValueNumber>(InVec.Y));
		return Array;
	}

	static TArray<TSharedPtr<FJsonValue>> ColorToJsonArray(const FLinearColor& InColor)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Add(MakeShared<FJsonValueNumber>(InColor.R));
		Array.Add(MakeShared<FJsonValueNumber>(InColor.G));
		Array.Add(MakeShared<FJsonValueNumber>(InColor.B));
		Array.Add(MakeShared<FJsonValueNumber>(InColor.A));
		return Array;
	}

	static const TCHAR* ResolveNodeKind(const UEdGraphNode* InNode)
	{
		if (InNode->IsA<USMGraphNode_StateMachineEntryNode>())
		{
			return TEXT("entry");
		}
		if (InNode->IsA<USMGraphNode_TransitionEdge>())
		{
			return TEXT("transition");
		}
		if (InNode->IsA<USMGraphNode_AnyStateNode>())
		{
			return TEXT("any_state");
		}
		if (InNode->IsA<USMGraphNode_LinkStateNode>())
		{
			return TEXT("link_state");
		}
		if (InNode->IsA<USMGraphNode_RerouteNode>())
		{
			return TEXT("reroute");
		}
		if (InNode->IsA<USMGraphNode_StateMachineStateNode>())
		{
			return TEXT("state_machine_state");
		}
		if (InNode->IsA<USMGraphNode_ConduitNode>())
		{
			return TEXT("conduit");
		}
		if (InNode->IsA<USMGraphNode_StateNodeBase>())
		{
			return TEXT("state");
		}
		return TEXT("unknown");
	}

	static void BuildPinJson(const UEdGraphNode* InNode, TArray<TSharedPtr<FJsonValue>>& OutPins)
	{
		for (const UEdGraphPin* Pin : InNode->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(Args::PinId, Pin->PinId.ToString());
			Entry->SetStringField(Args::PinName, Pin->PinName.ToString());
			Entry->SetStringField(Args::PinDirection, Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			OutPins.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
}

FSMAssistOperationResult LD::Assist::GetGraphView(const TSharedRef<FJsonObject>& InArgs)
{
	check(IsInGameThread());

	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	bool bIncludeTransitions = true;
	InArgs->TryGetBoolField(Args::IncludeTransitions, bIncludeTransitions);

	bool bIncludePins = false;
	InArgs->TryGetBoolField(Args::IncludePins, bIncludePins);

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	FString EditorError;
	FBlueprintEditor* BlueprintEditor = LD::Assist::Private::FindOrOpenBlueprintEditor(Blueprint, EditorError);
	if (!BlueprintEditor)
	{
		return FSMAssistOperationResult::MakeError(EditorError);
	}

	FString GraphError;
	TSharedPtr<SGraphEditor> GraphEditor = LD::Assist::Private::OpenAndFocusRootGraph(BlueprintEditor, Blueprint, GraphError);
	if (!GraphEditor.IsValid())
	{
		return FSMAssistOperationResult::MakeError(GraphError);
	}

	LD::Assist::Private::EnsureSlateLayoutReady();

	SGraphPanel* Panel = GraphEditor->GetGraphPanel();
	if (!Panel)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Graph editor has no panel."));
	}

	USMGraph* RootGraph = FSMBlueprintEditorUtils::GetRootStateMachineGraph(Blueprint);
	check(RootGraph);

	// Map data nodes to their slate widgets so we can co-iterate. GetAllChildren includes off-viewport nodes,
	// which is what we want; GetChildren would only report the currently-visible ones.
	TMap<const UEdGraphNode*, TSharedRef<SGraphNode>> NodeToWidget;
	if (FChildren* AllChildren = Panel->GetAllChildren())
	{
		const int32 NumChildren = AllChildren->Num();
		for (int32 ChildIdx = 0; ChildIdx < NumChildren; ++ChildIdx)
		{
			TSharedRef<SWidget> Widget = AllChildren->GetChildAt(ChildIdx);
			TSharedRef<SGraphNode> NodeWidget = StaticCastSharedRef<SGraphNode>(Widget);
			if (UEdGraphNode* DataNode = Cast<UEdGraphNode>(NodeWidget->GetObjectBeingDisplayed()))
			{
				NodeToWidget.Add(DataNode, NodeWidget);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> TransitionsArray;

	for (UEdGraphNode* Node : RootGraph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		const bool bIsTransition = Node->IsA<USMGraphNode_TransitionEdge>();
		if (bIsTransition && !bIncludeTransitions)
		{
			continue;
		}

		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(Args::NodeGuid, Node->NodeGuid.ToString());
		Entry->SetStringField(Args::Kind, LD::Assist::Private::ResolveNodeKind(Node));

		TArray<TSharedPtr<FJsonValue>> LogicalPosition;
		LogicalPosition.Add(MakeShared<FJsonValueNumber>(Node->NodePosX));
		LogicalPosition.Add(MakeShared<FJsonValueNumber>(Node->NodePosY));
		Entry->SetArrayField(Args::LogicalPosition, LogicalPosition);

		if (const TSharedRef<SGraphNode>* WidgetPtr = NodeToWidget.Find(Node))
		{
			const TSharedRef<SGraphNode>& NodeWidget = *WidgetPtr;
			Entry->SetArrayField(Args::WidgetPosition, LD::Assist::Private::Vec2fToJsonArray(NodeWidget->GetPosition2f()));
			Entry->SetArrayField(Args::WidgetSize, LD::Assist::Private::Vec2fToJsonArray(NodeWidget->GetDesiredSizeForMarquee2f()));
			Entry->SetStringField(Args::TitleText, NodeWidget->GetEditableNodeTitleAsText().ToString());
			Entry->SetArrayField(Args::BodyColor, LD::Assist::Private::ColorToJsonArray(NodeWidget->GetNodeBodyColor().GetSpecifiedColor()));
			Entry->SetArrayField(Args::TitleColor, LD::Assist::Private::ColorToJsonArray(NodeWidget->GetNodeTitleColor().GetSpecifiedColor()));
		}

		const FString NodeComment = Node->NodeComment;
		if (!NodeComment.IsEmpty())
		{
			Entry->SetStringField(Args::Comment, NodeComment);
		}

		Entry->SetBoolField(Args::IsSelected, Panel->SelectionManager.SelectedNodes.Contains(Node));

		if (bIsTransition)
		{
			if (const USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node))
			{
				if (const USMGraphNode_StateNodeBase* FromState = TransitionEdge->GetFromState())
				{
					Entry->SetStringField(Args::FromStateGuid, FromState->NodeGuid.ToString());
				}
				if (const USMGraphNode_StateNodeBase* ToState = TransitionEdge->GetToState())
				{
					Entry->SetStringField(Args::ToStateGuid, ToState->NodeGuid.ToString());
				}
			}
		}

		if (bIncludePins)
		{
			TArray<TSharedPtr<FJsonValue>> Pins;
			LD::Assist::Private::BuildPinJson(Node, Pins);
			Entry->SetArrayField(Args::Pins, Pins);
		}

		if (bIsTransition)
		{
			TransitionsArray.Add(MakeShared<FJsonValueObject>(Entry));
		}
		else
		{
			NodesArray.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	const TSharedRef<FJsonObject> PanelView = MakeShared<FJsonObject>();
	{
		FVector2f ViewLocation = FVector2f::ZeroVector;
		float ZoomAmount = 1.0f;
		GraphEditor->GetViewLocation(ViewLocation, ZoomAmount);
		PanelView->SetNumberField(Args::Zoom, ZoomAmount);
		PanelView->SetArrayField(Args::ViewOffset, LD::Assist::Private::Vec2fToJsonArray(ViewLocation));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetObjectField(Args::PanelView, PanelView);
	Payload->SetArrayField(Args::Nodes, NodesArray);
	if (bIncludeTransitions)
	{
		Payload->SetArrayField(Args::Transitions, TransitionsArray);
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::CaptureGraphView(const TSharedRef<FJsonObject>& InArgs)
{
	check(IsInGameThread());

	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	bool bClipToPanel = true;
	InArgs->TryGetBoolField(Args::ClipToPanel, bClipToPanel);

	bool bFitToContent = true;
	InArgs->TryGetBoolField(Args::FitToContent, bFitToContent);

	FString NodeGuidStr;
	const bool bHasNodeGuid = InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr) && !NodeGuidStr.IsEmpty();
	FGuid NodeGuid;
	if (bHasNodeGuid && !FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not parse node_guid '%s'."), *NodeGuidStr));
	}

	FString OutputSubdir = TEXT("LogicDriver");
	InArgs->TryGetStringField(Args::OutputSubdir, OutputSubdir);

	FString Prefix;
	InArgs->TryGetStringField(Args::Prefix, Prefix);

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* FocusNode = nullptr;
	if (bHasNodeGuid)
	{
		FocusNode = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
		if (!FocusNode)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
		}
	}

	FString EditorError;
	FBlueprintEditor* BlueprintEditor = LD::Assist::Private::FindOrOpenBlueprintEditor(Blueprint, EditorError);
	if (!BlueprintEditor)
	{
		return FSMAssistOperationResult::MakeError(EditorError);
	}

	FString GraphError;
	TSharedPtr<SGraphEditor> GraphEditor = LD::Assist::Private::OpenAndFocusRootGraph(BlueprintEditor, Blueprint, GraphError);
	if (!GraphEditor.IsValid())
	{
		return FSMAssistOperationResult::MakeError(GraphError);
	}

	LD::Assist::Private::EnsureSlateLayoutReady();

	SGraphPanel* Panel = GraphEditor->GetGraphPanel();
	if (!Panel)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Graph editor has no panel."));
	}

	if (FocusNode)
	{
		// node_guid takes precedence over fit_to_content: focusing a single node and fitting to all
		// content are mutually exclusive intents. The deferred-focus pump leaves the panel framed on
		// the chosen node when capture proceeds below.
		LD::Assist::Private::FocusGraphPanelOnNode(GraphEditor.ToSharedRef(), Panel, FocusNode);
	}
	else if (bFitToContent)
	{
		LD::Assist::Private::FitGraphPanelToContent(GraphEditor.ToSharedRef(), Panel);
	}

	TSharedPtr<SWidget> TargetWidget;
	if (bClipToPanel)
	{
		TargetWidget = Panel->AsShared();
	}
	else
	{
		// Capture the entire blueprint editor window the panel is parented in.
		TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(Panel->AsShared());
		if (!Window.IsValid())
		{
			return FSMAssistOperationResult::MakeError(TEXT("Could not locate the editor window for capture."));
		}
		TargetWidget = Window;
	}

	TArray<FColor> ColorData;
	FIntVector OutSize(0, 0, 0);
	if (!FSlateApplication::Get().TakeScreenshot(TargetWidget.ToSharedRef(), ColorData, OutSize))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Slate screenshot capture failed."));
	}
	if (OutSize.X <= 0 || OutSize.Y <= 0 || ColorData.Num() == 0)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Screenshot returned an empty image."));
	}

	TArray64<uint8> PngBytes;
	FImageUtils::PNGCompressImageArray(
		OutSize.X, OutSize.Y,
		TArrayView64<const FColor>(ColorData.GetData(), ColorData.Num()),
		PngBytes);

	if (PngBytes.Num() == 0)
	{
		return FSMAssistOperationResult::MakeError(TEXT("PNG encoding produced zero bytes."));
	}

	if (Prefix.IsEmpty())
	{
		Prefix = FString::Printf(TEXT("%s_%s"), *Blueprint->GetName(), *FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S")));
	}

	const FString FileName = Prefix + TEXT(".png");
	const FString TargetDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), OutputSubdir));
	IFileManager::Get().MakeDirectory(*TargetDir, /*Tree=*/true);
	const FString TargetPath = FPaths::Combine(TargetDir, FileName);

	if (!FFileHelper::SaveArrayToFile(PngBytes, *TargetPath))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Failed to write PNG to '%s'."), *TargetPath));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::Path, TargetPath);
	Payload->SetNumberField(Args::Width, OutSize.X);
	Payload->SetNumberField(Args::Height, OutSize.Y);
	Payload->SetNumberField(Args::Bytes, PngBytes.Num());
	Payload->SetStringField(Args::Mime, TEXT("image/png"));
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::ClearScreenshots(const TSharedRef<FJsonObject>& InArgs)
{
	check(IsInGameThread());

	FString OutputSubdir = TEXT("LogicDriver");
	InArgs->TryGetStringField(Args::OutputSubdir, OutputSubdir);

	double OlderThanSeconds = 0.0;
	const bool bHasAgeFilter = InArgs->TryGetNumberField(Args::OlderThanSeconds, OlderThanSeconds) && OlderThanSeconds > 0.0;

	bool bDryRun = false;
	InArgs->TryGetBoolField(Args::DryRun, bDryRun);

	const FString TargetDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), OutputSubdir));

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::Directory, TargetDir);
	Payload->SetBoolField(Args::DryRun, bDryRun);

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*TargetDir))
	{
		Payload->SetNumberField(Args::DeletedCount, 0);
		Payload->SetNumberField(Args::FreedBytes, 0);
		Payload->SetArrayField(Args::Paths, TArray<TSharedPtr<FJsonValue>>());
		return FSMAssistOperationResult::MakeSuccess(Payload);
	}

	TArray<FString> RelativeFiles;
	const FString PngFilter = FPaths::Combine(TargetDir, TEXT("*.png"));
	FileManager.FindFiles(RelativeFiles, *PngFilter, /*Files=*/true, /*Directories=*/false);

	const FDateTime Now = FDateTime::UtcNow();

	int32 RemovedCount = 0;
	int64 FreedBytes = 0;
	TArray<TSharedPtr<FJsonValue>> RemovedPaths;
	RemovedPaths.Reserve(RelativeFiles.Num());

	for (const FString& RelativeFile : RelativeFiles)
	{
		const FString AbsolutePath = FPaths::Combine(TargetDir, RelativeFile);

		if (bHasAgeFilter)
		{
			const FDateTime FileTime = FileManager.GetTimeStamp(*AbsolutePath);
			if (FileTime == FDateTime::MinValue())
			{
				continue;
			}
			const FTimespan Age = Now - FileTime;
			if (Age.GetTotalSeconds() < OlderThanSeconds)
			{
				continue;
			}
		}

		const int64 FileBytes = FileManager.FileSize(*AbsolutePath);

		if (!bDryRun)
		{
			if (!FileManager.Delete(*AbsolutePath, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true))
			{
				continue;
			}
		}

		++RemovedCount;
		if (FileBytes > 0)
		{
			FreedBytes += FileBytes;
		}
		RemovedPaths.Add(MakeShared<FJsonValueString>(AbsolutePath));
	}

	Payload->SetNumberField(Args::DeletedCount, RemovedCount);
	Payload->SetNumberField(Args::FreedBytes, FreedBytes);
	Payload->SetArrayField(Args::Paths, RemovedPaths);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	// Walk the BP and gather every USMGraph reachable through state-machine state nodes. The root
	// graph is index 0; nested graphs follow in encounter order. Used by sm.layout_states with
	// scope=all so a single op call can lay out an entire hierarchical state machine.
	static void CollectAllStateMachineGraphs(USMGraph* InRoot, TArray<USMGraph*>& OutGraphs)
	{
		if (!InRoot || OutGraphs.Contains(InRoot))
		{
			return;
		}
		OutGraphs.Add(InRoot);
		for (UEdGraphNode* Node : InRoot->Nodes)
		{
			if (USMGraphNode_StateMachineStateNode* SubMachineNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
			{
				if (USMGraph* NestedSM = Cast<USMGraph>(SubMachineNode->GetBoundGraph()))
				{
					CollectAllStateMachineGraphs(NestedSM, OutGraphs);
				}
			}
		}
	}

	static FGuid FindEntryStateGuid(USMGraph* InGraph)
	{
		if (!InGraph)
		{
			return FGuid();
		}
		USMGraphNode_StateMachineEntryNode* EntryNode = InGraph->GetEntryNode();
		if (!EntryNode)
		{
			return FGuid();
		}
		const UEdGraphPin* OutputPin = EntryNode->GetOutputPin();
		if (!OutputPin)
		{
			return FGuid();
		}
		for (UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
		{
			if (!LinkedPin)
			{
				continue;
			}
			if (USMGraphNode_StateNodeBase* State = Cast<USMGraphNode_StateNodeBase>(LinkedPin->GetOwningNode()))
			{
				return State->NodeGuid;
			}
		}
		return FGuid();
	}

	// Build a path label like "RootStateMachine" or "RootStateMachine/StateA" for nested graphs so
	// the response can disambiguate when scope=all returns multiple graphs.
	static FString BuildGraphPathLabel(USMGraph* InGraph, USMGraph* InRoot)
	{
		if (InGraph == InRoot || !InGraph)
		{
			return InGraph ? InGraph->GetName() : FString();
		}
		TArray<FString> Segments;
		UEdGraph* Current = InGraph;
		while (Current && Current != InRoot)
		{
			Segments.Add(Current->GetName());
			UObject* Outer = Current->GetOuter();
			UEdGraph* ParentGraph = nullptr;
			while (Outer)
			{
				if (UEdGraphNode* OuterNode = Cast<UEdGraphNode>(Outer))
				{
					ParentGraph = OuterNode->GetGraph();
					break;
				}
				Outer = Outer->GetOuter();
			}
			Current = ParentGraph;
		}
		Segments.Add(InRoot->GetName());
		Algo::Reverse(Segments);
		return FString::Join(Segments, TEXT("/"));
	}

	// Translate one USMGraph into a Layout::FLayoutInput. NodeToWidget supplies measured widget
	// sizes for nodes whose Slate widget exists; the algorithm falls back to a per-kind default
	// table for the rest. Comment, transition, entry, and reroute nodes are filtered out; only
	// flow nodes (state, conduit, reference, link_state, any_state) are passed to the algorithm.
	// Transitions become FLayoutEdge entries.
	static void BuildLayoutInputForGraph(
		USMGraph* InGraph,
		const TMap<const UEdGraphNode*, TSharedRef<SGraphNode>>* InNodeToWidget,
		LD::Assist::Layout::FLayoutInput& OutInput)
	{
		if (!InGraph)
		{
			return;
		}
		OutInput.EntryGuid = FindEntryStateGuid(InGraph);

		for (UEdGraphNode* Node : InGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			if (Node->IsA<USMGraphNode_StateMachineEntryNode>())
			{
				continue;
			}
			if (Node->IsA<USMGraphNode_RerouteNode>())
			{
				continue;
			}
			if (Node->IsA<UEdGraphNode_Comment>())
			{
				continue;
			}
			if (USMGraphNode_TransitionEdge* TransitionEdge = Cast<USMGraphNode_TransitionEdge>(Node))
			{
				const USMGraphNode_StateNodeBase* From = TransitionEdge->GetFromState();
				const USMGraphNode_StateNodeBase* To = TransitionEdge->GetToState();
				if (From && To && From != To)
				{
					LD::Assist::Layout::FLayoutEdge Edge;
					Edge.FromGuid = From->NodeGuid;
					Edge.ToGuid = To->NodeGuid;
					OutInput.Edges.Add(Edge);
				}
				continue;
			}

			LD::Assist::Layout::FLayoutNode LayoutNode;
			LayoutNode.Node = Node;
			LayoutNode.NodeGuid = Node->NodeGuid;
			LayoutNode.Name = Node->GetNodeTitle(ENodeTitleType::EditableTitle).ToString();
			LayoutNode.Kind = ResolveNodeKind(Node);
			LayoutNode.OldPosition = FVector2f(static_cast<float>(Node->NodePosX), static_cast<float>(Node->NodePosY));
			if (InNodeToWidget)
			{
				if (const TSharedRef<SGraphNode>* WidgetPtr = InNodeToWidget->Find(Node))
				{
					LayoutNode.WidgetSize = (*WidgetPtr)->GetDesiredSizeForMarquee2f();
				}
			}
			OutInput.Nodes.Add(MoveTemp(LayoutNode));
		}
	}

	static TSharedRef<FJsonValue> Vec2fToJsonArrayValue(const FVector2f& InVec)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Add(MakeShared<FJsonValueNumber>(InVec.X));
		Array.Add(MakeShared<FJsonValueNumber>(InVec.Y));
		return MakeShared<FJsonValueArray>(Array);
	}

	// Apply NodePosX / NodePosY for a single node via the property pipeline so listeners and
	// dependent UI refresh the same way a details-panel edit would. Wraps Modify() for transaction
	// support; caller is responsible for the surrounding FScopedTransaction.
	static bool ApplyNodePosition(UEdGraphNode* InNode, const FVector2f& InNewPosition)
	{
		if (!InNode)
		{
			return false;
		}
		FProperty* PosXProperty = InNode->GetClass()->FindPropertyByName(TEXT("NodePosX"));
		FProperty* PosYProperty = InNode->GetClass()->FindPropertyByName(TEXT("NodePosY"));
		if (!PosXProperty || !PosYProperty)
		{
			return false;
		}
		InNode->Modify();
		LD::Editor::PropertyUtils::SetPropertyValue(
			PosXProperty,
			FString::FromInt(FMath::RoundToInt(InNewPosition.X)),
			InNode);
		LD::Editor::PropertyUtils::SetPropertyValue(
			PosYProperty,
			FString::FromInt(FMath::RoundToInt(InNewPosition.Y)),
			InNode);
		return true;
	}

	struct FLayoutGraphContext
	{
		USMGraph* Graph = nullptr;
		FString GraphPathLabel;
		LD::Assist::Layout::FLayoutGraphResult Result;
	};
}

FSMAssistOperationResult LD::Assist::LayoutStates(const TSharedRef<FJsonObject>& InArgs)
{
	check(IsInGameThread());

	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	LD::Assist::Layout::ELayoutStrategy Strategy = LD::Assist::Layout::ELayoutStrategy::LeftToRight;
	{
		FString StrategyStr;
		if (InArgs->TryGetStringField(Args::Strategy, StrategyStr) && !StrategyStr.IsEmpty())
		{
			if (!LD::Assist::Layout::TryParseStrategy(StrategyStr, Strategy))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Unknown strategy '%s'. Use 'left_to_right' or 'top_to_bottom'."), *StrategyStr));
			}
		}
	}

	bool bApply = false;
	InArgs->TryGetBoolField(Args::Apply, bApply);

	FString ScopeStr = TEXT("root");
	InArgs->TryGetStringField(Args::Scope, ScopeStr);
	bool bScopeAll = false;
	if (ScopeStr.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		bScopeAll = true;
	}
	else if (!ScopeStr.Equals(TEXT("root"), ESearchCase::IgnoreCase))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Unknown scope '%s'. Use 'root' or 'all'."), *ScopeStr));
	}

	double ColumnGap = 80.0;
	InArgs->TryGetNumberField(Args::ColumnGap, ColumnGap);
	double RowGap = 40.0;
	InArgs->TryGetNumberField(Args::RowGap, RowGap);
	// Entry node lives at (0, 0) on every state machine and is not part of the layout. Default the
	// first state's anchor along the flow axis to 200 so the laid-out main row sits clearly past
	// Entry, matching the plugin's "states flow rightward from Entry" / "downward from Entry"
	// convention. Default the perpendicular axis to 0 (centered).
	double StartX = (Strategy == LD::Assist::Layout::ELayoutStrategy::LeftToRight) ? 200.0 : 0.0;
	double StartY = (Strategy == LD::Assist::Layout::ELayoutStrategy::TopToBottom) ? 200.0 : 0.0;
	InArgs->TryGetNumberField(Args::StartX, StartX);
	InArgs->TryGetNumberField(Args::StartY, StartY);

	bool bRespectExistingOrder = true;
	InArgs->TryGetBoolField(Args::RespectExistingOrder, bRespectExistingOrder);
	bool bSnapToGrid = true;
	InArgs->TryGetBoolField(Args::SnapToGrid, bSnapToGrid);

	TSet<FGuid> PinnedGuids;
	{
		const TArray<TSharedPtr<FJsonValue>>* PinArray = nullptr;
		if (InArgs->TryGetArrayField(Args::PinNodeGuids, PinArray) && PinArray)
		{
			for (const TSharedPtr<FJsonValue>& Value : *PinArray)
			{
				FString GuidStr;
				if (Value.IsValid() && Value->TryGetString(GuidStr) && !GuidStr.IsEmpty())
				{
					FGuid Parsed;
					if (FGuid::Parse(GuidStr, Parsed))
					{
						PinnedGuids.Add(Parsed);
					}
				}
			}
		}
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraph* RootGraph = FSMBlueprintEditorUtils::GetRootStateMachineGraph(Blueprint);
	if (!RootGraph)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Blueprint has no root state machine graph."));
	}

	FString EditorError;
	FBlueprintEditor* BlueprintEditor = LD::Assist::Private::FindOrOpenBlueprintEditor(Blueprint, EditorError);
	if (!BlueprintEditor)
	{
		return FSMAssistOperationResult::MakeError(EditorError);
	}

	FString GraphError;
	TSharedPtr<SGraphEditor> GraphEditor = LD::Assist::Private::OpenAndFocusRootGraph(BlueprintEditor, Blueprint, GraphError);
	if (!GraphEditor.IsValid())
	{
		return FSMAssistOperationResult::MakeError(GraphError);
	}
	LD::Assist::Private::EnsureSlateLayoutReady();

	// NodeToWidget map only covers the focused (root) panel. Nested graphs in scope=all rely on
	// the per-kind default size table inside the layout module, which keeps the op cheap and avoids
	// disruptively opening every nested graph in the user's editor.
	TMap<const UEdGraphNode*, TSharedRef<SGraphNode>> NodeToWidget;
	if (SGraphPanel* Panel = GraphEditor->GetGraphPanel())
	{
		if (FChildren* AllChildren = Panel->GetAllChildren())
		{
			const int32 NumChildren = AllChildren->Num();
			for (int32 ChildIdx = 0; ChildIdx < NumChildren; ++ChildIdx)
			{
				TSharedRef<SWidget> Widget = AllChildren->GetChildAt(ChildIdx);
				TSharedRef<SGraphNode> NodeWidget = StaticCastSharedRef<SGraphNode>(Widget);
				if (UEdGraphNode* DataNode = Cast<UEdGraphNode>(NodeWidget->GetObjectBeingDisplayed()))
				{
					NodeToWidget.Add(DataNode, NodeWidget);
				}
			}
		}
	}

	TArray<USMGraph*> GraphsToLayout;
	if (bScopeAll)
	{
		LD::Assist::Private::CollectAllStateMachineGraphs(RootGraph, GraphsToLayout);
	}
	else
	{
		GraphsToLayout.Add(RootGraph);
	}

	const float SnapGridSize = static_cast<float>(SNodePanel::GetSnapGridSize());

	TArray<LD::Assist::Private::FLayoutGraphContext> GraphContexts;
	GraphContexts.Reserve(GraphsToLayout.Num());

	for (USMGraph* Graph : GraphsToLayout)
	{
		LD::Assist::Private::FLayoutGraphContext Context;
		Context.Graph = Graph;
		Context.GraphPathLabel = LD::Assist::Private::BuildGraphPathLabel(Graph, RootGraph);

		LD::Assist::Layout::FLayoutInput Input;
		Input.Strategy = Strategy;
		Input.ColumnGap = static_cast<float>(ColumnGap);
		Input.RowGap = static_cast<float>(RowGap);
		Input.Start = (Graph == RootGraph)
			? FVector2f(static_cast<float>(StartX), static_cast<float>(StartY))
			: FVector2f::ZeroVector;
		Input.PinnedGuids = PinnedGuids;
		Input.bRespectExistingOrder = bRespectExistingOrder;
		Input.bSnapToGrid = bSnapToGrid;
		Input.SnapGridSize = SnapGridSize > 0.0f ? SnapGridSize : 16.0f;

		const TMap<const UEdGraphNode*, TSharedRef<SGraphNode>>* NodeToWidgetForGraph = (Graph == RootGraph) ? &NodeToWidget : nullptr;
		LD::Assist::Private::BuildLayoutInputForGraph(Graph, NodeToWidgetForGraph, Input);

		Context.Result = LD::Assist::Layout::ComputeLayout(Input);
		GraphContexts.Add(MoveTemp(Context));
	}

	// Apply path: single transaction wraps all writes across all graphs so undo is one step.
	if (bApply)
	{
		FScopedTransaction Transaction(NSLOCTEXT("SMAssist", "LayoutStatesTransaction", "Auto-Layout States"));
		TSet<USMGraph*> DirtyGraphs;
		for (const LD::Assist::Private::FLayoutGraphContext& Context : GraphContexts)
		{
			for (const LD::Assist::Layout::FLayoutNode& Node : Context.Result.Nodes)
			{
				if (!Node.Node || Node.bPinned)
				{
					continue;
				}
				if (Node.NewPosition == Node.OldPosition)
				{
					continue;
				}
				if (LD::Assist::Private::ApplyNodePosition(Node.Node, Node.NewPosition))
				{
					DirtyGraphs.Add(Context.Graph);
				}
			}
		}
		for (USMGraph* DirtyGraph : DirtyGraphs)
		{
			if (DirtyGraph)
			{
				DirtyGraph->NotifyGraphChanged();
			}
		}
		if (DirtyGraphs.Num() > 0)
		{
			Blueprint->GetPackage()->MarkPackageDirty();
			LD::Assist::Private::EnsureSlateLayoutReady();
		}
	}

	// Build response payload.
	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, Blueprint->GetPathName());
	Payload->SetStringField(Args::Strategy, LD::Assist::Layout::StrategyToString(Strategy));
	Payload->SetStringField(Args::Scope, bScopeAll ? TEXT("all") : TEXT("root"));
	Payload->SetBoolField(Args::Applied, bApply);

	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	GraphsArray.Reserve(GraphContexts.Num());
	for (const LD::Assist::Private::FLayoutGraphContext& Context : GraphContexts)
	{
		const TSharedRef<FJsonObject> GraphEntry = MakeShared<FJsonObject>();
		GraphEntry->SetStringField(Args::GraphPath, Context.GraphPathLabel);

		TArray<TSharedPtr<FJsonValue>> NodeLayoutArray;
		NodeLayoutArray.Reserve(Context.Result.Nodes.Num());
		for (const LD::Assist::Layout::FLayoutNode& Node : Context.Result.Nodes)
		{
			const TSharedRef<FJsonObject> NodeEntry = MakeShared<FJsonObject>();
			NodeEntry->SetStringField(Args::NodeGuid, Node.NodeGuid.ToString());
			NodeEntry->SetStringField(Args::Name, Node.Name);
			NodeEntry->SetStringField(Args::Kind, Node.Kind);
			NodeEntry->SetField(Args::LogicalPosition, LD::Assist::Private::Vec2fToJsonArrayValue(Node.OldPosition));
			NodeEntry->SetField(Args::ProposedPosition, LD::Assist::Private::Vec2fToJsonArrayValue(Node.NewPosition));
			NodeEntry->SetField(Args::Delta, LD::Assist::Private::Vec2fToJsonArrayValue(Node.NewPosition - Node.OldPosition));
			if (Node.Layer != INDEX_NONE && Node.Lane == LD::Assist::Layout::ELayoutLane::Main)
			{
				NodeEntry->SetNumberField(Args::Layer, Node.Layer);
			}
			NodeEntry->SetStringField(Args::Lane, LD::Assist::Layout::LaneToString(Node.Lane));
			if (Node.bPinned)
			{
				NodeEntry->SetBoolField(TEXT("pinned"), true);
			}
			NodeLayoutArray.Add(MakeShared<FJsonValueObject>(NodeEntry));
		}
		GraphEntry->SetArrayField(Args::NodeLayout, NodeLayoutArray);

		TArray<TSharedPtr<FJsonValue>> WarningsArray;
		WarningsArray.Reserve(Context.Result.Warnings.Num());
		for (const FString& Warning : Context.Result.Warnings)
		{
			WarningsArray.Add(MakeShared<FJsonValueString>(Warning));
		}
		GraphEntry->SetArrayField(Args::Warnings, WarningsArray);

		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphEntry));
	}
	Payload->SetArrayField(Args::Graphs, GraphsArray);

	return FSMAssistOperationResult::MakeSuccess(Payload);
}

namespace LD::Assist::Private
{
	static FString PinTypeToShortString(const FEdGraphPinType& InType)
	{
		FString Out = InType.PinCategory.ToString();
		if (!InType.PinSubCategory.IsNone())
		{
			Out += TEXT("/");
			Out += InType.PinSubCategory.ToString();
		}
		if (InType.PinSubCategoryObject.IsValid())
		{
			Out += TEXT(":");
			Out += InType.PinSubCategoryObject->GetName();
		}
		switch (InType.ContainerType)
		{
		case EPinContainerType::Array: Out += TEXT("[]"); break;
		case EPinContainerType::Set:   Out += TEXT("{set}"); break;
		case EPinContainerType::Map:   Out += TEXT("{map}"); break;
		default: break;
		}
		return Out;
	}

	static TSharedRef<FJsonObject> PinTreeToJson(const UEdGraphPin* InPin, int32 InDepth)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("pin_name"), InPin->PinName.ToString());
		Obj->SetStringField(TEXT("pin_id"), InPin->PinId.ToString());
		Obj->SetStringField(TEXT("pin_type"), PinTypeToShortString(InPin->PinType));
		Obj->SetStringField(TEXT("default_value"), InPin->DefaultValue);
		Obj->SetStringField(TEXT("autogenerated_default_value"), InPin->AutogeneratedDefaultValue);
		Obj->SetBoolField(TEXT("matches_autogenerated"), InPin->DoesDefaultValueMatchAutogenerated());
		Obj->SetNumberField(TEXT("linked_to_count"), InPin->LinkedTo.Num());
		Obj->SetNumberField(TEXT("sub_pins_count"), InPin->SubPins.Num());
		Obj->SetNumberField(TEXT("depth"), InDepth);
		Obj->SetBoolField(TEXT("is_root_result"), InPin->ParentPin == nullptr);

		if (InPin->SubPins.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Children;
			Children.Reserve(InPin->SubPins.Num());
			for (UEdGraphPin* Sub : InPin->SubPins)
			{
				if (!Sub)
				{
					continue;
				}
				Children.Add(MakeShared<FJsonValueObject>(PinTreeToJson(Sub, InDepth + 1)));
			}
			Obj->SetArrayField(TEXT("sub_pins"), Children);
		}
		return Obj;
	}
}

FSMAssistOperationResult LD::Assist::GetPropertyPins(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	FString FilterVariable;
	InArgs->TryGetStringField(Args::VariableName, FilterVariable);
	const FName VariableFilter = FilterVariable.IsEmpty() ? NAME_None : FName(*FilterVariable);

	TArray<TSharedPtr<FJsonValue>> PropArr;
	for (const TPair<FGuid, TObjectPtr<USMGraphK2Node_PropertyNode_Base>>& Pair : Node->GetAllPropertyGraphNodes())
	{
		USMGraphK2Node_PropertyNode_Base* ResultNode = Pair.Value.Get();
		if (!ResultNode)
		{
			continue;
		}

		FName VariableName;
		if (const FSMGraphProperty_Base* Prop = ResultNode->GetPropertyNodeConst())
		{
			VariableName = Prop->VariableName;
		}
		if (!VariableFilter.IsNone() && VariableName != VariableFilter)
		{
			continue;
		}

		UEdGraphPin* Root = ResultNode->GetResultPin(EGPD_Input);
		if (!Root)
		{
			continue;
		}

		const TSharedRef<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(Args::VariableName, VariableName.ToString());
		PropObj->SetStringField(TEXT("guid"), Pair.Key.ToString());
		PropObj->SetObjectField(TEXT("result_pin"), LD::Assist::Private::PinTreeToJson(Root, 0));
		PropArr.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	Payload->SetNumberField(Args::Count, PropArr.Num());
	Payload->SetArrayField(Args::Properties, PropArr);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::ResetNodeProperty(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString NodeGuidStr;
	if (!InArgs->TryGetStringField(Args::NodeGuid, NodeGuidStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'node_guid'."));
	}

	FString PropertyName;
	if (!InArgs->TryGetStringField(Args::PropertyName, PropertyName) || PropertyName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'property_name'."));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Invalid 'node_guid' '%s'."), *NodeGuidStr));
	}

	FString LoadError;
	USMBlueprint* Blueprint = LD::Assist::Utils::LoadStateMachineBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	USMGraphNode_Base* Node = LD::Assist::Utils::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not find node with guid '%s'."), *NodeGuidStr));
	}

	USMNodeInstance* TargetTemplate = nullptr;
	int32 StackIndex = INDEX_NONE;
	const bool bStackIndexProvided = InArgs->TryGetNumberField(Args::StackIndex, StackIndex) && StackIndex >= 0;
	if (bStackIndexProvided)
	{
		TargetTemplate = Node->GetTemplateFromIndex(StackIndex);
		if (!TargetTemplate)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("No stack template at index %d on node '%s'."), StackIndex, *NodeGuidStr));
		}
	}

	int32 ArrayIndex = 0;
	InArgs->TryGetNumberField(Args::ArrayIndex, ArrayIndex);

	FString GraphGenError;
	ISMGraphGeneration* GraphGen = LD::Assist::Private::GetGraphGeneration(GraphGenError);
	if (!GraphGen)
	{
		return FSMAssistOperationResult::MakeError(GraphGenError);
	}

	ISMGraphGeneration::FResetNodePropertyArgs ResetArgs;
	ResetArgs.PropertyName = *PropertyName;
	ResetArgs.PropertyIndex = ArrayIndex;
	ResetArgs.NodeInstance = TargetTemplate;

	const bool bReset = GraphGen->ResetNodePropertyValue(Node, ResetArgs);
	if (!bReset)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Property '%s' could not be reset on node '%s' (not exposed as a graph property)."),
				*PropertyName, *NodeGuidStr));
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::NodeGuid, NodeGuidStr);
	Payload->SetStringField(Args::PropertyName, PropertyName);
	if (bStackIndexProvided)
	{
		Payload->SetNumberField(Args::StackIndex, StackIndex);
	}
	if (InArgs->HasField(Args::ArrayIndex))
	{
		Payload->SetNumberField(Args::ArrayIndex, ArrayIndex);
	}
	return FSMAssistOperationResult::MakeSuccess(Payload);
}
