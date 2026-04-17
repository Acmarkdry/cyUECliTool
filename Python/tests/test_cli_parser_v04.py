# coding: utf-8
"""
Property-based tests for CLI Parser array syntax equivalence.

Feature: v0.4.0-platform-extensions, Property 3: array syntax equivalence

Uses Hypothesis to generate arrays of same-type elements (strings or numbers)
and verifies that comma shorthand syntax (e.g., ``a,b,c``) produces the same
result as JSON array syntax (e.g., ``["a","b","c"]``).

**Validates: Requirements 13.4, 13.1, 13.2, 13.3**
"""

from __future__ import annotations

import json
from typing import Any

from hypothesis import given, settings, assume
from hypothesis import strategies as st

from ue_cli_tool.cli_parser import CliParser
from ue_cli_tool.registry import ActionDef, ActionRegistry

# ---------------------------------------------------------------------------
# Test helpers
# ---------------------------------------------------------------------------

# A dummy command name used in tests.
_TEST_COMMAND = "test_array_cmd"
_TEST_PARAM = "items"


def _make_array_registry() -> ActionRegistry:
    """Create a minimal ActionRegistry with one action whose ``items``
    parameter has schema type ``array``."""
    registry = ActionRegistry()
    registry.register(
        ActionDef(
            id="test.array",
            command=_TEST_COMMAND,
            tags=("test",),
            description="Test action with array param",
            input_schema={
                "type": "object",
                "properties": {
                    _TEST_PARAM: {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "Array parameter for testing",
                    },
                },
                "required": [_TEST_PARAM],
            },
        )
    )
    return registry


def _make_parser() -> CliParser:
    """Create a CliParser backed by the array-param test registry."""
    return CliParser(_make_array_registry())


# ---------------------------------------------------------------------------
# Strategies
# ---------------------------------------------------------------------------

# Strings that are valid comma-separated elements: non-empty, no commas,
# no leading/trailing whitespace that would be stripped, no characters that
# look like JSON or flags, and not parseable as bool/int/float so they
# stay as strings through both paths.
_safe_string_element = st.text(
    alphabet=st.sampled_from(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"
    ),
    min_size=1,
    max_size=20,
).filter(lambda s: s.lower() not in ("true", "false"))

# Integer elements — use a moderate range to avoid float precision issues.
_int_element = st.integers(min_value=-9999, max_value=9999)

# Float elements — use finite floats that round-trip cleanly through JSON.
_float_element = st.floats(
    min_value=-9999.0,
    max_value=9999.0,
    allow_nan=False,
    allow_infinity=False,
).filter(lambda f: f == float(json.loads(json.dumps(f))))

# Arrays of same-type elements (at least 2 elements to have commas).
_string_array = st.lists(_safe_string_element, min_size=2, max_size=10)
_int_array = st.lists(_int_element, min_size=2, max_size=10)
_float_array = st.lists(_float_element, min_size=2, max_size=10)


# ---------------------------------------------------------------------------
# Property 3: array syntax equivalence (comma shorthand ≡ JSON)
# ---------------------------------------------------------------------------


@given(elements=_string_array)
@settings(max_examples=100)
def test_string_array_comma_shorthand_equals_json(elements: list[str]):
    """For any array of string elements, comma shorthand ``a,b,c`` produces
    the same parsed result as JSON array ``["a","b","c"]``.

    Feature: v0.4.0-platform-extensions, Property 3: array syntax equivalence

    **Validates: Requirements 13.4, 13.1, 13.2, 13.3**
    """
    parser = _make_parser()

    # Build comma shorthand: a,b,c
    comma_val = ",".join(elements)
    # Build JSON array: ["a","b","c"]
    json_val = json.dumps(elements)

    # Parse via comma shorthand (schema-aware path)
    comma_result = parser._coerce_value_with_schema(
        comma_val, _TEST_PARAM, _TEST_COMMAND
    )
    # Parse via JSON array (starts with '[', uses existing JSON path)
    json_result = parser._coerce_value_with_schema(
        json_val, _TEST_PARAM, _TEST_COMMAND
    )

    assert comma_result == json_result, (
        f"Comma shorthand and JSON array should produce equal results.\n"
        f"  elements:     {elements!r}\n"
        f"  comma_val:    {comma_val!r} → {comma_result!r}\n"
        f"  json_val:     {json_val!r} → {json_result!r}"
    )


@given(elements=_int_array)
@settings(max_examples=100)
def test_int_array_comma_shorthand_equals_json(elements: list[int]):
    """For any array of integer elements, comma shorthand ``1,2,3`` produces
    the same parsed result as JSON array ``[1,2,3]``.

    Feature: v0.4.0-platform-extensions, Property 3: array syntax equivalence

    **Validates: Requirements 13.4, 13.1, 13.2, 13.3**
    """
    parser = _make_parser()

    # Build comma shorthand: 1,2,3
    comma_val = ",".join(str(e) for e in elements)
    # Build JSON array: [1,2,3]
    json_val = json.dumps(elements)

    comma_result = parser._coerce_value_with_schema(
        comma_val, _TEST_PARAM, _TEST_COMMAND
    )
    json_result = parser._coerce_value_with_schema(
        json_val, _TEST_PARAM, _TEST_COMMAND
    )

    assert comma_result == json_result, (
        f"Comma shorthand and JSON array should produce equal results.\n"
        f"  elements:     {elements!r}\n"
        f"  comma_val:    {comma_val!r} → {comma_result!r}\n"
        f"  json_val:     {json_val!r} → {json_result!r}"
    )


@given(elements=_float_array)
@settings(max_examples=100)
def test_float_array_comma_shorthand_equals_json(elements: list[float]):
    """For any array of float elements, comma shorthand ``1.5,2.5`` produces
    the same parsed result as JSON array ``[1.5,2.5]``.

    Feature: v0.4.0-platform-extensions, Property 3: array syntax equivalence

    **Validates: Requirements 13.4, 13.1, 13.2, 13.3**
    """
    parser = _make_parser()

    # Build comma shorthand: 1.5,2.5
    comma_val = ",".join(str(e) for e in elements)
    # Build JSON array: [1.5,2.5]
    json_val = json.dumps(elements)

    comma_result = parser._coerce_value_with_schema(
        comma_val, _TEST_PARAM, _TEST_COMMAND
    )
    json_result = parser._coerce_value_with_schema(
        json_val, _TEST_PARAM, _TEST_COMMAND
    )

    assert comma_result == json_result, (
        f"Comma shorthand and JSON array should produce equal results.\n"
        f"  elements:     {elements!r}\n"
        f"  comma_val:    {comma_val!r} → {comma_result!r}\n"
        f"  json_val:     {json_val!r} → {json_result!r}"
    )


# ---------------------------------------------------------------------------
# Property 4 helpers
# ---------------------------------------------------------------------------

_TEST_OBJECT_COMMAND = "test_object_cmd"
_TEST_OBJECT_PARAM = "props"


def _make_object_registry() -> ActionRegistry:
    """Create a minimal ActionRegistry with one action whose ``props``
    parameter has schema type ``object``."""
    registry = ActionRegistry()
    registry.register(
        ActionDef(
            id="test.object",
            command=_TEST_OBJECT_COMMAND,
            tags=("test",),
            description="Test action with object param",
            input_schema={
                "type": "object",
                "properties": {
                    _TEST_OBJECT_PARAM: {
                        "type": "object",
                        "description": "Object parameter for testing",
                    },
                },
                "required": [_TEST_OBJECT_PARAM],
            },
        )
    )
    return registry


def _make_object_parser() -> CliParser:
    """Create a CliParser backed by the object-param test registry."""
    return CliParser(_make_object_registry())


# ---------------------------------------------------------------------------
# Property 4 strategies
# ---------------------------------------------------------------------------

# Keys: simple alphanumeric strings (no commas, no equals signs, no whitespace).
_safe_key = st.text(
    alphabet=st.sampled_from(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
    ),
    min_size=1,
    max_size=15,
).filter(lambda s: s.lower() not in ("true", "false") and not s.startswith("_"))

# String values: no commas, no equals signs, no whitespace, not parseable as
# bool/int/float so they stay as strings through both paths.
_safe_string_value = st.text(
    alphabet=st.sampled_from(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"
    ),
    min_size=1,
    max_size=20,
).filter(lambda s: s.lower() not in ("true", "false"))

# Integer values — moderate range.
_int_value = st.integers(min_value=-9999, max_value=9999)

# A single key-value pair where value is a string.
_string_kv_pair = st.tuples(_safe_key, _safe_string_value)

# A single key-value pair where value is an integer.
_int_kv_pair = st.tuples(_safe_key, _int_value)

# Dicts of string values (at least 1 entry so there's an '=' sign).
_string_object = st.lists(
    _string_kv_pair, min_size=1, max_size=8
).filter(
    # Ensure unique keys
    lambda pairs: len(set(k for k, _ in pairs)) == len(pairs)
)

# Dicts of integer values (at least 1 entry).
_int_object = st.lists(
    _int_kv_pair, min_size=1, max_size=8
).filter(
    lambda pairs: len(set(k for k, _ in pairs)) == len(pairs)
)

# Mixed dicts: values are either strings or integers.
_mixed_value = st.one_of(_safe_string_value, _int_value)
_mixed_kv_pair = st.tuples(_safe_key, _mixed_value)
_mixed_object = st.lists(
    _mixed_kv_pair, min_size=1, max_size=8
).filter(
    lambda pairs: len(set(k for k, _ in pairs)) == len(pairs)
)


# ---------------------------------------------------------------------------
# Property 4: object syntax equivalence (key=value shorthand ≡ JSON)
# ---------------------------------------------------------------------------


@given(pairs=_string_object)
@settings(max_examples=100)
def test_string_object_kv_shorthand_equals_json(pairs: list[tuple[str, str]]):
    """For any object with string values, key=value shorthand
    ``name=Sword,type=Weapon`` produces the same parsed result as JSON object
    ``{"name":"Sword","type":"Weapon"}``.

    Feature: v0.4.0-platform-extensions, Property 4: object syntax equivalence

    **Validates: Requirements 14.3, 14.1, 14.2**
    """
    parser = _make_object_parser()

    obj = {k: v for k, v in pairs}

    # Build key=value shorthand: name=Sword,type=Weapon
    kv_val = ",".join(f"{k}={v}" for k, v in pairs)
    # Build JSON object: {"name":"Sword","type":"Weapon"}
    json_val = json.dumps(obj)

    # Parse via key=value shorthand (schema-aware path)
    kv_result = parser._coerce_value_with_schema(
        kv_val, _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
    )
    # Parse via JSON object (starts with '{', uses existing JSON path)
    json_result = parser._coerce_value_with_schema(
        json_val, _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
    )

    assert kv_result == json_result, (
        f"Key=value shorthand and JSON object should produce equal results.\n"
        f"  pairs:      {pairs!r}\n"
        f"  kv_val:     {kv_val!r} → {kv_result!r}\n"
        f"  json_val:   {json_val!r} → {json_result!r}"
    )


@given(pairs=_int_object)
@settings(max_examples=100)
def test_int_object_kv_shorthand_equals_json(pairs: list[tuple[str, int]]):
    """For any object with integer values, key=value shorthand
    ``damage=50,health=100`` produces the same parsed result as JSON object
    ``{"damage":50,"health":100}``.

    Feature: v0.4.0-platform-extensions, Property 4: object syntax equivalence

    **Validates: Requirements 14.3, 14.1, 14.2**
    """
    parser = _make_object_parser()

    obj = {k: v for k, v in pairs}

    # Build key=value shorthand: damage=50,health=100
    kv_val = ",".join(f"{k}={v}" for k, v in pairs)
    # Build JSON object: {"damage":50,"health":100}
    json_val = json.dumps(obj)

    kv_result = parser._coerce_value_with_schema(
        kv_val, _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
    )
    json_result = parser._coerce_value_with_schema(
        json_val, _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
    )

    assert kv_result == json_result, (
        f"Key=value shorthand and JSON object should produce equal results.\n"
        f"  pairs:      {pairs!r}\n"
        f"  kv_val:     {kv_val!r} → {kv_result!r}\n"
        f"  json_val:   {json_val!r} → {json_result!r}"
    )


@given(pairs=_mixed_object)
@settings(max_examples=100)
def test_mixed_object_kv_shorthand_equals_json(
    pairs: list[tuple[str, str | int]],
):
    """For any object with mixed string/integer values, key=value shorthand
    ``name=Sword,damage=50`` produces the same parsed result as JSON object
    ``{"name":"Sword","damage":50}``.

    Feature: v0.4.0-platform-extensions, Property 4: object syntax equivalence

    **Validates: Requirements 14.3, 14.1, 14.2**
    """
    parser = _make_object_parser()

    obj = {k: v for k, v in pairs}

    # Build key=value shorthand: name=Sword,damage=50
    kv_val = ",".join(f"{k}={v}" for k, v in pairs)
    # Build JSON object: {"name":"Sword","damage":50}
    json_val = json.dumps(obj)

    kv_result = parser._coerce_value_with_schema(
        kv_val, _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
    )
    json_result = parser._coerce_value_with_schema(
        json_val, _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
    )

    assert kv_result == json_result, (
        f"Key=value shorthand and JSON object should produce equal results.\n"
        f"  pairs:      {pairs!r}\n"
        f"  kv_val:     {kv_val!r} → {kv_result!r}\n"
        f"  json_val:   {json_val!r} → {json_result!r}"
    )


# ===========================================================================
# Example-based unit tests for CLI Parser v0.4 extensions (Task 4.4)
# ===========================================================================
# These tests validate specific examples and edge cases for the comma-
# separated array shorthand, key=value object shorthand, JSON compatibility,
# schema fallback, and parse-failure fallback behaviors.
#
# Requirements: 13.1, 13.2, 13.3, 13.4, 14.1, 14.2, 14.3
# ===========================================================================


class TestCommaSeparatedStringArray:
    """Comma-separated string values are parsed as a string array when
    the schema type is ``array``.

    **Validates: Requirements 13.1**
    """

    def test_basic_string_array(self):
        parser = _make_parser()
        result = parser._coerce_value_with_schema("a,b,c", _TEST_PARAM, _TEST_COMMAND)
        assert result == ["a", "b", "c"]

    def test_single_element_no_comma(self):
        """A single element without commas should NOT be parsed as an array."""
        parser = _make_parser()
        result = parser._coerce_value_with_schema("hello", _TEST_PARAM, _TEST_COMMAND)
        # No comma → falls through to _coerce_value → plain string
        assert result == "hello"

    def test_two_elements(self):
        parser = _make_parser()
        result = parser._coerce_value_with_schema("foo,bar", _TEST_PARAM, _TEST_COMMAND)
        assert result == ["foo", "bar"]


class TestCommaSeparatedNumberArray:
    """Comma-separated numeric values are parsed as a number array when
    the schema type is ``array``.

    **Validates: Requirements 13.2**
    """

    def test_integer_array(self):
        parser = _make_parser()
        result = parser._coerce_value_with_schema("1,2,3", _TEST_PARAM, _TEST_COMMAND)
        assert result == [1, 2, 3]

    def test_negative_integers(self):
        parser = _make_parser()
        result = parser._coerce_value_with_schema("-1,0,5", _TEST_PARAM, _TEST_COMMAND)
        assert result == [-1, 0, 5]

    def test_float_array(self):
        parser = _make_parser()
        result = parser._coerce_value_with_schema(
            "1.5,2.5,3.5", _TEST_PARAM, _TEST_COMMAND
        )
        assert result == [1.5, 2.5, 3.5]


class TestKeyValueShorthand:
    """Key=value shorthand is parsed as an object when the schema type
    is ``object``.

    **Validates: Requirements 14.1, 14.2**
    """

    def test_mixed_string_and_number_values(self):
        parser = _make_object_parser()
        result = parser._coerce_value_with_schema(
            "name=Sword,damage=50", _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
        )
        assert result == {"name": "Sword", "damage": 50}

    def test_all_string_values(self):
        parser = _make_object_parser()
        result = parser._coerce_value_with_schema(
            "type=Weapon,rarity=Rare", _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
        )
        assert result == {"type": "Weapon", "rarity": "Rare"}

    def test_all_integer_values(self):
        parser = _make_object_parser()
        result = parser._coerce_value_with_schema(
            "x=10,y=20,z=30", _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
        )
        assert result == {"x": 10, "y": 20, "z": 30}

    def test_single_key_value_pair(self):
        parser = _make_object_parser()
        result = parser._coerce_value_with_schema(
            "name=Sword", _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
        )
        assert result == {"name": "Sword"}


class TestJSONSyntaxCompatibility:
    """Existing JSON array and object syntax must remain fully compatible
    and take priority over shorthand parsing.

    **Validates: Requirements 13.3, 14.2**
    """

    def test_json_array_not_affected(self):
        parser = _make_parser()
        result = parser._coerce_value_with_schema(
            "[1,2,3]", _TEST_PARAM, _TEST_COMMAND
        )
        assert result == [1, 2, 3]

    def test_json_object_not_affected(self):
        parser = _make_object_parser()
        result = parser._coerce_value_with_schema(
            '{"a":1}', _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
        )
        assert result == {"a": 1}

    def test_json_string_array(self):
        parser = _make_parser()
        result = parser._coerce_value_with_schema(
            '["x","y","z"]', _TEST_PARAM, _TEST_COMMAND
        )
        assert result == ["x", "y", "z"]

    def test_json_nested_object(self):
        parser = _make_object_parser()
        result = parser._coerce_value_with_schema(
            '{"a":{"b":2}}', _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
        )
        assert result == {"a": {"b": 2}}


class TestNoSchemaFallback:
    """When no schema information is available (unknown command), the parser
    falls back to the original ``_coerce_value()`` behavior.

    **Validates: Requirements 13.1, 14.1 (fallback clause)**
    """

    def test_comma_value_without_schema_stays_string(self):
        """Without schema info, ``a,b,c`` is NOT parsed as an array."""
        parser = _make_parser()
        # Use an unknown command so no schema is found
        result = parser._coerce_value_with_schema(
            "a,b,c", "unknown_param", "unknown_command"
        )
        # _coerce_value treats this as a plain string
        assert result == "a,b,c"

    def test_kv_value_without_schema_stays_string(self):
        """Without schema info, ``name=Sword`` is NOT parsed as an object."""
        parser = _make_object_parser()
        result = parser._coerce_value_with_schema(
            "name=Sword", "unknown_param", "unknown_command"
        )
        assert result == "name=Sword"

    def test_number_without_schema_coerced_normally(self):
        """Without schema, a plain number is still coerced to int/float."""
        parser = _make_parser()
        result = parser._coerce_value_with_schema(
            "42", "unknown_param", "unknown_command"
        )
        assert result == 42

    def test_bool_without_schema_coerced_normally(self):
        """Without schema, ``true``/``false`` are still coerced to bool."""
        parser = _make_parser()
        result = parser._coerce_value_with_schema(
            "true", "unknown_param", "unknown_command"
        )
        assert result is True


class TestParseFailureFallback:
    """When shorthand parsing fails or produces unexpected results, the
    parser falls back to returning the value as a plain string.

    **Validates: Requirements 13.1, 14.1 (error handling)**
    """

    def test_empty_string_stays_string(self):
        """An empty string should be returned as-is."""
        parser = _make_parser()
        result = parser._coerce_value_with_schema("", _TEST_PARAM, _TEST_COMMAND)
        assert result == ""

    def test_json_parse_failure_falls_back_to_string(self):
        """Malformed JSON starting with ``[`` falls back to string."""
        parser = _make_parser()
        result = parser._coerce_value_with_schema(
            "[not valid json", _TEST_PARAM, _TEST_COMMAND
        )
        # Starts with '[' → tries JSON parse → fails → returns as string
        assert result == "[not valid json"

    def test_malformed_json_object_falls_back_to_string(self):
        """Malformed JSON starting with ``{`` falls back to string."""
        parser = _make_object_parser()
        result = parser._coerce_value_with_schema(
            "{bad json", _TEST_OBJECT_PARAM, _TEST_OBJECT_COMMAND
        )
        assert result == "{bad json"
