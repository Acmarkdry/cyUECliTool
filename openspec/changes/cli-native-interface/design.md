# Design: CLI-Native Interface + Full Rename

## Architecture (After)

```
┌─────────────────────────────────────────────────────────────┐
│                         AI (Claude etc.)                     │
│                                                              │
│   ue_cli(command="@BP_Enemy\n                               │
│     create_blueprint --parent_class Actor\n                  │
│     add_component_to_blueprint StaticMeshComponent Mesh\n    │
│     compile_blueprint")                                      │
│                                                              │
│   ue_query(query="help create_blueprint")                    │
├──────────────────────────┬──────────────────────────────────┤
│         MCP Protocol     │    (stdio JSON — transport only)  │
├──────────────────────────┴──────────────────────────────────┤
│                                                              │
│    server.py  (2 tools: ue_cli + ue_query)                   │
│         │                                                    │
│    cli_parser.py  ← ActionRegistry (auto-derive params)     │
│         │                                                    │
│    connection.py  (CircuitBreaker + Metrics)                 │
│         │  TCP 55558                                         │
├─────────┼────────────────────────────────────────────────────┤
│    C++ MCPServer → Game Thread → MCPBridge → EditorActions   │
└─────────────────────────────────────────────────────────────┘
```

## Module Design

### 1. CLI Parser (`cli_parser.py`)

**Core principle: zero hand-maintained tables.** Everything derived from ActionRegistry at runtime.

```python
class CliParser:
    """Parse CLI-style text into executable command dicts.
    
    Automatically derives positional parameter mapping from
    ActionRegistry input_schema.required fields.
    """
    
    def __init__(self, registry: ActionRegistry):
        self._registry = registry
        # Cache: command_name → [ordered required param names]
        self._positional_cache: dict[str, list[str]] = {}
    
    def parse(self, text: str) -> ParseResult:
        """Parse one or more CLI lines into command dicts."""
    
    def parse_line(self, line: str, context: dict) -> CommandDict:
        """Parse a single CLI line with context injection."""
```

**Parameter auto-derivation algorithm:**

```python
def _get_positional_order(self, command: str) -> list[str]:
    """Derive positional param order from registry."""
    if command in self._positional_cache:
        return self._positional_cache[command]
    
    action = self._registry.get_by_command(command)
    if action is None:
        return []
    
    # The 'required' field in input_schema defines the order
    required = action.input_schema.get("required", [])
    self._positional_cache[command] = required
    return required
```

**Line parsing algorithm:**

```
Input:  "add_component_to_blueprint StaticMeshComponent Mesh --location [0,0,100]"
Context: {"blueprint_name": "BP_Enemy"}

1. Tokenize: ["add_component_to_blueprint", "StaticMeshComponent", "Mesh", "--location", "[0,0,100]"]
2. command = "add_component_to_blueprint"
3. positional_order = ["blueprint_name", "component_type", "component_name"]  ← from registry
4. Inject context:
   - "blueprint_name" is in context → skip, mark as filled
   - available_slots = ["component_type", "component_name"]
5. Map positional args:
   - "StaticMeshComponent" → component_type
   - "Mesh" → component_name
6. Parse --flags:
   - --location [0,0,100] → {"location": [0, 0, 100]}
7. Merge all:
   {
     "blueprint_name": "BP_Enemy",
     "component_type": "StaticMeshComponent",
     "component_name": "Mesh",
     "location": [0, 0, 100]
   }
```

**Context inheritance rules:**

| Command domain | Context parameter | Detection |
|---|---|---|
| Blueprint commands | `blueprint_name` | Has `blueprint_name` in schema properties |
| Material commands | `material_name` | Has `material_name` in schema properties |
| Widget commands | `widget_name` | Has `widget_name` in schema properties |

Detection is automatic — parser checks if the command's schema has these properties.

### 2. MCP Server (`server.py`, formerly `server_unified.py`)

**Only 2 tools:**

#### `ue_cli` — Execute commands

```python
Tool(
    name="ue_cli",
    description="""Execute Unreal Editor commands using CLI syntax.

SYNTAX:
  <command> [positional_args...] [--flag value ...]
  @<target>     Set context (auto-fills blueprint_name/material_name/widget_name)
  # comment     Ignored
  Multiple lines = batch execution (single round-trip)

EXAMPLES:
  # Single command
  create_blueprint BP_Player --parent_class Character

  # Multi-step with context
  @BP_Player
  add_component_to_blueprint CapsuleComponent Capsule
  add_blueprint_variable Health --variable_type Float
  add_blueprint_event_node ReceiveBeginPlay
  compile_blueprint

  # Material
  @M_Glow
  create_material --path /Game/Materials
  add_material_expression MaterialExpressionVectorParameter BaseColor

POSITIONAL ARGS:
  Mapped to 'required' params in order. Context target fills the first
  matching param (blueprint_name etc.), remaining positionals fill the rest.

AVAILABLE COMMANDS:
  Use ue_query(query="help") for full command list.
  Use ue_query(query="help <command>") for command-specific help.""",
    inputSchema={
        "type": "object",
        "properties": {
            "command": {
                "type": "string",
                "description": "CLI command(s), one per line.",
            },
        },
        "required": ["command"],
    },
)
```

#### `ue_query` — Read-only queries

```python
Tool(
    name="ue_query",
    description="""Query information from Unreal Editor or the tool itself.

QUERIES:
  help                    List all available commands
  help <command>          Show command syntax, params, examples
  search <keyword>        Search commands by keyword
  context                 Get session context (open assets, recent operations)
  logs [--n 20]           Tail recent logs
  logs --source editor    UE editor log ring buffer
  metrics                 Performance statistics
  health                  Connection + circuit breaker status
  resources <name>        Read embedded docs (conventions.md, error_codes.md)""",
    inputSchema={
        "type": "object",
        "properties": {
            "query": {
                "type": "string",
                "description": "Query string.",
            },
        },
        "required": ["query"],
    },
)
```

### 3. Rename Mapping

**Python files (rename + content update):**

| Old path | New path |
|---|---|
| `Python/ue_editor_mcp/__init__.py` | `Python/ue_cli_tool/__init__.py` |
| `Python/ue_editor_mcp/server_unified.py` | `Python/ue_cli_tool/server.py` |
| `Python/ue_editor_mcp/connection.py` | `Python/ue_cli_tool/connection.py` |
| `Python/ue_editor_mcp/metrics.py` | `Python/ue_cli_tool/metrics.py` |
| `Python/ue_editor_mcp/tracer.py` | `Python/ue_cli_tool/tracer.py` |
| `Python/ue_editor_mcp/command_proxy.py` | `Python/ue_cli_tool/command_proxy.py` |
| `Python/ue_editor_mcp/pipeline.py` | `Python/ue_cli_tool/pipeline.py` |
| `Python/ue_editor_mcp/script.py` | **DELETED** |
| `Python/ue_editor_mcp/registry/` | `Python/ue_cli_tool/registry/` |
| `Python/ue_editor_mcp/context/` | `Python/ue_cli_tool/context/` |
| `Python/ue_editor_mcp/skills/` | `Python/ue_cli_tool/skills/` |
| (new) | `Python/ue_cli_tool/cli_parser.py` |

**C++ files:**

| Old path | New path |
|---|---|
| `UEEditorMCP.uplugin` | `UECliTool.uplugin` |
| `Source/UEEditorMCP/UEEditorMCP.Build.cs` | `Source/UECliTool/UECliTool.Build.cs` |
| `Source/UEEditorMCP/` | `Source/UECliTool/` |

**Config/docs:**

| File | Change |
|---|---|
| `.github/workflows/ci.yml` | Path refs + pip install path |
| `README.md` | Full rewrite |
| `docs/architecture.md` | Update architecture |
| `docs/development.md` | Update dev guide |
| `pyproject.toml` | Package name + paths |
| `tests/*.py` | Import paths |

### 4. `ue_query` Handler — Help Generation

Help text is auto-generated from ActionRegistry:

```python
def _handle_help(command_name: str = None) -> str:
    if command_name is None:
        # List all commands grouped by domain
        return _format_command_list()
    
    action = registry.get_by_command(command_name)
    if action is None:
        return f"Unknown command: {command_name}"
    
    # Auto-generate CLI help from schema
    required = action.input_schema.get("required", [])
    properties = action.input_schema.get("properties", {})
    
    lines = [f"  {command_name}", f"  {action.description}", ""]
    
    # Positional args
    lines.append("  POSITIONAL (required):")
    for param in required:
        desc = properties.get(param, {}).get("description", "")
        lines.append(f"    <{param}>  {desc}")
    
    # Optional flags
    optional = [k for k in properties if k not in required]
    if optional:
        lines.append("  FLAGS (optional):")
        for param in optional:
            desc = properties.get(param, {}).get("description", "")
            ptype = properties.get(param, {}).get("type", "string")
            lines.append(f"    --{param} <{ptype}>  {desc}")
    
    # Examples
    if action.examples:
        lines.append("  EXAMPLES:")
        for ex in action.examples:
            cli_line = _format_example_as_cli(command_name, ex, required)
            lines.append(f"    {cli_line}")
    
    return "\n".join(lines)
```

### 5. What Gets Deleted

- `Python/ue_editor_mcp/script.py` — replaced by `cli_parser.py`
- `tests/test_script.py` — replaced by `test_cli_parser.py`
- All 12 old MCP tool definitions and handlers in `server_unified.py`
- `server_unified.py` itself — replaced by cleaner `server.py`

## Backward Compatibility

**None.** This is a clean break as confirmed by user. All old tool names, import paths, and JSON interfaces are removed.

## Testing Strategy

- `test_cli_parser.py`: 40+ test cases covering parsing, context injection, auto-derivation, edge cases
- `test_schema_contract.py`: Updated imports, same registry validation logic
- `test_context.py`: Updated imports
- `test_skills.py`: Updated imports
- GitHub Actions CI must pass on push
