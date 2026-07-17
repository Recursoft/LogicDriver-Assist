// Copyright Recursoft LLC. All Rights Reserved.

#include "SMAssistSubsystem.h"
#include "Operations/SMAssistOperationInfo.h"
#include "Operations/SMAssistOperationResult.h"

#include "Helpers/SMTestHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FSMGetLocalGraphSpec, "LogicDriver.Assist.GetLocalGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	FString CreateTransientBlueprint()
	{
		USMAssistSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			return FString();
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), FGuid::NewGuid().ToString());
		Args->SetStringField(TEXT("path"), FAssetHandler::DefaultGamePath());

		const FSMAssistOperationResult Result = Subsystem->ExecuteOperation(FName(TEXT("ld.create_blueprint")), Args);
		FString AssetPath;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("asset_path"), AssetPath);
		}
		return AssetPath;
	}

	FString AddState(const FString& InAssetPath, const FString& InStateName)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("state_name"), InStateName);

		const FSMAssistOperationResult Result = GetSubsystem()->ExecuteOperation(FName(TEXT("ld.add_state")), Args);
		FString StateGuid;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
		}
		return StateGuid;
	}

	FString AddConduit(const FString& InAssetPath, const FString& InName)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("state_name"), InName);

		const FSMAssistOperationResult Result = GetSubsystem()->ExecuteOperation(FName(TEXT("ld.add_conduit")), Args);
		FString StateGuid;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
		}
		return StateGuid;
	}

	FString AddTransition(const FString& InAssetPath, const FString& InFromGuid, const FString& InToGuid)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("from_state_guid"), InFromGuid);
		Args->SetStringField(TEXT("to_state_guid"), InToGuid);

		const FSMAssistOperationResult Result = GetSubsystem()->ExecuteOperation(FName(TEXT("ld.add_transition")), Args);
		FString TransitionGuid;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("transition_guid"), TransitionGuid);
		}
		return TransitionGuid;
	}

	FString AddTransitionReroute(const FString& InAssetPath, const FString& InTransitionGuid)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("transition_guid"), InTransitionGuid);

		const FSMAssistOperationResult Result = GetSubsystem()->ExecuteOperation(FName(TEXT("ld.add_transition_reroute")), Args);
		FString RerouteGuid;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("reroute_guid"), RerouteGuid);
		}
		return RerouteGuid;
	}

	FString CollapseToStateMachine(const FString& InAssetPath, const TArray<FString>& InNodeGuids)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		TArray<TSharedPtr<FJsonValue>> GuidValues;
		for (const FString& Guid : InNodeGuids)
		{
			GuidValues.Add(MakeShared<FJsonValueString>(Guid));
		}
		Args->SetArrayField(TEXT("node_guids"), GuidValues);

		const FSMAssistOperationResult Result = GetSubsystem()->ExecuteOperation(FName(TEXT("ld.collapse_to_state_machine")), Args);
		FString StateGuid;
		if (Result.bSuccess && Result.Payload.IsValid())
		{
			Result.Payload->TryGetStringField(TEXT("state_guid"), StateGuid);
		}
		return StateGuid;
	}

	FSMAssistOperationResult RunGetLocalGraph(const FString& InAssetPath, const FString& InNodeGuid)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), InAssetPath);
		Args->SetStringField(TEXT("node_guid"), InNodeGuid);
		return GetSubsystem()->ExecuteOperation(FName(TEXT("ld.get_local_graph")), Args);
	}

	// Return the id of the node flagged is_result, or empty when none is flagged.
	static FString FindResultNodeId(const TSharedPtr<FJsonObject>& InPayload)
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!InPayload->TryGetArrayField(TEXT("nodes"), Nodes))
		{
			return FString();
		}
		for (const TSharedPtr<FJsonValue>& Value : *Nodes)
		{
			const TSharedPtr<FJsonObject>* NodeObj = nullptr;
			if (!Value->TryGetObject(NodeObj))
			{
				continue;
			}
			bool bIsResult = false;
			if ((*NodeObj)->TryGetBoolField(TEXT("is_result"), bIsResult) && bIsResult)
			{
				FString Id;
				(*NodeObj)->TryGetStringField(TEXT("id"), Id);
				return Id;
			}
		}
		return FString();
	}

	static int32 NodeCount(const TSharedPtr<FJsonObject>& InPayload)
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		return InPayload->TryGetArrayField(TEXT("nodes"), Nodes) ? Nodes->Num() : 0;
	}
END_DEFINE_SPEC(FSMGetLocalGraphSpec)

void FSMGetLocalGraphSpec::Define()
{
	It("returns a transition graph with its evaluation pin as the wire-into anchor", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString FromGuid = AddState(AssetPath, TEXT("From"));
		const FString ToGuid = AddState(AssetPath, TEXT("To"));
		const FString TransitionGuid = AddTransition(AssetPath, FromGuid, ToGuid);
		if (!TestTrue(TEXT("Transition guid populated"), !TransitionGuid.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunGetLocalGraph(AssetPath, TransitionGuid);
		if (!TestTrue(TEXT("Result success"), Result.bSuccess) || !TestTrue(TEXT("Payload valid"), Result.Payload.IsValid()))
		{
			return;
		}

		FString NodeKind;
		Result.Payload->TryGetStringField(TEXT("node_kind"), NodeKind);
		TestEqual(TEXT("node_kind is Transition"), NodeKind, FString(TEXT("Transition")));

		FString GraphName;
		TestTrue(TEXT("graph_name present"), Result.Payload->TryGetStringField(TEXT("graph_name"), GraphName) && !GraphName.IsEmpty());

		FString ResultNodeName;
		TestTrue(TEXT("result_node_name present"), Result.Payload->TryGetStringField(TEXT("result_node_name"), ResultNodeName) && !ResultNodeName.IsEmpty());

		FString ResultPinId;
		TestTrue(TEXT("result_pin_id present"), Result.Payload->TryGetStringField(TEXT("result_pin_id"), ResultPinId) && !ResultPinId.IsEmpty());

		TestTrue(TEXT("nodes non-empty"), NodeCount(Result.Payload) > 0);
		TestEqual(TEXT("flagged result node matches result_node_name"), FindResultNodeId(Result.Payload), ResultNodeName);
	});

	It("returns a state graph with its entry nodes", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString StateGuid = AddState(AssetPath, TEXT("S1"));
		if (!TestTrue(TEXT("State guid populated"), !StateGuid.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunGetLocalGraph(AssetPath, StateGuid);
		if (!TestTrue(TEXT("Result success"), Result.bSuccess) || !TestTrue(TEXT("Payload valid"), Result.Payload.IsValid()))
		{
			return;
		}

		FString NodeKind;
		Result.Payload->TryGetStringField(TEXT("node_kind"), NodeKind);
		TestEqual(TEXT("node_kind is State"), NodeKind, FString(TEXT("State")));

		TestTrue(TEXT("state graph exposes entry nodes"), NodeCount(Result.Payload) > 0);

		// A state graph has no single condition anchor.
		FString ResultNodeName;
		TestFalse(TEXT("no result_node_name on a state graph"), Result.Payload->TryGetStringField(TEXT("result_node_name"), ResultNodeName));
	});

	It("returns a conduit graph with its evaluation pin", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString ConduitGuid = AddConduit(AssetPath, TEXT("Cond"));
		if (!TestTrue(TEXT("Conduit guid populated"), !ConduitGuid.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunGetLocalGraph(AssetPath, ConduitGuid);
		if (!TestTrue(TEXT("Result success"), Result.bSuccess) || !TestTrue(TEXT("Payload valid"), Result.Payload.IsValid()))
		{
			return;
		}

		FString NodeKind;
		Result.Payload->TryGetStringField(TEXT("node_kind"), NodeKind);
		TestEqual(TEXT("node_kind is Conduit"), NodeKind, FString(TEXT("Conduit")));

		FString ResultPinId;
		TestTrue(TEXT("result_pin_id present on conduit"), Result.Payload->TryGetStringField(TEXT("result_pin_id"), ResultPinId) && !ResultPinId.IsEmpty());
	});

	It("normalizes a reroute waypoint to the primary transition graph", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString FromGuid = AddState(AssetPath, TEXT("From"));
		const FString ToGuid = AddState(AssetPath, TEXT("To"));
		const FString TransitionGuid = AddTransition(AssetPath, FromGuid, ToGuid);
		const FString RerouteGuid = AddTransitionReroute(AssetPath, TransitionGuid);
		if (!TestTrue(TEXT("Reroute guid populated"), !RerouteGuid.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunGetLocalGraph(AssetPath, RerouteGuid);
		if (!TestTrue(TEXT("Result success"), Result.bSuccess) || !TestTrue(TEXT("Payload valid"), Result.Payload.IsValid()))
		{
			return;
		}

		bool bIsRerouted = false;
		Result.Payload->TryGetBoolField(TEXT("is_rerouted"), bIsRerouted);
		TestTrue(TEXT("is_rerouted flagged"), bIsRerouted);

		// Resolution lands on the transition, not the reroute waypoint.
		FString ResolvedGuid;
		Result.Payload->TryGetStringField(TEXT("node_guid"), ResolvedGuid);
		TestFalse(TEXT("resolved node is not the reroute waypoint"), ResolvedGuid.Equals(RerouteGuid, ESearchCase::IgnoreCase));

		FString ResultPinId;
		TestTrue(TEXT("reroute resolves to a transition evaluation pin"), Result.Payload->TryGetStringField(TEXT("result_pin_id"), ResultPinId) && !ResultPinId.IsEmpty());
	});

	It("errors on an unknown node guid", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Result = RunGetLocalGraph(AssetPath, FGuid::NewGuid().ToString());
		TestFalse(TEXT("unknown guid fails"), Result.bSuccess);
	});

	It("resolves a state graph nested one level inside a collapsed sub state machine", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString StateA = AddState(AssetPath, TEXT("A"));
		const FString StateB = AddState(AssetPath, TEXT("B"));
		AddTransition(AssetPath, StateA, StateB);

		// Collapsing preserves each collapsed node's guid; A now lives inside the container's bound graph.
		const FString Container = CollapseToStateMachine(AssetPath, { StateA, StateB });
		if (!TestTrue(TEXT("Container created"), !Container.IsEmpty()))
		{
			return;
		}

		// FindNodeByGuid must descend into the nested state machine to resolve a node one level deep.
		const FSMAssistOperationResult Result = RunGetLocalGraph(AssetPath, StateA);
		if (!TestTrue(TEXT("nested state resolves"), Result.bSuccess))
		{
			return;
		}
		FString NodeKind;
		Result.Payload->TryGetStringField(TEXT("node_kind"), NodeKind);
		TestEqual(TEXT("resolved node is the nested State"), NodeKind, FString(TEXT("State")));
	});

	It("resolves a node nested two levels deep", [this]()
	{
		const FString AssetPath = CreateTransientBlueprint();
		if (!TestTrue(TEXT("Blueprint created"), !AssetPath.IsEmpty()))
		{
			return;
		}

		const FString StateA = AddState(AssetPath, TEXT("A"));
		const FString StateB = AddState(AssetPath, TEXT("B"));
		AddTransition(AssetPath, StateA, StateB);
		const FString Inner = CollapseToStateMachine(AssetPath, { StateA, StateB });
		if (!TestTrue(TEXT("Inner container created"), !Inner.IsEmpty()))
		{
			return;
		}

		const FString StateC = AddState(AssetPath, TEXT("C"));
		AddTransition(AssetPath, Inner, StateC);
		const FString Outer = CollapseToStateMachine(AssetPath, { Inner, StateC });
		if (!TestTrue(TEXT("Outer container created"), !Outer.IsEmpty()))
		{
			return;
		}

		// Outer (root) -> Inner (depth 1) -> A (depth 2). The nested sub-machine and the leaf both resolve.
		TestTrue(TEXT("sub-machine one level deep resolves"), RunGetLocalGraph(AssetPath, Inner).bSuccess);
		TestTrue(TEXT("state two levels deep resolves"), RunGetLocalGraph(AssetPath, StateA).bSuccess);
	});
}

#endif

#endif
