# UE CLI Tool Conventions

## Action ID Naming

Action IDs use **dot notation**: `domain.verb` or `domain.noun_verb`.

| Domain       | Description                  |
|-------------|------------------------------|
| `blueprint` | Blueprint asset management   |
| `component` | Component property management|
| `editor`    | Level actors & viewport      |
| `layout`    | Node auto-layout             |
| `node`      | Blueprint graph nodes        |
| `variable`  | Variable CRUD                |
| `function`  | Function CRUD                |
| `dispatcher`| Event dispatcher management  |
| `graph`     | Graph editing operations     |
| `material`  | Material creation & editing  |
| `widget`    | UMG Widget Blueprints        |
| `input`     | Input mapping & actions      |

## AI Workflow (CLI Syntax)

```
# Discover commands
ue_query(query="search blueprint")

# Get command help
ue_query(query="help create_blueprint")

# Execute via CLI
ue_cli(command="create_blueprint BP_Lamp --parent_class Actor")
```

For multi-command with context:
```
ue_cli(command="@BP_Lamp\nadd_component_to_blueprint PointLightComponent Light\ncompile_blueprint")
```

## Common Patterns

### Create a Blueprint with a component
```
@BP_Lamp
create_blueprint --parent_class Actor
add_component_to_blueprint PointLightComponent Light
compile_blueprint
```

### Add BeginPlay logic
```
@BP_Lamp
add_blueprint_event_node ReceiveBeginPlay
add_blueprint_function_node Light SetIntensity
connect_blueprint_nodes <GUID1> Then <GUID2> execute
```

### Create a UI widget
```
@WBP_HUD
create_umg_widget_blueprint
add_widget_component TextBlock ScoreText --text "Score: 0" --font_size 24
add_widget_component Button RestartBtn --text Restart
```

## Risk Levels

- **safe** — read-only or easily reversible (default)
- **moderate** — modifies or deletes something, but scoped to named object
- **destructive** — deletes entire assets, hard to undo

## Position Arrays

- **Node position**: `[X, Y]` in graph space (integers)
- **Actor location**: `[X, Y, Z]` in world space (floats)
- **Actor rotation**: `[Pitch, Yaw, Roll]` in degrees
- **Color**: `[R, G, B]` or `[R, G, B, A]` — values 0.0–1.0