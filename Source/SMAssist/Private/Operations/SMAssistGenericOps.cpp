// Copyright Recursoft LLC. All Rights Reserved.

#include "Operations/SMAssistGenericOps.h"

#include "Operations/SMAssistGenericOpKeys.h"
#include "Utilities/SMAssistUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/Char.h"
#include "Misc/ScopeExit.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace LD::Assist::GenericOps::Private
{
	struct FSegment
	{
		FString Name;
		FString Subscript;
		bool bHasSubscript = false;
	};

	static bool ParseSegment(const FString& InSegment, FSegment& OutSegment, FString& OutError)
	{
		int32 Open = INDEX_NONE;
		if (!InSegment.FindChar(TEXT('['), Open))
		{
			OutSegment.Name = InSegment;
			OutSegment.bHasSubscript = false;
			return !InSegment.IsEmpty();
		}

		int32 Close = INDEX_NONE;
		if (!InSegment.FindChar(TEXT(']'), Close) || Close < Open)
		{
			OutError = FString::Printf(TEXT("Malformed subscript in path segment '%s'."), *InSegment);
			return false;
		}

		OutSegment.Name = InSegment.Left(Open);
		OutSegment.Subscript = InSegment.Mid(Open + 1, Close - Open - 1);
		OutSegment.bHasSubscript = true;
		return !OutSegment.Name.IsEmpty() && !OutSegment.Subscript.IsEmpty();
	}

	static bool ParseArrayIndex(const FString& InText, int32& OutIndex)
	{
		if (InText.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Ch : InText)
		{
			if (!FChar::IsDigit(Ch))
			{
				return false;
			}
		}
		OutIndex = FCString::Atoi(*InText);
		return true;
	}

	// Allocates and text-imports a map key into a heap buffer. Caller must FreeMapKey on the result.
	static void* AllocImportMapKey(const FMapProperty* InMapProp, const FString& InKeyText, UObject* InOwner, FString& OutError)
	{
		FProperty* KeyProp = InMapProp->KeyProp;
		void* KeyBuffer = FMemory::Malloc(KeyProp->GetSize(), KeyProp->GetMinAlignment());
		KeyProp->InitializeValue(KeyBuffer);

		if (KeyProp->ImportText_Direct(*InKeyText, KeyBuffer, InOwner, PPF_None, nullptr) == nullptr)
		{
			KeyProp->DestroyValue(KeyBuffer);
			FMemory::Free(KeyBuffer);
			OutError = FString::Printf(TEXT("Could not parse map key '%s' as %s."), *InKeyText, *KeyProp->GetCPPType());
			return nullptr;
		}
		return KeyBuffer;
	}

	static void FreeMapKey(const FMapProperty* InMapProp, void* InKey)
	{
		InMapProp->KeyProp->DestroyValue(InKey);
		FMemory::Free(InKey);
	}

	static UObject* ResolveTargetObject(const FString& InObject, const FString& InTarget, int32 InPieInstance, bool& bOutRuntime, FString& OutError)
	{
		bOutRuntime = InTarget.Equals(TEXT("runtime"), ESearchCase::IgnoreCase);
		if (bOutRuntime)
		{
			UWorld* PieWorld = LD::Assist::Utils::GetActivePIEWorld(InPieInstance, OutError);
			if (!PieWorld)
			{
				return nullptr;
			}

			AActor* Actor = LD::Assist::Utils::FindActorByIdentifier(PieWorld, InObject);
			if (!Actor)
			{
				OutError = FString::Printf(TEXT("No actor matching '%s' in the running PIE world."), *InObject);
			}
			return Actor;
		}

		const FSoftObjectPath ObjectPath(InObject);
		UObject* Loaded = ObjectPath.TryLoad();
		if (!Loaded)
		{
			OutError = FString::Printf(TEXT("Could not load object '%s'."), *InObject);
			return nullptr;
		}

		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Loaded))
		{
			if (!Blueprint->GeneratedClass)
			{
				OutError = FString::Printf(TEXT("Blueprint '%s' has no generated class."), *InObject);
				return nullptr;
			}
			return Blueprint->GeneratedClass->GetDefaultObject();
		}
		if (UClass* AsClass = Cast<UClass>(Loaded))
		{
			return AsClass->GetDefaultObject();
		}
		return Loaded;
	}

	// Resolves one path segment against (InStruct, InContainer) with read semantics. On success OutProp/OutAddr
	// describe the segment's (element) value; container subscripts fail when the index or key is absent.
	static bool ResolveSegmentRead(UStruct* InStruct, void* InContainer, UObject* InOwner, const FSegment& InSeg, FProperty*& OutProp, void*& OutAddr, FString& OutError)
	{
		FProperty* Prop = FindFProperty<FProperty>(InStruct, *InSeg.Name);
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on '%s'."), *InSeg.Name, *InStruct->GetName());
			return false;
		}

		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(InContainer);

		if (!InSeg.bHasSubscript)
		{
			OutProp = Prop;
			OutAddr = PropAddr;
			return true;
		}

		if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			int32 Index = INDEX_NONE;
			if (!ParseArrayIndex(InSeg.Subscript, Index))
			{
				OutError = FString::Printf(TEXT("Array index '%s' is not a non-negative integer."), *InSeg.Subscript);
				return false;
			}

			FScriptArrayHelper Helper(ArrayProp, PropAddr);
			if (Index >= Helper.Num())
			{
				OutError = FString::Printf(TEXT("Array index %d out of range (size %d)."), Index, Helper.Num());
				return false;
			}

			OutProp = ArrayProp->Inner;
			OutAddr = Helper.GetRawPtr(Index);
			return true;
		}

		if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			void* KeyBuffer = AllocImportMapKey(MapProp, InSeg.Subscript, InOwner, OutError);
			if (!KeyBuffer)
			{
				return false;
			}
			ON_SCOPE_EXIT { FreeMapKey(MapProp, KeyBuffer); };

			// ValuePtr points into the map's value storage (independent of the key buffer), so it stays valid
			// after the key is freed on scope exit.
			FScriptMapHelper Helper(MapProp, PropAddr);
			uint8* ValuePtr = Helper.FindValueFromHash(KeyBuffer);

			if (!ValuePtr)
			{
				OutError = FString::Printf(TEXT("Map key '%s' not present in '%s'."), *InSeg.Subscript, *InSeg.Name);
				return false;
			}

			OutProp = MapProp->ValueProp;
			OutAddr = ValuePtr;
			return true;
		}

		OutError = FString::Printf(TEXT("Property '%s' is not a container; cannot subscript with '[%s]'."), *InSeg.Name, *InSeg.Subscript);
		return false;
	}

	// Walks InTokens[0, InCount) descending into structs and object boundaries, yielding the container the
	// remaining segments resolve against. Intermediate container elements must already exist.
	static bool ResolveParentContainer(UObject* InObject, const TArray<FString>& InTokens, int32 InCount, UStruct*& OutStruct, void*& OutContainer, UObject*& OutOwner, FString& OutError)
	{
		UStruct* CurStruct = InObject->GetClass();
		void* CurContainer = InObject;
		UObject* CurOwner = InObject;

		for (int32 Idx = 0; Idx < InCount; ++Idx)
		{
			FSegment Seg;
			if (!ParseSegment(InTokens[Idx], Seg, OutError))
			{
				return false;
			}

			FProperty* EffProp = nullptr;
			void* EffAddr = nullptr;
			if (!ResolveSegmentRead(CurStruct, CurContainer, CurOwner, Seg, EffProp, EffAddr, OutError))
			{
				return false;
			}

			if (FStructProperty* StructProp = CastField<FStructProperty>(EffProp))
			{
				CurStruct = StructProp->Struct;
				CurContainer = EffAddr;
			}
			else if (FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(EffProp))
			{
				UObject* SubObject = ObjectProp->GetObjectPropertyValue(EffAddr);
				if (!SubObject)
				{
					OutError = FString::Printf(TEXT("Object property '%s' is null; cannot descend."), *Seg.Name);
					return false;
				}
				CurStruct = SubObject->GetClass();
				CurContainer = SubObject;
				CurOwner = SubObject;
			}
			else
			{
				OutError = FString::Printf(TEXT("Cannot descend into '%s' (not a struct or object)."), *Seg.Name);
				return false;
			}
		}

		OutStruct = CurStruct;
		OutContainer = CurContainer;
		OutOwner = CurOwner;
		return true;
	}

	static bool ResolveReadAddress(UObject* InObject, const FString& InPath, FProperty*& OutProp, void*& OutAddr, UObject*& OutOwner, FString& OutError)
	{
		TArray<FString> Tokens;
		InPath.ParseIntoArray(Tokens, TEXT("."), true);
		if (Tokens.Num() == 0)
		{
			OutError = TEXT("Empty 'property_path'.");
			return false;
		}

		UStruct* ParentStruct = nullptr;
		void* ParentContainer = nullptr;
		if (!ResolveParentContainer(InObject, Tokens, Tokens.Num() - 1, ParentStruct, ParentContainer, OutOwner, OutError))
		{
			return false;
		}

		FSegment Final;
		if (!ParseSegment(Tokens.Last(), Final, OutError))
		{
			return false;
		}

		return ResolveSegmentRead(ParentStruct, ParentContainer, OutOwner, Final, OutProp, OutAddr, OutError);
	}
}

FSMAssistOperationResult LD::Assist::GenericOps::ReadProperty(const TSharedRef<FJsonObject>& InArgs)
{
	using namespace LD::Assist::GenericOps::Private;

	FString ObjectId;
	if (!InArgs->TryGetStringField(Args::Object, ObjectId) || ObjectId.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'object'."));
	}

	FString PathStr;
	if (!InArgs->TryGetStringField(Args::PropertyPath, PathStr) || PathStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'property_path'."));
	}

	FString Target;
	InArgs->TryGetStringField(Args::Target, Target);

	int32 PieInstance = 0;
	InArgs->TryGetNumberField(Args::PieInstance, PieInstance);

	bool bRuntime = false;
	FString ResolveError;
	UObject* TargetObject = ResolveTargetObject(ObjectId, Target, PieInstance, bRuntime, ResolveError);
	if (!TargetObject)
	{
		return FSMAssistOperationResult::MakeError(ResolveError);
	}

	FProperty* LeafProp = nullptr;
	void* ValueAddr = nullptr;
	UObject* Owner = nullptr;
	FString PathError;
	if (!ResolveReadAddress(TargetObject, PathStr, LeafProp, ValueAddr, Owner, PathError))
	{
		return FSMAssistOperationResult::MakeError(PathError);
	}

	FString ValueString;
	LeafProp->ExportTextItem_Direct(ValueString, ValueAddr, nullptr, Owner, PPF_None);

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::ObjectResolved, bRuntime ? TargetObject->GetName() : TargetObject->GetPathName());
	Payload->SetStringField(Args::Target, bRuntime ? TEXT("runtime") : TEXT("edit"));
	Payload->SetStringField(Args::PropertyPath, PathStr);
	Payload->SetStringField(Args::PropertyType, LeafProp->GetCPPType());
	Payload->SetStringField(Args::Value, ValueString);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::GenericOps::WriteProperty(const TSharedRef<FJsonObject>& InArgs)
{
	using namespace LD::Assist::GenericOps::Private;

	FString ObjectId;
	if (!InArgs->TryGetStringField(Args::Object, ObjectId) || ObjectId.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'object'."));
	}

	FString PathStr;
	if (!InArgs->TryGetStringField(Args::PropertyPath, PathStr) || PathStr.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'property_path'."));
	}

	FString ValueStr;
	if (!InArgs->TryGetStringField(Args::Value, ValueStr))
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'value'."));
	}

	FString Target;
	InArgs->TryGetStringField(Args::Target, Target);

	int32 PieInstance = 0;
	InArgs->TryGetNumberField(Args::PieInstance, PieInstance);

	bool bRuntime = false;
	FString ResolveError;
	UObject* TargetObject = ResolveTargetObject(ObjectId, Target, PieInstance, bRuntime, ResolveError);
	if (!TargetObject)
	{
		return FSMAssistOperationResult::MakeError(ResolveError);
	}

	TArray<FString> Tokens;
	PathStr.ParseIntoArray(Tokens, TEXT("."), true);
	if (Tokens.Num() == 0)
	{
		return FSMAssistOperationResult::MakeError(TEXT("Empty 'property_path'."));
	}

	UStruct* ParentStruct = nullptr;
	void* ParentContainer = nullptr;
	UObject* Owner = nullptr;
	FString PathError;
	if (!ResolveParentContainer(TargetObject, Tokens, Tokens.Num() - 1, ParentStruct, ParentContainer, Owner, PathError))
	{
		return FSMAssistOperationResult::MakeError(PathError);
	}

	FSegment Final;
	if (!ParseSegment(Tokens.Last(), Final, PathError))
	{
		return FSMAssistOperationResult::MakeError(PathError);
	}

	FProperty* Prop = FindFProperty<FProperty>(ParentStruct, *Final.Name);
	if (!Prop)
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Property '%s' not found on '%s'."), *Final.Name, *ParentStruct->GetName()));
	}

	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(ParentContainer);
	FString LeafType = Prop->GetCPPType();

	if (!Final.bHasSubscript)
	{
		if (!LD::Assist::Utils::IntegerPropertyTextParses(Prop, ValueStr)
			|| Prop->ImportText_Direct(*ValueStr, PropAddr, Owner, PPF_None, nullptr) == nullptr)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not parse 'value' as %s."), *Prop->GetCPPType()));
		}
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
	{
		int32 Index = INDEX_NONE;
		if (!ParseArrayIndex(Final.Subscript, Index))
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Array index '%s' is not a non-negative integer."), *Final.Subscript));
		}

		FScriptArrayHelper Helper(ArrayProp, PropAddr);
		if (Index >= Helper.Num())
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Array index %d out of range (size %d); write_property does not grow arrays."), Index, Helper.Num()));
		}

		LeafType = ArrayProp->Inner->GetCPPType();
		if (!LD::Assist::Utils::IntegerPropertyTextParses(ArrayProp->Inner, ValueStr)
			|| ArrayProp->Inner->ImportText_Direct(*ValueStr, Helper.GetRawPtr(Index), Owner, PPF_None, nullptr) == nullptr)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not parse 'value' as %s."), *LeafType));
		}
	}
	else if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
	{
		FString KeyError;
		void* KeyBuffer = AllocImportMapKey(MapProp, Final.Subscript, Owner, KeyError);
		if (!KeyBuffer)
		{
			return FSMAssistOperationResult::MakeError(KeyError);
		}
		ON_SCOPE_EXIT { FreeMapKey(MapProp, KeyBuffer); };

		FScriptMapHelper Helper(MapProp, PropAddr);
		int32 PairIndex = Helper.FindMapPairIndexFromHash(KeyBuffer);
		const bool bAdded = (PairIndex == INDEX_NONE);
		if (bAdded)
		{
			PairIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
			MapProp->KeyProp->CopyCompleteValue(Helper.GetKeyPtr(PairIndex), KeyBuffer);
		}

		LeafType = MapProp->ValueProp->GetCPPType();
		const bool bValueParsed =
			LD::Assist::Utils::IntegerPropertyTextParses(MapProp->ValueProp, ValueStr)
			&& MapProp->ValueProp->ImportText_Direct(*ValueStr, Helper.GetValuePtr(PairIndex), Owner, PPF_None, nullptr) != nullptr;
		if (bAdded)
		{
			// Keep the op atomic: a freshly-added pair whose value failed to parse must not linger.
			if (!bValueParsed)
			{
				Helper.RemoveAt(PairIndex);
			}
			Helper.Rehash();
		}

		if (!bValueParsed)
		{
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Could not parse 'value' as %s."), *LeafType));
		}
	}
	else
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Property '%s' is not a container; cannot subscript with '[%s]'."), *Final.Name, *Final.Subscript));
	}

	if (!bRuntime)
	{
		TargetObject->MarkPackageDirty();
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::ObjectResolved, bRuntime ? TargetObject->GetName() : TargetObject->GetPathName());
	Payload->SetStringField(Args::Target, bRuntime ? TEXT("runtime") : TEXT("edit"));
	Payload->SetStringField(Args::PropertyPath, PathStr);
	Payload->SetStringField(Args::PropertyType, LeafType);
	Payload->SetStringField(Args::Value, ValueStr);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}

FSMAssistOperationResult LD::Assist::GenericOps::AddDispatcher(const TSharedRef<FJsonObject>& InArgs)
{
	FString AssetPath;
	if (!InArgs->TryGetStringField(Args::AssetPath, AssetPath) || AssetPath.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'asset_path'."));
	}

	FString DispatcherName;
	if (!InArgs->TryGetStringField(Args::Name, DispatcherName) || DispatcherName.IsEmpty())
	{
		return FSMAssistOperationResult::MakeError(TEXT("Missing required arg 'name'."));
	}

	FString LoadError;
	UBlueprint* Blueprint = LD::Assist::Utils::LoadBlueprint(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FSMAssistOperationResult::MakeError(LoadError);
	}

	const FName DispatcherFName(*DispatcherName);
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Validate the whole param signature before touching the blueprint, so a bad param can never leave a
	// half-created dispatcher (dangling variable + orphaned graph) behind.
	struct FPendingParam
	{
		FName Name;
		FEdGraphPinType PinType;
		FString TypeToken;
	};
	TArray<FPendingParam> PendingParams;
	TSet<FName> SeenNames;
	const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
	if (InArgs->TryGetArrayField(Args::Params, Params) && Params)
	{
		for (const TSharedPtr<FJsonValue>& ParamValue : *Params)
		{
			const TSharedPtr<FJsonObject>* ParamObject = nullptr;
			if (!ParamValue.IsValid() || !ParamValue->TryGetObject(ParamObject) || !ParamObject)
			{
				continue;
			}

			FString ParamName;
			FString ParamType;
			(*ParamObject)->TryGetStringField(Args::ParamName, ParamName);
			(*ParamObject)->TryGetStringField(Args::ParamType, ParamType);
			if (ParamName.IsEmpty() || ParamType.IsEmpty())
			{
				return FSMAssistOperationResult::MakeError(
					TEXT("Each entry in 'params' requires a non-empty 'name' and 'type'."));
			}

			FName Category;
			FName SubCategory;
			UObject* SubCategoryObject = nullptr;
			if (!LD::Assist::Utils::ResolveTerminalType(ParamType, Category, SubCategory, SubCategoryObject))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Unrecognized param type '%s' for '%s'."), *ParamType, *ParamName));
			}

			const FName ParamFName(*ParamName);
			if (SeenNames.Contains(ParamFName))
			{
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Duplicate param name '%s'."), *ParamName));
			}
			SeenNames.Add(ParamFName);

			FPendingParam Pending;
			Pending.Name = ParamFName;
			Pending.PinType = FEdGraphPinType(Category, SubCategory, SubCategoryObject, EPinContainerType::None, false, FEdGraphTerminalType());
			Pending.TypeToken = ParamType;
			PendingParams.Add(MoveTemp(Pending));
		}
	}

	// Mirrors FBlueprintEditor::OnAddNewDelegate: the member variable (the PC_MCDelegate property) is what
	// survives compile; the signature graph alone does not. Monolith's add_event_dispatcher omits the variable.
	FEdGraphPinType DelegateType;
	DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, DispatcherFName, DelegateType))
	{
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not add dispatcher '%s' (name may already be in use)."), *DispatcherName));
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, DispatcherFName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!NewGraph)
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DispatcherFName);
		return FSMAssistOperationResult::MakeError(
			FString::Printf(TEXT("Could not create signature graph for dispatcher '%s'."), *DispatcherName));
	}

	const auto Rollback = [&]()
	{
		FBlueprintEditorUtils::RemoveGraph(Blueprint, NewGraph, EGraphRemoveFlags::Default);
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DispatcherFName);
	};

	NewGraph->bEditable = false;
	K2Schema->CreateDefaultNodesForGraph(*NewGraph);
	K2Schema->CreateFunctionGraphTerminators(*NewGraph, static_cast<UClass*>(nullptr));
	K2Schema->AddExtraFunctionFlags(NewGraph, (FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public));
	K2Schema->MarkFunctionEntryAsEditable(NewGraph, true);

	TArray<TSharedPtr<FJsonValue>> ParamsApplied;
	if (PendingParams.Num() > 0)
	{
		UK2Node_FunctionEntry* EntryNode = nullptr;
		for (UEdGraphNode* Node : NewGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* AsEntry = Cast<UK2Node_FunctionEntry>(Node))
			{
				EntryNode = AsEntry;
				break;
			}
		}

		if (!EntryNode)
		{
			Rollback();
			return FSMAssistOperationResult::MakeError(
				FString::Printf(TEXT("Dispatcher '%s' signature graph has no entry node for parameters."), *DispatcherName));
		}

		for (const FPendingParam& Pending : PendingParams)
		{
			if (!EntryNode->CreateUserDefinedPin(Pending.Name, Pending.PinType, EGPD_Output))
			{
				Rollback();
				return FSMAssistOperationResult::MakeError(
					FString::Printf(TEXT("Could not add param '%s' to dispatcher '%s'."), *Pending.Name.ToString(), *DispatcherName));
			}

			const TSharedRef<FJsonObject> AppliedEntry = MakeShared<FJsonObject>();
			AppliedEntry->SetStringField(Args::ParamName, Pending.Name.ToString());
			AppliedEntry->SetStringField(Args::ParamType, Pending.TypeToken);
			ParamsApplied.Add(MakeShared<FJsonValueObject>(AppliedEntry));
		}
	}

	Blueprint->DelegateSignatureGraphs.Add(NewGraph);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(Args::AssetPath, AssetPath);
	Payload->SetStringField(Args::DispatcherName, DispatcherName);
	Payload->SetArrayField(Args::ParamsApplied, ParamsApplied);
	return FSMAssistOperationResult::MakeSuccess(Payload);
}
