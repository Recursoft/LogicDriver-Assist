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

BEGIN_DEFINE_SPEC(FSMLocalGraphWriteSpec, "LogicDriver.Assist.LocalGraphWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

	USMAssistSubsystem* GetSubsystem() const
	{
		return GEditor ? GEditor->GetEditorSubsystem<USMAssistSubsystem>() : nullptr;
	}

	FSMAssistOperationResult Run(const TCHAR* InOp, const TSharedRef<FJsonObject>& InArgs)
	{
		return GetSubsystem()->ExecuteOperation(FName(InOp), InArgs);
	}

	static TSharedRef<FJsonObject> Obj(std::initializer_list<TPair<FString, FString>> InFields)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Field : InFields)
		{
			Args->SetStringField(Field.Key, Field.Value);
		}
		return Args;
	}

	FString Str(const FSMAssistOperationResult& InResult, const TCHAR* InField)
	{
		FString Value;
		if (InResult.Payload.IsValid())
		{
			InResult.Payload->TryGetStringField(InField, Value);
		}
		return Value;
	}

	FString CreateBlueprint()
	{
		const FSMAssistOperationResult R = Run(TEXT("ld.create_blueprint"),
			Obj({ { TEXT("name"), FGuid::NewGuid().ToString() }, { TEXT("path"), FAssetHandler::DefaultGamePath() } }));
		return Str(R, TEXT("asset_path"));
	}

	FString AddState(const FString& InAsset, const FString& InName)
	{
		return Str(Run(TEXT("ld.add_state"), Obj({ { TEXT("asset_path"), InAsset }, { TEXT("state_name"), InName } })), TEXT("state_guid"));
	}

	FString AddTransition(const FString& InAsset, const FString& InFrom, const FString& InTo)
	{
		return Str(Run(TEXT("ld.add_transition"),
			Obj({ { TEXT("asset_path"), InAsset }, { TEXT("from_state_guid"), InFrom }, { TEXT("to_state_guid"), InTo } })), TEXT("transition_guid"));
	}

	// Find a node in a get_local_graph payload by a class-name substring; return its id and first output pin.
	static bool FindNodeByClass(const TSharedPtr<FJsonObject>& InPayload, const FString& InClassSubstr, FString& OutId, FString& OutFirstOutputPin)
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!InPayload.IsValid() || !InPayload->TryGetArrayField(TEXT("nodes"), Nodes))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Nodes)
		{
			const TSharedPtr<FJsonObject>* Node = nullptr;
			if (!Value->TryGetObject(Node))
			{
				continue;
			}
			FString Class;
			(*Node)->TryGetStringField(TEXT("class"), Class);
			if (!Class.Contains(InClassSubstr))
			{
				continue;
			}
			(*Node)->TryGetStringField(TEXT("id"), OutId);
			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if ((*Node)->TryGetArrayField(TEXT("pins"), Pins))
			{
				for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
				{
					const TSharedPtr<FJsonObject>* Pin = nullptr;
					if (PinValue->TryGetObject(Pin))
					{
						FString Dir;
						(*Pin)->TryGetStringField(TEXT("direction"), Dir);
						if (Dir == TEXT("output"))
						{
							(*Pin)->TryGetStringField(TEXT("name"), OutFirstOutputPin);
							break;
						}
					}
				}
			}
			return true;
		}
		return false;
	}

	// First output pin name from a payload that carries a top-level pins array (add/spawn node results).
	static FString FirstOutputPin(const TSharedPtr<FJsonObject>& InPayload)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
		if (!InPayload.IsValid() || !InPayload->TryGetArrayField(TEXT("pins"), Pins))
		{
			return FString();
		}
		for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
		{
			const TSharedPtr<FJsonObject>* Pin = nullptr;
			if (PinValue->TryGetObject(Pin))
			{
				FString Dir;
				(*Pin)->TryGetStringField(TEXT("direction"), Dir);
				if (Dir == TEXT("output"))
				{
					FString Name;
					(*Pin)->TryGetStringField(TEXT("name"), Name);
					return Name;
				}
			}
		}
		return FString();
	}

	// Does the transition result pin report a connection back to the given source node id?
	static bool ResultPinConnectedFrom(const TSharedPtr<FJsonObject>& InPayload, const FString& InSourceNodeId)
	{
		FString ResultNodeName;
		InPayload->TryGetStringField(TEXT("result_node_name"), ResultNodeName);
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!InPayload->TryGetArrayField(TEXT("nodes"), Nodes))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Nodes)
		{
			const TSharedPtr<FJsonObject>* Node = nullptr;
			if (!Value->TryGetObject(Node))
			{
				continue;
			}
			FString Id;
			(*Node)->TryGetStringField(TEXT("id"), Id);
			if (Id != ResultNodeName)
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if ((*Node)->TryGetArrayField(TEXT("pins"), Pins))
			{
				for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
				{
					const TSharedPtr<FJsonObject>* Pin = nullptr;
					if (!PinValue->TryGetObject(Pin))
					{
						continue;
					}
					const TArray<TSharedPtr<FJsonValue>>* Conn = nullptr;
					if ((*Pin)->TryGetArrayField(TEXT("connected_to"), Conn))
					{
						for (const TSharedPtr<FJsonValue>& ConnValue : *Conn)
						{
							if (ConnValue->AsString().StartsWith(InSourceNodeId + TEXT(".")))
							{
								return true;
							}
						}
					}
				}
			}
		}
		return false;
	}
END_DEFINE_SPEC(FSMLocalGraphWriteSpec)

void FSMLocalGraphWriteSpec::Define()
{
	It("adds a call_function node into a transition graph and reports its pins", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);
		if (!TestTrue(TEXT("transition created"), !Trans.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Add = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("call_function") }, { TEXT("function_name"), TEXT("Greater_DoubleDouble") } }));
		if (!TestTrue(TEXT("add success"), Add.bSuccess) || !TestTrue(TEXT("payload valid"), Add.Payload.IsValid()))
		{
			return;
		}
		TestFalse(TEXT("node id returned"), Str(Add, TEXT("id")).IsEmpty());
		TestEqual(TEXT("function echoed"), Str(Add, TEXT("function")), FString(TEXT("Greater_DoubleDouble")));

		const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
		TestTrue(TEXT("node reports pins"), Add.Payload->TryGetArrayField(TEXT("pins"), Pins) && Pins->Num() >= 3);
	});

	It("spawns an arbitrary K2 node by class name", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		// A node type with no friendly alias, addressed by its class name; needs no config.
		const FSMAssistOperationResult Seq = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_class"), TEXT("K2Node_ExecutionSequence") } }));
		if (!TestTrue(TEXT("sequence added"), Seq.bSuccess) || !TestTrue(TEXT("payload valid"), Seq.Payload.IsValid()))
		{
			return;
		}
		TestEqual(TEXT("class echoed"), Str(Seq, TEXT("node_class")), FString(TEXT("K2Node_ExecutionSequence")));

		// A friendly alias resolves to the concrete class.
		const FSMAssistOperationResult Branch = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_class"), TEXT("branch") } }));
		TestTrue(TEXT("branch added"), Branch.bSuccess);
		TestEqual(TEXT("branch resolves to IfThenElse"), Str(Branch, TEXT("node_class")), FString(TEXT("K2Node_IfThenElse")));
	});

	It("errors when node_class is unresolvable", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult R = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_class"), TEXT("K2Node_NotARealNodeXYZ") } }));
		TestFalse(TEXT("unknown node class fails"), R.bSuccess);
	});

	// Regression: an AnimGraphNode_* class passed the UK2Node gate, then PostPlacedNewNode
	// CastChecked'd the owning blueprint to UAnimBlueprint and crashed the editor.
	It("rejects a schema-foreign node class cleanly", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);
		if (!TestTrue(TEXT("transition created"), !Trans.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult R = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("AnimGraphNode_SequencePlayer") } }));
		TestFalse(TEXT("anim node class fails"), R.bSuccess);
		TestTrue(TEXT("error reports incompatibility"), R.ErrorMessage.Contains(TEXT("not compatible")));
	});

	It("rejects a Logic Driver structural node class cleanly", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);
		if (!TestTrue(TEXT("transition created"), !Trans.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult R = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("SMGraphK2Node_StateMachineNode") } }));
		TestFalse(TEXT("structural node class fails"), R.bSuccess);
		TestTrue(TEXT("error reports the structural rejection"), R.ErrorMessage.Contains(TEXT("structural")));
	});

	// Regression: a nested state machine's bound graph is a USMGraph whose schema forbids plain K2
	// nodes; spawning one there corrupted the asset at compile.
	It("rejects spawning a K2 node into a state machine graph cleanly", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString A = AddState(Asset, TEXT("A"));
		const FString B = AddState(Asset, TEXT("B"));
		const FString C = AddState(Asset, TEXT("C"));

		TArray<TSharedPtr<FJsonValue>> NodeGuids;
		NodeGuids.Add(MakeShared<FJsonValueString>(B));
		NodeGuids.Add(MakeShared<FJsonValueString>(C));
		const TSharedRef<FJsonObject> CollapseArgs = MakeShared<FJsonObject>();
		CollapseArgs->SetStringField(TEXT("asset_path"), Asset);
		CollapseArgs->SetArrayField(TEXT("node_guids"), NodeGuids);
		const FSMAssistOperationResult Collapse = Run(TEXT("ld.collapse_to_state_machine"), CollapseArgs);
		if (!TestTrue(TEXT("collapse succeeded"), Collapse.bSuccess))
		{
			return;
		}
		const FString ContainerGuid = Str(Collapse, TEXT("state_guid"));

		const FSMAssistOperationResult R = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), ContainerGuid },
				  { TEXT("node_class"), TEXT("K2Node_ExecutionSequence") } }));
		TestFalse(TEXT("K2 node into state machine graph fails"), R.bSuccess);
		TestTrue(TEXT("error reports the state machine graph target"),
			R.ErrorMessage.Contains(TEXT("state machine graph")));
	});

	It("errors when the function cannot be resolved", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult Add = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("call_function") }, { TEXT("function_name"), TEXT("ThisFunctionDoesNotExist_XYZ") } }));
		TestFalse(TEXT("unresolved function fails"), Add.bSuccess);
	});

	It("builds a time-in-state gate end to end that compiles clean", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString Red = AddState(Asset, TEXT("Red"));
		const FString Green = AddState(Asset, TEXT("Green"));
		Run(TEXT("ld.set_initial_state"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("state_guid"), Red } }));
		const FString Trans = AddTransition(Asset, Red, Green);
		if (!TestTrue(TEXT("transition created"), !Trans.IsEmpty()))
		{
			return;
		}

		// The wire-into anchor.
		const FSMAssistOperationResult Before = Run(TEXT("ld.get_local_graph"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans } }));
		const FString ResultNode = Str(Before, TEXT("result_node_name"));
		const FString ResultPin = Str(Before, TEXT("result_pin_name"));
		if (!TestTrue(TEXT("result anchor present"), !ResultNode.IsEmpty() && !ResultPin.IsEmpty()))
		{
			return;
		}

		// LD special: TimeInState read node.
		const FSMAssistOperationResult Tis = Run(TEXT("ld.spawn_local_graph_read_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("type"), TEXT("TimeInState") } }));
		if (!TestTrue(TEXT("TimeInState spawned"), Tis.bSuccess))
		{
			return;
		}

		const FSMAssistOperationResult Mid = Run(TEXT("ld.get_local_graph"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans } }));
		FString TisId;
		FString TisOut;
		if (!TestTrue(TEXT("TimeInState node found"), FindNodeByClass(Mid.Payload, TEXT("TimeInState"), TisId, TisOut)))
		{
			return;
		}

		// Regression guard: the spawn payload itself carries id + pins (matching add_local_graph_node),
		// so wiring needs no intervening get_local_graph. It must match the authoritative re-read above.
		TestEqual(TEXT("spawn read node payload exposes id"), Str(Tis, TEXT("id")), TisId);
		TestEqual(TEXT("spawn read node payload exposes output pin"), FirstOutputPin(Tis.Payload), TisOut);

		// Generic comparison node via the write op.
		const FSMAssistOperationResult Add = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("call_function") }, { TEXT("function_name"), TEXT("Greater_DoubleDouble") } }));
		const FString GreaterId = Str(Add, TEXT("id"));
		if (!TestTrue(TEXT("Greater added"), !GreaterId.IsEmpty()))
		{
			return;
		}

		// Wire and seed.
		const FSMAssistOperationResult C1 = Run(TEXT("ld.connect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("from_node_id"), TisId }, { TEXT("from_pin"), TisOut }, { TEXT("to_node_id"), GreaterId }, { TEXT("to_pin"), TEXT("A") } }));
		TestTrue(TEXT("TimeInState -> Greater.A connected"), C1.bSuccess);

		const FSMAssistOperationResult SetB = Run(TEXT("ld.set_local_graph_pin_default"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_id"), GreaterId }, { TEXT("pin"), TEXT("B") }, { TEXT("value"), TEXT("2.5") } }));
		TestTrue(TEXT("Greater.B default set"), SetB.bSuccess);
		TestEqual(TEXT("Greater.B default is 2.5"), Str(SetB, TEXT("value")), FString(TEXT("2.5")));

		const FSMAssistOperationResult C2 = Run(TEXT("ld.connect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("from_node_id"), GreaterId }, { TEXT("from_pin"), TEXT("ReturnValue") }, { TEXT("to_node_id"), ResultNode }, { TEXT("to_pin"), ResultPin } }));
		TestTrue(TEXT("Greater.ReturnValue -> result connected"), C2.bSuccess);

		// Compiles clean.
		const FSMAssistOperationResult Compile = Run(TEXT("ld.compile"), Obj({ { TEXT("asset_path"), Asset } }));
		if (!TestTrue(TEXT("compile success"), Compile.bSuccess))
		{
			return;
		}
		bool bHasErrors = true;
		Compile.Payload->TryGetBoolField(TEXT("has_errors"), bHasErrors);
		TestFalse(TEXT("compiles with no errors"), bHasErrors);

		// Read the wiring back.
		const FSMAssistOperationResult After = Run(TEXT("ld.get_local_graph"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans } }));
		TestTrue(TEXT("result pin wired from the comparison"), ResultPinConnectedFrom(After.Payload, GreaterId));
	});

	It("spawns an LD read node into a rerouted transition's graph", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString A = AddState(Asset, TEXT("A"));
		const FString B = AddState(Asset, TEXT("B"));
		Run(TEXT("ld.set_initial_state"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("state_guid"), A } }));
		const FString Trans = AddTransition(Asset, A, B);

		const FSMAssistOperationResult RR = Run(TEXT("ld.add_transition_reroute"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("transition_guid"), Trans } }));
		const FString Reroute = Str(RR, TEXT("reroute_guid"));
		if (!TestTrue(TEXT("reroute created"), !Reroute.IsEmpty()))
		{
			return;
		}

		// The reroute waypoint owns no graph, so every local-graph op normalizes it to the primary transition.
		const FSMAssistOperationResult Gl = Run(TEXT("ld.get_local_graph"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Reroute } }));
		TestEqual(TEXT("reroute normalizes to primary transition"), Str(Gl, TEXT("node_guid")), Trans);

		// Regression: spawn read must accept the same reroute guid the generic add/connect ops do.
		const FSMAssistOperationResult Tis = Run(TEXT("ld.spawn_local_graph_read_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Reroute }, { TEXT("type"), TEXT("TimeInState") } }));
		if (!TestTrue(TEXT("spawn read node via reroute guid succeeds"), Tis.bSuccess))
		{
			return;
		}
		TestFalse(TEXT("spawn via reroute returns id"), Str(Tis, TEXT("id")).IsEmpty());
		TestFalse(TEXT("spawn via reroute returns output pin"), FirstOutputPin(Tis.Payload).IsEmpty());
	});

	It("errors connecting to an unknown node id", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult C = Run(TEXT("ld.connect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("from_node_id"), TEXT("NoSuchNode") }, { TEXT("from_pin"), TEXT("ReturnValue") },
				  { TEXT("to_node_id"), TEXT("AlsoNoSuchNode") }, { TEXT("to_pin"), TEXT("bCanEnterTransition") } }));
		TestFalse(TEXT("unknown node fails"), C.bSuccess);
	});

	It("removes a spawned node by id and refuses structural root nodes", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult Add = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("call_function") }, { TEXT("function_name"), TEXT("Greater_DoubleDouble") } }));
		const FString AddedId = Str(Add, TEXT("id"));
		if (!TestTrue(TEXT("node added"), !AddedId.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Remove = Run(TEXT("ld.remove_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_id"), AddedId } }));
		TestTrue(TEXT("remove succeeds"), Remove.bSuccess);

		const FSMAssistOperationResult After = Run(TEXT("ld.get_local_graph"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans } }));
		FString Ignore1;
		FString Ignore2;
		TestFalse(TEXT("removed node no longer present"), FindNodeByClass(After.Payload, TEXT("K2Node_CallFunction"), Ignore1, Ignore2));

		// The result node is a structural root and must be refused.
		const FString ResultNode = Str(After, TEXT("result_node_name"));
		const FSMAssistOperationResult RemoveRoot = Run(TEXT("ld.remove_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_id"), ResultNode } }));
		TestFalse(TEXT("result node cannot be removed"), RemoveRoot.bSuccess);
	});

	It("disconnects a wired pin", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult Before = Run(TEXT("ld.get_local_graph"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans } }));
		const FString ResultNode = Str(Before, TEXT("result_node_name"));
		const FString ResultPin = Str(Before, TEXT("result_pin_name"));

		const FSMAssistOperationResult Add = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("call_function") }, { TEXT("function_name"), TEXT("Greater_DoubleDouble") } }));
		const FString GreaterId = Str(Add, TEXT("id"));

		const FSMAssistOperationResult C = Run(TEXT("ld.connect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("from_node_id"), GreaterId }, { TEXT("from_pin"), TEXT("ReturnValue") }, { TEXT("to_node_id"), ResultNode }, { TEXT("to_pin"), ResultPin } }));
		if (!TestTrue(TEXT("connected"), C.bSuccess))
		{
			return;
		}

		const FSMAssistOperationResult D = Run(TEXT("ld.disconnect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("from_node_id"), GreaterId }, { TEXT("from_pin"), TEXT("ReturnValue") }, { TEXT("to_node_id"), ResultNode }, { TEXT("to_pin"), ResultPin } }));
		if (!TestTrue(TEXT("disconnect succeeds"), D.bSuccess))
		{
			return;
		}
		bool bDisconnected = false;
		D.Payload->TryGetBoolField(TEXT("disconnected"), bDisconnected);
		TestTrue(TEXT("reports disconnected"), bDisconnected);

		const FSMAssistOperationResult After = Run(TEXT("ld.get_local_graph"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans } }));
		TestFalse(TEXT("result pin no longer wired"), ResultPinConnectedFrom(After.Payload, GreaterId));
	});

	It("repositions, comments and disables a node", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult Add = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_class"), TEXT("branch") } }));
		const FString NodeId = Str(Add, TEXT("id"));
		if (!TestTrue(TEXT("node added"), !NodeId.IsEmpty()))
		{
			return;
		}

		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), Asset);
		Args->SetStringField(TEXT("node_guid"), Trans);
		Args->SetStringField(TEXT("node_id"), NodeId);
		Args->SetNumberField(TEXT("position_x"), 123.0);
		Args->SetNumberField(TEXT("position_y"), 456.0);
		Args->SetStringField(TEXT("comment"), TEXT("gate check"));
		Args->SetBoolField(TEXT("enabled"), false);
		const FSMAssistOperationResult Set = Run(TEXT("ld.set_local_graph_node"), Args);
		if (!TestTrue(TEXT("set succeeds"), Set.bSuccess) || !TestTrue(TEXT("payload valid"), Set.Payload.IsValid()))
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
		if (TestTrue(TEXT("pos present"), Set.Payload->TryGetArrayField(TEXT("pos"), Pos) && Pos->Num() == 2))
		{
			TestEqual(TEXT("x moved"), static_cast<int32>((*Pos)[0]->AsNumber()), 123);
			TestEqual(TEXT("y moved"), static_cast<int32>((*Pos)[1]->AsNumber()), 456);
		}
		TestEqual(TEXT("comment set"), Str(Set, TEXT("comment")), FString(TEXT("gate check")));
		bool bEnabled = true;
		Set.Payload->TryGetBoolField(TEXT("enabled"), bEnabled);
		TestFalse(TEXT("node reports disabled"), bEnabled);
	});

	It("errors when set_local_graph_node has nothing to change", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult Add = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_class"), TEXT("branch") } }));
		const FString NodeId = Str(Add, TEXT("id"));

		const FSMAssistOperationResult Set = Run(TEXT("ld.set_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_id"), NodeId } }));
		TestFalse(TEXT("no-op set fails"), Set.bSuccess);
	});

	It("spawns a variable getter for a compiled FSM variable and rejects an unknown one", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		Run(TEXT("ld.add_sm_variable"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("variable_name"), TEXT("Threshold") }, { TEXT("var_type"), TEXT("float") } }));
		Run(TEXT("ld.compile"), Obj({ { TEXT("asset_path"), Asset } }));

		const FSMAssistOperationResult Get = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("get_variable") }, { TEXT("variable_name"), TEXT("Threshold") } }));
		if (!TestTrue(TEXT("getter added"), Get.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("resolves to VariableGet"), Str(Get, TEXT("node_class")), FString(TEXT("K2Node_VariableGet")));

		const FSMAssistOperationResult Missing = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("get_variable") }, { TEXT("variable_name"), TEXT("NoSuchVar") } }));
		TestFalse(TEXT("unknown variable rejected"), Missing.bSuccess);
	});

	It("spawns a cast with a target class and rejects one without", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult Cast = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("cast") }, { TEXT("target_class"), TEXT("/Script/Engine.Actor") } }));
		if (!TestTrue(TEXT("cast added"), Cast.bSuccess))
		{
			return;
		}
		TestEqual(TEXT("resolves to DynamicCast"), Str(Cast, TEXT("node_class")), FString(TEXT("K2Node_DynamicCast")));

		const FSMAssistOperationResult NoTarget = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_class"), TEXT("cast") } }));
		TestFalse(TEXT("cast without target_class rejected"), NoTarget.bSuccess);
	});

	It("disconnect reports false when the pins were not linked", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult Add = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("node_class"), TEXT("call_function") }, { TEXT("function_name"), TEXT("Greater_DoubleDouble") } }));
		const FString GreaterId = Str(Add, TEXT("id"));

		const FSMAssistOperationResult Before = Run(TEXT("ld.get_local_graph"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans } }));
		const FString ResultNode = Str(Before, TEXT("result_node_name"));
		const FString ResultPin = Str(Before, TEXT("result_pin_name"));

		// These pins were never connected.
		const FSMAssistOperationResult D = Run(TEXT("ld.disconnect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("from_node_id"), GreaterId }, { TEXT("from_pin"), TEXT("ReturnValue") }, { TEXT("to_node_id"), ResultNode }, { TEXT("to_pin"), ResultPin } }));
		if (!TestTrue(TEXT("disconnect op succeeds"), D.bSuccess))
		{
			return;
		}
		bool bDisconnected = true;
		D.Payload->TryGetBoolField(TEXT("disconnected"), bDisconnected);
		TestFalse(TEXT("not-linked reports disconnected=false"), bDisconnected);
	});

	It("rejects a type-incompatible connection", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult B1 = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_class"), TEXT("branch") } }));
		const FSMAssistOperationResult B2 = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("node_class"), TEXT("branch") } }));
		const FString Branch1 = Str(B1, TEXT("id"));
		const FString Branch2 = Str(B2, TEXT("id"));
		if (!TestTrue(TEXT("branches added"), !Branch1.IsEmpty() && !Branch2.IsEmpty()))
		{
			return;
		}

		// Exec output 'then' into bool input 'Condition' is type-incompatible; the schema must refuse it.
		const FSMAssistOperationResult C = Run(TEXT("ld.connect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans },
				  { TEXT("from_node_id"), Branch1 }, { TEXT("from_pin"), TEXT("then") }, { TEXT("to_node_id"), Branch2 }, { TEXT("to_pin"), TEXT("Condition") } }));
		TestFalse(TEXT("incompatible connection rejected"), C.bSuccess);
	});

	It("spawns an OnInitialized event node into a transition graph and compiles clean", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		Run(TEXT("ld.set_initial_state"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("state_guid"), From } }));
		const FString Trans = AddTransition(Asset, From, To);
		if (!TestTrue(TEXT("transition created"), !Trans.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Ev = Run(TEXT("ld.spawn_local_graph_event_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("type"), TEXT("OnInitialized") } }));
		if (!TestTrue(TEXT("event node spawned"), Ev.bSuccess))
		{
			return;
		}
		TestFalse(TEXT("event node returns id"), Str(Ev, TEXT("id")).IsEmpty());
		TestFalse(TEXT("event node returns node_guid"), Str(Ev, TEXT("node_guid")).IsEmpty());

		const FSMAssistOperationResult Graph = Run(TEXT("ld.get_local_graph"), Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans } }));
		FString Id;
		FString Ignore;
		TestTrue(TEXT("OnTransitionInitialized node present in graph"),
			FindNodeByClass(Graph.Payload, TEXT("TransitionInitialized"), Id, Ignore));

		const FSMAssistOperationResult Compile = Run(TEXT("ld.compile"), Obj({ { TEXT("asset_path"), Asset } }));
		if (!TestTrue(TEXT("compile success"), Compile.bSuccess))
		{
			return;
		}
		bool bHasErrors = true;
		Compile.Payload->TryGetBoolField(TEXT("has_errors"), bHasErrors);
		TestFalse(TEXT("event node compiles with no errors"), bHasErrors);
	});

	It("refuses the state lifecycle events a state graph already owns and stays compilable", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString State = AddState(Asset, TEXT("Solo"));
		if (!TestTrue(TEXT("state created"), !State.IsEmpty()))
		{
			return;
		}

		// All three are named as non-spawnable in the op's own schema, so none may come back as a typo.
		for (const TCHAR* Type : { TEXT("OnStateBegin"), TEXT("on_state_update"), TEXT("OnStateEnd") })
		{
			const FSMAssistOperationResult Ev = Run(TEXT("ld.spawn_local_graph_event_node"),
				Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), State }, { TEXT("type"), Type } }));
			TestFalse(FString::Printf(TEXT("%s refused"), Type), Ev.bSuccess);
			TestFalse(FString::Printf(TEXT("%s is not treated as a typo"), Type),
				Ev.ErrorMessage.Contains(TEXT("Unrecognized 'type'")));
			TestTrue(FString::Printf(TEXT("%s error names the existing node"), Type),
				Ev.ErrorMessage.Contains(TEXT("ld.get_local_graph")));
		}

		// A refused op must not have half-authored anything, so prove the asset still compiles.
		const FSMAssistOperationResult Compile = Run(TEXT("ld.compile"), Obj({ { TEXT("asset_path"), Asset } }));
		if (!TestTrue(TEXT("compile success"), Compile.bSuccess))
		{
			return;
		}
		bool bHasErrors = true;
		Compile.Payload->TryGetBoolField(TEXT("has_errors"), bHasErrors);
		TestFalse(TEXT("state graph compiles with no errors"), bHasErrors);
	});

	It("wires update logic off the On State Update node a fresh state already owns", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString State = AddState(Asset, TEXT("Solo"));
		if (!TestTrue(TEXT("state created"), !State.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Graph = Run(TEXT("ld.get_local_graph"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), State } }));
		FString UpdateId;
		FString UpdateExecPin;
		if (!TestTrue(TEXT("On State Update listed on a fresh state"),
			FindNodeByClass(Graph.Payload, TEXT("StateUpdate"), UpdateId, UpdateExecPin)))
		{
			return;
		}
		FString EndId;
		FString EndExecPin;
		TestTrue(TEXT("On State End listed on a fresh state"),
			FindNodeByClass(Graph.Payload, TEXT("StateEnd"), EndId, EndExecPin));

		const FSMAssistOperationResult Add = Run(TEXT("ld.add_local_graph_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), State }, { TEXT("node_class"), TEXT("K2Node_ExecutionSequence") } }));
		const FString SequenceId = Str(Add, TEXT("id"));
		if (!TestTrue(TEXT("sequence added"), !SequenceId.IsEmpty()))
		{
			return;
		}

		// Those entry nodes ship as ghosts, and the link is what promotes them to real nodes.
		const FSMAssistOperationResult Connect = Run(TEXT("ld.connect_local_graph_pins"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), State },
				  { TEXT("from_node_id"), UpdateId }, { TEXT("from_pin"), UpdateExecPin },
				  { TEXT("to_node_id"), SequenceId }, { TEXT("to_pin"), TEXT("execute") } }));
		if (!TestTrue(TEXT("update exec wired into the sequence"), Connect.bSuccess))
		{
			return;
		}

		const FSMAssistOperationResult Compile = Run(TEXT("ld.compile"), Obj({ { TEXT("asset_path"), Asset } }));
		if (!TestTrue(TEXT("compile success"), Compile.bSuccess))
		{
			return;
		}
		bool bHasErrors = true;
		Compile.Payload->TryGetBoolField(TEXT("has_errors"), bHasErrors);
		TestFalse(TEXT("wired update logic compiles with no errors"), bHasErrors);
	});

	It("spawns an OnRootStateMachineStart event node into a state graph via snake_case", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString State = AddState(Asset, TEXT("Solo"));
		if (!TestTrue(TEXT("state created"), !State.IsEmpty()))
		{
			return;
		}

		const FSMAssistOperationResult Ev = Run(TEXT("ld.spawn_local_graph_event_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), State }, { TEXT("type"), TEXT("on_root_state_machine_start") } }));
		TestTrue(TEXT("OnRootStateMachineStart spawned in state graph"), Ev.bSuccess);
		TestFalse(TEXT("event node returns id"), Str(Ev, TEXT("id")).IsEmpty());
	});

	It("rejects an unknown event type", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);

		const FSMAssistOperationResult Ev = Run(TEXT("ld.spawn_local_graph_event_node"),
			Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("type"), TEXT("NotARealEvent") } }));
		TestFalse(TEXT("unknown event type fails"), Ev.bSuccess);
	});

	It("does not answer a transition target with state-graph advice", [this]()
	{
		const FString Asset = CreateBlueprint();
		const FString From = AddState(Asset, TEXT("From"));
		const FString To = AddState(Asset, TEXT("To"));
		const FString Trans = AddTransition(Asset, From, To);
		if (!TestTrue(TEXT("transition created"), !Trans.IsEmpty()))
		{
			return;
		}

		// A transition graph has no On State Begin/Update/End, so telling the caller to go find one
		// would send it hunting for a node that cannot exist there.
		for (const TCHAR* Type : { TEXT("OnStateBegin"), TEXT("OnStateUpdate"), TEXT("OnStateEnd") })
		{
			const FSMAssistOperationResult Ev = Run(TEXT("ld.spawn_local_graph_event_node"),
				Obj({ { TEXT("asset_path"), Asset }, { TEXT("node_guid"), Trans }, { TEXT("type"), Type } }));
			TestFalse(FString::Printf(TEXT("%s refused on a transition"), Type), Ev.bSuccess);
			TestFalse(FString::Printf(TEXT("%s error does not cite the state graph"), Type),
				Ev.ErrorMessage.Contains(TEXT("find the node titled")));
		}
	});
}

#endif

#endif
