"""
Compact Script parser and executor for UE MCP Bridge.

Provides a token-efficient, human-readable script format that replaces
verbose JSON command sequences.  A single script can express 10+ operations
in ~20% of the tokens that JSON would require.

Syntax
------
::

    # Comment
    @BP_Enemy                          # Set context target (blueprint_name etc.)
    create Actor /Game/Blueprints      # command arg1 arg2 (positional → required params)
    +comp StaticMeshComponent Mesh     # Short alias
    +event ReceiveBeginPlay
    +var Health Float is_exposed=true  # Mixed positional + named params
    compile
    save_all

Context Inheritance
-------------------
``@<name>`` sets the *target* for all subsequent commands.  The target is
injected as the first required param if the command expects ``blueprint_name``,
``material_name``, or ``widget_name`` and it wasn't provided explicitly.

Short Aliases
-------------
Frequently used commands have short aliases to save tokens:

=========  =======================================
Alias      Full command
=========  =======================================
+comp      add_component_to_blueprint
+event     add_blueprint_event_node
+func      add_blueprint_function_node
+var       add_blueprint_variable
+varget    add_blueprint_variable_get
+varset    add_blueprint_variable_set
+pin       set_node_pin_default
+conn      connect_blueprint_nodes
+custev    add_blueprint_custom_event
+cast      add_blueprint_cast_node
+selfref   add_blueprint_get_self_component_reference
compile    compile_blueprint
save       save_all
summary    get_blueprint_summary
=========  =======================================
"""

from __future__ import annotations

import logging
import re
import shlex
from dataclasses import dataclass, field
from typing import Any, Optional

logger = logging.getLogger(__name__)

# ═══════════════════════════════════════════════════════════════════
# Alias table
# ═══════════════════════════════════════════════════════════════════

_ALIASES: dict[str, str] = {
    # Blueprint components
    "+comp": "add_component_to_blueprint",
    "+component": "add_component_to_blueprint",
    # Blueprint nodes — events
    "+event": "add_blueprint_event_node",
    "+custev": "add_blueprint_custom_event",
    "+input": "add_blueprint_input_action_node",
    "+einput": "add_enhanced_input_action_node",
    # Blueprint nodes — functions
    "+func": "add_blueprint_function_node",
    "+call": "call_blueprint_function",
    "+createfunc": "create_blueprint_function",
    # Blueprint nodes — variables
    "+var": "add_blueprint_variable",
    "+varget": "add_blueprint_variable_get",
    "+varset": "add_blueprint_variable_set",
    "+vardefault": "set_blueprint_variable_default",
    # Blueprint nodes — graph wiring
    "+conn": "connect_blueprint_nodes",
    "+pin": "set_node_pin_default",
    "+disconnect": "disconnect_blueprint_pin",
    "+move": "move_node",
    "+delete": "delete_blueprint_node",
    "+comment": "add_blueprint_comment",
    # Blueprint nodes — dispatchers
    "+dispatch": "add_event_dispatcher",
    "+calldispatch": "call_event_dispatcher",
    "+binddispatch": "bind_event_dispatcher",
    # Blueprint nodes — references / flow
    "+cast": "add_blueprint_cast_node",
    "+selfref": "add_blueprint_get_self_component_reference",
    "+spawn": "add_spawn_actor_from_class_node",
    "+sequence": "add_sequence_node",
    "+macro": "add_macro_instance_node",
    # Material
    "+matexpr": "add_material_expression",
    "+matconn": "connect_material_expressions",
    "+matprop": "set_material_expression_property",
    # Widget
    "+widget": "add_widget_component",
    "+widgetprop": "set_widget_properties",
    # Convenience shortcuts (no + prefix)
    "create": "create_blueprint",
    "compile": "compile_blueprint",
    "save": "save_all",
    "summary": "get_blueprint_summary",
    "describe": "describe_blueprint_full",
    "find": "find_blueprint_nodes",
    "pins": "get_node_pins",
    "actors": "get_actors_in_level",
    "context": "get_context",
    "ping": "ping",
    # Material shortcuts
    "create_mat": "create_material",
    "compile_mat": "compile_material",
    # Widget shortcuts
    "create_widget": "create_umg_widget_blueprint",
}

# ═══════════════════════════════════════════════════════════════════
# Positional parameter ordering (required params in order)
# ═══════════════════════════════════════════════════════════════════

# Maps C++ command → ordered list of required param names.
# Context target (blueprint_name etc.) is auto-injected and NOT listed here.
_POSITIONAL_PARAMS: dict[str, list[str]] = {
    # Blueprint CRUD
    "create_blueprint": ["name", "parent_class", "path"],
    "compile_blueprint": [],
    "get_blueprint_summary": [],
    # Components
    "add_component_to_blueprint": [
        "component_type",
        "component_name",
    ],
    # Nodes — events
    "add_blueprint_event_node": ["event_name"],
    "add_blueprint_custom_event": ["event_name"],
    "add_blueprint_input_action_node": ["action_name"],
    "add_enhanced_input_action_node": ["action_name"],
    # Nodes — functions
    "add_blueprint_function_node": ["target", "function_name"],
    "call_blueprint_function": ["target_blueprint", "function_name"],
    "create_blueprint_function": ["function_name"],
    # Nodes — variables
    "add_blueprint_variable": ["variable_name", "variable_type"],
    "add_blueprint_variable_get": ["variable_name"],
    "add_blueprint_variable_set": ["variable_name"],
    "set_blueprint_variable_default": ["variable_name", "default_value"],
    # Graph wiring
    "connect_blueprint_nodes": [
        "source_node_id",
        "source_pin",
        "target_node_id",
        "target_pin",
    ],
    "set_node_pin_default": ["node_id", "pin_name", "default_value"],
    "find_blueprint_nodes": [],
    "get_node_pins": ["node_id"],
    "move_node": ["node_id"],
    "delete_blueprint_node": ["node_id"],
    "disconnect_blueprint_pin": ["node_id", "pin_name"],
    "add_blueprint_comment": ["comment_text"],
    # Dispatchers
    "add_event_dispatcher": ["dispatcher_name"],
    "call_event_dispatcher": ["dispatcher_name"],
    "bind_event_dispatcher": ["dispatcher_name"],
    # References / flow
    "add_blueprint_cast_node": ["target_class"],
    "add_blueprint_get_self_component_reference": ["component_name"],
    "add_spawn_actor_from_class_node": ["class_to_spawn"],
    "add_macro_instance_node": ["macro_name"],
    # Variable management
    "delete_blueprint_variable": ["variable_name"],
    "rename_blueprint_variable": ["old_name", "new_name"],
    "set_variable_metadata": ["variable_name"],
    "delete_blueprint_function": ["function_name"],
    # Material
    "create_material": ["material_name", "path"],
    "add_material_expression": ["expression_class", "node_name"],
    "connect_material_expressions": [
        "source_node",
        "target_node",
        "target_input",
    ],
    "set_material_expression_property": ["node_name", "property_name"],
    "compile_material": [],
    # Widget
    "create_umg_widget_blueprint": ["widget_name", "parent_class", "path"],
    "add_widget_component": ["component_type", "component_name"],
    "set_widget_properties": ["target"],
    # Python exec
    "exec_python": ["code"],
    # Actors / editor
    "spawn_actor": ["name", "type"],
    "get_actors_in_level": [],
    "save_all": [],
    "ping": [],
    "get_context": [],
}

# Parameter names that receive the context target
_CONTEXT_PARAMS = ("blueprint_name", "material_name", "widget_name")


# ═══════════════════════════════════════════════════════════════════
# Data model
# ═══════════════════════════════════════════════════════════════════


@dataclass
class ScriptLine:
    """A single parsed script line."""

    line_number: int
    raw: str
    command: str  # resolved C++ command name
    params: dict[str, Any] = field(default_factory=dict)


@dataclass
class ParsedScript:
    """Result of parsing a UE script."""

    lines: list[ScriptLine] = field(default_factory=list)
    context_target: Optional[str] = None
    errors: list[str] = field(default_factory=list)


# ═══════════════════════════════════════════════════════════════════
# Parser
# ═══════════════════════════════════════════════════════════════════


def parse_script(text: str) -> ParsedScript:
    """Parse a compact script into a list of commands.

    Args:
        text: Script text (multi-line string)

    Returns:
        ParsedScript with resolved commands and parameters.
    """
    result = ParsedScript()

    for lineno, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()

        # Skip empty lines and comments
        if not line or line.startswith("#"):
            continue

        # Context target: @BP_Enemy or @MyMaterial
        if line.startswith("@"):
            target = line[1:].strip()
            if not target:
                result.errors.append(f"Line {lineno}: empty context target '@'")
                continue
            result.context_target = target
            continue

        # Parse the line into tokens
        try:
            tokens = _tokenize(line)
        except ValueError as e:
            result.errors.append(f"Line {lineno}: {e}")
            continue

        if not tokens:
            continue

        # Resolve command alias
        cmd_token = tokens[0]
        command = _ALIASES.get(cmd_token, cmd_token)
        remaining = tokens[1:]

        # Split remaining tokens into positional args and key=value pairs
        positionals: list[str] = []
        named: dict[str, str] = {}
        for tok in remaining:
            if "=" in tok and not tok.startswith("=") and not tok.endswith("="):
                key, _, value = tok.partition("=")
                named[key] = value
            else:
                positionals.append(tok)

        # Build params dict
        params: dict[str, Any] = {}

        # Inject context target if applicable
        if result.context_target and command in _POSITIONAL_PARAMS:
            # Determine which context param this command needs
            # For create_blueprint the target becomes "name", not blueprint_name
            if command == "create_blueprint":
                if "name" not in named:
                    params["name"] = result.context_target
            elif command == "create_material":
                if "material_name" not in named:
                    params["material_name"] = result.context_target
            elif command == "create_umg_widget_blueprint":
                if "widget_name" not in named:
                    params["widget_name"] = result.context_target
            else:
                # Most commands: inject as blueprint_name / material_name / widget_name
                for ctx_key in _CONTEXT_PARAMS:
                    if ctx_key not in named:
                        params[ctx_key] = result.context_target
                        break

        # Map positional args to parameter names, skipping those already
        # filled by context injection.
        param_order = _POSITIONAL_PARAMS.get(command, [])
        available_slots = [k for k in param_order if k not in params]
        for i, val in enumerate(positionals):
            if i < len(available_slots):
                params[available_slots[i]] = _coerce_value(val)
            else:
                # Extra positional — store as _arg_N
                params[f"_arg_{i}"] = _coerce_value(val)

        # Merge named params (override positional)
        for key, val in named.items():
            params[key] = _coerce_value(val)

        result.lines.append(
            ScriptLine(
                line_number=lineno,
                raw=raw_line.strip(),
                command=command,
                params=params,
            )
        )

    return result


def _tokenize(line: str) -> list[str]:
    """Tokenize a script line, respecting quoted strings."""
    try:
        return shlex.split(line)
    except ValueError:
        # Fallback: simple split
        return line.split()


def _coerce_value(val: str) -> Any:
    """Coerce a string value to its natural Python type."""
    # Boolean
    if val.lower() == "true":
        return True
    if val.lower() == "false":
        return False
    # Integer
    try:
        return int(val)
    except ValueError:
        pass
    # Float
    try:
        return float(val)
    except ValueError:
        pass
    # JSON array/object
    if val.startswith("[") or val.startswith("{"):
        import json

        try:
            return json.loads(val)
        except json.JSONDecodeError:
            pass
    return val


# ═══════════════════════════════════════════════════════════════════
# Executor
# ═══════════════════════════════════════════════════════════════════


def script_to_batch_commands(script: ParsedScript) -> list[dict[str, Any]]:
    """Convert a parsed script to batch_execute command list."""
    commands = []
    for sl in script.lines:
        entry: dict[str, Any] = {"type": sl.command}
        if sl.params:
            entry["params"] = sl.params
        commands.append(entry)
    return commands


def execute_script(
    text: str,
    connection: Any,
    *,
    continue_on_error: bool = True,
) -> dict[str, Any]:
    """Parse and execute a compact script.

    Args:
        text: Script text
        connection: PersistentUnrealConnection instance
        continue_on_error: Whether to continue after individual failures

    Returns:
        Result dict with per-line results.
    """
    parsed = parse_script(text)

    if parsed.errors:
        return {
            "success": False,
            "error": f"Script parse errors: {'; '.join(parsed.errors)}",
            "parse_errors": parsed.errors,
        }

    if not parsed.lines:
        return {"success": True, "total": 0, "executed": 0, "results": []}

    commands = script_to_batch_commands(parsed)

    # Execute via batch_execute for single round-trip
    from .connection import TimeoutTier

    result = connection.send_raw_dict(
        "batch_execute",
        {
            "commands": commands,
            "continue_on_error": continue_on_error,
        },
        timeout_tier=TimeoutTier.EXTRA_SLOW,
    )

    # Enrich results with original script lines
    batch_results = result.get("results", [])
    for i, br in enumerate(batch_results):
        if i < len(parsed.lines):
            br["_script_line"] = parsed.lines[i].line_number
            br["_script_raw"] = parsed.lines[i].raw

    result["context_target"] = parsed.context_target
    result["script_lines"] = len(parsed.lines)

    return result


def format_script_result(result: dict[str, Any]) -> str:
    """Format script execution result for human-readable display."""
    lines = []
    target = result.get("context_target", "")
    if target:
        lines.append(f"Target: @{target}")

    total = result.get("script_lines", result.get("total", 0))
    executed = result.get("executed", total)
    failed = result.get("failed", 0)
    lines.append(f"Commands: {executed}/{total} executed, {failed} failed")
    lines.append("")

    for i, r in enumerate(result.get("results", [])):
        raw = r.get("_script_raw", f"command {i + 1}")
        success = r.get("success", False)
        icon = "✓" if success else "✗"
        detail = ""
        if not success:
            detail = f" — {r.get('error', 'unknown error')}"
        lines.append(f"  {icon} {raw}{detail}")

    return "\n".join(lines)
