// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMStateInstance.h"
#include "SMTransitionInstance.h"

#include "SMAssistTestClasses.generated.h"

UCLASS()
class USMAssistArrayStateInstance : public USMStateInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FString SingleString;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	TArray<FString> StringArray;
};

UCLASS()
class USMAssistTestTransitionInstance : public USMTransitionInstance
{
	GENERATED_BODY()

protected:

	virtual bool CanEnterTransition_Implementation() const override
	{
		return true;
	}
};
