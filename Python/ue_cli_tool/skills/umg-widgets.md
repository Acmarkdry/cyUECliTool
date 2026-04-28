# UMG Widgets & MVVM — Workflow Tips

## Widget Creation Flow

```
@WBP_HUD
create_umg_widget_blueprint
add_widget_component CanvasPanel RootCanvas
add_widget_component TextBlock ScoreText --text "Score: 0" --font_size 24
add_widget_component Button RestartBtn
add_widget_child ScoreText --parent RootCanvas
add_widget_child RestartBtn --parent RootCanvas
compile_blueprint
```

## Supported Component Types (24)

TextBlock, Button, Image, Border, Overlay, HorizontalBox, VerticalBox,
Slider, ProgressBar, SizeBox, ScaleBox, CanvasPanel, ComboBox, CheckBox,
SpinBox, EditableTextBox, ScrollBox, WidgetSwitcher, BackgroundBlur,
UniformGridPanel, Spacer, RichTextBlock, WrapBox, CircularThrobber

## MVVM Workflow

```
@WBP_HUD
widget_mvvm_add_viewmodel StatusViewModel --viewmodel_name StatusVM --creation_type CreateInstance
widget_mvvm_add_binding --viewmodel_name StatusVM --source_property HealthPercent --destination_widget HealthBar --destination_property Percent --binding_mode OneWayToDestination
```

## Key Patterns

- Components are added to root by default — use `add_widget_child` to build hierarchy
- `widget_reparent` moves multiple widgets into a container at once
- `widget_get_tree` returns full hierarchy with slot info and render transforms
- `set_widget_properties` handles slot, visibility, alignment, and padding in one call
- Binding modes: OneTimeToDestination, OneWayToDestination, TwoWay, OneTimeToSource, OneWayToSource

## Enhanced Input System

```
input_create_action IA_Move --value_type Axis2D
input_create_mapping_context IMC_Default
input_add_key_mapping --context_name IMC_Default --action_name IA_Move --key W --modifiers ["SwizzleYXZ"]
input_add_key_mapping --context_name IMC_Default --action_name IA_Move --key S --modifiers ["Negate","SwizzleYXZ"]
```


## Widget Analysis (v0.4.0)

### Full Snapshot

```
describe_widget_blueprint_full WBP_HUD
```

Returns component hierarchy tree, event bindings, UMG animations, MVVM bindings, and variables in a single call.

### Animation Workflow

```
# List existing animations
widget_list_animations WBP_HUD

# Create a new animation
widget_create_animation WBP_HUD --animation_name FadeIn --duration 0.5

# Add a property track to the animation
widget_add_animation_track WBP_HUD --animation_name FadeIn --component_name ScoreText --property_name Opacity
```

### Reference Analysis

```
# What other Widget Blueprints does this one use?
widget_get_references WBP_HUD

# What assets reference this Widget Blueprint?
widget_get_referencers WBP_HUD
```

### Style Audit

```
# Get all component styles
widget_batch_get_styles WBP_HUD

# Filter to TextBlock components only
widget_batch_get_styles WBP_HUD --filter_type TextBlock
```
