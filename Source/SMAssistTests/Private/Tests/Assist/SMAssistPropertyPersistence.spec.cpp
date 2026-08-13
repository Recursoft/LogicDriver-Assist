// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistTestClasses.h"

#include "Helpers/SMTestHelpers.h"

#include "Blueprints/SMBlueprint.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_Base.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

// A value written through ld.set_node_property has to survive the refresh paths that re-seed a pin from
// the node class default. Asserting straight after the write proves nothing: the write itself always
// lands, and the loss happens later, when something reconstructs the graph.
BEGIN_DEFINE_SPEC(FAssistPropertyPersistenceSpec, "LogicDriver.Assist.PropertyPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	FSMAssistOperationResult Run(const TCHAR* InOp, const TSharedRef<FJsonObject>& InArgs)
	{
		return GetSubsystem()->ExecuteOperation(FName(InOp), InArgs);
	}

	FString CreateBlueprint()
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), FGuid::NewGuid().ToString());
		Args->SetStringField(TEXT("path"), FAssetHandler::DefaultGamePath());

		const FSMAssistOperationResult Result = Run(TEXT("ld.create_blueprint"), Args);
		FString AssetPath;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("asset_path"), AssetPath);
		}
		return AssetPath;
	}

	FString AddTunedState(const FString& InAssetPath)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("state_name"), TEXT("TunedState"));
		Args->SetStringField(TEXT("state_class"), USMAssistTunedState::StaticClass()->GetPathName());

		const FSMAssistOperationResult Result = Run(TEXT("ld.add_state"), Args);
		FString StateGuid;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
		}
		return StateGuid;
	}

	FString AddReferenceTo(const FString& InHostPath, const FString& InReferencedPath)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InHostPath);
		Args->SetStringField(TEXT("reference_asset_path"), InReferencedPath);
		Args->SetStringField(TEXT("state_name"), TEXT("RefNode"));

		const FSMAssistOperationResult Result = Run(TEXT("ld.add_reference"), Args);
		FString StateGuid;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
		}
		return StateGuid;
	}

	FSMAssistOperationResult SetProperty(const FString& InAssetPath, const FString& InNodeGuid,
		const TCHAR* InPropertyName, const TCHAR* InValue)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("node_guid"), InNodeGuid);
		Args->SetStringField(TEXT("property_name"), InPropertyName);
		Args->SetStringField(TEXT("value"), InValue);
		return Run(TEXT("ld.set_node_property"), Args);
	}

	FSMAssistOperationResult SetPropertyAtPath(const FString& InAssetPath, const FString& InNodeGuid,
		const TCHAR* InPropertyName, const TCHAR* InPropertyPath, const TCHAR* InValue)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("node_guid"), InNodeGuid);
		Args->SetStringField(TEXT("property_name"), InPropertyName);
		Args->SetStringField(TEXT("property_path"), InPropertyPath);
		Args->SetStringField(TEXT("value"), InValue);
		return Run(TEXT("ld.set_node_property"), Args);
	}

	FSMAssistOperationResult Compile(const FString& InAssetPath)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		return Run(TEXT("ld.compile"), Args);
	}

	USMGraphNode_Base* NodeByGuid(const FString& InAssetPath, const FString& InGuidStr)
	{
		USMBlueprint* Blueprint = Cast<USMBlueprint>(FSoftObjectPath(InAssetPath).TryLoad());
		if (!Blueprint)
		{
			return nullptr;
		}
		FGuid Guid;
		if (!FGuid::Parse(InGuidStr, Guid))
		{
			return nullptr;
		}
		TArray<UEdGraphNode*> AllNodes;
		FBlueprintEditorUtils::GetAllNodesOfClassEx<USMGraphNode_Base>(Blueprint, AllNodes);
		for (UEdGraphNode* Candidate : AllNodes)
		{
			if (Candidate && Candidate->NodeGuid == Guid)
			{
				return Cast<USMGraphNode_Base>(Candidate);
			}
		}
		return nullptr;
	}

	// Re-resolved after every compile: reinstancing can hand the node a fresh template object.
	USMAssistTunedState* TemplateByGuid(const FString& InAssetPath, const FString& InGuidStr)
	{
		USMGraphNode_Base* Node = NodeByGuid(InAssetPath, InGuidStr);
		return Node ? Cast<USMAssistTunedState>(Node->GetNodeTemplate()) : nullptr;
	}

	// A split struct parent holds its values on the sub-pins, so the leaf is named by suffix. Matches
	// LD::Editor::SubPinNaming, which lives in SMSystemEditor/Private and so cannot be included here.
	const UEdGraphPin* FindPin(const FString& InAssetPath, const FString& InGuidStr,
		const TCHAR* InPropertyName, const TCHAR* InSubPinSuffix = nullptr)
	{
		USMGraphNode_Base* Node = NodeByGuid(InAssetPath, InGuidStr);
		if (!Node)
		{
			return nullptr;
		}
		const USMGraphK2Node_PropertyNode_Base* PropertyNode =
			Node->GetGraphPropertyNode(FName(InPropertyName), Node->GetNodeTemplate());
		if (!PropertyNode)
		{
			return nullptr;
		}
		const UEdGraphPin* ResultPin = PropertyNode->GetResultPin();
		if (!ResultPin || !InSubPinSuffix)
		{
			return ResultPin;
		}
		for (const UEdGraphPin* SubPin : ResultPin->SubPins)
		{
			if (SubPin && SubPin->PinName.ToString().EndsWith(InSubPinSuffix, ESearchCase::CaseSensitive))
			{
				return SubPin;
			}
		}
		return nullptr;
	}

	void TestPinValue(const FString& InAssetPath, const FString& InGuidStr, const TCHAR* InPropertyName,
		const TCHAR* InSubPinSuffix, float InExpected, const TCHAR* InWhen)
	{
		const UEdGraphPin* Pin = FindPin(InAssetPath, InGuidStr, InPropertyName, InSubPinSuffix);
		const FString Label = InSubPinSuffix
			? FString::Printf(TEXT("%s%s pin %s"), InPropertyName, InSubPinSuffix, InWhen)
			: FString::Printf(TEXT("%s pin %s"), InPropertyName, InWhen);
		if (!TestNotNull(*(Label + TEXT(" resolved")), Pin))
		{
			return;
		}
		TestEqual(*Label, FCString::Atof(*Pin->DefaultValue), InExpected);
	}

	void TestTunedValues(const FString& InAssetPath, const FString& InGuidStr, const TCHAR* InWhen)
	{
		const USMAssistTunedState* Template = TemplateByGuid(InAssetPath, InGuidStr);
		if (!TestNotNull(FString::Printf(TEXT("Template resolved %s"), InWhen), Template))
		{
			return;
		}
		TestEqual(FString::Printf(TEXT("Template ScalarValue %s"), InWhen), Template->ScalarValue, 7.5f);
		TestEqual(FString::Printf(TEXT("Template TunedStruct.Close %s"), InWhen), Template->TunedStruct.Close, 0.5f);
		TestEqual(FString::Printf(TEXT("Template TunedStruct.Far %s"), InWhen), Template->TunedStruct.Far, 1.0f);

		TestPinValue(InAssetPath, InGuidStr, TEXT("ScalarValue"), nullptr, 7.5f, InWhen);
		TestPinValue(InAssetPath, InGuidStr, TEXT("TunedStruct"), TEXT("_Close"), 0.5f, InWhen);
		TestPinValue(InAssetPath, InGuidStr, TEXT("TunedStruct"), TEXT("_Far"), 1.0f, InWhen);
	}

	FSMAssistOperationResult SplitPin(const FString& InAssetPath, const FString& InNodeGuid, const TCHAR* InVariableName)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("node_guid"), InNodeGuid);
		Args->SetStringField(TEXT("variable_name"), InVariableName);
		return Run(TEXT("ld.split_pin"), Args);
	}

	// The struct is split first because that is how a struct exposed on a node face is authored.
	FString MakeTunedState(const FString& InAssetPath)
	{
		const FString StateGuid = AddTunedState(InAssetPath);
		if (StateGuid.IsEmpty())
		{
			AddError(TEXT("Could not add the tuned state."));
			return FString();
		}

		const FSMAssistOperationResult Split = SplitPin(InAssetPath, StateGuid, TEXT("TunedStruct"));
		if (!Split.bSuccess)
		{
			AddError(FString::Printf(TEXT("Split failed: %s"), *Split.ErrorMessage));
			return FString();
		}

		const FSMAssistOperationResult SetScalar = SetProperty(InAssetPath, StateGuid, TEXT("ScalarValue"), TEXT("7.5"));
		if (!SetScalar.bSuccess)
		{
			AddError(FString::Printf(TEXT("Scalar set failed: %s"), *SetScalar.ErrorMessage));
			return FString();
		}
		const FSMAssistOperationResult SetStruct = SetProperty(InAssetPath, StateGuid, TEXT("TunedStruct"),
			TEXT("(Close=0.5,Far=1.0)"));
		if (!SetStruct.bSuccess)
		{
			AddError(FString::Printf(TEXT("Struct set failed: %s"), *SetStruct.ErrorMessage));
			return FString();
		}
		return StateGuid;
	}

END_DEFINE_SPEC(FAssistPropertyPersistenceSpec)

void FAssistPropertyPersistenceSpec::Define()
{
	Describe("ld.set_node_property", [this]()
	{
		It("Precondition: the write reaches both the template and the pin", [this]()
		{
			const FString AssetPath = CreateBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = MakeTunedState(AssetPath);
			if (StateGuid.IsEmpty())
			{
				return;
			}
			TestTunedValues(AssetPath, StateGuid, TEXT("after the write"));
		});

		It("Keeps the written values through a compile of the owning blueprint", [this]()
		{
			const FString AssetPath = CreateBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = MakeTunedState(AssetPath);
			if (StateGuid.IsEmpty())
			{
				return;
			}

			TestTrue("Compile succeeds", Compile(AssetPath).bSuccess);
			TestTunedValues(AssetPath, StateGuid, TEXT("after the owning compile"));
		});

		// Measured to revert the written sub-pins to the class default before the fix, the same as a
		// direct compile. Which step of the reference setup triggers the reseed is not established, so
		// this covers the arrangement rather than a named mechanism.
		It("Keeps the written values when a referencing machine is compiled", [this]()
		{
			const FString PhasePath = CreateBlueprint();
			const FString RootPath = CreateBlueprint();
			if (!TestFalse("Phase blueprint created", PhasePath.IsEmpty())
				|| !TestFalse("Root blueprint created", RootPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = MakeTunedState(PhasePath);
			if (StateGuid.IsEmpty())
			{
				return;
			}

			if (!TestFalse("Reference node added", AddReferenceTo(RootPath, PhasePath).IsEmpty()))
			{
				return;
			}
			TestTrue("Root compile succeeds", Compile(RootPath).bSuccess);
			TestTunedValues(PhasePath, StateGuid, TEXT("after the referencing compile"));
		});

		// The other write path: property_path addresses one leaf instead of replacing the whole struct.
		It("Keeps a property_path leaf write through a compile", [this]()
		{
			const FString AssetPath = CreateBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddTunedState(AssetPath);
			if (!TestFalse("State created", StateGuid.IsEmpty()))
			{
				return;
			}
			if (!TestTrue("Split succeeds", SplitPin(AssetPath, StateGuid, TEXT("TunedStruct")).bSuccess))
			{
				return;
			}

			const FSMAssistOperationResult Set = SetPropertyAtPath(AssetPath, StateGuid, TEXT("TunedStruct"),
				TEXT("Close"), TEXT("0.5"));
			if (!TestTrue("Leaf set succeeds", Set.bSuccess))
			{
				AddError(FString::Printf(TEXT("Leaf set error: %s"), *Set.ErrorMessage));
				return;
			}

			TestTrue("Compile succeeds", Compile(AssetPath).bSuccess);

			const USMAssistTunedState* Template = TemplateByGuid(AssetPath, StateGuid);
			if (!TestNotNull("Template resolved after compile", Template))
			{
				return;
			}
			TestEqual("Template Close survives the compile", Template->TunedStruct.Close, 0.5f);
			TestPinValue(AssetPath, StateGuid, TEXT("TunedStruct"), TEXT("_Close"), 0.5f,
				TEXT("after the owning compile"));
		});

		// The authored mark has to be earned, not stamped on every write. A node written to its own
		// class default has authored nothing, and marking it would freeze it against later changes to
		// that default.
		It("Leaves the pin unmarked when the write matches the class default", [this]()
		{
			const FString AssetPath = CreateBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = AddTunedState(AssetPath);
			if (!TestFalse("State created", StateGuid.IsEmpty()))
			{
				return;
			}
			if (!TestTrue("Split succeeds", SplitPin(AssetPath, StateGuid, TEXT("TunedStruct")).bSuccess))
			{
				return;
			}

			TestTrue("Scalar set succeeds",
				SetProperty(AssetPath, StateGuid, TEXT("ScalarValue"), TEXT("1.0")).bSuccess);
			TestTrue("Struct set succeeds",
				SetProperty(AssetPath, StateGuid, TEXT("TunedStruct"), TEXT("(Close=3.0,Far=7.0)")).bSuccess);

			USMGraphNode_Base* Node = NodeByGuid(AssetPath, StateGuid);
			if (!TestNotNull("Node resolved", Node))
			{
				return;
			}
			USMNodeInstance* Template = Node->GetNodeTemplate();
			if (!TestNotNull("Template resolved", Template))
			{
				return;
			}

			const USMGraphK2Node_PropertyNode_Base* ScalarNode =
				Node->GetGraphPropertyNode(FName(TEXT("ScalarValue")), Template);
			if (TestNotNull("ScalarValue is exposed as a property node", ScalarNode))
			{
				TestFalse("ScalarValue pin stays unmarked", ScalarNode->HasDefaultValueExplicitlyBeenChanged());
			}

			const USMGraphK2Node_PropertyNode_Base* StructNode =
				Node->GetGraphPropertyNode(FName(TEXT("TunedStruct")), Template);
			if (TestNotNull("TunedStruct is exposed as a property node", StructNode))
			{
				TestFalse("TunedStruct pin stays unmarked", StructNode->HasDefaultValueExplicitlyBeenChanged());
			}
		});

		// The direct oracle for the cause. A keystroke into the pin sets bDefaultValueChanged, which is
		// the flag every re-seed path checks before overwriting. Without it the pin reads as untouched
		// no matter what value it holds, which is what licenses the re-seeds above.
		It("Marks the written pin as explicitly authored", [this]()
		{
			const FString AssetPath = CreateBlueprint();
			if (!TestFalse("Blueprint created", AssetPath.IsEmpty()))
			{
				return;
			}
			const FString StateGuid = MakeTunedState(AssetPath);
			if (StateGuid.IsEmpty())
			{
				return;
			}

			USMGraphNode_Base* Node = NodeByGuid(AssetPath, StateGuid);
			if (!TestNotNull("Node resolved", Node))
			{
				return;
			}
			USMNodeInstance* Template = Node->GetNodeTemplate();
			if (!TestNotNull("Template resolved", Template))
			{
				return;
			}

			// An unsplit scalar carries the node-wide mark, matching a result-pin edit.
			const USMGraphK2Node_PropertyNode_Base* ScalarNode =
				Node->GetGraphPropertyNode(FName(TEXT("ScalarValue")), Template);
			if (TestNotNull("ScalarValue is exposed as a property node", ScalarNode))
			{
				TestTrue("ScalarValue pin is marked authored", ScalarNode->HasDefaultValueExplicitlyBeenChanged());
			}

			// A split struct carries it per leaf instead, matching a sub-pin edit, so that untouched
			// leaves of other nodes keep tracking the class default.
			const USMGraphK2Node_PropertyNode_Base* StructNode =
				Node->GetGraphPropertyNode(FName(TEXT("TunedStruct")), Template);
			if (TestNotNull("TunedStruct is exposed as a property node", StructNode))
			{
				const UEdGraphPin* ResultPin = StructNode->GetResultPin();
				if (TestNotNull("TunedStruct result pin", ResultPin))
				{
					const FString Prefix = ResultPin->PinName.ToString();
					TestTrue("TunedStruct Close leaf is marked authored",
						StructNode->IsSubPinAuthored(*FString::Printf(TEXT("%s_Close"), *Prefix)));
					TestTrue("TunedStruct Far leaf is marked authored",
						StructNode->IsSubPinAuthored(*FString::Printf(TEXT("%s_Far"), *Prefix)));
				}
			}
		});
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
