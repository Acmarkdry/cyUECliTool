# cyUECliTool

**CLI-native AI tool for controlling Unreal Engine Editor.**

> 12 JSON tools → 2 CLI tools. 73% fewer tokens. Zero new syntax to learn.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     AI (Claude, GPT, etc.)                    │
│                                                              │
│   ue_cli(command="@BP_Enemy\n                               │
│     add_component_to_blueprint StaticMeshComponent Mesh\n    │
│     compile_blueprint")                                      │
│                                                              │
│   ue_query(query="help create_blueprint")                    │
├──────────────────────────┬──────────────────────────────────┤
│       MCP Protocol       │    (stdio JSON — transport only)  │
├──────────────────────────┴──────────────────────────────────┤
│                                                              │
│    server.py  (2 tools: ue_cli + ue_query)                   │
│         │                                                    │
│    cli_parser.py  ← ActionRegistry (auto-derive params)     │
│         │                                                    │
│    connection.py  (CircuitBreaker + Metrics + Persistent)    │
│         │  TCP 55558                                         │
├─────────┼────────────────────────────────────────────────────┤
│    C++ MCPServer → Game Thread → MCPBridge → EditorActions   │
└─────────────────────────────────────────────────────────────┘
```

## CLI Syntax

```bash
<command> [positional_args...] [--flag value ...]
@<target>     # Set context (auto-fills blueprint_name/material_name/widget_name)
# comment     # Ignored
```

### Single Command
```
create_blueprint BP_Player --parent_class Character
```

### Multi-Command with Context
```
@BP_Player
add_component_to_blueprint CapsuleComponent Capsule
add_blueprint_variable Health --variable_type Float
add_blueprint_event_node ReceiveBeginPlay
compile_blueprint
```

### Material Workflow
```
@M_Glow
create_material --path /Game/Materials
add_material_expression MaterialExpressionVectorParameter BaseColor
compile_material
```

### Positional Args
Mapped to `required` params in order from the command's schema.
Context target fills the first matching param (`blueprint_name`, etc.),
remaining positionals fill the rest.

## Query Examples

```
ue_query(query="help")                          # List all commands
ue_query(query="help create_blueprint")         # Command-specific help
ue_query(query="search material")               # Search commands
ue_query(query="context")                       # Session context
ue_query(query="logs --n 50 --source editor")   # Editor logs
ue_query(query="health")                        # Connection status
ue_query(query="skills")                        # Skill catalog
ue_query(query="resources conventions.md")      # Embedded docs
```

## Installation

### 1. UE Plugin
Copy the plugin folder to your project's `Plugins/` directory:
```
YourProject/Plugins/UECliTool/
├── Source/UECliTool/
├── Python/ue_cli_tool/
├── UECliTool.uplugin
└── ...
```

### 2. Python Environment
```bash
cd Plugins/UECliTool/Python
pip install -e ".[dev]"
```

### 3. MCP Client Configuration
Add to your MCP client config (e.g. Claude Desktop):
```json
{
  "mcpServers": {
    "ue-cli-tool": {
      "command": "ue-cli-tool"
    }
  }
}
```

## Development

```bash
# Run tests
python -m pytest tests/ -v

# Format check
python -m black --check Python/ tests/

# Run server directly
python -m ue_cli_tool.server
```

## Credits

Based on [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP) (MIT License).

## License

MIT