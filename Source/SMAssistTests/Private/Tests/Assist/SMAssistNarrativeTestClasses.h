// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMStateInstance.h"
#include "SMStateMachineInstance.h"
#include "SMTextGraphProperty.h"
#include "SMTransitionInstance.h"

#include "SMAssistNarrativeTestClasses.generated.h"

// Headless twins of the live narrative capstone classes. The live system stores
// shared conversation state on an actor component; these fixtures store it on the
// state machine context so the same topology runs without a world.
UCLASS()
class USMAssistNarrativeContext : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY()
	int32 VisitCount = 0;

	UPROPERTY()
	int32 ChoiceIndex = 0;

	UPROPERTY()
	FString Breadcrumbs;

	void RecordVisit(const FString& InName)
	{
		Breadcrumbs += TEXT(",") + InName;
	}
};

UCLASS()
class USMAssistNarrativeLineState : public USMStateInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FSMTextGraphProperty Line;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	FName SpeakerTag;

protected:

	virtual void OnStateBegin_Implementation() override
	{
		if (USMAssistNarrativeContext* Ctx = Cast<USMAssistNarrativeContext>(GetContext()))
		{
			Ctx->RecordVisit(GetNodeName());
		}
	}
};

UCLASS()
class USMAssistKeeperLineState : public USMAssistNarrativeLineState
{
	GENERATED_BODY()

public:

	USMAssistKeeperLineState()
	{
		SpeakerTag = TEXT("Keeper");
	}
};

UCLASS()
class USMAssistSailorLineState : public USMAssistNarrativeLineState
{
	GENERATED_BODY()

public:

	USMAssistSailorLineState()
	{
		SpeakerTag = TEXT("Sailor");
	}
};

// Scripted director: each hub entry selects the choice matching the visit count,
// so a single run walks every branch deterministically.
UCLASS()
class USMAssistNarrativeDirectorState : public USMStateInstance
{
	GENERATED_BODY()

protected:

	virtual void OnStateBegin_Implementation() override
	{
		if (USMAssistNarrativeContext* Ctx = Cast<USMAssistNarrativeContext>(GetContext()))
		{
			Ctx->ChoiceIndex = Ctx->VisitCount;
		}
	}
};

UCLASS()
class USMAssistMarkVisitState : public USMStateInstance
{
	GENERATED_BODY()

protected:

	virtual void OnStateBegin_Implementation() override
	{
		if (USMAssistNarrativeContext* Ctx = Cast<USMAssistNarrativeContext>(GetContext()))
		{
			++Ctx->VisitCount;
			Ctx->RecordVisit(GetNodeName());
		}
	}
};

UCLASS()
class USMAssistChoiceGateTransition : public USMTransitionInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	int32 RequiredChoice = 0;

protected:

	virtual bool CanEnterTransition_Implementation() const override
	{
		const USMAssistNarrativeContext* Ctx = Cast<USMAssistNarrativeContext>(GetContext());
		return Ctx && Ctx->ChoiceIndex == RequiredChoice;
	}
};

UCLASS()
class USMAssistVisitGateTransition : public USMTransitionInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	bool bRequireReturning = false;

protected:

	virtual bool CanEnterTransition_Implementation() const override
	{
		const USMAssistNarrativeContext* Ctx = Cast<USMAssistNarrativeContext>(GetContext());
		return Ctx && (Ctx->VisitCount > 0) == bRequireReturning;
	}
};

UCLASS()
class USMAssistAdvanceAfterLineTransition : public USMTransitionInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Test)
	float MinSeconds = 0.6f;

protected:

	virtual bool CanEnterTransition_Implementation() const override
	{
		const USMStateInstance_Base* Previous = GetPreviousStateInstance();
		return Previous && Previous->GetTimeInState() >= MinSeconds;
	}
};

UCLASS()
class USMAssistSubDoneGateTransition : public USMTransitionInstance
{
	GENERATED_BODY()

protected:

	virtual bool CanEnterTransition_Implementation() const override
	{
		const USMStateMachineInstance* SubMachine = Cast<USMStateMachineInstance>(GetPreviousStateInstance());
		return SubMachine && SubMachine->IsStateMachineInEndState();
	}
};
