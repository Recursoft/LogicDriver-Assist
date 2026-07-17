# Logic Driver Assist (`SMAssist`)

Programmatic, headless authoring API for [Logic Driver Pro](https://logicdriver.com) state machines, built for AI assistants and tooling to drive. It exposes the same editor operations a human performs in the Logic Driver graph editor as a registry of named, JSON-in/JSON-out operations. Those cover creating assets, adding states/transitions/conduits, setting node properties, wiring property graphs, laying out graphs, capturing screenshots, and introspecting a running PIE instance.

> [!IMPORTANT]
> **Requires Logic Driver Pro 2.11 or newer.**

> [!WARNING]
> **Experimental.** `IsExperimentalVersion` is set in the `.uplugin`. The operation surface and payload shapes are still evolving.

## Quick start

To get an agent authoring Logic Driver graphs through this plugin:

1. **Place the plugins.** Drop `LogicDriver/` and `LogicDriver-Assist/` into your host project's `Plugins/` folder (plus a transport bridge such as `Monolith/`, if you use one).
2. **Build** the Development Editor target. The host must be a C++ project. The optional transport modules light up automatically based on which plugins are present (Monolith in your project, ToolsetRegistry in the engine).
3. **Launch the editor.** Every operation registers automatically at startup.
4. **Connect your MCP client** through whichever transport you run. See [Using the operations](#using-the-operations) for each transport's endpoint and calling convention.

To confirm the plugin loaded before wiring any client, run `LDAssist.List` in the editor console.

## What it is and isn't

- **Editor-only.** Every module is an editor module. There is no runtime/cooked footprint, so the plugin does nothing in a packaged game.
- **A thin, generic operation layer.** `ld.*` operations route through Logic Driver's own editor APIs (`ISMGraphGeneration`, the graph schema, blueprint utils) so results are identical to a user editing by hand. The layer stays unopinionated: no vertical-specific (dialogue, combat) logic lives here. Verticals are authored *using* these generic operations.
- **Transport-agnostic.** The plugin never talks to a model itself. The same operation registry is reachable from console commands, the Monolith MCP bridge, and the engine ToolsetRegistry, each a small adapter over one registry.

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
| `SMAssist` | Editor | Default | Core subsystem, operation registry, all `ld.*` and `ld_ue.*` handlers. Also registers the `LDAssist.Exec` / `LDAssist.List` console commands. |
| `SMAssistMonolithBridge` | Editor (Optional) | PostEngineInit | Mirrors every registered operation into the [Monolith](https://github.com/Recursoft/monolith) MCP tool registry, keyed by `namespace.action`. No-op stub when Monolith is absent. |
| `SMAssistToolset` | Editor (Optional) | PostEngineInit | Exposes operations as `UToolsetDefinition` `AICallable` UFUNCTIONs through the engine-bundled experimental `ToolsetRegistry` (UE 5.8+). No-op shell when ToolsetRegistry is absent. |
| `SMAssistTests` | UncookedOnly | Default | Automation specs covering the operation handlers and an end-to-end authoring scenario. Enabled locally only (see [Tests](#tests)). |

Both optional transport modules are independent adapters over the same registry, so the shared operation-handler tests cover the behavior of both. Each has its own thin wiring spec.

### Dependencies

Required:

- **Logic Driver Pro** (`SMSystem` + `SMSystemEditor` + `SMAssetTools`). `SMAssist` routes only through Logic Driver's public editor APIs, so it needs no include paths into the core plugin. `SMAssistTests` still expects Logic Driver checked out as a sibling at `../LogicDriver`, because it reuses core test helpers from `SMTests/Private`.

Optional (the plugin still builds when any of these are absent):

- **Monolith** (`MonolithCore`) — enables `SMAssistMonolithBridge`. Presence is detected at build time via `<Project>/Plugins/Monolith` and exposed as `WITH_MONOLITH=1`/`0`.
- **ToolsetRegistry** (engine experimental, UE 5.8+) — enables `SMAssistToolset`. Detected at build time via `<Engine>/Plugins/Experimental/ToolsetRegistry` and exposed as `WITH_TOOLSET_REGISTRY=1`/`0`.
- **ModelContextProtocol** (engine MCP plugin, UE 5.8+). This is the MCP server that reads the ToolsetRegistry and serves the toolset over HTTP. SMAssist has no build-time dependency on it and gates nothing on it, so it just needs to be enabled in the editor for the ToolsetRegistry transport to be reachable.

## Setup

This plugin is a private editor plugin, not a Marketplace install. It is dropped into a host project's `Plugins/` folder alongside the Logic Driver plugin.

1. **Place the plugins.** From the host project root, clone (or symlink) both plugins under `Plugins/`:
   - `Plugins/LogicDriver/` — Logic Driver Pro (`SMSystem`).
   - `Plugins/LogicDriver-Assist/` — this plugin.
   - `Plugins/Monolith/` — optional, only if you want the MCP bridge.

2. **Enable the plugin.** Add `SMAssist` to the host `.uproject` plugin list (or rely on the dependency chain). The `.uplugin` already enables `SMSystem` and optionally `Monolith`, `ToolsetRegistry`, and `ModelContextProtocol`.

3. **Build the editor target.** It is a C++ plugin, so the host project must be a C++ project (or have one C++ module). Build the Development Editor target normally. The optional modules light up automatically based on which plugins are present (Monolith in your project, ToolsetRegistry in the engine).

4. **(MCP) Wire the transport.** For the Monolith bridge, it registers itself at `PostEngineInit` once the editor is up. Point your MCP client at the Monolith HTTP server (default port `9316`). When running multiple editors at once, give each its own port so the bridges don't collide: create (or edit) `Config/DefaultMonolith.ini` in your project and set `ServerPort=` under `[/Script/MonolithCore.MonolithSettings]`.

## Using the operations

### Console (always available)

Two console commands are registered by `SMAssist`:

```
LDAssist.List
LDAssist.Exec <operation> [json_args]
```

Example:

```
LDAssist.Exec ld.create_blueprint {"name":"SM_Door","path":"/Game/StateMachines"}
LDAssist.Exec ld.add_state {"asset_path":"/Game/StateMachines/SM_Door.SM_Door","state_name":"Closed","position_x":200}
LDAssist.Exec ld.get_asset {"asset_path":"/Game/StateMachines/SM_Door.SM_Door"}
```

Success prints `[op] ok: <json payload>`. Failure prints `[op] error: <message>`.

### Monolith MCP bridge

When Monolith is present, each operation registers as `namespace.action` (the dot in the operation name splits namespace from action). An MCP client calls e.g. the `ld` namespace's `add_state` action with the same JSON arguments. The bridge tracks registration/unregistration live, so operations added at runtime appear without a restart.

#### Monolith's native `logicdriver` namespace vs LD-Assist's `ld`

<details>
<summary>Both surfaces appear at once, so prefer <code>ld.*</code>. Expand for why, and the rule that keeps agents on it.</summary>

Monolith ships its own `MonolithLogicDriver` module that registers a native `logicdriver.*` namespace (scaffold helpers like `logicdriver.scaffold_hello_world_sm`, plus `logicdriver.get_dialogue_flow`, `logicdriver.get_text_graph_content`, and so on). That module auto-enables (`WITH_LOGICDRIVER=1`) whenever Logic Driver is detected (in the project or as an engine plugin), which is the same condition under which LD-Assist is installed. So an MCP client typically sees **both** surfaces at once (the `ld_query` and `logicdriver_query` tools).

**Prefer the LD-Assist `ld.*` surface.** It is maintained by Recursoft alongside the plugin, tracks the plugin's editor internals, and is the generic, unopinionated primitive set this repo exists to provide. Treat Monolith's native `logicdriver.*` namespace as a fallback only.

You can silence the native `logicdriver.*` namespace on its own without giving up the rest of Monolith: set `bEnableLogicDriver=false` under `[/Script/MonolithCore.MonolithSettings]` (the "Enable Logic Driver Integration" project setting), and the native module registers zero actions, leaving only `ld.*`. No build-time switch targets just this namespace. The one build-time off-switch, `MONOLITH_RELEASE_BUILD=1`, turns off *all* of Monolith's optional integrations at once. If you keep both surfaces live, steer the agent at the instruction layer (see the tip below).

**Tip — make the agent actually use it.** Put an explicit rule in the consuming session's instructions (the project `CLAUDE.md`, an `AGENTS.md`, or the MCP client's system prompt). For example:

> For Logic Driver authoring, use the `ld.*` namespace (the `ld_query` tool, backed by LD-Assist). Treat Monolith's native `logicdriver.*` namespace (`logicdriver_query`) as a last-resort fallback, and log a one-line note whenever you fall back to it.

Because both namespaces ride the same Monolith server, a name-level rule like this is what reliably keeps the agent on the LD-Assist path. Tool descriptions alone don't disambiguate the overlap.

</details>

### ToolsetRegistry (UE 5.8+)

When the engine ships ToolsetRegistry, `ULogicDriverToolset` exposes each operation as an `AICallable` UFUNCTION with typed parameters. A marshal layer (`SMAssistToolsetMarshal.h`) converts typed args into the same JSON envelope the subsystem expects, so operation *behavior* matches the Monolith path exactly. Only the call mechanics and the outer wire envelope differ:

- **Call mechanics.** The server is UE 5.8's stock `ModelContextProtocol` plugin (default `http://localhost:8000/mcp`). With the default tool-search mode it advertises three meta-tools: `list_toolsets`, `describe_toolset`, and `call_tool`. Invoke an operation with `call_tool` passing `{ "toolset_name": "SMAssistToolset.LogicDriverToolset", "tool_name": "AddState", "arguments": { ... } }` (the `tool_name` omits the toolset prefix). `tools/list` returns plain JSON. `tools/call` returns an SSE stream (`event: message` / `data: {...}`), so parse the last `data:` line.
- **Every param is required** at the MCP-schema layer (the dispatcher rejects omitted fields regardless of C++ defaults). Most C++ sentinel defaults (empty string, `-1` for indices, `-1.0` for gaps/durations) mean "use the SMAssist default", and the marshal skips them when building the JSON. Canvas coordinates are the exception: a negative coordinate is a legitimate value (the default state row sits near `y=-43`), so placement carries a companion `bAutoPosition` flag (`bDefaultOrigin` for `LayoutStates`) that defaults to auto; set it false to send explicit `PositionX`/`PositionY`.
- **Object args need a full object path.** A parameter typed as a UObject (`Blueprint`, and the like) resolves from a full object path such as `/Game/Path/SM_Foo.SM_Foo` (the `asset_path` that `CreateBlueprint` / `GetAsset` return), not the bare package path `/Game/Path/SM_Foo`. A bare path is rejected during argument conversion, before the operation runs, so the call comes back as a parameter error rather than a result.
- **Result envelope.** Success returns the operation payload serialized as a JSON string under the reply's `returnValue` field (parse it to recover the payload object). There are no `bSuccess` / `error` fields. Failure surfaces as a tool-level MCP error carrying the SMAssist error text.

The **Monolith bridge** (above) carries the identical payload behind a different outer envelope: `FMonolithActionResult` with an explicit `bSuccess` + `ErrorMessage`, its `*_query` dispatcher nests args under a `params` object, and it addresses assets by an `asset_path` string. Both transports share `USMAssistSubsystem::ExecuteOperation`, so keep the two sections in sync.

### Programmatic (C++)

```cpp
USMAssistSubsystem* Assist = GEditor->GetEditorSubsystem<USMAssistSubsystem>();
TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
Args->SetStringField(TEXT("name"), TEXT("SM_Door"));
const FSMAssistOperationResult Result = Assist->ExecuteOperation(TEXT("ld.create_blueprint"), Args);
```

You can register your own operations via `USMAssistSubsystem::RegisterOperation` and they flow out through every active transport automatically.

## Operation surface

The `ld.*` names live in `SMAssistOpKeys.h`, and the `ld_ue.*` fallback names in `SMAssistGenericOpKeys.h`. Each operation carries a detailed description and JSON input schema (the canonical, always-current reference is `RegisterBuiltInOperations` in `SMAssistSubsystem.cpp`). Grouped by area:

- **Assets / topology** — `create_blueprint`, `list_assets`, `get_asset`, `add_state`, `add_transition`, `add_transition_reroute`, `add_conduit`, `add_reference`, `configure_reference`, `add_any_state`, `add_link_state`, `remove_node`, `rename_state`, `set_initial_state`, `compile`, `collapse_to_state_machine`, `merge_states`, `replace_node`, `convert_to_reference`.
- **Node configuration** — `set_node_property`, `set_node_class`, `reset_node_property`, `get_node_properties`, `set_transition_condition`, `set_conduit_condition`, `add_state_stack`, `add_transition_stack`.
- **Property graphs / pins** — `get_property_pins`, `get_property_graph`, `set_property_graph_edit_mode`, `split_pin`, `recombine_pin`.
- **Variables / wiring** — `add_sm_variable`, `add_node_variable`, `add_blueprint_variable`, `configure_node_variable`, `connect_node_variable_output`, `disconnect_node_variable_output`.
- **Logic Driver K2 specials** — `spawn_local_graph_read_node`, `spawn_local_graph_write_node`, `spawn_local_graph_event_node`, `find_node_types`, `configure_transition_event`, `spawn_actor_context_component`. These wrap LD's own node spawners. The engine's generic `create_node` action menu cannot place them.
- **Local (bound) graph authoring** — `get_local_graph`, `add_local_graph_node`, `set_local_graph_pin_default`, `connect_local_graph_pins`, `disconnect_local_graph_pins`, `set_local_graph_node`, `remove_local_graph_node`. Read and edit the K2 graph bound to a state or transition node (a transition's condition, a state's entry/update/end logic) directly. Generic blueprint tools cannot address these bound graphs.
- **Components / runtime** — `configure_sm_component_on_actor`, `runtime_get_state` (live PIE introspection).
- **Visualization** — `get_graph_view` (live Slate geometry/colors), `capture_graph_view` (PNG to disk), `capture_local_graph` (PNG of a node's bound graph), `layout_states` (Sugiyama-style auto-layout), `clear_screenshots`.

### `ld_ue.*` fallback surface

A small generic-engine surface (`ld_ue.read_property`, `ld_ue.write_property`, `ld_ue.add_dispatcher`) exists as a **fallback only**, for cases the official engine `blueprint.*` MCP tools cannot express (TMap/TArray element values, a non-self target object, dispatchers that must survive compile). These are not Logic Driver operations. Prefer the engine tools when they suffice.

## Authoring conventions baked into the operations

The operation descriptions encode layout rules so generated graphs look human-authored:

- Entry sits at `(0, 0)` and the main flow runs left-to-right with positive X. Put the first state around `(200, 0)` to leave room for the Entry node, then keep roughly `350` units of X between states so their bodies and transition arrows don't crowd each other. State nodes are about 130-150 units wide at 1:1 zoom, and wider for longer display names, which is why that spacing matters.
- Use Y only for deliberate parallel/branching rows, and keep ≥150 units between rows so transitions never cross unrelated nodes.
- Prefer `layout_states(apply=true)` over manual coordinates for greenfield graphs.
- Conduits, references, link states, and any-states must be wired into the flow in the same authoring step. Orphan nodes are a layout failure.
- An always-true entry gate is better modeled as an empty state than a conduit.

## Tests

Automation specs live in `Source/SMAssistTests/` (operation handlers, an end-to-end authoring scenario, local-graph discovery), `Source/SMAssistMonolithBridge/.../Tests` (Monolith bridge wiring), and `Source/SMAssistToolset/.../Tests` (ToolsetRegistry wiring). Run them through the host project's headless automation runner filtered to the relevant group.

The `SMAssistTests` module is not enabled in the committed `SMAssist.uplugin`. To run the specs, add its module entry to your local `.uplugin` and rebuild:

```json
{
  "Name": "SMAssistTests",
  "Type": "UncookedOnly",
  "LoadingPhase": "Default",
  "PlatformAllowList": [ "Win64", "Mac", "Linux" ]
}
```

Keep this edit local. Don't commit it to `SMAssist.uplugin`.
