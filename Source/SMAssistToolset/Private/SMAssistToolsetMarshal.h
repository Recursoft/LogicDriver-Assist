// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Math/NumericLimits.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Operations/SMAssistOperationResult.h"
#include "SMAssistSubsystem.h"

namespace LD::Assist::Toolset::Marshal
{
	/** Adds a field with the path-name of a UObject. No-op when `Object` is null (caller validates). */
	inline void AddObjectPath(FJsonObject& Json, FStringView Field, const UObject* Object)
	{
		if (Object)
		{
			Json.SetStringField(FString(Field), Object->GetPathName());
		}
	}

	/** Adds a string field when the value is non-empty. Empty = sentinel for "use SMAssist default". */
	inline void AddIfNonEmpty(FJsonObject& Json, FStringView Field, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Json.SetStringField(FString(Field), Value);
		}
	}

	/** Adds an int32 field when the value differs from INDEX_NONE. -1 = sentinel for "use SMAssist default". */
	inline void AddIfNotIndexNone(FJsonObject& Json, FStringView Field, int32 Value)
	{
		if (Value != INDEX_NONE)
		{
			Json.SetNumberField(FString(Field), static_cast<double>(Value));
		}
	}

	/**
	 * Adds a double field when the value is non-negative. Negative sentinel (e.g., -1.0) =
	 * "use SMAssist default". Used for positions, gaps, and durations — none of which take
	 * meaningful negative values in SMAssist.
	 */
	inline void AddIfNonNegative(FJsonObject& Json, FStringView Field, double Value)
	{
		if (Value >= 0.0)
		{
			Json.SetNumberField(FString(Field), Value);
		}
	}

	/** Adds a bool field unconditionally. Bools have no sentinel; the C++ default mirrors the SMAssist default for that field. */
	inline void AddBool(FJsonObject& Json, FStringView Field, bool Value)
	{
		Json.SetBoolField(FString(Field), Value);
	}

	/**
	 * Adds a variant-typed field whose value is JSON-encoded text. Parses `JsonText`
	 * and emits the resulting JSON value (scalar / array / object) under `Field`.
	 * Empty `JsonText` = sentinel for "use SMAssist default" — no field emitted.
	 * Malformed JSON produces a script error and the field is skipped.
	 */
	inline void AddJsonValue(FJsonObject& Json, FStringView Field, const FString& JsonText)
	{
		if (JsonText.IsEmpty())
		{
			return;
		}
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		TSharedPtr<FJsonValue> Parsed;
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
		{
			UKismetSystemLibrary::RaiseScriptError(FString::Printf(
				TEXT("Field '%.*s': malformed JSON value."), Field.Len(), Field.GetData()));
			return;
		}
		Json.SetField(FString(Field), Parsed);
	}

	/**
	 * Serializes a FJsonObject to a condensed JSON string. Used internally by `Execute`
	 * to render the SMAssist result payload as a tool return value.
	 */
	inline FString JsonObjectToString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	/**
	 * Dispatches an SMAssist operation by name. On success, returns the operation's payload
	 * serialized as a JSON string. On error, raises a script error (surfaced as a tool-level
	 * MCP error) and returns an empty string. On missing subsystem (no editor context),
	 * same — raises and returns empty.
	 */
	inline FString Execute(FName OperationName, const TSharedRef<FJsonObject>& Args)
	{
		USMAssistSubsystem* Subsystem =
			GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UKismetSystemLibrary::RaiseScriptError(
				TEXT("SMAssistSubsystem unavailable (editor not initialized)."));
			return FString();
		}

		FSMAssistOperationResult Result = Subsystem->ExecuteOperation(OperationName, Args);
		if (!Result.bSuccess)
		{
			UKismetSystemLibrary::RaiseScriptError(Result.ErrorMessage);
			return FString();
		}

		return Result.Payload.IsValid() ? JsonObjectToString(Result.Payload.ToSharedRef()) : FString(TEXT("{}"));
	}
}
