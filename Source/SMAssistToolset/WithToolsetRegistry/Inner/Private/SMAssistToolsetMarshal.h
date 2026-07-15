// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Kismet/KismetSystemLibrary.h"
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
	 * "use SMAssist default". Only for quantities with no meaningful negative value (gaps,
	 * durations); canvas coordinates are legitimately negative and use AddPosition instead.
	 */
	inline void AddIfNonNegative(FJsonObject& Json, FStringView Field, double Value)
	{
		if (Value >= 0.0)
		{
			Json.SetNumberField(FString(Field), Value);
		}
	}

	/**
	 * Adds both canvas-position fields for a manual placement; bAuto=true emits neither so
	 * SMAssist auto-positions. A separate bool carries the "omit" signal because negative
	 * coordinates are legitimate positions (the default state row sits near y=-43), so no
	 * numeric sentinel can distinguish "unset" from a real value.
	 */
	inline void AddPosition(FJsonObject& Json, bool bAuto, FStringView FieldX, FStringView FieldY, double X, double Y)
	{
		if (!bAuto)
		{
			Json.SetNumberField(FString(FieldX), X);
			Json.SetNumberField(FString(FieldY), Y);
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
	 * Empty `JsonText` is the sentinel for "use SMAssist default"; no field is emitted.
	 * Malformed JSON raises a script error and returns false; the caller must return
	 * without dispatching, since RaiseScriptError does not abort C++ and executing with
	 * the field silently dropped would mutate the asset while reporting a tool error.
	 */
	[[nodiscard]] inline bool AddJsonValue(FJsonObject& Json, FStringView Field, const FString& JsonText)
	{
		if (JsonText.IsEmpty())
		{
			return true;
		}
		// UE's top-level JSON reader only accepts objects and arrays, so a bare scalar
		// ("1.5", "\"red\"", "true") would round-trip as malformed even though it is
		// valid JSON. Wrap in a synthetic object and pull the value back out so every
		// JSON type the schema advertises (string, number, boolean, null, array, object)
		// reaches the handler. The single-key check rejects trailing-garbage payloads
		// like `1,"x":2` that would otherwise smuggle extra fields into the wrapper.
		const FString Wrapped = FString::Printf(TEXT("{\"v\":%s}"), *JsonText);
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Wrapped);
		TSharedPtr<FJsonObject> ParsedObject;
		const bool bParsed =
			FJsonSerializer::Deserialize(Reader, ParsedObject)
			&& ParsedObject.IsValid()
			&& ParsedObject->Values.Num() == 1;
		const TSharedPtr<FJsonValue> Parsed = bParsed ? ParsedObject->TryGetField(TEXT("v")) : nullptr;
		if (!Parsed.IsValid())
		{
			UKismetSystemLibrary::RaiseScriptError(FString::Printf(
				TEXT("Field '%.*s': malformed JSON value."), Field.Len(), Field.GetData()));
			return false;
		}
		Json.SetField(FString(Field), Parsed);
		return true;
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
	 * same behavior: raises and returns empty.
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
