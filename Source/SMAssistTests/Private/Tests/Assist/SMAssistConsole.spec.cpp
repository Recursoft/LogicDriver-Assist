// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationInfo.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/NameTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

namespace LD::Assist::Tests::Console
{
	/** Collects the lines a console command writes so the spec can assert on the transport's output. */
	class FCapturingOutputDevice final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* InText, ELogVerbosity::Type, const FName&) override
		{
			Lines.Add(InText);
		}

	public:
		TArray<FString> Lines;
	};
}

// The console commands are the only transport that reaches Assist without linking it, so external
// bridges parse this output. Its shape is a contract.
BEGIN_DEFINE_SPEC(FSMAssistConsoleSpec, "LogicDriver.Assist.Console",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	static TArray<FString> RunConsoleCommand(const TCHAR* InCommand, const TArray<FString>& InArgs)
	{
		IConsoleObject* Object = IConsoleManager::Get().FindConsoleObject(InCommand);
		IConsoleCommand* Command = Object ? Object->AsCommand() : nullptr;
		if (!Command)
		{
			return TArray<FString>();
		}

		LD::Assist::Tests::Console::FCapturingOutputDevice Capture;
		Command->Execute(InArgs, GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, Capture);
		return Capture.Lines;
	}

	static TSharedPtr<FJsonObject> ParseEntry(const FString& InLine)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InLine.TrimStartAndEnd());
		TSharedPtr<FJsonObject> Parsed;
		FJsonSerializer::Deserialize(Reader, Parsed);
		return Parsed;
	}

END_DEFINE_SPEC(FSMAssistConsoleSpec)

void FSMAssistConsoleSpec::Define()
{
	It("Describes every registered operation as one parseable JSON object per line", [this]()
	{
		USMAssistSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
		if (!TestNotNull(TEXT("Subsystem available"), Subsystem))
		{
			return;
		}

		const TArray<FString> Lines = RunConsoleCommand(TEXT("LDAssist.Describe"), TArray<FString>());
		if (!TestTrue(TEXT("Command produced output"), Lines.Num() > 1))
		{
			return;
		}

		const int32 EntryCount = Lines.Num() - 1;
		TestEqual(TEXT("One entry per registered operation"), EntryCount, Subsystem->GetAllOperationInfos().Num());

		for (int32 LineIdx = 1; LineIdx < Lines.Num(); ++LineIdx)
		{
			const TSharedPtr<FJsonObject> Entry = ParseEntry(Lines[LineIdx]);
			if (!TestTrue(TEXT("Entry parses as JSON"), Entry.IsValid()))
			{
				return;
			}
			TestTrue(TEXT("Entry carries a name"), Entry->HasTypedField<EJson::String>(TEXT("name")));
			TestTrue(TEXT("Entry carries a description"), Entry->HasTypedField<EJson::String>(TEXT("description")));
			TestTrue(TEXT("Entry carries an impact"), Entry->HasTypedField<EJson::String>(TEXT("impact")));
			TestTrue(TEXT("Entry carries an input schema"), Entry->HasTypedField<EJson::Object>(TEXT("input_schema")));
		}
	});

	It("Describes a single operation with its impact and schema", [this]()
	{
		const TArray<FString> Lines = RunConsoleCommand(TEXT("LDAssist.Describe"), { TEXT("ld.create_blueprint") });
		if (!TestEqual(TEXT("Header plus one entry"), Lines.Num(), 2))
		{
			return;
		}

		const TSharedPtr<FJsonObject> Entry = ParseEntry(Lines[1]);
		if (!TestTrue(TEXT("Entry parses as JSON"), Entry.IsValid()))
		{
			return;
		}

		FString Name;
		Entry->TryGetStringField(TEXT("name"), Name);
		TestEqual(TEXT("Name matches the requested operation"), Name, FString(TEXT("ld.create_blueprint")));

		FString Impact;
		Entry->TryGetStringField(TEXT("impact"), Impact);
		TestEqual(TEXT("A mutating operation reports destructive"), Impact, FString(TEXT("destructive")));

		const TSharedPtr<FJsonObject>* Schema = nullptr;
		if (TestTrue(TEXT("Schema present"), Entry->TryGetObjectField(TEXT("input_schema"), Schema)))
		{
			TestTrue(TEXT("Schema declares properties"), (*Schema)->HasField(TEXT("properties")));
		}
	});

	It("Reports read-only operations as read_only", [this]()
	{
		const TArray<FString> Lines = RunConsoleCommand(TEXT("LDAssist.Describe"), { TEXT("ld.get_asset") });
		if (!TestEqual(TEXT("Header plus one entry"), Lines.Num(), 2))
		{
			return;
		}

		const TSharedPtr<FJsonObject> Entry = ParseEntry(Lines[1]);
		if (!TestTrue(TEXT("Entry parses as JSON"), Entry.IsValid()))
		{
			return;
		}

		FString Impact;
		Entry->TryGetStringField(TEXT("impact"), Impact);
		TestEqual(TEXT("A read operation reports read_only"), Impact, FString(TEXT("read_only")));
	});

	It("Rejects an unknown operation without dumping the catalog", [this]()
	{
		const TArray<FString> Lines = RunConsoleCommand(TEXT("LDAssist.Describe"), { TEXT("ld.not_a_real_operation") });
		if (!TestEqual(TEXT("Single line response"), Lines.Num(), 1))
		{
			return;
		}
		TestTrue(TEXT("Response names the unknown operation"), Lines[0].Contains(TEXT("Unknown operation")));
	});

	It("Survives a filter token longer than an FName", [this]()
	{
		// FName construction asserts at NAME_SIZE, so the filter is matched as a string. A caller that
		// pastes a malformed token must get an error rather than a fatal.
		const FString Overlong = FString::ChrN(NAME_SIZE + 8, TEXT('x'));
		const TArray<FString> Lines = RunConsoleCommand(TEXT("LDAssist.Describe"), { Overlong });
		if (!TestEqual(TEXT("Single line response"), Lines.Num(), 1))
		{
			return;
		}
		TestTrue(TEXT("Response names the unknown operation"), Lines[0].Contains(TEXT("Unknown operation")));
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
