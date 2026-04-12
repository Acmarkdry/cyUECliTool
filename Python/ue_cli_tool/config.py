# coding: utf-8
"""
Project Configuration — persistent per-project settings loaded from
``ue_mcp_config.yaml``.

Provides :class:`ProjectConfig` (a dataclass holding engine paths, TCP port,
and extension directories) together with helpers to **load**, **save** and
**merge** configuration files.

Search strategy: starting from a given directory, walk upward until the
filesystem root looking for ``ue_mcp_config.yaml``.  The first match wins.
If no file is found the caller gets a default :class:`ProjectConfig`.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field, fields
from pathlib import Path
from typing import Any, Optional

import yaml

logger = logging.getLogger(__name__)

# ──── constants ──────────────────────────────────────────────────────────────

CONFIG_FILENAME = "ue_mcp_config.yaml"
_DEFAULT_TCP_PORT = 55558
_PORT_MIN = 1
_PORT_MAX = 65535

# Fields that are known to ProjectConfig (used for unknown-key detection).
_KNOWN_KEYS: frozenset[str] = frozenset()  # populated lazily after class def


# ──── dataclass ──────────────────────────────────────────────────────────────


@dataclass
class ProjectConfig:
    """Per-project configuration persisted in ``ue_mcp_config.yaml``."""

    engine_root: str | None = None
    project_root: str | None = None
    tcp_port: int = _DEFAULT_TCP_PORT
    lua_script_dirs: list[str] = field(default_factory=list)
    extra_action_paths: list[str] = field(default_factory=list)


# Populate _KNOWN_KEYS now that the class exists.
_KNOWN_KEYS = frozenset(f.name for f in fields(ProjectConfig))


# ──── helpers ────────────────────────────────────────────────────────────────


def _find_config_file(start_dir: Path) -> Optional[Path]:
    """Walk *start_dir* and its parents looking for ``ue_mcp_config.yaml``.

    Returns the :class:`Path` of the first match, or ``None``.
    """
    current = start_dir.resolve()
    while True:
        candidate = current / CONFIG_FILENAME
        if candidate.is_file():
            return candidate
        parent = current.parent
        if parent == current:
            # Reached filesystem root.
            break
        current = parent
    return None


def _validate_port(value: Any) -> int:
    """Return *value* as a valid TCP port, or fall back to the default."""
    try:
        port = int(value)
    except (TypeError, ValueError):
        logger.warning(
            "tcp_port value %r is not a valid integer; falling back to default %d",
            value,
            _DEFAULT_TCP_PORT,
        )
        return _DEFAULT_TCP_PORT

    if not (_PORT_MIN <= port <= _PORT_MAX):
        logger.warning(
            "tcp_port %d is out of valid range (%d–%d); falling back to default %d",
            port,
            _PORT_MIN,
            _PORT_MAX,
            _DEFAULT_TCP_PORT,
        )
        return _DEFAULT_TCP_PORT
    return port


def _dict_to_config(data: dict[str, Any]) -> ProjectConfig:
    """Build a :class:`ProjectConfig` from a raw dictionary.

    Unknown keys are logged and ignored.  ``tcp_port`` is validated.
    """
    unknown = set(data.keys()) - _KNOWN_KEYS
    for key in sorted(unknown):
        logger.warning("Ignoring unknown config key: %s", key)

    kwargs: dict[str, Any] = {}
    for f in fields(ProjectConfig):
        if f.name in data:
            kwargs[f.name] = data[f.name]

    # Validate tcp_port specifically.
    if "tcp_port" in kwargs:
        kwargs["tcp_port"] = _validate_port(kwargs["tcp_port"])

    return ProjectConfig(**kwargs)


# ──── public API ─────────────────────────────────────────────────────────────


def load_config(start_dir: Optional[Path] = None) -> ProjectConfig:
    """Load project configuration by searching upward from *start_dir*.

    * If *start_dir* is ``None``, the current working directory is used.
    * If no ``ue_mcp_config.yaml`` is found, a default config is returned.
    * If the file contains invalid YAML, an ERROR is logged and the default
      config is returned.
    """
    if start_dir is None:
        start_dir = Path.cwd()
    else:
        start_dir = Path(start_dir)

    config_path = _find_config_file(start_dir)
    if config_path is None:
        return ProjectConfig()

    try:
        text = config_path.read_text(encoding="utf-8")
    except OSError as exc:
        logger.error("Failed to read config file %s: %s", config_path, exc)
        return ProjectConfig()

    try:
        data = yaml.safe_load(text)
    except yaml.YAMLError as exc:
        logger.error(
            "Invalid YAML in config file %s: %s — falling back to default config",
            config_path,
            exc,
        )
        return ProjectConfig()

    if not isinstance(data, dict):
        # e.g. the file is empty or contains only a scalar
        if data is not None:
            logger.error(
                "Config file %s does not contain a YAML mapping — falling back to default config",
                config_path,
            )
        return ProjectConfig()

    return _dict_to_config(data)


def save_config(config: ProjectConfig, path: Path) -> None:
    """Serialize *config* to YAML and write it to *path*.

    * ``None``-valued fields are **not** written.
    * Empty-list fields are **not** written.
    """
    path = Path(path)
    data: dict[str, Any] = {}
    for f in fields(config):
        value = getattr(config, f.name)
        if value is None:
            continue
        if isinstance(value, list) and len(value) == 0:
            continue
        data[f.name] = value

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        yaml.dump(data, default_flow_style=False, allow_unicode=True, sort_keys=False),
        encoding="utf-8",
    )


def merge_config(existing: ProjectConfig, updates: dict[str, Any]) -> ProjectConfig:
    """Merge *updates* into *existing*, returning a **new** :class:`ProjectConfig`.

    Keys present in *updates* overwrite the corresponding field in *existing*.
    Keys in *existing* that are **not** in *updates* are preserved unchanged.
    Unknown keys in *updates* are ignored (with a warning).
    """
    unknown = set(updates.keys()) - _KNOWN_KEYS
    for key in sorted(unknown):
        logger.warning("Ignoring unknown config key during merge: %s", key)

    merged: dict[str, Any] = {}
    for f in fields(existing):
        if f.name in updates:
            merged[f.name] = updates[f.name]
        else:
            merged[f.name] = getattr(existing, f.name)

    # Validate tcp_port if it was part of the update.
    if "tcp_port" in updates:
        merged["tcp_port"] = _validate_port(merged["tcp_port"])

    return ProjectConfig(**merged)
