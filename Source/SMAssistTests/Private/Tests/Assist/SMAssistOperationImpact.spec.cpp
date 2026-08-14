// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistGenericOpKeys.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOpKeys.h"

#include "Containers/Set.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FAssistOperationImpactSpec, "LogicDriver.Assist.OperationImpact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	// Only the operations this plugin owns. A consumer project may register its own into the same
	// subsystem, and those are not this spec's to classify.
	static bool IsBuiltIn(FName InName)
	{
		const FString NameStr = InName.ToString();
		return NameStr.StartsWith(TEXT("ld.")) || NameStr.StartsWith(TEXT("ld_ue."));
	}

	// Clients may auto-approve these, so this list is the security boundary and is pinned exactly.
	// Everything else must be Destructive, which is also the field default, so an operation added
	// without a deliberate classification fails safe rather than joining this set by accident.
	static TSet<FName> ExpectedReadOnly()
	{
		namespace Ops = LD::Assist::Ops;
		namespace GOps = LD::Assist::GenericOps::Ops;

		return TSet<FName>
		{
			Ops::ListAssets,
			Ops::GetAsset,
			Ops::GetNodeProperties,
			Ops::GetPropertyPins,
			Ops::GetPropertyGraph,
			Ops::GetLocalGraph,
			Ops::GetGraphView,
			Ops::FindNodeTypes,
			Ops::RuntimeGetState,
			GOps::ReadProperty
		};
	}

END_DEFINE_SPEC(FAssistOperationImpactSpec)

void FAssistOperationImpactSpec::Define()
{
	It("Marks exactly the pinned operations read-only", [this]()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!TestNotNull("Assist subsystem available", Subsystem))
		{
			return;
		}

		const TSet<FName> Expected = ExpectedReadOnly();
		TSet<FName> Actual;

		for (const FSMAssistOperationInfo& Info : Subsystem->GetAllOperationInfos())
		{
			if (!IsBuiltIn(Info.Name))
			{
				continue;
			}

			if (Info.Impact == ESMAssistOperationImpact::ReadOnly)
			{
				Actual.Add(Info.Name);
			}
			else if (Info.Impact != ESMAssistOperationImpact::Destructive)
			{
				// Fires if a third impact value is introduced without revisiting this spec.
				AddError(FString::Printf(TEXT("'%s' has an unhandled impact value %u."),
					*Info.Name.ToString(), static_cast<uint8>(Info.Impact)));
			}
		}

		for (const FName& Name : Actual.Difference(Expected))
		{
			AddError(FString::Printf(
				TEXT("'%s' is marked read-only but is not pinned here. Auto-approval means it runs unattended: ")
				TEXT("confirm nothing in its call graph mutates state, then add it."), *Name.ToString()));
		}

		for (const FName& Name : Expected.Difference(Actual))
		{
			AddError(FString::Printf(TEXT("'%s' is pinned read-only but is not registered as read-only."),
				*Name.ToString()));
		}
	});

	It("Defaults an unclassified operation to the safest impact", [this]()
	{
		const FSMAssistOperationInfo Defaults;
		TestTrue("Default impact is Destructive",
			Defaults.Impact == ESMAssistOperationImpact::Destructive);
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
