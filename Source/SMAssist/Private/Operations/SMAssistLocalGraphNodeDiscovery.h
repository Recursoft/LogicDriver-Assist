// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "ISMGraphGeneration.h"

class UEdGraph;
class USMBlueprint;

namespace LD::Assist
{
	/**
	 * Arguments for enumerating the Logic Driver K2 read/write kinds that are spawnable into a
	 * supplied bound graph. The result includes only kinds whose concrete USMGraphK2Node subclass
	 * accepts the graph via its IsCompatibleWithGraph override, so the caller can avoid the
	 * round-trip of spawning into a wrong graph and getting rejected.
	 */
	struct FFindLocalGraphNodeTypesArgs
	{
		/** [Required] Bound graph the spawn would target (state graph, transition graph, conduit graph, or intermediate graph). */
		UEdGraph* TargetGraph = nullptr;

		/**
		 * [Optional] Case-insensitive substring match against the kind name (e.g., "evaluate"
		 * matches CanEvaluate and CanEvaluateFromEvent). Empty = return every compatible kind.
		 */
		FString TypeIdFilter;
	};

	/**
	 * Result of FindLocalGraphNodeTypes. Read and write kinds are reported separately so the caller
	 * can route them to the right spawn op (sm.spawn_local_graph_read_node vs
	 * sm.spawn_local_graph_write_node).
	 */
	struct FFindLocalGraphNodeTypesResult
	{
		/** Compatible read kinds. Spawn via ISMGraphGeneration::CreateLocalGraphReadNode / sm.spawn_local_graph_read_node. */
		TArray<ISMGraphGeneration::ELocalGraphReadNodeType> ReadKinds;

		/** Compatible write kinds. Spawn via ISMGraphGeneration::CreateLocalGraphWriteNode / sm.spawn_local_graph_write_node. */
		TArray<ISMGraphGeneration::ELocalGraphWriteNodeType> WriteKinds;
	};

	/**
	 * Enumerate the Logic Driver K2 read/write kinds spawnable into InArgs.TargetGraph. This is the
	 * Logic Driver companion to BlueprintTools.find_node_types: the engine's action-menu surface
	 * doesn't list LD K2 nodes (they're filtered out by IsActionFilteredOut and spawned through
	 * dedicated CreateLocalGraph*Node paths), so even a fixed upstream find_node_types would never
	 * include them.
	 *
	 * Engine K2 node types (math operators, function calls, etc.) are NOT enumerated here. They
	 * remain the domain of BlueprintTools.find_node_types. Note that as of UE 5.8, upstream
	 * find_node_types rejects SM transition/conduit bound graphs with a "Cannot cast type ... to
	 * Blueprint" error; the agent-side workaround is to query find_node_types against any non-SM
	 * UBlueprint's EventGraph since type_ids are universal across graphs.
	 *
	 * @param InBlueprint The owning state-machine blueprint. The target graph must belong to it.
	 * @param InArgs Discovery arguments. TargetGraph is required.
	 *
	 * @return The compatible read and write kinds, filtered by TypeIdFilter when set. Empty result on null TargetGraph.
	 */
	FFindLocalGraphNodeTypesResult FindLocalGraphNodeTypes(USMBlueprint* InBlueprint, const FFindLocalGraphNodeTypesArgs& InArgs);
}
