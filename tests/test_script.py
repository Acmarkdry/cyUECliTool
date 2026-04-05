"""
Tests for compact script parser.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Python"))

from ue_editor_mcp.script import (
    parse_script,
    script_to_batch_commands,
    format_script_result,
    _coerce_value,
    ParsedScript,
)

# ── Parsing basics ──────────────────────────────────────────────────


class TestParseBasics:
    def test_empty_script(self):
        result = parse_script("")
        assert result.lines == []
        assert result.errors == []

    def test_comments_only(self):
        result = parse_script("# just a comment\n# another")
        assert result.lines == []

    def test_context_target(self):
        result = parse_script("@BP_Enemy")
        assert result.context_target == "BP_Enemy"
        assert result.lines == []

    def test_empty_context_target_error(self):
        result = parse_script("@")
        assert len(result.errors) == 1
        assert "empty context target" in result.errors[0]

    def test_simple_command(self):
        result = parse_script("ping")
        assert len(result.lines) == 1
        assert result.lines[0].command == "ping"
        assert result.lines[0].params == {}

    def test_blank_lines_skipped(self):
        result = parse_script("ping\n\n\nping")
        assert len(result.lines) == 2


# ── Alias resolution ───────────────────────────────────────────────


class TestAliases:
    def test_compile_alias(self):
        result = parse_script("compile")
        assert result.lines[0].command == "compile_blueprint"

    def test_save_alias(self):
        result = parse_script("save")
        assert result.lines[0].command == "save_all"

    def test_comp_alias(self):
        result = parse_script("+comp StaticMeshComponent Mesh")
        assert result.lines[0].command == "add_component_to_blueprint"

    def test_event_alias(self):
        result = parse_script("+event ReceiveBeginPlay")
        assert result.lines[0].command == "add_blueprint_event_node"

    def test_var_alias(self):
        result = parse_script("+var Health Float")
        assert result.lines[0].command == "add_blueprint_variable"

    def test_func_alias(self):
        result = parse_script("+func self PrintString")
        assert result.lines[0].command == "add_blueprint_function_node"

    def test_conn_alias(self):
        result = parse_script("+conn N1 exec N2 exec")
        assert result.lines[0].command == "connect_blueprint_nodes"

    def test_unknown_command_passes_through(self):
        result = parse_script("some_custom_command arg1")
        assert result.lines[0].command == "some_custom_command"


# ── Positional parameters ──────────────────────────────────────────


class TestPositionalParams:
    def test_create_blueprint(self):
        result = parse_script("create BP_Test Actor /Game/Blueprints")
        sl = result.lines[0]
        assert sl.command == "create_blueprint"
        assert sl.params["name"] == "BP_Test"
        assert sl.params["parent_class"] == "Actor"
        assert sl.params["path"] == "/Game/Blueprints"

    def test_add_variable(self):
        result = parse_script("+var Health Float")
        sl = result.lines[0]
        assert sl.params["variable_name"] == "Health"
        assert sl.params["variable_type"] == "Float"

    def test_set_pin_default(self):
        result = parse_script("+pin NODE123 InString HelloWorld")
        sl = result.lines[0]
        assert sl.params["node_id"] == "NODE123"
        assert sl.params["pin_name"] == "InString"
        assert sl.params["default_value"] == "HelloWorld"

    def test_connect_nodes(self):
        result = parse_script("+conn SRC exec TGT then")
        sl = result.lines[0]
        assert sl.params["source_node_id"] == "SRC"
        assert sl.params["source_pin"] == "exec"
        assert sl.params["target_node_id"] == "TGT"
        assert sl.params["target_pin"] == "then"


# ── Named parameters ───────────────────────────────────────────────


class TestNamedParams:
    def test_named_only(self):
        result = parse_script("+var variable_name=Health variable_type=Float")
        sl = result.lines[0]
        assert sl.params["variable_name"] == "Health"
        assert sl.params["variable_type"] == "Float"

    def test_mixed_positional_and_named(self):
        result = parse_script("+var Health Float is_exposed=true")
        sl = result.lines[0]
        assert sl.params["variable_name"] == "Health"
        assert sl.params["variable_type"] == "Float"
        assert sl.params["is_exposed"] is True

    def test_named_override_positional(self):
        result = parse_script("+var Health Float variable_type=Integer")
        sl = result.lines[0]
        assert sl.params["variable_type"] == "Integer"


# ── Context inheritance ─────────────────────────────────────────────


class TestContext:
    def test_context_injected_as_blueprint_name(self):
        result = parse_script("@BP_Enemy\ncompile")
        sl = result.lines[0]
        assert sl.command == "compile_blueprint"
        assert sl.params.get("blueprint_name") == "BP_Enemy"

    def test_context_injected_for_component(self):
        result = parse_script("@BP_Enemy\n+comp StaticMeshComponent Mesh")
        sl = result.lines[0]
        assert sl.params["blueprint_name"] == "BP_Enemy"
        assert sl.params["component_type"] == "StaticMeshComponent"
        assert sl.params["component_name"] == "Mesh"

    def test_context_for_create_uses_name(self):
        result = parse_script("@BP_Enemy\ncreate Actor")
        sl = result.lines[0]
        assert sl.params["name"] == "BP_Enemy"
        assert sl.params["parent_class"] == "Actor"

    def test_context_applies_to_all_subsequent(self):
        script = "@BP_Enemy\n+event ReceiveBeginPlay\n+var Health Float\ncompile"
        result = parse_script(script)
        for sl in result.lines:
            assert sl.params.get("blueprint_name") == "BP_Enemy"

    def test_no_context_no_injection(self):
        result = parse_script("compile")
        assert "blueprint_name" not in result.lines[0].params

    def test_context_change_midscript(self):
        script = "@BP_A\ncompile\n@BP_B\ncompile"
        result = parse_script(script)
        assert result.lines[0].params["blueprint_name"] == "BP_A"
        assert result.lines[1].params["blueprint_name"] == "BP_B"


# ── Value coercion ──────────────────────────────────────────────────


class TestCoercion:
    def test_bool_true(self):
        assert _coerce_value("true") is True

    def test_bool_false(self):
        assert _coerce_value("false") is False

    def test_int(self):
        assert _coerce_value("42") == 42

    def test_float(self):
        assert _coerce_value("3.14") == 3.14

    def test_string(self):
        assert _coerce_value("hello") == "hello"

    def test_json_array(self):
        assert _coerce_value("[1,2,3]") == [1, 2, 3]

    def test_json_object(self):
        assert _coerce_value('{"a":1}') == {"a": 1}


# ── Batch conversion ───────────────────────────────────────────────


class TestBatchConversion:
    def test_simple_batch(self):
        parsed = parse_script("@BP_Test\ncreate Actor\ncompile\nsave")
        commands = script_to_batch_commands(parsed)
        assert len(commands) == 3
        assert commands[0]["type"] == "create_blueprint"
        assert commands[0]["params"]["name"] == "BP_Test"
        assert commands[1]["type"] == "compile_blueprint"
        assert commands[2]["type"] == "save_all"

    def test_empty_params_not_included(self):
        parsed = parse_script("save")
        commands = script_to_batch_commands(parsed)
        assert commands[0] == {"type": "save_all"}


# ── Quoted strings ──────────────────────────────────────────────────


class TestQuotedStrings:
    def test_quoted_value(self):
        result = parse_script('+comment "This is a multi word comment"')
        sl = result.lines[0]
        assert sl.params["comment_text"] == "This is a multi word comment"

    def test_quoted_named_value(self):
        result = parse_script('+pin NODE1 InString default_value="Hello World"')
        sl = result.lines[0]
        assert sl.params["default_value"] == "Hello World"


# ── Full scenario ───────────────────────────────────────────────────


class TestFullScenario:
    def test_complete_blueprint_script(self):
        script = """
# Create an enemy character blueprint
@BP_EnemyCharacter
create Character /Game/Blueprints

+comp CapsuleComponent Capsule
+comp SkeletalMeshComponent CharMesh

+var Health Float is_exposed=true
+var MaxHealth Float
+var MoveSpeed Float

+event ReceiveBeginPlay
+event ReceiveTick

compile
save
"""
        parsed = parse_script(script)
        assert parsed.context_target == "BP_EnemyCharacter"
        assert len(parsed.lines) == 10
        assert parsed.errors == []

        # Verify create
        assert parsed.lines[0].command == "create_blueprint"
        assert parsed.lines[0].params["name"] == "BP_EnemyCharacter"
        assert parsed.lines[0].params["parent_class"] == "Character"

        # Verify components
        assert parsed.lines[1].params["component_type"] == "CapsuleComponent"
        assert parsed.lines[1].params["blueprint_name"] == "BP_EnemyCharacter"

        # Verify variables
        assert parsed.lines[3].params["variable_name"] == "Health"
        assert parsed.lines[3].params["is_exposed"] is True

        # Verify events
        assert parsed.lines[6].params["event_name"] == "ReceiveBeginPlay"

        # Convert to batch
        commands = script_to_batch_commands(parsed)
        assert len(commands) == 10

    def test_token_savings(self):
        """Demonstrate token savings: script vs JSON."""
        script = "@BP_Enemy\ncreate Actor\n+comp StaticMeshComponent Mesh\n+event ReceiveBeginPlay\ncompile"
        # Script is ~80 characters
        assert len(script) < 100

        # Equivalent JSON would be much longer
        parsed = parse_script(script)
        commands = script_to_batch_commands(parsed)
        import json

        json_equivalent = json.dumps(commands)
        # JSON is 3-5x longer
        assert len(json_equivalent) > len(script) * 2


# ── Format result ───────────────────────────────────────────────────


class TestFormatResult:
    def test_format_success(self):
        result = {
            "context_target": "BP_Test",
            "script_lines": 2,
            "executed": 2,
            "failed": 0,
            "results": [
                {"success": True, "_script_raw": "create Actor"},
                {"success": True, "_script_raw": "compile"},
            ],
        }
        formatted = format_script_result(result)
        assert "Target: @BP_Test" in formatted
        assert "✓ create Actor" in formatted
        assert "✓ compile" in formatted

    def test_format_failure(self):
        result = {
            "script_lines": 1,
            "executed": 1,
            "failed": 1,
            "results": [
                {
                    "success": False,
                    "_script_raw": "compile",
                    "error": "Blueprint not found",
                },
            ],
        }
        formatted = format_script_result(result)
        assert "✗ compile" in formatted
        assert "Blueprint not found" in formatted
