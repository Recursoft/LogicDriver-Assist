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
- For greenfield graphs, lay out with ld.layout_states apply=true, once, at the
  end. It measures every graph in scope as rendered and spaces by that. It
  starts each graph one column gap past that graph's own Entry node. It carries
  any edge that would draw its marker over a state on a rail of reroute nodes.
  It lays out a second time if a node grew after it was measured, and 'passes'
  says whether it did. Check the top-level 'measurement_warnings' for
  anything it could not measure, and each graphs[].warnings for what it had to
  flow around. Hand coordinates are for targeted tweaks only: positive X, and
  >=150 units between parallel rows. Budget much more for a state that displays
  property widgets or dialogue text: a node's size grows with its DisplayName and
  with everything its body draws, so a state showing a few properties runs
  300-380 wide and 100-280 tall against roughly 70-160 x 44 for a bare one, where
  a bare state's width is almost entirely its name: a two-character name measures
  69 wide and a thirty-six-character one measures 417.
- Find collisions with ld.get_graph_view rather than a screenshot. Its
  'overlaps' array lists every intersecting pair among the nodes a layout places
  (states, conduits, references, link states, any states) plus the Entry node,
  which a layout flows around rather than moves and which a state dropped on top
  of hides completely; its 'transition_overlaps' array lists transition markers
  and reroutes stacked on each other, which spacing does not fix and a reroute
  does; and its widget_size is measured at 1:1 zoom whatever the panel is
  showing. Read 'measurement_warnings' first: the arrays mean nothing while it is
  non-empty, because the unmeasured part of the graph contributes no overlaps to
  either.
- Judge readability with a ld.capture_graph_view, which is a different question
  from collisions and the one the arrays cannot answer: a transition line routed
  across intervening states shows up in neither. Empty arrays mean nothing
  collides, not that the graph reads well. Capture to look at colors or titles,
  to judge a finished layout, or to show a result; not to find collisions.
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
  End show dimmed until something wires into them, and connecting enables them.
  ld.spawn_local_graph_event_node refuses all three.
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

### Nested state machines

A nested state machine is one asset, not many. `ld.get_asset` reports the root graph by default and shows a container as a single `state_machine_state` entry; pass `scope="all"` to walk every nesting level, where each state and transition also carries `graph_path` and `parent_state_guid` so levels can be told apart. `is_entry` is always relative to a node's own graph, while the top-level `entry_state_guids` stays the root graph's.

To author inside a container, pass its guid as `parent_state_guid` to `ld.add_state`, `ld.add_conduit`, `ld.add_any_state`, `ld.add_link_state`, `ld.add_reference`, or `ld.add_transition_reroute`; omitting it targets the root graph. Everything else already resolves a guid at any depth, so `ld.add_transition`, `ld.set_initial_state`, `ld.set_node_property`, `ld.rename_state`, and `ld.collapse_to_state_machine` need no extra argument. Transitions must stay within one graph, and a state machine REFERENCE delegates its states to another asset, so target that asset directly rather than the reference node.

For a visual check, `ld.get_graph_view` takes the same `parent_state_guid` to measure a nested graph, and `ld.capture_local_graph` screenshots it. `ld.layout_states` with `scope="all"` lays out every level in one transaction, measuring each level on its own panel and restoring the tab you started on when it is done.

Collapsing is not destructive to identity: `ld.collapse_to_state_machine` moves the same nodes rather than cloning them, so guids recorded beforehand stay valid. Its `node_guids` response lists what the container now holds, which is not the set you passed: boundary transitions stay in the parent graph. Pass every interior transition too, because one whose endpoints both moved is deleted rather than carried in. There is no un-collapse operation.

### Clean-room authoring

Author new work into a dedicated folder such as `/Game/MCP/<Feature>/`. Treat the user's existing assets as a behavior reference, not something to edit in place, unless they explicitly ask you to modify them. This keeps a build reproducible and easy to discard, and it proves the machine can be built from a prompt rather than cloned.

### Layout

Generated graphs should read like a person laid them out. For a greenfield graph, prefer `ld.layout_states` with `apply=true` over hand-picking coordinates; hand placement is for targeted adjustments after an auto-layout.

Run it once, after the last edit. It handles three things in one call. Each graph is anchored one column gap past its own Entry node and centered on it, so the first state never lands on top of Entry, in the root graph or in any nested one. Every graph in scope is measured on its own panel, so a nested graph is spaced by what it renders at rather than by a default. Placement comes first. The op picks the within-layer order that draws fewest transitions through a state box, measuring each candidate order by placing it, and only then rails what ordering could not clear. A transition whose wire would still be drawn through a state is carried on a rail of two reroute nodes clear of the flow. How many layers an edge spans does not decide this by itself: a long edge that runs clear of every state gets no rail, and an edge between neighboring layers gets one if a state sits in its way. Reroutes are cosmetic and change nothing at runtime; the op adds and repositions them but never removes one, because a reroute already in the graph may be the user's own. Pass `route_edges=false` to leave those edges drawn straight.

It reports three more things afterwards. `passes` is 2 when a node grew after it was measured and the layout had to run again. `icon_location_adjustments` counts the transition markers it slid along their own wires. Sliding is what separates two markers from different state pairs whose midpoints landed on the same point. State spacing never separates those, because the midpoint moves with the states. `skipped` lists work the op could not do, such as a rail it could not create. An empty array means nothing was dropped.

When you do place by hand, Entry sits at `(0, 0)` in a fresh graph and the main flow runs left to right with positive X. Put the first state far enough right to clear the Entry node, then keep roughly `350` units of X between states. State nodes are about 130-150 units wide at 1:1 zoom and wider for longer display names, which is why that spacing matters. Use Y only for deliberate parallel or branching rows and keep at least `150` units between rows so transitions never cross unrelated nodes.

### No orphans

Every state must be wired into the flow, and exactly one must be the machine's initial state, connected from Entry (pass `is_entry=true` to `ld.add_state`, or call `ld.set_initial_state`). Without an initial state the machine compiles but has no active state at runtime. Conduits, references, link states, and any states must likewise be wired in the same authoring step that creates them. A node with no connections is a failure, not an in-progress state.

### Any States

Use an Any State when most or all of the states need the same outgoing transition. It exists to save authoring that transition by hand from every state, so it earns its place exactly when the alternative is a pile of duplicate edges. "Dies from any state" is the shape it is for.

When only two or three states need the transition, author those edges directly. An Any State fed into one target from a handful of sources costs a node, draws a fan of long wires, and tells a reader the transition applies everywhere when it does not.

Restricting an Any State to a subset of states is not something the `ld.*` surface can do yet. `AnyStateTags` and `AnyStateTagQuery` exist on the node but no operation sets them, so a subset means explicit transitions from the states in it.

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

Node logic lives in two different graphs, reached by two different surfaces. A *state's* entry, update, and end logic is a bound graph, authored with the `ld.*` local-graph ops: `ld.get_local_graph` on the state node lists whatever entry nodes the graph currently holds. `On State Begin` (the graph's container), `On State Update`, and `On State End` are all present from the moment the state is created and none of them can be deleted, so wire begin, update, and end logic off the existing node's output directly. Find the one to wire from by its `title` (`On State Begin`, `On State Update`, `On State End`). `ld.spawn_local_graph_event_node` recognizes those three names but refuses them. It is for the entries that are absent until spawned: `OnInitialized` and `OnShutdown` (valid in a state, transition, or conduit graph), `OnTransitionEntered` and the pre/post-evaluate pair, and the root state machine start/stop entries. The *machine's* own `OnStateMachineStart` (and `Tick`) live in the blueprint's ordinary top-level event graph, so they are reached with the generic engine blueprint tools, not `ld.*`. A fresh state machine ships with `OnStateMachineStart` already placed there, shown disabled ("This node is disabled and will not be called") until it is used. Do not add the override again; it already exists and the add fails. Wire your logic off its execution pin instead, which activates it on the next compile. For logic that should run once when the machine begins, `OnStateMachineStart` or the entry state's `OnStateBegin` both work; choose by whether the logic is machine-wide or specific to that first state.

### Components and running the machine

The state machine graph is authored through `ld.*`, but making an actor run it is partly an editor-surface job. The actor blueprint and its `USMStateMachineComponent` are set up in the editor (by hand, or with generic engine/actor tools), not through `ld.*`. `ld.configure_sm_component_on_actor` then configures that existing component's *template*, matched by the SCS variable name you pass as `component_name` (it errors if no such component exists): it sets `StateMachineClass`, so every placed actor runs that machine, plus the lifecycle and replication config. If you need different machines on different placed actors, override `StateMachineClass` per instance in the editor; no `ld.*` operation writes a per-instance value.

The runtime default for `bStartOnBeginPlay` is false, so a configured-but-unstarted machine never ticks. Pass `b_start_on_begin_play=true` to run a placed machine on begin play, and leave the other config fields unset to keep whatever the template already has.

### Compile and verify

Compiling is the expensive step. Do all the graph edits first and `ld.compile` once at the end rather than after each change. Confirm the result two ways: the compile reports clean, and, after placing the actor in a level and starting PIE (an editor-surface step), `ld.runtime_get_state` returns the expected active state. A machine that compiles but sits in the wrong state, or reports no active state at all, usually has an unwired transition, no initial state, or a missing start flag.
