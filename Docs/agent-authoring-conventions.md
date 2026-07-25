# Agent authoring conventions

Guidance for an AI agent that authors Logic Driver state machines through the Logic Driver Assist operations (`ld.*`). These are the conventions an agent cannot read off a tool schema: which surface to prefer, how to lay a graph out so it looks human-authored, and the ordering that keeps a build compiling and verifiable.

The MCP tools are self-describing. Each operation carries its own description and JSON input schema, and those are the source of truth for arguments and return shapes. This document does not restate them. It captures the orchestration knowledge that lives around the calls.

Drop the **Paste-in rules** block below into the consuming session's instructions (a project `CLAUDE.md`, an `AGENTS.md`, or the MCP client's system prompt). The rest of the page explains each rule.

## Paste-in rules

```text
Logic Driver authoring (via the LogicDriver-Assist ld.* operations):

- Prefer the ld.* surface for all Logic Driver work. If both an `ld` and a
  `logicdriver` namespace are present (the Monolith bridge exposes both),
  treat logicdriver.* as a last-resort fallback and log a one-line note
  whenever you use it.
- List and describe the tools before authoring. The operation descriptions
  and JSON schemas are authoritative; do not assume argument names or shapes.
- Address assets by the full object path (/Game/.../SM_Foo.SM_Foo) that
  create_blueprint and get_asset return, not the bare package path. Thread the
  returned asset_path forward rather than rebuilding it.
- Wherever an operation accepts a CLASS token (target_class, cast_class,
  state_class, transition_class, a variable's object type), pass the full
  object path form (/Game/.../BP_Foo.BP_Foo_C), never the bare class name.
  Bare names resolve by global search and can silently bind a same-named class
  from elsewhere in the project; the failure surfaces later as wrong pin names
  or "function not found", not at bind time.
- Author into a clean folder such as /Game/MCP/<Feature>/. Do not mutate the
  user's existing reference assets unless they ask for it.
- For greenfield graphs, lay out with ld.layout_states apply=true. Hand
  coordinates are for targeted tweaks only: Entry at (0,0), first state near
  (200,0), ~350 units of X between states, positive X, and >=150 units between
  parallel rows (much more for states displaying property widgets, which are
  several times taller than plain states). Layout measures real node sizes
  only once the graph editor has been opened (ld.capture_graph_view opens it);
  re-run the layout if the first pass overlaps, and read the result back
  visually with ld.capture_graph_view.
- Wire every state into the flow, and make exactly one of them the initial
  state, connected from Entry (add_state is_entry=true, or ld.set_initial_state).
  Every conduit, reference, link state, and any state must be wired in the same
  step you add it; orphan nodes are a failure. Model an always-true entry gate
  as an empty state, not a conduit.
- A transition with no condition and no transition class never fires. For an
  unconditional edge, set it default-true (what ld.set_transition_condition does:
  it writes a constant true/false onto the eval pin, valid only when there is no
  node class). For a real gate, assign a transition class, or author the
  transition graph inline: ld.get_local_graph (returns the result pin to wire
  into) -> ld.spawn_local_graph_read_node (e.g. TimeInState) ->
  ld.add_local_graph_node (call_function Greater_DoubleDouble) ->
  ld.set_local_graph_pin_default (threshold on pin B) ->
  ld.connect_local_graph_pins twice (read output -> compare A, compare
  ReturnValue -> result pin) -> ld.compile. The add and spawn ops return the new
  node's id and pins, so no intervening re-read is needed.
  ld.set_transition_condition writes only a constant, so it does not combine with
  a wired result pin.
- A gate that reads a variable needs something to WRITE that variable, or every
  such gate reads the default and the branch never varies at runtime. If a
  transition reads a player's choice or a visit count, author the writer too (a
  state's OnStateBegin, a driver, or player input), or acknowledge a fixed
  default. A clean compile does not prove the branch varies.
- Transitions have two independent axes: the CONDITION (the gate above) and the
  TRIGGER (when it is checked). By default a transition polls every tick. To fire
  it from an event, bind one with ld.configure_transition_event
  (delegate_property_name on a delegate_owner_instance of This/Context/
  PreviousState; Context also needs delegate_owner_class), then pick
  event_triggers_targeted_update (this edge + destination, preferred) or
  event_triggers_full_update (whole machine, legacy). It auto-places the return
  node; never spawn that yourself. Binding leaves the edge tick+event; to make it
  event-ONLY, turn tick off via ld.set_node_property (bCanEvaluate=false on the
  transition, or bDisableTickTransitionEvaluation=true on the from-state, which
  suppresses tick for all its outgoing edges). An event-only edge only fires
  when something broadcasts the bound delegate, so author or confirm that
  broadcaster just as a gated variable needs a writer. Verify with
  ld.get_asset: each transition reports evaluation (tick/event/tick+event/none)
  and, when bound, an event object. Graph logic calling
  EvaluateFromManuallyBoundEvent is not reflected in those fields.
- Node logic lives in two graphs, reached differently. A state's entry/update/end
  logic is a bound graph, authored with the ld.* local-graph ops (ld.get_local_graph
  on the state node, then wire off the "On State Begin" entry node's then pin). All
  three entry nodes already exist in every state graph; On State Update and On State
  End show dimmed until something wires into them, and connecting enables them. Do
  not spawn those two with ld.spawn_local_graph_event_node, which adds a duplicate.
  The machine's own OnStateMachineStart lives in the blueprint's top-level event graph,
  reached with generic blueprint tools; it ships already placed (shown disabled), so
  wire off its then pin to activate it. Do NOT add a new override; it already exists
  and the add fails. The same rule generalizes: new blueprints ship with their
  common override events pre-placed (BeginPlay and Tick on an actor), so when an
  add reports "already exists", wire the node id the error names.
- Compile a blueprint before referencing its newly added functions from another
  blueprint's graph; the generated class does not carry them until compiled.
- The state machine graph is authored via ld.*, but the actor blueprint and its
  USMStateMachineComponent are set up on the editor surface (by hand or generic
  engine tools), not via ld.*. ld.configure_sm_component_on_actor then configures
  that existing named component's template: it sets StateMachineClass (persisting
  to every placed actor) plus lifecycle and replication config.
- To run on begin play, pass b_start_on_begin_play=true to
  ld.configure_sm_component_on_actor (the runtime default is false). Leave other
  config fields unset to keep the template's existing values.
- Batch graph edits, then ld.compile once. Compiling per edit is slow.
- Verify: compile clean, then (an editor-surface step) place the actor in a
  level and start PIE, and use ld.runtime_get_state to confirm the active state.
```

## Conventions in depth

### Prefer the `ld.*` surface

The `ld.*` operations are Logic Driver Assist, maintained alongside the plugin and routed through Logic Driver's own editor APIs, so results match hand-authoring exactly. When the Monolith bridge is in use it also exposes a native `logicdriver.*` namespace (the `logicdriver_query` tool) covering scaffold helpers and opinionated readers. Both surfaces appear at once. Tool descriptions alone do not disambiguate the overlap, so a name-level rule in the session instructions is what reliably keeps an agent on the `ld.*` path. The native namespace can also be silenced entirely with `bEnableLogicDriver=false` under `[/Script/MonolithCore.MonolithSettings]`.

### Discover before authoring

Operation names, arguments, and payload shapes are still evolving (the plugin is experimental). Enumerate the tools and read the operation description for anything you have not called recently, rather than relying on a remembered signature. `ld.find_node_types` reports which node classes are available to place, and `ld.get_asset` reads back the current topology of an asset.

Address assets by the full object path that `ld.create_blueprint` and `ld.get_asset` return (`/Game/.../SM_Foo.SM_Foo`), not the bare package path. Thread the returned `asset_path` forward instead of rebuilding it; the ToolsetRegistry transport rejects a bare path during argument conversion, before the operation runs.

### Clean-room authoring

Author new work into a dedicated folder such as `/Game/MCP/<Feature>/`. Treat the user's existing assets as a behavior reference, not something to edit in place, unless they explicitly ask you to modify them. This keeps a build reproducible and easy to discard, and it proves the machine can be built from a prompt rather than cloned.

### Layout

Generated graphs should read like a person laid them out. For a greenfield graph, prefer `ld.layout_states` with `apply=true` over hand-picking coordinates; hand placement is for targeted adjustments after an auto-layout.

When you do place by hand, Entry sits at `(0, 0)` and the main flow runs left to right with positive X. Put the first state around `(200, 0)` to leave room for the Entry node, then keep roughly `350` units of X between states. State nodes are about 130-150 units wide at 1:1 zoom and wider for longer display names, which is why that spacing matters. Use Y only for deliberate parallel or branching rows and keep at least `150` units between rows so transitions never cross unrelated nodes.

### No orphans

Every state must be wired into the flow, and exactly one must be the machine's initial state, connected from Entry (pass `is_entry=true` to `ld.add_state`, or call `ld.set_initial_state`). Without an initial state the machine compiles but has no active state at runtime. Conduits, references, link states, and any states must likewise be wired in the same authoring step that creates them. A node with no connections is a failure, not an in-progress state.

### Transitions fire only with a condition

A transition that has no authored condition and no transition class evaluates to false, so the machine never leaves the source state. There are three ways to make it fire. For an unconditional edge, set it default-true; that is what `ld.set_transition_condition` does, writing a constant true or false onto the evaluation pin (valid only when the transition has no node class). For reusable logic, assign a transition class whose `CanEnterTransition` evaluates the condition. For inline logic such as a time-in-state gate, author the transition graph directly with the local-graph ops.

Read it first with `ld.get_local_graph`, which returns the existing nodes and, for a transition, the wire-into anchor (`result_node_name` and `result_pin_name`, typically `bCanEnterTransition`). Place the state read with `ld.spawn_local_graph_read_node` (type `TimeInState`); the generic node-create menu cannot place Logic Driver's bound nodes, which is why a dedicated op exists. Place the comparison with `ld.add_local_graph_node` (`node_class` `call_function`, `function_name` `Greater_DoubleDouble`); that op writes any K2 node straight into the bound graph, bypassing the transport's generic graph resolver, so the comparison lands even though generic blueprint tools cannot address the graph at all. Seed the threshold with `ld.set_local_graph_pin_default` (pin `B` = `2.5`). Then wire twice with `ld.connect_local_graph_pins`: the read node's output into the comparison's `A`, and the comparison's `ReturnValue` into the transition's result pin. Both `ld.spawn_local_graph_read_node` and `ld.add_local_graph_node` return the new node's id and pins, so you can wire immediately without a re-read. Finish with `ld.compile`.

A wired result pin and `ld.set_transition_condition` are mutually exclusive: once the result pin is wired, its constant default is ignored (`ld.get_local_graph` shows `bCanEnterTransition` change from a `True` default to a connection). An always-true *entry* gate is better modeled as an empty state than as a conduit.

### Feed the variables a gate reads

A transition can compile, evaluate every frame, and still never change which branch it takes. A gate that reads a variable (a player's choice, a visit count, a flag) only does something once something *writes* that variable. If nothing does, every such gate reads the default: the machine always takes the same branch, or, when the default satisfies no edge, sits at the source state. Authoring the read is half the job. Author the write too: a state's `OnStateBegin` that increments a counter on entry, a driver that sets the choice before a hub, or real player input. When a genuine runtime input is out of scope, pick and state a fixed default rather than leaving the variable unbacked. Treat a clean compile as "the class built," never as "the branch varies at runtime."

### Event transitions

A transition has two independent axes. The *condition* ("what must be true") is the gate covered above: a transition class, a constant, or inline graph logic. The *trigger* ("when is that condition checked") is separate: by default a transition is polled every tick, but it can instead fire from an event, or do both. The two are set independently on the transition, so choosing one never constrains the other.

Bind an auto-bound event with `ld.configure_transition_event`. It attaches a multicast delegate, named by `delegate_property_name`, that lives on the `delegate_owner_instance` you name: `This` (the state machine instance itself), `Context`, or `PreviousState`. A `Context` owner also needs `delegate_owner_class` to resolve the property. The op mirrors a Details-panel edit exactly, including the cascading resets (changing the owner clears the delegate name), and it auto-places the `TransitionEventReturn` node in the transition's bound graph. Never place that node yourself with `ld.spawn_local_graph_write_node`. Passing an empty `delegate_property_name` clears the binding while preserving any downstream logic wired off the return node, so unbind and rebind cycles do not lose work.

When the delegate fires, the transition re-evaluates. Pick what update runs: `event_triggers_targeted_update` re-evaluates just this transition and its destination state (the focused, preferred behavior), while `event_triggers_full_update` runs a whole-machine update (the older, broader behavior, applied after the targeted one). These map to the transition's "Targeted Update" and "Full Update" settings; those Details-panel labels do not appear in the operation, only the argument keys.

Binding an event does not stop the tick poll: the edge becomes `tick+event`. To make it event-only, turn the tick side off with `ld.set_node_property`. Setting `bCanEvaluate` to false on the transition stops that one edge from polling; setting `bDisableTickTransitionEvaluation` to true on the *from-state* stops tick evaluation of every edge leaving that state. Either leaves the transition firing on its event alone. An event-only edge fires only when something broadcasts the bound delegate, so if nothing ever does it can never trigger and the machine sits at the source state on a clean compile. Author or confirm the broadcaster the same way a gated variable needs a writer.

Read the result back with `ld.get_asset` without opening a bound graph. Each transition entry carries an `evaluation` field (`tick`, `event`, `tick+event`, or `none`) and, when an event is bound, an `event` object mirroring the `configure_transition_event` fields, so a caller can confirm the binding landed. One case stays invisible to these fields: a transition whose graph logic calls `EvaluateFromManuallyBoundEvent` directly, with no auto-bound delegate, has no static binding to report, so `evaluation` reflects only the tick and auto-event configuration.

`ld.set_node_property` writes these flags' authoring-time defaults. To flip `CanEvaluate` or `CanEvaluateFromEvent` at runtime from within graph logic instead, spawn the matching write node with `ld.spawn_local_graph_write_node` (types `CanEvaluate`, `CanEvaluateFromEvent`); their read counterparts spawn with `ld.spawn_local_graph_read_node`.

### Where entry and start logic live

Node logic lives in two different graphs, reached by two different surfaces. A *state's* entry, update, and end logic is a bound graph, authored with the `ld.*` local-graph ops: `ld.get_local_graph` on the state node lists whatever entry nodes the graph currently holds. `On State Begin` is always present (it is the graph's container), so wire begin logic off its output directly. `On State Update` and `On State End` do not exist on a state with no node class until you create them with `ld.spawn_local_graph_event_node` (types `OnStateUpdate`, `OnStateEnd`); spawn the one you need, then wire off its output. The same op adds a transition's or conduit's lifecycle-event entries (`OnInitialized`, `OnTransitionEntered`, and so on), which are likewise absent until spawned. The *machine's* own `OnStateMachineStart` (and `Tick`) live in the blueprint's ordinary top-level event graph, so they are reached with the generic engine blueprint tools, not `ld.*`. A fresh state machine ships with `OnStateMachineStart` already placed there, shown disabled ("This node is disabled and will not be called") until it is used. Do not add the override again; it already exists and the add fails. Wire your logic off its execution pin instead, which activates it on the next compile. For logic that should run once when the machine begins, `OnStateMachineStart` or the entry state's `OnStateBegin` both work; choose by whether the logic is machine-wide or specific to that first state.

### Components and running the machine

The state machine graph is authored through `ld.*`, but making an actor run it is partly an editor-surface job. The actor blueprint and its `USMStateMachineComponent` are set up in the editor (by hand, or with generic engine/actor tools), not through `ld.*`. `ld.configure_sm_component_on_actor` then configures that existing component's *template*, matched by the SCS variable name you pass as `component_name` (it errors if no such component exists): it sets `StateMachineClass`, so every placed actor runs that machine, plus the lifecycle and replication config. If you need different machines on different placed actors, override `StateMachineClass` per instance in the editor; no `ld.*` operation writes a per-instance value.

The runtime default for `bStartOnBeginPlay` is false, so a configured-but-unstarted machine never ticks. Pass `b_start_on_begin_play=true` to run a placed machine on begin play, and leave the other config fields unset to keep whatever the template already has.

### Compile and verify

Compiling is the expensive step. Do all the graph edits first and `ld.compile` once at the end rather than after each change. Confirm the result two ways: the compile reports clean, and, after placing the actor in a level and starting PIE (an editor-surface step), `ld.runtime_get_state` returns the expected active state. A machine that compiles but sits in the wrong state, or reports no active state at all, usually has an unwired transition, no initial state, or a missing start flag.
