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
#include "EdGraphSchema_K2.h"
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

		// Regression: a failed parse ran RemoveAt on a pair added via AddDefaultValue_Invalid_NeedsRehash
		// before Rehash(), unlinking against a hash never built for it; a populated (rehashed) map is the
		// state where that could corrupt.
		It("Leaves a populated map intact when a new value fails to parse", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			if (!TestTrue("Seed first key", Write(Blueprint->GetPathName(), TEXT("IntMap[Gold]"), TEXT("42")).bSuccess)
				|| !TestTrue("Seed second key", Write(Blueprint->GetPathName(), TEXT("IntMap[Silver]"), TEXT("7")).bSuccess))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(Blueprint->GetPathName(), TEXT("IntMap[Bad]"), TEXT("notanumber"));
			TestFalse("Bad value is rejected", WriteResult.bSuccess);

			TestEqual("First key intact", PayloadValue(Read(Blueprint->GetPathName(), TEXT("IntMap[Gold]"))), FString(TEXT("42")));
			TestEqual("Second key intact", PayloadValue(Read(Blueprint->GetPathName(), TEXT("IntMap[Silver]"))), FString(TEXT("7")));

			const FSMAssistOperationResult ReadBad = Read(Blueprint->GetPathName(), TEXT("IntMap[Bad]"));
			TestFalse("Failed write left no entry", ReadBad.bSuccess);
			TestTrue("Read reports the key absent", ReadBad.ErrorMessage.Contains(TEXT("not present")));
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

		// Regression: ImportText commits fields as it parses, so a value that fails mid-way (an
		// unterminated struct literal; unknown fields are skipped rather than failing) wrote OuterInt
		// before erroring, leaving the struct half-written and the package not dirtied.
		It("Leaves a struct unchanged when the value fails to parse mid-way", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			if (!TestTrue("Seed struct", Write(Blueprint->GetPathName(), TEXT("Outer.OuterInt"), TEXT("3")).bSuccess))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(
				Blueprint->GetPathName(), TEXT("Outer"), TEXT("(OuterInt=9,Nested=(InnerInt=2"));
			TestFalse("Partial struct value is rejected", WriteResult.bSuccess);

			TestEqual("Parsed-first field not committed",
				PayloadValue(Read(Blueprint->GetPathName(), TEXT("Outer.OuterInt"))), FString(TEXT("3")));
		});

		// Regression: the scratch-import fix initially started from a default-initialized buffer, so
		// a partial struct literal reset every member the literal did not name.
		It("Merges a partial struct literal onto the existing members", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			if (!TestTrue("Seed outer field", Write(Blueprint->GetPathName(), TEXT("Outer.OuterInt"), TEXT("3")).bSuccess)
				|| !TestTrue("Seed nested field", Write(Blueprint->GetPathName(), TEXT("Outer.Nested.InnerInt"), TEXT("7")).bSuccess))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(
				Blueprint->GetPathName(), TEXT("Outer"), TEXT("(OuterInt=9)"));
			TestTrue("Partial literal accepted", WriteResult.bSuccess);

			TestEqual("Named member updated",
				PayloadValue(Read(Blueprint->GetPathName(), TEXT("Outer.OuterInt"))), FString(TEXT("9")));
			TestEqual("Unlisted member preserved",
				PayloadValue(Read(Blueprint->GetPathName(), TEXT("Outer.Nested.InnerInt"))), FString(TEXT("7")));
		});

		// Regression: the FName length guard only checked FName-typed leaves, so an over-long token
		// nested inside a struct literal still reached FName construction and crashed the editor.
		It("Rejects an over-long FName token nested in a struct literal", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FString Value = FString::Printf(
				TEXT("(OuterInt=1,Nested=(InnerName=%s))"), *FString::ChrN(1100, TEXT('n')));
			const FSMAssistOperationResult WriteResult = Write(Blueprint->GetPathName(), TEXT("Outer"), Value);
			TestFalse("Over-long nested token rejected", WriteResult.bSuccess);
			TestTrue("Error reports the token bound", WriteResult.ErrorMessage.Contains(TEXT("token")));

			TestEqual("Struct unchanged",
				PayloadValue(Read(Blueprint->GetPathName(), TEXT("Outer.OuterInt"))), FString(TEXT("0")));
		});

		It("Rejects an array index that overflows int32 on read and write", [this]()
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

			const FSMAssistOperationResult ReadResult = Read(
				Blueprint->GetPathName(), TEXT("IntArray[99999999999999999999]"));
			TestFalse("Overflowing read index rejected", ReadResult.bSuccess);

			const FSMAssistOperationResult WriteResult = Write(
				Blueprint->GetPathName(), TEXT("IntArray[99999999999999999999]"), TEXT("5"));
			TestFalse("Overflowing write index rejected", WriteResult.bSuccess);

			TestEqual("Array unchanged", PayloadValue(Read(Blueprint->GetPathName(), TEXT("IntArray[0]"))), FString(TEXT("10")));
		});

		It("Rejects a non-integer key on an integer-keyed map", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(Blueprint->GetPathName(), TEXT("IntKeyMap[abc]"), TEXT("1"));
			TestFalse("Non-integer key rejected", WriteResult.bSuccess);
			TestTrue("Error mentions the map key", WriteResult.ErrorMessage.Contains(TEXT("map key")));

			const FSMAssistOperationResult ReadResult = Read(Blueprint->GetPathName(), TEXT("IntKeyMap[0]"));
			TestFalse("No phantom key 0 written", ReadResult.bSuccess);
		});
	});

	// Regression: FName construction fatally asserts at NAME_SIZE (1024) characters, so any over-long
	// input that reached an FName sink (soft object paths, FName property imports) crashed the editor.
	Describe("input length bounds", [this]()
	{
		It("Rejects an over-long 'object' cleanly", [this]()
		{
			const FSMAssistOperationResult Result = Read(FString::ChrN(1100, TEXT('a')), TEXT("ScalarInt"));
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error reports the length bound", Result.ErrorMessage.Contains(TEXT("characters")));
		});

		It("Rejects an over-long property-path segment cleanly", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult Result = Read(Blueprint->GetPathName(), FString::ChrN(1100, TEXT('a')));
			TestFalse("Result is failure", Result.bSuccess);
		});

		It("Rejects an over-long value for an FName property and leaves it unchanged", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult WriteResult = Write(
				Blueprint->GetPathName(), TEXT("NameValue"), FString::ChrN(1100, TEXT('a')));
			TestFalse("Result is failure", WriteResult.bSuccess);
			TestTrue("Error reports the length bound", WriteResult.ErrorMessage.Contains(TEXT("characters")));

			const FSMAssistOperationResult ReadResult = Read(Blueprint->GetPathName(), TEXT("NameValue"));
			TestTrue("Read succeeds", ReadResult.bSuccess);
			TestEqual("FName property unchanged", PayloadValue(ReadResult), FString(TEXT("None")));
		});

		It("Rejects an over-long key for an FName-keyed map cleanly", [this]()
		{
			UBlueprint* Blueprint = MakeStateBlueprint();
			if (!TestNotNull("State blueprint created", Blueprint))
			{
				return;
			}

			const FString LongKey = FString::ChrN(1100, TEXT('k'));
			const FSMAssistOperationResult WriteResult = Write(
				Blueprint->GetPathName(), FString::Printf(TEXT("NameKeyMap[%s]"), *LongKey), TEXT("1"));
			TestFalse("Result is failure", WriteResult.bSuccess);
			TestTrue("Error reports the length bound", WriteResult.ErrorMessage.Contains(TEXT("characters")));
		});

		It("Rejects an over-long dispatcher name cleanly", [this]()
		{
			UBlueprint* Blueprint = MakeActorBlueprint();
			if (!TestNotNull("Actor blueprint created", Blueprint))
			{
				return;
			}

			const FSMAssistOperationResult Result = AddDispatcher(
				Blueprint->GetPathName(), FString::ChrN(1100, TEXT('d')), {});
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error reports the length bound", Result.ErrorMessage.Contains(TEXT("characters")));
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

		// Regression: only variable names were checked, so a name matching an existing function graph
		// made CreateNewGraph silently rename that graph aside, breaking its CallFunction sites while
		// the op reported success.
		It("Rejects a name colliding with an existing function graph and leaves the graph intact", [this]()
		{
			UBlueprint* Blueprint = MakeActorBlueprint();
			if (!TestNotNull("Actor blueprint created", Blueprint))
			{
				return;
			}

			UEdGraph* FuncGraph = FBlueprintEditorUtils::CreateNewGraph(
				Blueprint, FName(TEXT("Foo")), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			if (!TestNotNull("Function graph created", FuncGraph))
			{
				return;
			}
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, FuncGraph, /*bIsUserCreated=*/true, nullptr);

			const FSMAssistOperationResult Result = AddDispatcher(Blueprint->GetPathName(), TEXT("Foo"), {});
			TestFalse("Result is failure", Result.bSuccess);
			TestTrue("Error reports the name collision", Result.ErrorMessage.Contains(TEXT("already in use")));

			bool bFunctionGraphIntact = false;
			for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				if (Graph && Graph->GetFName() == FName(TEXT("Foo")))
				{
					bFunctionGraphIntact = true;
					break;
				}
			}
			TestTrue("Function graph 'Foo' still present under its own name", bFunctionGraphIntact);
			TestEqual("No dangling delegate variable",
				FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(TEXT("Foo"))), INDEX_NONE);
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
