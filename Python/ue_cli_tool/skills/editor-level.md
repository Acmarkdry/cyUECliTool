# Editor & Level Management — Workflow Tips

## Actor Management (via ue_python_exec)

> **Note**: Actor spawning, transforms, properties, and outliner management
> are now handled through `ue_python_exec`. See the `python-api` skill.

```python
# Spawn a point light and set its properties
import unreal
loc = unreal.Vector(100.0, 0.0, 200.0)
actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.PointLight, loc)
actor.point_light_component.set_editor_property("intensity", 5000.0)
_result = actor.get_name()
```

## PIE (Play In Editor) Control (via ue_python_exec)

```python
# Start PIE
import unreal
unreal.AutomationLibrary.start_pie()
```

```python
# Stop PIE
import unreal
unreal.AutomationLibrary.end_play_map()
```

## Log-Based Testing

```
editor_clear_logs
# Start PIE via ue_python_exec, wait, then stop
editor_assert_log --pattern "Player spawned" --category LogGame --should_exist true
```

## Outliner Management (via ue_python_exec)

```python
# List all actors with folder info
import unreal
actors = unreal.EditorLevelLibrary.get_all_level_actors()
_result = [{"name": a.get_name(), "class": a.get_class().get_name(),
            "folder": a.get_folder_path().to_string()} for a in actors]
```

## Source Control Diff (retained C++ action)

```
editor_diff_against_depot --asset_path /Game/Blueprints/BP_Player
# Returns: hasDifferences, summary, diffs[] with node-level changes
```

## Key Patterns

- Actor/viewport/PIE operations → use `ue_python_exec` with `import unreal`
- `editor_get_selected_asset_thumbnail` returns PNG base64 for Content Browser selection
- `editor_is_ready` to check if editor is fully initialized before operations
- `editor_get_logs` / `editor_clear_logs` / `editor_assert_log` for log inspection
- `editor_diff_against_depot` and `editor_get_asset_history` for source control
- For long-running operations, use async execution