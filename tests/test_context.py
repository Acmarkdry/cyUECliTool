"""
Tests for ContextStore — session management, history, workset, persistence.
"""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path

import pytest

# Adjust import path so we can import ContextStore regardless of installed package
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Python"))

from ue_editor_mcp.context import ContextStore, ASSET_PARAM_KEYS


@pytest.fixture
def ctx_dir(tmp_path: Path) -> Path:
    """Return a temporary .context directory."""
    d = tmp_path / ".context"
    d.mkdir()
    return d


@pytest.fixture
def store(ctx_dir: Path) -> ContextStore:
    """Create a fresh ContextStore."""
    return ContextStore(ctx_dir)


# ── Session lifecycle ───────────────────────────────────────────────────


class TestSessionLifecycle:
    def test_new_session_created(self, store: ContextStore, ctx_dir: Path):
        session = json.loads((ctx_dir / "session.json").read_text("utf-8"))
        assert session["status"] == "active"
        assert session["session_id"]
        assert session["started_at"]
        assert session["previous_session"] is None

    def test_shutdown_marks_ended(self, store: ContextStore, ctx_dir: Path):
        store.shutdown()
        session = json.loads((ctx_dir / "session.json").read_text("utf-8"))
        assert session["status"] == "ended"
        assert session["ended_at"] is not None

    def test_abnormal_previous_session(self, ctx_dir: Path):
        # Simulate a session that was never properly ended
        (ctx_dir / "session.json").write_text(
            json.dumps({"session_id": "old-id", "status": "active", "started_at": "2024-01-01T00:00:00+00:00", "op_count": 5}),
            encoding="utf-8",
        )
        store = ContextStore(ctx_dir)
        session = json.loads((ctx_dir / "session.json").read_text("utf-8"))
        assert session["previous_session"]["status"] == "abnormal"
        assert session["previous_session"]["session_id"] == "old-id"
        assert session["previous_session"]["op_count"] == 5

    def test_normal_previous_session(self, ctx_dir: Path):
        (ctx_dir / "session.json").write_text(
            json.dumps({"session_id": "old-id", "status": "ended", "started_at": "2024-01-01T00:00:00+00:00", "op_count": 3}),
            encoding="utf-8",
        )
        store = ContextStore(ctx_dir)
        session = json.loads((ctx_dir / "session.json").read_text("utf-8"))
        assert session["previous_session"]["status"] == "ended"


# ── Operation history ───────────────────────────────────────────────────


class TestHistory:
    def test_record_and_query(self, store: ContextStore):
        store.record_operation("ue_actions_run", "graph.describe", {"blueprint_name": "BP_Player"}, True, {"success": True, "message": "ok"}, 42.5)
        history = store.get_history(10)
        assert len(history) == 1
        entry = history[0]
        assert entry["tool"] == "ue_actions_run"
        assert entry["action_id"] == "graph.describe"
        assert entry["success"] is True
        assert entry["duration_ms"] == 42.5

    def test_history_persisted(self, store: ContextStore, ctx_dir: Path):
        store.record_operation("ue_ping", None, {}, True, {"pong": True}, 1.0)
        assert (ctx_dir / "history.jsonl").exists()
        lines = (ctx_dir / "history.jsonl").read_text("utf-8").strip().split("\n")
        assert len(lines) == 1

    def test_history_limit(self, store: ContextStore):
        for i in range(30):
            store.record_operation("ue_ping", None, {}, True, {}, 1.0)
        assert len(store.get_history(20)) == 20
        assert len(store.get_history(200)) == 30  # capped at 100 by method, but only 30 exist

    def test_history_truncation_on_boot(self, ctx_dir: Path):
        # Write 510 entries
        with open(ctx_dir / "history.jsonl", "w", encoding="utf-8") as f:
            for i in range(510):
                f.write(json.dumps({"i": i}) + "\n")
        store = ContextStore(ctx_dir)
        # Should be truncated to 500
        assert len(store.get_history(100)) == 100  # returns max query limit
        assert len(store._history) == 500

    def test_code_truncation_in_params_summary(self, store: ContextStore):
        long_code = "x" * 200
        summary = store._summarize_params({"code": long_code})
        assert len(summary["code"]) == 83  # 80 + "..."
        assert summary["code"].endswith("...")

    def test_failed_result_summary(self, store: ContextStore):
        summary = store._summarize_result({"error": "Connection lost"}, False)
        assert "Connection lost" in summary


# ── Working set ─────────────────────────────────────────────────────────


class TestWorkset:
    def test_track_assets(self, store: ContextStore):
        store.track_assets({"blueprint_name": "BP_Player", "foo": "bar"}, "graph.describe")
        ws = store.get_workset()
        assert "BP_Player" in ws
        assert ws["BP_Player"]["op_count"] == 1
        assert ws["BP_Player"]["last_op"] == "graph.describe"

    def test_track_updates_existing(self, store: ContextStore):
        store.track_assets({"blueprint_name": "BP_Player"}, "graph.describe")
        store.track_assets({"blueprint_name": "BP_Player"}, "graph.connect_nodes")
        ws = store.get_workset()
        assert ws["BP_Player"]["op_count"] == 2
        assert ws["BP_Player"]["last_op"] == "graph.connect_nodes"

    def test_workset_persisted(self, store: ContextStore, ctx_dir: Path):
        store.track_assets({"material_name": "M_Test"}, "material.analyze")
        data = json.loads((ctx_dir / "workset.json").read_text("utf-8"))
        assert "M_Test" in data

    def test_clear_resets_everything(self, store: ContextStore):
        store.record_operation("ue_ping", None, {}, True, {}, 1.0)
        store.track_assets({"blueprint_name": "BP_X"})
        store.clear()
        assert store.get_workset() == {}
        assert store.get_history() == []
        status = store.get_status()
        assert status["op_count"] == 0

    def test_all_asset_param_keys_tracked(self, store: ContextStore):
        params = {key: f"value_{i}" for i, key in enumerate(ASSET_PARAM_KEYS)}
        store.track_assets(params, "test")
        ws = store.get_workset()
        assert len(ws) == len(ASSET_PARAM_KEYS)


# ── File persistence resilience ─────────────────────────────────────────


class TestPersistence:
    def test_corrupted_session_json(self, ctx_dir: Path):
        (ctx_dir / "session.json").write_text("NOT JSON", encoding="utf-8")
        store = ContextStore(ctx_dir)
        # Should recover gracefully — no previous session detected
        session = json.loads((ctx_dir / "session.json").read_text("utf-8"))
        assert session["status"] == "active"

    def test_missing_context_dir_auto_created(self, tmp_path: Path):
        new_dir = tmp_path / "new_context"
        store = ContextStore(new_dir)
        assert new_dir.exists()

    def test_corrupted_workset_json(self, ctx_dir: Path):
        (ctx_dir / "workset.json").write_text("{bad json", encoding="utf-8")
        store = ContextStore(ctx_dir)
        assert store.get_workset() == {}

    def test_corrupted_history_jsonl(self, ctx_dir: Path):
        with open(ctx_dir / "history.jsonl", "w", encoding="utf-8") as f:
            f.write('{"valid": true}\n')
            f.write("NOT JSON\n")
            f.write('{"also_valid": true}\n')
        store = ContextStore(ctx_dir)
        # Should skip bad line, keep valid ones
        assert len(store._history) == 2


# ── UE connection state ────────────────────────────────────────────────


class TestUEConnectionState:
    def test_state_change_updates_session(self, store: ContextStore, ctx_dir: Path):
        store._on_ue_state_change("alive", "unknown")
        session = json.loads((ctx_dir / "session.json").read_text("utf-8"))
        assert session["ue_connection"] == "alive"

    def test_crash_persists_context(self, store: ContextStore, ctx_dir: Path):
        store.track_assets({"blueprint_name": "BP_Player"}, "graph.describe")
        store.record_operation("ue_actions_run", "graph.describe", {"blueprint_name": "BP_Player"}, True, {}, 10.0)
        store._on_ue_state_change("crashed", "alive")
        session = json.loads((ctx_dir / "session.json").read_text("utf-8"))
        assert session["ue_connection"] == "crashed"
        assert "crash_context" in session
        assert session["crash_context"]["crash_time"]
        assert "BP_Player" in session["crash_context"]["workset"]

    def test_recovery_from_crash(self, store: ContextStore, ctx_dir: Path):
        store._on_ue_state_change("crashed", "alive")
        store._on_ue_state_change("alive", "crashed")
        session = json.loads((ctx_dir / "session.json").read_text("utf-8"))
        assert session["ue_connection"] == "alive"
        assert session.get("recovered_from_crash") is True


# ── Resume payload ──────────────────────────────────────────────────────


class TestResumePayload:
    def test_resume_no_previous_session(self, store: ContextStore):
        payload = store.get_resume_payload()
        assert payload["previous_session"] is None
        assert payload["ue_connection"] == "unknown"
        assert payload["workset"] == []
        assert payload["recent_ops"] == []

    def test_resume_with_previous_session(self, ctx_dir: Path):
        # Set up a "previous" ended session
        (ctx_dir / "session.json").write_text(
            json.dumps({"session_id": "old", "status": "ended", "started_at": "2024-01-01T00:00:00+00:00", "ended_at": "2024-01-01T01:00:00+00:00", "ue_connection": "alive", "op_count": 10}),
            encoding="utf-8",
        )
        store = ContextStore(ctx_dir)
        payload = store.get_resume_payload()
        assert payload["previous_session"]["status"] == "ended_normally"
        assert payload["previous_session"]["op_count"] == 10

    def test_resume_after_crash(self, ctx_dir: Path):
        (ctx_dir / "session.json").write_text(
            json.dumps({"session_id": "old", "status": "active", "started_at": "2024-01-01T00:00:00+00:00", "ue_connection": "crashed", "op_count": 5, "crash_context": {"crash_time": "2024-01-01T00:30:00+00:00"}}),
            encoding="utf-8",
        )
        store = ContextStore(ctx_dir)
        payload = store.get_resume_payload()
        assert payload["previous_session"]["status"] == "ue_crashed"
        assert "recovery_hint" in payload["previous_session"]


# ── Status ──────────────────────────────────────────────────────────────


class TestStatus:
    def test_status_fields(self, store: ContextStore):
        status = store.get_status()
        assert "session_id" in status
        assert "started_at" in status
        assert status["op_count"] == 0
        assert status["workset_size"] == 0
        assert status["last_op_at"] is None

    def test_status_after_operations(self, store: ContextStore):
        store.record_operation("ue_ping", None, {}, True, {}, 1.0)
        store.track_assets({"blueprint_name": "BP_X"})
        status = store.get_status()
        assert status["op_count"] == 1
        assert status["workset_size"] == 1
        assert status["last_op_at"] is not None
