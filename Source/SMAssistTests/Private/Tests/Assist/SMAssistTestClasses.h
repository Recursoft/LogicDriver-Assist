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

UCLASS()
class USMAssistFalseTransitionInstance : public USMTransitionInstance
{
	GENERATED_BODY()

protected:

	virtual bool CanEnterTransition_Implementation() const override
	{
		return false;
	}
};

USTRUCT(BlueprintType)
struct FSMAssistSplitInnerStruct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	int32 InnerInt = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	float InnerFloat = 0.0f;
};

USTRUCT(BlueprintType)
struct FSMAssistSplitOuterStruct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	int32 OuterInt = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FSMAssistSplitInnerStruct NestedStruct;
};

UCLASS()
class USMAssistSplitTestState : public USMStateInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FSMAssistSplitOuterStruct OurStruct;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FText NonSplittableText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	TArray<FSMAssistSplitOuterStruct> StructArray;
};
