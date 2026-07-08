// Copyright Recursoft LLC. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

#if WITH_TOOLSET_REGISTRY

#include "LogicDriverToolset.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"
#include "SMAssistSubsystem.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"

BEGIN_DEFINE_SPEC(FSMAssistToolsetSpec, "LogicDriver.Assist.Toolset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	/**
	 * Translates a canonical SMAssist op name (e.g. "sm.add_state" or "ld_ue.read_property") into
	 * the expected static UFUNCTION name on ULogicDriverToolset ("AddState", "ReadProperty").
	 * Drops the namespace (`sm.` or `ld_ue.`), PascalCases the remaining snake_case parts.
	 * Returns NAME_None if the op name carries neither known namespace (unexpected; caller treats
	 * as a registration drift bug).
	 */
	static FName UFunctionNameForOp(FName InOpName)
	{
		FString OpString = InOpName.ToString();

		bool bStripped = false;
		for (const FString& NamespacePrefix : { FString(TEXT("sm.")), FString(TEXT("ld_ue.")) })
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
}

#endif // WITH_TOOLSET_REGISTRY

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
