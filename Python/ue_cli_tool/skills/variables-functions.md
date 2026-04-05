# Variable & Function Management — Workflow Tips

## Variable Lifecycle

```
@BP_Player
add_blueprint_variable Health --variable_type Float
set_blueprint_variable_default Health --default_value 100.0
set_variable_metadata Health --category Stats --instance_editable true --tooltip "Current health points"
compile_blueprint
```

## Variable Types

Boolean, Integer, Int64, Float, Double, String, Name, Text,
Vector, Rotator, Transform, LinearColor, Object (UObject references)

## Function Creation

```
@BP_Player
create_blueprint_function TakeDamage --inputs [{"name":"Amount","type":"Float"}] --outputs [{"name":"IsDead","type":"Boolean"}] --pure false --category Combat
```

## Key Patterns

- `variable_rename` auto-updates all getter/setter node references
- `function_rename` auto-updates all CallFunction references + FunctionEntry/FunctionResult nodes
- `macro_rename` auto-updates all macro instance references
- `function_call` adds a CallFunction node for a Blueprint-defined function (not engine functions)
- Use `variable_add_local` for function-scoped variables