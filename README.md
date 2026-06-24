# Logic Driver - Assist (`SMAssist`)

Programmatic, headless authoring API for [Logic Driver Pro](https://recursoft.net/logicdriver) state machines. It exposes the editor operations a human performs in the Logic Driver graph editor (create assets, add states/transitions/conduits, set node properties, wire property graphs, lay out graphs, capture screenshots, introspect a running PIE instance) as a registry of named, JSON-in/JSON-out operations that AI assistants and tooling can call.

The plugin is the substrate that powers MCP-style integrations. It does not talk to any model itself; it registers operations and lets transport bridges (Monolith, the engine ToolsetRegistry) surface them to an MCP client.

> **Status: experimental.** `IsExperimentalVersion` is set in the `.uplugin`. The operation surface and payload shapes are still evolving.

## What it is and isn't

- **Editor-only.** Every module is an editor module. There is no runtime/cooked footprint; the plugin does nothing in a packaged game.
- **A thin, generic operation layer.** `sm.*` operations route through Logic Driver's own editor APIs (`ISMGraphGeneration`, the graph schema, blueprint utils) so results are identical to a user editing by hand. The layer stays unopinionated: no vertical-specific (dialogue, combat) logic lives here. Verticals are authored *using* these generic operations.
- **Transport-agnostic.** The same operation registry is reachable from console commands, the Monolith MCP bridge, and the engine ToolsetRegistry. Each transport is a small adapter over one registry.

## Architecture

A single editor subsystem, `USMAssistSubsystem`, owns a registry of `FSMAssistOperationInfo` entries (name, description, JSON input schema, handler delegate). Operations are dispatched by name with a `FJsonObject` of arguments and return a structured `FSMAssistOperationResult` (`bSuccess`, `ErrorMessage`, JSON `Payload`).

```
 transport (console / Monolith / ToolsetRegistry)
        │  name + JSON args
        ▼
 USMAssistSubsystem::ExecuteOperation
        │
        ▼
 handler  ──►  Logic Driver editor APIs (ISMGraphGeneration, schema, BP utils)
        │
        ▼
 FSMAssistOperationResult  ──►  JSON payload back to the transport
```

### Modules

| Module | Type | Loading phase | Purpose |
|---|---|---|---|
| `SMAssist` | Editor | Default | Core subsystem, operation registry, all `sm.*` and `ld_ue.*` handlers. Also registers the `LDAssist.Exec` / `LDAssist.List` console commands. |
| `SMAssistMonolithBridge` | Editor (Optional) | PostEngineInit | Mirrors every registered operation into the [Monolith](https://github.com/Recursoft/monolith) MCP tool registry, keyed by `namespace.action`. No-op stub when Monolith is absent. |
| `SMAssistToolset` | Editor (Optional) | PostEngineInit | Exposes operations as `UToolsetDefinition` `AICallable` UFUNCTIONs through the engine-bundled experimental `ToolsetRegistry` (UE 5.8+). No-op shell when ToolsetRegistry is absent. |
| `SMAssistTests` | UncookedOnly | Default | Automation specs covering the operation handlers and the Monolith bridge. |

Both bridge modules are independent transports over the same registry, so the same `SMAssist` behavior tests cover both wire paths.

### Dependencies

Required:

- **Logic Driver Pro** (`SMSystem` + `SMSystemEditor` + `SMAssetTools`). `SMAssist` reaches into `SMSystemEditor/Private`, so it expects the Logic Driver plugin checked out as a sibling at `../LogicDriver` relative to this plugin.

Optional (each gated, plugin still builds when absent):

- **Monolith** (`MonolithCore`) — enables `SMAssistMonolithBridge`. Presence is detected at build time via `<Project>/Plugins/Monolith`; compiles to `WITH_MONOLITH=1`/`0`.
- **ToolsetRegistry** (engine experimental, UE 5.8+) — enables `SMAssistToolset`. Detected at build time via `<Engine>/Plugins/Experimental/ToolsetRegistry`; compiles to `WITH_TOOLSET_REGISTRY=1`/`0`.
- **ModelContextProtocol** — engine MCP plumbing used by the ToolsetRegistry path.

## Setup

This plugin is a private editor plugin, not a Marketplace install. It is dropped into a host project's `Plugins/` folder alongside the Logic Driver plugin.

1. **Place the plugins.** From the host project root, clone (or symlink) both plugins under `Plugins/`:
   - `Plugins/LogicDriver/` — Logic Driver Pro (`SMSystem`).
   - `Plugins/LogicDriver-Assist/` — this plugin.
   - `Plugins/Monolith/` — optional, only if you want the MCP bridge.

   The two plugins are independent checkouts; neither needs to track the host project's branch.

2. **Enable the plugin.** Add `SMAssist` to the host `.uproject` plugin list (or rely on the dependency chain). The `.uplugin` already enables `SMSystem` and optionally `Monolith`, `ToolsetRegistry`, and `ModelContextProtocol`.

3. **Build the editor target.** It is a C++ plugin, so the host project must be a C++ project (or have one C++ module). Build the Development Editor target normally; the optional modules light up automatically based on which sibling plugins are present.

4. **(MCP) Wire the transport.** For the Monolith bridge, the bridge registers itself at `PostEngineInit` once the editor is up. Point your MCP client at the Monolith HTTP server (default port `9316`). When running multiple editors at once, give each its own port via `Config/DefaultMonolith.ini` (`[/Script/MonolithCore.MonolithSettings]` / `ServerPort=`) so the bridges don't collide.

## Using the operations

### Console (always available)

Two console commands are registered by `SMAssist`:

```
LDAssist.List
LDAssist.Exec <operation> [json_args]
```

Example:

```
LDAssist.Exec sm.create_blueprint {"name":"SM_Door","path":"/Game/StateMachines"}
LDAssist.Exec sm.add_state {"asset_path":"/Game/StateMachines/SM_Door.SM_Door","state_name":"Closed","position_x":200}
LDAssist.Exec sm.get_asset {"asset_path":"/Game/StateMachines/SM_Door.SM_Door"}
```

Success prints `[op] ok: <json payload>`; failure prints `[op] error: <message>`.

### Monolith MCP bridge

When Monolith is present, each operation registers as `namespace.action` (the dot in the operation name splits namespace from action). An MCP client calls e.g. the `sm` namespace's `add_state` action with the same JSON arguments. The bridge tracks registration/unregistration live, so operations added at runtime appear without a restart.

#### Monolith's native `logicdriver` namespace vs LD-Assist's `sm`

Monolith ships its own `MonolithLogicDriver` module that registers a native `logicdriver.*` namespace (scaffold helpers like `logicdriver.scaffold_hello_world_sm`, plus `logicdriver.get_dialogue_flow`, `logicdriver.get_text_graph_content`, and so on). That module auto-enables (`WITH_LOGICDRIVER=1`) whenever Logic Driver is present in the project, which is the same condition under which LD-Assist is installed. So an MCP client typically sees **both** surfaces at once (the `sm_query` and `logicdriver_query` tools).

**Prefer the LD-Assist `sm.*` surface.** It is maintained by Recursoft alongside the plugin, tracks the plugin's editor internals, and is the generic, unopinionated primitive set this repo exists to provide. Treat Monolith's native `logicdriver.*` namespace as a fallback only.

The two can't be cleanly separated at build time: Monolith's native module keys off Logic Driver's mere presence, and its only off-switch is `MONOLITH_RELEASE_BUILD=1`, which disables *all* of Monolith's optional integrations. So the steering happens at the instruction layer, not the build layer.

**Tip — make the agent actually use it.** Put an explicit rule in the consuming session's instructions (the project `CLAUDE.md`, an `AGENTS.md`, or the MCP client's system prompt). For example:

> For Logic Driver authoring, use the `sm.*` namespace (the `sm_query` tool, backed by LD-Assist). Treat Monolith's native `logicdriver.*` namespace (`logicdriver_query`) as a last-resort fallback, and log a one-line note whenever you fall back to it.

Because both namespaces ride the same Monolith server, a name-level rule like this is what reliably keeps the agent on the LD-Assist path; tool descriptions alone don't disambiguate the overlap.

### ToolsetRegistry (UE 5.8+)

When the engine ships ToolsetRegistry, `ULogicDriverToolset` exposes each operation as an `AICallable` UFUNCTION with typed parameters. A marshal layer converts typed args into the same JSON envelope the subsystem expects, so behavior matches the Monolith path exactly. Convention: every MCP-schema param is required, and C++ sentinel defaults (empty string, `-1`, `-1.0`) mean "use the SMAssist default."

### Programmatic (C++)

```cpp
USMAssistSubsystem* Assist = GEditor->GetEditorSubsystem<USMAssistSubsystem>();
TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
Args->SetStringField(TEXT("name"), TEXT("SM_Door"));
const FSMAssistOperationResult Result = Assist->ExecuteOperation(TEXT("sm.create_blueprint"), Args);
```

You can register your own operations via `USMAssistSubsystem::RegisterOperation` and they flow out through every active transport automatically.

## Operation surface

Names live in `SMAssistOpKeys.h`. Each operation carries a detailed description and JSON input schema (the canonical, always-current reference is `RegisterBuiltInOperations` in `SMAssistSubsystem.cpp`). Grouped by area:

- **Assets / topology** — `create_blueprint`, `list_assets`, `get_asset`, `add_state`, `add_transition`, `add_transition_reroute`, `add_conduit`, `add_reference`, `configure_reference`, `add_any_state`, `add_link_state`, `remove_node`, `rename_state`, `set_initial_state`, `compile`, `collapse_to_state_machine`, `merge_states`, `replace_node`, `convert_to_reference`.
- **Node configuration** — `set_node_property`, `reset_node_property`, `get_node_properties`, `set_transition_condition`, `set_conduit_condition`, `add_state_stack`, `add_transition_stack`.
- **Property graphs / pins** — `get_property_pins`, `get_property_graph`, `set_property_graph_edit_mode`, `split_pin`, `recombine_pin`.
- **Variables / wiring** — `add_sm_variable`, `add_node_variable`, `add_blueprint_variable`, `configure_node_variable`, `connect_node_variable_output`, `disconnect_node_variable_output`.
- **Logic Driver K2 specials** — `spawn_local_graph_read_node`, `spawn_local_graph_write_node`, `find_node_types`, `configure_transition_event`, `spawn_actor_context_component`. These wrap LD's own node spawners; the engine's generic `create_node` action menu cannot place them.
- **Components / runtime** — `configure_sm_component_on_actor`, `runtime_get_state` (live PIE introspection).
- **Visualization** — `get_graph_view` (live slate geometry/colors), `capture_graph_view` (PNG to disk), `layout_states` (Sugiyama-style auto-layout), `clear_screenshots`.

### `ld_ue.*` fallback surface

A small generic-engine surface (`ld_ue.read_property`, `ld_ue.write_property`, `ld_ue.add_dispatcher`) exists as a **fallback only**, for cases the official engine `blueprint.*` MCP tools cannot express (TMap/TArray element values, a non-self target object, dispatchers that must survive compile). These are not Logic Driver operations; prefer the engine tools when they suffice.

## Authoring conventions baked into the operations

The operation descriptions encode layout rules so generated graphs look human-authored:

- Entry sits at `(0, 0)`; main flow runs left-to-right with positive X. First state ~`(200, 0)`, subsequent states `+250` X. State nodes are ~130-150 px wide and the editor draws an Entry-pointer marker ~200 px left of the entry state, so entry states near X=0 are visually eclipsed.
- Use Y only for deliberate parallel/branching rows; keep ≥150 units between rows so transitions never cross unrelated nodes.
- Prefer `layout_states(apply=true)` over manual coordinates for greenfield graphs.
- Conduits, references, link states, and any-states must be wired into the flow in the same authoring step; orphan nodes are a layout failure.
- An always-true entry gate is better modeled as an empty state than a conduit.

## Tests

Automation specs live in `Source/SMAssistTests/` (operation handlers, an end-to-end authoring scenario, local-graph discovery) and `Source/SMAssistMonolithBridge/.../Tests` (bridge wiring). Run them through the host project's headless automation runner filtered to the relevant group.
