// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistModule.h"

#include "SMAssistLog.h"
#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "UObject/NameTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "SMAssistModule"

DEFINE_LOG_CATEGORY(LogLogicDriverAssist)

void FSMAssistModule::StartupModule()
{
	ExecCommand = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
		TEXT("LDAssist.Exec"),
		TEXT("Execute an SMAssist operation. Usage: LDAssist.Exec <operation> [json_args]"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FSMAssistModule::HandleExecCommand));

	ListCommand = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
		TEXT("LDAssist.List"),
		TEXT("List all registered SMAssist operations."),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FSMAssistModule::HandleListCommand));

	DescribeCommand = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
		TEXT("LDAssist.Describe"),
		TEXT("Print the impact classification and JSON input schema of an SMAssist operation, or of every operation when called with no argument. Usage: LDAssist.Describe [operation]"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FSMAssistModule::HandleDescribeCommand));
}

void FSMAssistModule::ShutdownModule()
{
	ExecCommand.Reset();
	ListCommand.Reset();
	DescribeCommand.Reset();
}

void FSMAssistModule::HandleExecCommand(const TArray<FString>& InArgs, FOutputDevice& InAr)
{
	if (InArgs.Num() == 0)
	{
		InAr.Log(TEXT("Usage: LDAssist.Exec <operation> [json_args]"));
		return;
	}

	if (!GEditor)
	{
		InAr.Log(TEXT("GEditor is not available."));
		return;
	}

	USMAssistSubsystem* Subsystem = GEditor->GetEditorSubsystem<USMAssistSubsystem>();
	if (!Subsystem)
	{
		InAr.Log(TEXT("SMAssist subsystem is not available."));
		return;
	}

	// FName construction fatally asserts at NAME_SIZE; bound the raw console token first.
	if (InArgs[0].Len() >= NAME_SIZE)
	{
		InAr.Logf(TEXT("Operation name is %d characters; the maximum is %d."), InArgs[0].Len(), NAME_SIZE - 1);
		return;
	}

	const FName OperationName(*InArgs[0]);

	TSharedRef<FJsonObject> ParsedArgs = MakeShared<FJsonObject>();
	if (InArgs.Num() > 1)
	{
		FString JoinedJson;
		for (int32 ArgIdx = 1; ArgIdx < InArgs.Num(); ++ArgIdx)
		{
			if (ArgIdx > 1)
			{
				JoinedJson += TEXT(" ");
			}
			JoinedJson += InArgs[ArgIdx];
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JoinedJson);
		TSharedPtr<FJsonObject> JsonArgs;
		if (!FJsonSerializer::Deserialize(Reader, JsonArgs) || !JsonArgs.IsValid())
		{
			InAr.Logf(TEXT("Failed to parse JSON args: %s"), *JoinedJson);
			return;
		}
		ParsedArgs = JsonArgs.ToSharedRef();
	}

	const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(OperationName, ParsedArgs);

	if (!Result.bSuccess)
	{
		InAr.Logf(TEXT("[%s] error: %s"), *OperationName.ToString(), *Result.ErrorMessage);
		return;
	}

	FString PayloadString = TEXT("{}");
	if (Result.Payload.IsValid())
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadString);
		FJsonSerializer::Serialize(Result.Payload.ToSharedRef(), Writer);
	}

	InAr.Logf(TEXT("[%s] ok: %s"), *OperationName.ToString(), *PayloadString);
}

void FSMAssistModule::HandleListCommand(const TArray<FString>& InArgs, FOutputDevice& InAr)
{
	if (!GEditor)
	{
		InAr.Log(TEXT("GEditor is not available."));
		return;
	}

	USMAssistSubsystem* Subsystem = GEditor->GetEditorSubsystem<USMAssistSubsystem>();
	if (!Subsystem)
	{
		InAr.Log(TEXT("SMAssist subsystem is not available."));
		return;
	}

	const TArray<FSMAssistOperationInfo> Infos = Subsystem->GetAllOperationInfos();
	InAr.Logf(TEXT("Registered operations (%d):"), Infos.Num());
	for (const FSMAssistOperationInfo& Info : Infos)
	{
		if (Info.Description.IsEmpty())
		{
			InAr.Logf(TEXT("  %s"), *Info.Name.ToString());
		}
		else
		{
			InAr.Logf(TEXT("  %s  %s"), *Info.Name.ToString(), *Info.Description);
		}
	}
}

void FSMAssistModule::HandleDescribeCommand(const TArray<FString>& InArgs, FOutputDevice& InAr)
{
	if (!GEditor)
	{
		InAr.Log(TEXT("GEditor is not available."));
		return;
	}

	USMAssistSubsystem* Subsystem = GEditor->GetEditorSubsystem<USMAssistSubsystem>();
	if (!Subsystem)
	{
		InAr.Log(TEXT("SMAssist subsystem is not available."));
		return;
	}

	// Matched as a string rather than an FName so an unregistered console token neither interns a new
	// name nor collapses to NAME_None, which would read as "no filter" and dump every operation.
	const FString Filter = InArgs.Num() > 0 ? InArgs[0] : FString();

	const TArray<FSMAssistOperationInfo> Infos = Subsystem->GetAllOperationInfos();
	TArray<const FSMAssistOperationInfo*> Selected;
	for (const FSMAssistOperationInfo& Info : Infos)
	{
		if (Filter.IsEmpty() || Info.Name.ToString().Equals(Filter, ESearchCase::IgnoreCase))
		{
			Selected.Add(&Info);
		}
	}

	if (!Filter.IsEmpty() && Selected.Num() == 0)
	{
		InAr.Logf(TEXT("Unknown operation '%s'."), *Filter);
		return;
	}

	// One condensed JSON object per line, so a caller reading the output device line by line parses
	// each entry whole instead of reassembling JSON that was wrapped across lines.
	InAr.Logf(TEXT("Operations (%d):"), Selected.Num());
	for (const FSMAssistOperationInfo* Info : Selected)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Info->Name.ToString());
		Entry->SetStringField(TEXT("description"), Info->Description);
		Entry->SetStringField(TEXT("impact"),
			Info->Impact == ESMAssistOperationImpact::ReadOnly ? TEXT("read_only") : TEXT("destructive"));
		Entry->SetObjectField(TEXT("input_schema"),
			Info->InputSchema.IsValid() ? Info->InputSchema : MakeShared<FJsonObject>());

		FString EntryText;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&EntryText);
		FJsonSerializer::Serialize(Entry, Writer);

		InAr.Logf(TEXT("  %s"), *EntryText);
	}
}

IMPLEMENT_MODULE(FSMAssistModule, SMAssist)

#undef LOCTEXT_NAMESPACE
