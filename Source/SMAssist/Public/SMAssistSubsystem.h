// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"

#include "SMAssistSubsystem.generated.h"

class FJsonObject;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSMAssistOperationRegisteredSignature, const FSMAssistOperationInfo& /* InInfo */);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSMAssistOperationUnregisteredSignature, FName /* InName */);

UCLASS()
class SMASSIST_API USMAssistSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& InCollection) override;
	virtual void Deinitialize() override;

	bool RegisterOperation(FSMAssistOperationInfo InInfo);

	bool UnregisterOperation(FName InName);

	bool HasOperation(FName InName) const;

	FSMAssistOperationResult ExecuteOperation(FName InName, const TSharedRef<FJsonObject>& InArgs);

	TArray<FName> GetRegisteredOperationNames() const;

	const FSMAssistOperationInfo* FindOperationInfo(FName InName) const;

	TArray<FSMAssistOperationInfo> GetAllOperationInfos() const;

	/**
	 * Fires whenever an operation is registered. External bridges should bind to this
	 * and iterate GetAllOperationInfos() once on bind to catch any operations that were
	 * already present.
	 */
	FOnSMAssistOperationRegisteredSignature& OnOperationRegistered() { return OnOperationRegisteredDelegate; }

	/** Fires whenever an operation is unregistered. */
	FOnSMAssistOperationUnregisteredSignature& OnOperationUnregistered() { return OnOperationUnregisteredDelegate; }

private:
	void RegisterBuiltInOperations();

	/** Registers the ld_ue.* generic fallback ops. */
	void RegisterGenericFallbackOperations();

private:
	TMap<FName, FSMAssistOperationInfo> Operations;

	FOnSMAssistOperationRegisteredSignature OnOperationRegisteredDelegate;

	FOnSMAssistOperationUnregisteredSignature OnOperationUnregisteredDelegate;
};
