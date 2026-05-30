// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistGenericOpsTestClasses.h"

#include "Helpers/SMTestHelpers.h"

#include "Blueprints/SMBlueprint.h"
#include "Blueprints/SMBlueprintGeneratedClass.h"
#include "SMStateInstance.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FAssistGenericOpsSpec, "LogicDriver.Assist.GenericOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	UBlueprint* MakeBlueprint(UClass* InParentClass, TSubclassOf<UBlueprint> InBlueprintClass, TSubclassOf<UBlueprintGeneratedClass> InGeneratedClass)
	{
		const FString PackageName = FAssetHandler::DefaultGamePath()
			+ FString::Printf(TEXT("BP_GenOps_%s"), *FGuid::NewGuid().ToString());
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}

		const FName BPName(*FPackageName::GetShortName(PackageName));
		return FKismetEditorUtilities::CreateBlueprint(
			InParentClass, Package, BPName, BPTYPE_Normal, InBlueprintClass, InGeneratedClass);
	}

	UBlueprint* MakeStateBlueprint()
	{
		return MakeBlueprint(USMAssistGenericOpsState::StaticClass(),
			USMNodeBlueprint::StaticClass(), USMNodeBlueprintGeneratedClass::StaticClass());
	}

	UBlueprint* MakeActorBlueprint()
	{
		return MakeBlueprint(AActor::StaticClass(),
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	}

	FSMAssistOperationResult Read(const FString& InObject, const FString& InPath)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("object"), InObject);
		Args->SetStringField(TEXT("property_path"), InPath);
		return GetSubsystem()->ExecuteOperation(FName(TEXT("ld_ue.read_property")), Args);
	}

	FSMAssistOperationResult Write(const FString& InObject, const FString& InPath, const FString& InValue)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("object"), InObject);
		Args->SetStringField(TEXT("property_path"), InPath);
		Args->SetStringField(TEXT("value"), InValue);
		return GetSubsystem()->ExecuteOperation(FName(TEXT("ld_ue.write_property")), Args);
	}

	static FString PayloadValue(const FSMAssistOperationResult& InResult)
	{
		FString Value;
		if (InResult.Payload.IsValid())
		{
			InResult.Payload->TryGetStringField(TEXT("value"), Value);
		}
		return Value;
	}

	FSMAssistOperationResult AddDispatcher(const FString& InAssetPath, const FString& InName, const TArray<TSharedPtr<FJsonValue>>& InParams)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("name"), InName);
		if (InParams.Num() > 0)
		{
			Args->SetArrayField(TEXT("params"), InParams);
		}
		return GetSubsystem()->ExecuteOperation(FName(TEXT("ld_ue.add_dispatcher")), Args);
	}

	static TSharedPtr<FJsonValue> MakeParam(const FString& InName, const FString& InType)
	{
		const TSharedRef<FJsonObject> Param = MakeShared<FJsonObject>();
		Param->SetStringField(TEXT("name"), InName);
		Param->SetStringField(TEXT("type"), InType);
		return MakeShared<FJsonValueObject>(Param);
	}

	static bool HasSignatureGraph(const UBlueprint* InBlueprint, FName InName)
	{
		for (const UEdGraph* Graph : InBlueprint->DelegateSignatureGraphs)
		{
			if (Graph && Graph->GetFName() == InName)
			{
				return true;
			}
		}
		return false;
	}

END_DEFINE_SPEC(FAssistGenericOpsSpec)

void FAssistGenericOpsSpec::Define()
{
	Describe("ld_ue.read_property", [this]()
	{
		It("Fails when 'object' is missing", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("property_path"), TEXT("ScalarInt"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("ld_ue.read_property")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'object'", Result.ErrorMessage.Contains(TEXT("object")));
		});

		It("Fails when 'property_path' is missing", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("object"), TEXT("/Game/Whatever.Whatever"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("ld_ue.read_property")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'property_path'", Result.ErrorMessage.Contains(TEXT("property_path")));
		});

		It("Fails when the object cannot be resolved", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const FSMAssistOperationResult Result = Read(TEXT("/Game/DoesNotExist.DoesNotExist"), TEXT("ScalarInt"));
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions the object", Result.ErrorMessage.Contains(TEXT("DoesNotExist")));
		});

		It("Reads a scalar property from the asset CDO", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult Result = Read(Blueprint->GetPathName(), TEXT("ScalarInt"));
			TestTrue("Result is success", Result.bSuccess);
			TestEqual("Default scalar value", PayloadValue(Result), FString(TEXT("0")));
		});

		It("Reads an array element after seeding the whole array", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult Seed = Write(Blueprint->GetPathName(), TEXT("IntArray"), TEXT("(10,20,30)"));
			if (!TestTrue("Whole-array write succeeds", Seed.bSuccess))
			{
				return;
			}

			const FSMAssistOperationResult Result = Read(Blueprint->GetPathName(), TEXT("IntArray[1]"));
			TestTrue("Result is success", Result.bSuccess);
			TestEqual("Second element value", PayloadValue(Result), FString(TEXT("20")));
		});

		It("Fails on an out-of-range array index", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult Result = Read(Blueprint->GetPathName(), TEXT("IntArray[0]"));
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions out of range", Result.ErrorMessage.Contains(TEXT("out of range")));
		});

		It("Fails on an absent map key", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult Result = Read(Blueprint->GetPathName(), TEXT("IntMap[Missing]"));
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions not present", Result.ErrorMessage.Contains(TEXT("not present")));
		});

		It("Reads a nested struct field", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult Result = Read(Blueprint->GetPathName(), TEXT("Outer.Nested.InnerInt"));
			TestTrue("Result is success", Result.bSuccess);
			TestEqual("Default nested value", PayloadValue(Result), FString(TEXT("0")));
		});

		It("Fails when subscripting a non-container", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult Result = Read(Blueprint->GetPathName(), TEXT("ScalarInt[0]"));
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions not a container", Result.ErrorMessage.Contains(TEXT("not a container")));
		});
	});

	Describe("ld_ue.write_property", [this]()
	{
		It("Fails when 'value' is missing", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("object"), TEXT("/Game/Whatever.Whatever"));
			Args->SetStringField(TEXT("property_path"), TEXT("ScalarInt"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("ld_ue.write_property")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'value'", Result.ErrorMessage.Contains(TEXT("value")));
		});

		It("Writes a scalar and reads it back", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(Blueprint->GetPathName(), TEXT("ScalarInt"), TEXT("7"));
			TestTrue("Write succeeds", WriteResult.bSuccess);

			const FSMAssistOperationResult ReadResult = Read(Blueprint->GetPathName(), TEXT("ScalarInt"));
			TestTrue("Read succeeds", ReadResult.bSuccess);
			TestEqual("Round-tripped scalar", PayloadValue(ReadResult), FString(TEXT("7")));
		});

		It("Writes an array element in range", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			if (!TestTrue("Seed array", Write(Blueprint->GetPathName(), TEXT("IntArray"), TEXT("(10,20,30)")).bSuccess))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(Blueprint->GetPathName(), TEXT("IntArray[2]"), TEXT("99"));
			TestTrue("Element write succeeds", WriteResult.bSuccess);

			const FSMAssistOperationResult ReadResult = Read(Blueprint->GetPathName(), TEXT("IntArray[2]"));
			TestEqual("Round-tripped element", PayloadValue(ReadResult), FString(TEXT("99")));
		});

		It("Refuses to grow an array", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult Result = Write(Blueprint->GetPathName(), TEXT("IntArray[0]"), TEXT("5"));
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions out of range", Result.ErrorMessage.Contains(TEXT("out of range")));
		});

		It("Adds a new map key and reads it back", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(Blueprint->GetPathName(), TEXT("IntMap[Gold]"), TEXT("42"));
			TestTrue("Map add succeeds", WriteResult.bSuccess);

			const FSMAssistOperationResult ReadResult = Read(Blueprint->GetPathName(), TEXT("IntMap[Gold]"));
			TestTrue("Read succeeds", ReadResult.bSuccess);
			TestEqual("Round-tripped map value", PayloadValue(ReadResult), FString(TEXT("42")));
		});

		It("Updates an existing map key", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			if (!TestTrue("Seed map key", Write(Blueprint->GetPathName(), TEXT("IntMap[Gold]"), TEXT("42")).bSuccess))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(Blueprint->GetPathName(), TEXT("IntMap[Gold]"), TEXT("7"));
			TestTrue("Map update succeeds", WriteResult.bSuccess);

			const FSMAssistOperationResult ReadResult = Read(Blueprint->GetPathName(), TEXT("IntMap[Gold]"));
			TestEqual("Updated map value", PayloadValue(ReadResult), FString(TEXT("7")));
		});

		It("Leaves no entry when a new map value fails to parse", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(Blueprint->GetPathName(), TEXT("IntMap[Bad]"), TEXT("notanumber"));
			TestFalse("Bad value is rejected", WriteResult.bSuccess);
			TestTrue("Error mentions parse failure", WriteResult.ErrorMessage.Contains(TEXT("Could not parse")));

			const FSMAssistOperationResult ReadResult = Read(Blueprint->GetPathName(), TEXT("IntMap[Bad]"));
			TestFalse("Failed write left no entry", ReadResult.bSuccess);
			TestTrue("Read reports the key absent", ReadResult.ErrorMessage.Contains(TEXT("not present")));
		});

		It("Writes a nested struct field and reads it back", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(Blueprint->GetPathName(), TEXT("Outer.Nested.InnerInt"), TEXT("5"));
			TestTrue("Nested write succeeds", WriteResult.bSuccess);

			const FSMAssistOperationResult ReadResult = Read(Blueprint->GetPathName(), TEXT("Outer.Nested.InnerInt"));
			TestEqual("Round-tripped nested value", PayloadValue(ReadResult), FString(TEXT("5")));
		});
	});

	Describe("ld_ue.add_dispatcher", [this]()
	{
		It("Fails when 'name' is missing", [this]()
		{
			USMAssistSubsystem* Subsystem = GetSubsystem();
			if (!TestNotNull("Assist subsystem available", Subsystem))
			{
				return;
			}

			const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), TEXT("/Game/Whatever.Whatever"));

			const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(
				FName(TEXT("ld_ue.add_dispatcher")), Args);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions 'name'", Result.ErrorMessage.Contains(TEXT("name")));
		});

		It("Rejects an unrecognized param type and leaves nothing behind", [this]()
		{
			UBlueprint* Blueprint = MakeActorBlueprint();
			if (!TestNotNull("Actor blueprint created", Blueprint))
			{
				return;
			}

			TArray<TSharedPtr<FJsonValue>> Params;
			Params.Add(MakeParam(TEXT("X"), TEXT("NotARealType")));

			const FSMAssistOperationResult Result = AddDispatcher(Blueprint->GetPathName(), TEXT("BadDisp"), Params);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions unrecognized type", Result.ErrorMessage.Contains(TEXT("Unrecognized param type")));

			TestEqual("No dangling delegate variable",
				FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(TEXT("BadDisp"))), INDEX_NONE);
			TestFalse("No dangling signature graph", HasSignatureGraph(Blueprint, FName(TEXT("BadDisp"))));
		});

		It("Rejects duplicate param names and leaves nothing behind", [this]()
		{
			UBlueprint* Blueprint = MakeActorBlueprint();
			if (!TestNotNull("Actor blueprint created", Blueprint))
			{
				return;
			}

			TArray<TSharedPtr<FJsonValue>> Params;
			Params.Add(MakeParam(TEXT("A"), TEXT("int")));
			Params.Add(MakeParam(TEXT("A"), TEXT("bool")));

			const FSMAssistOperationResult Result = AddDispatcher(Blueprint->GetPathName(), TEXT("DupDisp"), Params);
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error mentions duplicate", Result.ErrorMessage.Contains(TEXT("Duplicate param name")));

			TestEqual("No dangling delegate variable",
				FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(TEXT("DupDisp"))), INDEX_NONE);
			TestFalse("No dangling signature graph", HasSignatureGraph(Blueprint, FName(TEXT("DupDisp"))));
		});

		It("Creates a dispatcher with params that survives compile", [this]()
		{
			UBlueprint* Blueprint = MakeActorBlueprint();
			if (!TestNotNull("Actor blueprint created", Blueprint))
			{
				return;
			}

			TArray<TSharedPtr<FJsonValue>> Params;
			Params.Add(MakeParam(TEXT("ChoiceIndex"), TEXT("int")));
			Params.Add(MakeParam(TEXT("Speaker"), TEXT("Name")));

			const FSMAssistOperationResult Result = AddDispatcher(Blueprint->GetPathName(), TEXT("OnChoiceMade"), Params);
			if (!TestTrue("Result is success", Result.bSuccess) || !TestTrue("Payload populated", Result.Payload.IsValid()))
			{
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Applied = nullptr;
			if (TestTrue("Payload has params_applied", Result.Payload->TryGetArrayField(TEXT("params_applied"), Applied)))
			{
				TestEqual("Both params applied", Applied->Num(), 2);
			}

			FKismetEditorUtilities::CompileBlueprint(Blueprint);

			if (TestNotNull("Generated class present", Blueprint->GeneratedClass.Get()))
			{
				const FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
					Blueprint->GeneratedClass, FName(TEXT("OnChoiceMade")));
				TestNotNull("Dispatcher property survives compile", DelegateProp);
			}
		});
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
