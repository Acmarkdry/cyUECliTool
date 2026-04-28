# coding: utf-8
"""
Property-based tests for ProjectConfig YAML serialization round-trip.

Feature: v0.4.0-platform-extensions, Property 1: ProjectConfig YAML serialization round-trip

Uses Hypothesis to generate arbitrary valid ProjectConfig objects and verifies
that save_config → load_config produces an equivalent configuration.

**Validates: Requirements 1.6**
"""

from __future__ import annotations

import tempfile
from pathlib import Path

from hypothesis import given, settings
from hypothesis import strategies as st

from ue_cli_tool.config import (
    CONFIG_FILENAME,
    ProjectConfig,
    load_config,
    save_config,
    _DEFAULT_TCP_PORT,
    _PORT_MAX,
    _PORT_MIN,
)

# ---------------------------------------------------------------------------
# Strategies
# ---------------------------------------------------------------------------

# Generate path-like strings that are valid but non-empty, or None.
_path_str = st.one_of(
    st.none(),
    st.text(
        alphabet=st.sampled_from(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/\\:. "
        ),
        min_size=1,
        max_size=120,
    ),
)

# Valid TCP port within the allowed range.
_tcp_port = st.integers(min_value=_PORT_MIN, max_value=_PORT_MAX)

# Lists of path-like strings (for lua_script_dirs and extra_action_paths).
_path_list = st.lists(
    st.text(
        alphabet=st.sampled_from(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/\\:."
        ),
        min_size=1,
        max_size=60,
    ),
    min_size=0,
    max_size=5,
)

# Composite strategy that builds a valid ProjectConfig.
_project_config = st.builds(
    ProjectConfig,
    engine_root=_path_str,
    project_root=_path_str,
    tcp_port=_tcp_port,
    lua_script_dirs=_path_list,
    extra_action_paths=_path_list,
)


# ---------------------------------------------------------------------------
# Property 1: ProjectConfig YAML serialization round-trip
# ---------------------------------------------------------------------------


@given(config=_project_config)
@settings(max_examples=100)
def test_project_config_yaml_round_trip(config: ProjectConfig):
    """For any valid ProjectConfig, save_config → load_config produces an
    equivalent configuration object.

    Feature: v0.4.0-platform-extensions, Property 1: ProjectConfig YAML serialization round-trip

    **Validates: Requirements 1.6**
    """
    with tempfile.TemporaryDirectory() as tmp:
        yaml_path = Path(tmp) / CONFIG_FILENAME
        save_config(config, yaml_path)
        loaded = load_config(start_dir=Path(tmp))

    # --- field-by-field equivalence ---
    # engine_root and project_root: None values are not written, so they
    # round-trip back to the dataclass default (None).
    assert loaded.engine_root == config.engine_root, (
        f"engine_root mismatch: {loaded.engine_root!r} != {config.engine_root!r}"
    )
    assert loaded.project_root == config.project_root, (
        f"project_root mismatch: {loaded.project_root!r} != {config.project_root!r}"
    )

    # tcp_port is always written (it's an int, never None).
    assert loaded.tcp_port == config.tcp_port, (
        f"tcp_port mismatch: {loaded.tcp_port!r} != {config.tcp_port!r}"
    )

    # Lists: empty lists are not written → round-trip to default [].
    assert loaded.lua_script_dirs == config.lua_script_dirs, (
        f"lua_script_dirs mismatch: {loaded.lua_script_dirs!r} != {config.lua_script_dirs!r}"
    )
    assert loaded.extra_action_paths == config.extra_action_paths, (
        f"extra_action_paths mismatch: {loaded.extra_action_paths!r} != {config.extra_action_paths!r}"
    )


# ---------------------------------------------------------------------------
# Additional imports for Property 2
# ---------------------------------------------------------------------------

from dataclasses import fields as dataclass_fields

from ue_cli_tool.config import merge_config

# ---------------------------------------------------------------------------
# Strategies for Property 2
# ---------------------------------------------------------------------------

# Strategy that generates a dict of valid updates for ProjectConfig.
# Each key is a valid ProjectConfig field name, and the value matches
# the field's expected type.  We draw a random subset of field names
# so that some keys are present and some are absent.
_field_value_strategies: dict[str, st.SearchStrategy] = {
    "engine_root": _path_str,
    "project_root": _path_str,
    "tcp_port": _tcp_port,
    "lua_script_dirs": _path_list,
    "extra_action_paths": _path_list,
}

_updates_dict = st.fixed_dictionaries(
    {},  # no mandatory keys
    optional=_field_value_strategies,
)


# ---------------------------------------------------------------------------
# Property 2: config merge preserves user customizations
# ---------------------------------------------------------------------------


@given(existing=_project_config, updates=_updates_dict)
@settings(max_examples=100)
def test_config_merge_preserves_user_customizations(
    existing: ProjectConfig, updates: dict
):
    """For any valid ProjectConfig and any updates dict, merge_config
    preserves all keys from existing that are NOT in updates AND includes
    all keys from updates.

    Feature: v0.4.0-platform-extensions, Property 2: config merge preserves user customizations

    **Validates: Requirements 2.2**
    """
    merged = merge_config(existing, updates)

    known_field_names = {f.name for f in dataclass_fields(ProjectConfig)}

    for fname in known_field_names:
        merged_val = getattr(merged, fname)
        if fname in updates:
            # Special case: tcp_port is validated, so out-of-range values
            # fall back to the default.  The test only checks valid ports
            # (our strategy already constrains to valid range), so a
            # direct equality check is fine.
            expected = updates[fname]
            assert merged_val == expected, (
                f"Field '{fname}' should come from updates: "
                f"merged={merged_val!r}, updates={expected!r}"
            )
        else:
            # Key NOT in updates → must be preserved from existing.
            expected = getattr(existing, fname)
            assert merged_val == expected, (
                f"Field '{fname}' should be preserved from existing: "
                f"merged={merged_val!r}, existing={expected!r}"
            )


# ---------------------------------------------------------------------------
# Unit tests for ProjectConfig (example-based)
# ---------------------------------------------------------------------------

import logging

import yaml


class TestLoadConfigFileNotExist:
    """Requirement 1.2: When ue_mcp_config.yaml does not exist, load_config
    returns a default ProjectConfig without errors."""

    def test_returns_default_when_no_config_file(self, tmp_path):
        """Loading from a directory with no config file yields defaults."""
        config = load_config(start_dir=tmp_path)

        assert config.engine_root is None
        assert config.project_root is None
        assert config.tcp_port == _DEFAULT_TCP_PORT
        assert config.lua_script_dirs == []
        assert config.extra_action_paths == []


class TestLoadConfigInvalidYAML:
    """Requirement 1.5: Invalid YAML falls back to default config and logs
    an ERROR."""

    def test_invalid_yaml_falls_back_to_default(self, tmp_path, caplog):
        """A config file with broken YAML triggers an ERROR log and returns
        the default config."""
        bad_yaml = tmp_path / CONFIG_FILENAME
        bad_yaml.write_text("{{{{not: valid: yaml: [", encoding="utf-8")

        with caplog.at_level(logging.ERROR):
            config = load_config(start_dir=tmp_path)

        # Should fall back to defaults.
        assert config.engine_root is None
        assert config.project_root is None
        assert config.tcp_port == _DEFAULT_TCP_PORT
        assert config.lua_script_dirs == []
        assert config.extra_action_paths == []

        # An ERROR log must have been emitted.
        error_messages = [r.message for r in caplog.records if r.levelno >= logging.ERROR]
        assert any("Invalid YAML" in m or "falling back" in m for m in error_messages), (
            f"Expected an ERROR log about invalid YAML, got: {error_messages}"
        )


class TestLoadConfigUnknownKeys:
    """Requirement 1.4: Unknown keys in the config file are ignored and a
    WARNING is logged."""

    def test_unknown_keys_ignored_with_warning(self, tmp_path, caplog):
        """Extra keys not in ProjectConfig are silently dropped, but a
        WARNING is recorded."""
        cfg_path = tmp_path / CONFIG_FILENAME
        cfg_path.write_text(
            yaml.dump(
                {
                    "engine_root": "/some/path",
                    "tcp_port": 12345,
                    "totally_unknown_key": "surprise",
                    "another_mystery": 42,
                },
                default_flow_style=False,
            ),
            encoding="utf-8",
        )

        with caplog.at_level(logging.WARNING):
            config = load_config(start_dir=tmp_path)

        # Known fields should be loaded correctly.
        assert config.engine_root == "/some/path"
        assert config.tcp_port == 12345

        # WARNING logs for each unknown key.
        warning_messages = [r.message for r in caplog.records if r.levelno == logging.WARNING]
        assert any("totally_unknown_key" in m for m in warning_messages), (
            f"Expected WARNING about 'totally_unknown_key', got: {warning_messages}"
        )
        assert any("another_mystery" in m for m in warning_messages), (
            f"Expected WARNING about 'another_mystery', got: {warning_messages}"
        )


class TestTcpPortOutOfRange:
    """Requirement 1.3 / error handling: tcp_port out of valid range (1–65535)
    falls back to the default 55558."""

    def test_port_too_high_falls_back(self, tmp_path, caplog):
        cfg_path = tmp_path / CONFIG_FILENAME
        cfg_path.write_text(
            yaml.dump({"tcp_port": 99999}, default_flow_style=False),
            encoding="utf-8",
        )

        with caplog.at_level(logging.WARNING):
            config = load_config(start_dir=tmp_path)

        assert config.tcp_port == _DEFAULT_TCP_PORT

        warning_messages = [r.message for r in caplog.records if r.levelno == logging.WARNING]
        assert any("tcp_port" in m for m in warning_messages)

    def test_port_zero_falls_back(self, tmp_path, caplog):
        cfg_path = tmp_path / CONFIG_FILENAME
        cfg_path.write_text(
            yaml.dump({"tcp_port": 0}, default_flow_style=False),
            encoding="utf-8",
        )

        with caplog.at_level(logging.WARNING):
            config = load_config(start_dir=tmp_path)

        assert config.tcp_port == _DEFAULT_TCP_PORT

    def test_port_negative_falls_back(self, tmp_path, caplog):
        cfg_path = tmp_path / CONFIG_FILENAME
        cfg_path.write_text(
            yaml.dump({"tcp_port": -1}, default_flow_style=False),
            encoding="utf-8",
        )

        with caplog.at_level(logging.WARNING):
            config = load_config(start_dir=tmp_path)

        assert config.tcp_port == _DEFAULT_TCP_PORT


class TestSaveConfigOmitsNoneAndEmptyLists:
    """Requirement 1.6 / design: None values and empty lists are NOT written
    to the YAML output."""

    def test_none_values_not_written(self, tmp_path):
        """Fields set to None should be absent from the serialized YAML."""
        config = ProjectConfig(engine_root=None, project_root=None, tcp_port=55558)
        out_path = tmp_path / CONFIG_FILENAME
        save_config(config, out_path)

        raw = yaml.safe_load(out_path.read_text(encoding="utf-8"))
        assert "engine_root" not in raw
        assert "project_root" not in raw

    def test_empty_lists_not_written(self, tmp_path):
        """Fields that are empty lists should be absent from the serialized
        YAML."""
        config = ProjectConfig(
            engine_root="/e",
            lua_script_dirs=[],
            extra_action_paths=[],
        )
        out_path = tmp_path / CONFIG_FILENAME
        save_config(config, out_path)

        raw = yaml.safe_load(out_path.read_text(encoding="utf-8"))
        assert "lua_script_dirs" not in raw
        assert "extra_action_paths" not in raw
        # Non-empty / non-None fields should still be present.
        assert raw["engine_root"] == "/e"
        assert raw["tcp_port"] == 55558

    def test_populated_fields_are_written(self, tmp_path):
        """Non-None, non-empty fields should appear in the YAML."""
        config = ProjectConfig(
            engine_root="/engine",
            project_root="/project",
            tcp_port=9999,
            lua_script_dirs=["scripts/lua"],
            extra_action_paths=["extra/actions"],
        )
        out_path = tmp_path / CONFIG_FILENAME
        save_config(config, out_path)

        raw = yaml.safe_load(out_path.read_text(encoding="utf-8"))
        assert raw["engine_root"] == "/engine"
        assert raw["project_root"] == "/project"
        assert raw["tcp_port"] == 9999
        assert raw["lua_script_dirs"] == ["scripts/lua"]
        assert raw["extra_action_paths"] == ["extra/actions"]
