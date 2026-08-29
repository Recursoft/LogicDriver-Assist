// Copyright Recursoft LLC. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/Platform.h"

#if PLATFORM_DESKTOP

#if WITH_TOOLSET_REGISTRY

#include "LogicDriverToolset.h"
#include "SMAssistToolsetMarshal.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistSubsystem.h"

#include "Blueprints/SMBlueprint.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"

BEGIN_DEFINE_SPEC(FSMAssistToolsetSpec, "LogicDriver.Assist.Toolset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	/**
	 * Translates a canonical SMAssist op name (e.g. "ld.add_state" or "ld_ue.read_property") into
	 * the expected static UFUNCTION name on ULogicDriverToolset ("AddState", "ReadProperty").
	 * Drops the namespace (`ld.` or `ld_ue.`), PascalCases the remaining snake_case parts.
	 * Returns NAME_None if the op name carries neither known namespace (unexpected; caller treats
	 * as a registration drift bug).
	 */
	static FName UFunctionNameForOp(FName InOpName)
	{
		FString OpString = InOpName.ToString();

		bool bStripped = false;
		for (const FString& NamespacePrefix : { FString(TEXT("ld.")), FString(TEXT("ld_ue.")) })
		{
			if (OpString.StartsWith(NamespacePrefix))
			{
				OpString.RemoveAt(0, NamespacePrefix.Len());
				bStripped = true;
				break;
			}
		}
		if (!bStripped)
		{
			return NAME_None;
		}

		TArray<FString> Parts;
		OpString.ParseIntoArray(Parts, TEXT("_"), true);

		FString FunctionString;
		for (FString& Part : Parts)
		{
			if (Part.IsEmpty())
			{
				continue;
			}
			Part[0] = FChar::ToUpper(Part[0]);
			FunctionString += Part;
		}
		return FName(*FunctionString);
	}

END_DEFINE_SPEC(FSMAssistToolsetSpec)

void FSMAssistToolsetSpec::Define()
{
	It("Exposes every built-in SMAssist op as a UFUNCTION on ULogicDriverToolset", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		TestNotNull(TEXT("Subsystem available"), Subsystem);
		if (!Subsystem)
		{
			return;
		}

		const UClass* ToolsetClass = ULogicDriverToolset::StaticClass();
		TestNotNull(TEXT("ToolsetClass available"), ToolsetClass);
		if (!ToolsetClass)
		{
			return;
		}

		const TArray<FSMAssistOperationInfo> Ops = Subsystem->GetAllOperationInfos();
		TestTrue(TEXT("At least one op registered"), Ops.Num() > 0);

		for (const FSMAssistOperationInfo& Op : Ops)
		{
			const FName ExpectedFunctionName = UFunctionNameForOp(Op.Name);
			TestNotEqual(
				FString::Printf(TEXT("Op '%s' yields a non-empty UFUNCTION name"), *Op.Name.ToString()),
				ExpectedFunctionName, FName(NAME_None));

			const UFunction* Function = ToolsetClass->FindFunctionByName(ExpectedFunctionName);
			TestNotNull(
				*FString::Printf(TEXT("UFUNCTION exists for op '%s' (expected '%s')"),
					*Op.Name.ToString(), *ExpectedFunctionName.ToString()),
				Function);
		}
	});

	It("Round-trips a simple op end-to-end via the marshal layer", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		TestNotNull(TEXT("Subsystem available"), Subsystem);
		if (!Subsystem)
		{
			return;
		}

		const FString ResultJson = ULogicDriverToolset::ListAssets(FString());
		TestFalse(TEXT("ListAssets returned non-empty JSON"), ResultJson.IsEmpty());

		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
		const bool bParsed = FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid();
		TestTrue(TEXT("Result parses as a JSON object"), bParsed);
		if (!bParsed)
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* AssetsArray = nullptr;
		const bool bHasAssets = Parsed->TryGetArrayField(TEXT("assets"), AssetsArray);
		TestTrue(TEXT("Result has an 'assets' array"), bHasAssets);
		TestTrue(TEXT("Result has a 'count' field"), Parsed->HasTypedField<EJson::Number>(TEXT("count")));
	});

	It("Returns empty FString when SMAssist rejects the input", [this]()
	{
		// Calling GetAsset with a null blueprint produces a JSON envelope without `asset_path`,
		// SMAssist core rejects, and the marshal layer (a) routes the error through
		// UKismetSystemLibrary::RaiseScriptError for the MCP transport to surface as a tool
		// error and (b) returns an empty FString. The script-error log path goes through
		// FBlueprintCoreDelegates::OnScriptException rather than UE_LOG and is not interceptable
		// by AddExpectedError, so this test only verifies the return-value half of the contract.
		const FString ResultJson = ULogicDriverToolset::GetAsset(nullptr);
		TestTrue(TEXT("Errored op returns empty FString"), ResultJson.IsEmpty());
	});

	// Regression: AddJsonValue raised a script error on malformed variant JSON but returned void,
	// so the UFUNCTION continued to Execute without the field and mutated the asset while the
	// client saw a tool error. The bool return is the abort signal every call site must honor.
	It("AddJsonValue reports malformed variant JSON through its return value", [this]()
	{
		namespace LDA = LD::Assist::Toolset::Marshal;

		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		TestTrue(TEXT("Empty text is the omit sentinel and succeeds"),
			LDA::AddJsonValue(*Json, TEXT("field_a"), FString()));
		TestFalse(TEXT("Omit sentinel emits no field"), Json->HasField(TEXT("field_a")));

		TestTrue(TEXT("A bare scalar parses"), LDA::AddJsonValue(*Json, TEXT("field_b"), TEXT("1.5")));
		TestTrue(TEXT("Parsed field emitted"), Json->HasField(TEXT("field_b")));

		TestTrue(TEXT("An array parses"), LDA::AddJsonValue(*Json, TEXT("field_c"), TEXT("[\"a\",\"b\"]")));
		TestTrue(TEXT("Array field emitted"), Json->HasField(TEXT("field_c")));

		// The script error raised here is routed through OnScriptException (see the note above), so
		// only the return-value half of the contract is assertable.
		TestFalse(TEXT("Malformed JSON returns false"),
			LDA::AddJsonValue(*Json, TEXT("field_d"), TEXT("{broken")));
		TestFalse(TEXT("Malformed field not emitted"), Json->HasField(TEXT("field_d")));

		TestFalse(TEXT("Trailing garbage returns false"),
			LDA::AddJsonValue(*Json, TEXT("field_e"), TEXT("1,\"x\":2")));
		TestFalse(TEXT("Trailing-garbage field not emitted"), Json->HasField(TEXT("field_e")));
	});

	// Regression: coordinates were marshaled through a negative "use default" sentinel, but LD
	// canvases use negative coordinates routinely (the default state row sits near y=-43), so an
	// explicit (400, -43) placement silently dropped its Y and landed at (400, 0).
	It("AddState honors explicit negative coordinates and auto-positions by default", [this]()
	{
		const FString CreateJson = ULogicDriverToolset::CreateBlueprint(
			FGuid::NewGuid().ToString(), TEXT("/Temp/Automation/Transient"));
		TSharedPtr<FJsonObject> Created;
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CreateJson);
			if (!TestTrue(TEXT("CreateBlueprint returned JSON"),
				FJsonSerializer::Deserialize(Reader, Created) && Created.IsValid()))
			{
				return;
			}
		}
		FString AssetPath;
		Created->TryGetStringField(TEXT("asset_path"), AssetPath);
		USMBlueprint* Blueprint = Cast<USMBlueprint>(FSoftObjectPath(AssetPath).TryLoad());
		if (!TestNotNull(TEXT("Blueprint loaded"), Blueprint))
		{
			return;
		}

		auto ParseStateGuid = [this](const FString& InJson) -> FString
		{
			TSharedPtr<FJsonObject> Parsed;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
			FString Guid;
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				Parsed->TryGetStringField(TEXT("state_guid"), Guid);
			}
			return Guid;
		};

		const FString PlacedGuid = ParseStateGuid(ULogicDriverToolset::AddState(
			Blueprint, TEXT("Placed"), /*bIsEntry*/false, /*bAutoPosition*/false, 400.0, -43.0));
		const FString AutoGuid = ParseStateGuid(ULogicDriverToolset::AddState(Blueprint, TEXT("Auto")));
		if (!TestFalse(TEXT("Placed state created"), PlacedGuid.IsEmpty())
			|| !TestFalse(TEXT("Auto state created"), AutoGuid.IsEmpty()))
		{
			return;
		}

		TSharedPtr<FJsonObject> Asset;
		{
			const FString AssetJson = ULogicDriverToolset::GetAsset(Blueprint);
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(AssetJson);
			if (!TestTrue(TEXT("GetAsset returned JSON"),
				FJsonSerializer::Deserialize(Reader, Asset) && Asset.IsValid()))
			{
				return;
			}
		}

		auto GetPosition = [&Asset](const FString& InGuid, double& OutX, double& OutY) -> bool
		{
			const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
			if (!Asset->TryGetArrayField(TEXT("states"), States) || !States)
			{
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *States)
			{
				const TSharedPtr<FJsonObject>* Entry = nullptr;
				FString Guid;
				if (Value->TryGetObject(Entry) && Entry->IsValid()
					&& (*Entry)->TryGetStringField(TEXT("state_guid"), Guid) && Guid == InGuid)
				{
					return (*Entry)->TryGetNumberField(TEXT("position_x"), OutX)
						&& (*Entry)->TryGetNumberField(TEXT("position_y"), OutY);
				}
			}
			return false;
		};

		double PlacedX = 0.0;
		double PlacedY = 0.0;
		if (TestTrue(TEXT("Placed state has a position"), GetPosition(PlacedGuid, PlacedX, PlacedY)))
		{
			TestEqual(TEXT("Explicit X honored"), PlacedX, 400.0);
			TestEqual(TEXT("Explicit negative Y honored"), PlacedY, -43.0);
		}

		double AutoX = 0.0;
		double AutoY = 0.0;
		if (TestTrue(TEXT("Auto state has a position"), GetPosition(AutoGuid, AutoX, AutoY)))
		{
			TestFalse(TEXT("Auto placement did not collapse to (0, 0)"), AutoX == 0.0 && AutoY == 0.0);
		}
	});
}

#endif // WITH_TOOLSET_REGISTRY

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
