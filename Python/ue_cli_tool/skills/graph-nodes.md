# Graph Nodes & Wiring — Workflow Tips

## Node Creation → Connect → Compile

```
@BP_Player
add_blueprint_event_node ReceiveBeginPlay
add_blueprint_function_node self PrintString
connect_blueprint_nodes <GUID1> then <GUID2> execute
compile_blueprint
```

## Key Patterns

- Node creation returns `node_id` (GUID) — capture it for subsequent connect calls
- Use `ue_query(query="help graph_describe")` to inspect existing graph topology before modifications
- `graph_describe_enhanced` with `--compact true` reduces large graph output from 50-100KB to 10-20KB
- Pin names are case-sensitive: "then"/"execute" for exec, "ReturnValue" for outputs

## Patch System (Declarative Editing)

For complex graph modifications, prefer `graph_apply_patch` over individual node calls:
- Supports temp IDs: `"id": "my_node"` in add_node, reference as `"node": "my_node"` in connect
- Auto-compiles after execution
- Use `graph_validate_patch` for dry-run validation first

## Selection-Based Operations

1. `graph_get_selected_nodes` — read current editor selection
2. `graph_set_selected_nodes` — programmatically set selection
3. `graph_collapse_selection_to_function` / `graph_collapse_selection_to_macro` — refactor selected nodes
4. `graph_batch_select_and_act` — batch grouped selection + action (e.g., collapse per group)

## Cross-Graph Transfer

1. `graph_export_nodes` — serialize nodes to text
2. `graph_import_nodes` — paste into another graph (supports offset)

## Common Event Names

ReceiveBeginPlay, ReceiveTick, ReceiveEndPlay, ReceiveAnyDamage,
ReceiveActorBeginOverlap, ReceiveActorEndOverlap, ReceiveHit

## Flow Control Macros

ForEachLoop, ForLoop, WhileLoop, DoOnce, Gate, FlipFlop, Delay, Retriggerable Delay