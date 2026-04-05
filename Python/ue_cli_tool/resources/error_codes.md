# Error Codes & Troubleshooting

## Connection Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Not connected to Unreal` | Unreal Editor not running or MCP Bridge plugin not loaded | Start UE Editor with the project, ensure UECliTool plugin is enabled |
| `Connection lost and reconnect failed` | UE Editor crashed or network issue | Restart UE Editor, the server will auto-reconnect |
| `Connection refused` | TCP port 55558 not listening | Check UE Editor is running and MCP Bridge is active |
| `Timeout` | Command took too long (>120s default) | UE may be busy (compiling shaders, etc.). Wait and retry. |

## Action Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Unknown action: X` | Invalid command name | Use `ue_query(query="search <keyword>")` to find correct command |
| `Unknown tool: X` | Tool name not recognized | Use `ue_cli` for commands, `ue_query` for read-only queries |
| `Blueprint 'X' not found` | Blueprint doesn't exist at expected path | Check spelling, use `list_assets` to find it |
| `Node 'X' not found` | Invalid node GUID | Use `find_blueprint_nodes` to get current node IDs |
| `Unknown component_type: X` | Invalid UMG component type | Use `ue_query(query="help add_widget_component")` to see supported types |

## Batch Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Max N actions per batch` | Batch exceeds 20 action limit | Split into multiple `ue_cli` calls |
| `failed: N` in response | Some commands in batch failed | Check individual results, fix failing commands |

## Common Gotchas

1. **Node positions overlap**: Always specify `--node_position [X,Y]` to avoid nodes stacking at [0,0]. Use `layout_auto_selected` after placing nodes.

2. **Blueprint not compiled**: After adding nodes/variables, always end with `compile_blueprint` to validate.

3. **Connect after create**: `connect_blueprint_nodes` requires node GUIDs. Create nodes first, capture their IDs from the response, then connect.

4. **Case sensitivity**: Blueprint names, variable names, and function names are **case-sensitive**.

5. **Widget hierarchy**: After adding widget components, use `add_widget_child` to build the tree structure. Components are added to root by default.