// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMStateInstance.h"

#include "SMAssistGenericOpsTestClasses.generated.h"

USTRUCT(BlueprintType)
struct FSMAssistGenericInnerStruct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	int32 InnerInt = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FName InnerName;
};

USTRUCT(BlueprintType)
struct FSMAssistGenericOuterStruct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	int32 OuterInt = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FSMAssistGenericInnerStruct Nested;
};

// Fixture exercising every reflection shape the ld_ue.* property ops walk: scalar, array element,
// map element, and a nested struct chain.
UCLASS()
class USMAssistGenericOpsState : public USMStateInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	int32 ScalarInt = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	TArray<int32> IntArray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	TMap<FString, int32> IntMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	TMap<int32, int32> IntKeyMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FName NameValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	TMap<FName, int32> NameKeyMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FSMAssistGenericOuterStruct Outer;
};
