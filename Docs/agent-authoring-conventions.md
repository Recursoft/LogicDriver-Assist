# Agent authoring conventions

Guidance for an AI agent that authors Logic Driver state machines through the Logic Driver Assist operations (`sm.*`). These are the conventions an agent cannot read off a tool schema: which surface to prefer, how to lay a graph out so it looks human-authored, and the ordering that keeps a build compiling and verifiable.

The MCP tools are self-describing. Each operation carries its own description and JSON input schema, and those are the source of truth for arguments and return shapes. This document does not restate them. It captures the orchestration knowledge that lives around the calls.

Drop the **Paste-in rules** block below into the consuming session's instructions (a project `CLAUDE.md`, an `AGENTS.md`, or the MCP client's system prompt). The rest of the page explains each rule.

## Paste-in rules

```text
Logic Driver authoring (via the LogicDriver-Assist sm.* operations):

- Prefer the sm.* surface for all Logic Driver work. If both an `sm` and a
  `logicdriver` namespace are present (the Monolith bridge exposes both),
  treat logicdriver.* as a last-resort fallback and log a one-line note
  whenever you use it.
- List and describe the tools before authoring. The operation descriptions
  and JSON schemas are authoritative; do not assume argument names or shapes.
- Address assets by the full object path (/Game/.../SM_Foo.SM_Foo) that
  create_blueprint and get_asset return, not the bare package path. Thread the
  returned asset_path forward rather than rebuilding it.
- Author into a clean folder such as /Game/MCP/<Feature>/. Do not mutate the
  user's existing reference assets unless they ask for it.
- For greenfield graphs, lay out with sm.layout_states apply=true. Hand
  coordinates are for targeted tweaks only: Entry at (0,0), first state near
  (200,0), ~350 units of X between states, positive X, and >=150 units between
  parallel rows.
- Wire every state into the flow, and make exactly one of them the initial
  state, connected from Entry (add_state is_entry=true, or sm.set_initial_state).
  Every conduit, reference, link state, and any state must be wired in the same
  step you add it; orphan nodes are a failure. Model an always-true entry gate
  as an empty state, not a conduit.
- A transition with no condition and no transition class never fires. For an
  unconditional edge, set it default-true (what sm.set_transition_condition does:
  it writes a constant true/false onto the eval pin, valid only when there is no
  node class). For a real gate, assign a transition class, or author the
  transition graph inline: sm.get_local_graph (returns the result pin to wire
  into) -> sm.spawn_local_graph_read_node (e.g. TimeInState) ->
  sm.add_local_graph_node (call_function Greater_DoubleDouble) ->
  sm.set_local_graph_pin_default (threshold on pin B) ->
  sm.connect_local_graph_pins twice (read output -> compare A, compare
  ReturnValue -> result pin) -> sm.compile. The add and spawn ops return the new
  node's id and pins, so no intervening re-read is needed.
  sm.set_transition_condition writes only a constant, so it does not combine with
  a wired result pin.
- The state machine graph is authored via sm.*, but the actor blueprint and its
  USMStateMachineComponent are set up on the editor surface (by hand or generic
  engine tools), not via sm.*. sm.configure_sm_component_on_actor then configures
  that existing named component's template: it sets StateMachineClass (persisting
  to every placed actor) plus lifecycle and replication config.
- To run on begin play, pass b_start_on_begin_play=true to
  sm.configure_sm_component_on_actor (the runtime default is false). Leave other
  config fields unset to keep the template's existing values.
- Batch graph edits, then sm.compile once. Compiling per edit is slow.
- Verify: compile clean, then (an editor-surface step) place the actor in a
  level and start PIE, and use sm.runtime_get_state to confirm the active state.
```

## Conventions in depth

### Prefer the `sm.*` surface

The `sm.*` operations are Logic Driver Assist, maintained alongside the plugin and routed through Logic Driver's own editor APIs, so results match hand-authoring exactly. When the Monolith bridge is in use it also exposes a native `logicdriver.*` namespace (the `logicdriver_query` tool) covering scaffold helpers and opinionated readers. Both surfaces appear at once. Tool descriptions alone do not disambiguate the overlap, so a name-level rule in the session instructions is what reliably keeps an agent on the `sm.*` path. The native namespace can also be silenced entirely with `bEnableLogicDriver=false` under `[/Script/MonolithCore.MonolithSettings]`.

### Discover before authoring

Operation names, arguments, and payload shapes are still evolving (the plugin is experimental). Enumerate the tools and read the operation description for anything you have not called recently, rather than relying on a remembered signature. `sm.find_node_types` reports which node classes are available to place, and `sm.get_asset` reads back the current topology of an asset.

Address assets by the full object path that `sm.create_blueprint` and `sm.get_asset` return (`/Game/.../SM_Foo.SM_Foo`), not the bare package path. Thread the returned `asset_path` forward instead of rebuilding it; the ToolsetRegistry transport rejects a bare path during argument conversion, before the operation runs.

### Clean-room authoring

Author new work into a dedicated folder such as `/Game/MCP/<Feature>/`. Treat the user's existing assets as a behavior reference, not something to edit in place, unless they explicitly ask you to modify them. This keeps a build reproducible and easy to discard, and it proves the machine can be built from a prompt rather than cloned.

### Layout

Generated graphs should read like a person laid them out. For a greenfield graph, prefer `sm.layout_states` with `apply=true` over hand-picking coordinates; hand placement is for targeted adjustments after an auto-layout.

When you do place by hand, Entry sits at `(0, 0)` and the main flow runs left to right with positive X. Put the first state around `(200, 0)` to leave room for the Entry node, then keep roughly `350` units of X between states. State nodes are about 130-150 units wide at 1:1 zoom and wider for longer display names, which is why that spacing matters. Use Y only for deliberate parallel or branching rows and keep at least `150` units between rows so transitions never cross unrelated nodes.

### No orphans

Every state must be wired into the flow, and exactly one must be the machine's initial state, connected from Entry (pass `is_entry=true` to `sm.add_state`, or call `sm.set_initial_state`). Without an initial state the machine compiles but has no active state at runtime. Conduits, references, link states, and any states must likewise be wired in the same authoring step that creates them. A node with no connections is a failure, not an in-progress state.

### Transitions fire only with a condition

A transition that has no authored condition and no transition class evaluates to false, so the machine never leaves the source state. There are three ways to make it fire. For an unconditional edge, set it default-true; that is what `sm.set_transition_condition` does, writing a constant true or false onto the evaluation pin (valid only when the transition has no node class). For reusable logic, assign a transition class whose `CanEnterTransition` evaluates the condition. For inline logic such as a time-in-state gate, author the transition graph directly with the local-graph ops.

Read it first with `sm.get_local_graph`, which returns the existing nodes and, for a transition, the wire-into anchor (`result_node_name` and `result_pin_name`, typically `bCanEnterTransition`). Place the state read with `sm.spawn_local_graph_read_node` (type `TimeInState`); the generic node-create menu cannot place Logic Driver's bound nodes, which is why a dedicated op exists. Place the comparison with `sm.add_local_graph_node` (`node_class` `call_function`, `function_name` `Greater_DoubleDouble`); that op writes any K2 node straight into the bound graph, bypassing the transport's generic graph resolver, so the comparison lands even though generic blueprint tools cannot address the graph at all. Seed the threshold with `sm.set_local_graph_pin_default` (pin `B` = `2.5`). Then wire twice with `sm.connect_local_graph_pins`: the read node's output into the comparison's `A`, and the comparison's `ReturnValue` into the transition's result pin. Both `sm.spawn_local_graph_read_node` and `sm.add_local_graph_node` return the new node's id and pins, so you can wire immediately without a re-read. Finish with `sm.compile`.

A wired result pin and `sm.set_transition_condition` are mutually exclusive: once the result pin is wired, its constant default is ignored (`sm.get_local_graph` shows `bCanEnterTransition` change from a `True` default to a connection). An always-true *entry* gate is better modeled as an empty state than as a conduit.

### Components and running the machine

The state machine graph is authored through `sm.*`, but making an actor run it is partly an editor-surface job. The actor blueprint and its `USMStateMachineComponent` are set up in the editor (by hand, or with generic engine/actor tools), not through `sm.*`. `sm.configure_sm_component_on_actor` then configures that existing component's *template*, matched by the SCS variable name you pass as `component_name` (it errors if no such component exists): it sets `StateMachineClass`, so every placed actor runs that machine, plus the lifecycle and replication config. If you need different machines on different placed actors, override `StateMachineClass` per instance in the editor; no `sm.*` operation writes a per-instance value.

The runtime default for `bStartOnBeginPlay` is false, so a configured-but-unstarted machine never ticks. Pass `b_start_on_begin_play=true` to run a placed machine on begin play, and leave the other config fields unset to keep whatever the template already has.

### Compile and verify

Compiling is the expensive step. Do all the graph edits first and `sm.compile` once at the end rather than after each change. Confirm the result two ways: the compile reports clean, and, after placing the actor in a level and starting PIE (an editor-surface step), `sm.runtime_get_state` returns the expected active state. A machine that compiles but sits in the wrong state, or reports no active state at all, usually has an unwired transition, no initial state, or a missing start flag.
