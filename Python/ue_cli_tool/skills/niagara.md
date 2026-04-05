# Niagara 粒子系统工作流

## 快速开始：创建粒子系统

```
niagara_create_system NS_Fire --package_path /Game/FX
niagara_describe_system --system_path /Game/FX/NS_Fire
niagara_add_emitter --system_path /Game/FX/NS_Fire --emitter_name Sparks
niagara_compile --system_path /Game/FX/NS_Fire
```

## 查看模块堆栈

```
niagara_get_modules --system_path /Game/FX/NS_Fire
```

返回所有发射器及其模块堆栈（Spawn、Update、Render）。

## 修改模块参数

```
niagara_set_module_param --system_path /Game/FX/NS_Fire --emitter_name Sparks --module_name SpawnRate --param_name SpawnRate --value 100.0
```

## 移除发射器

```
niagara_remove_emitter --system_path /Game/FX/NS_Fire --emitter_name Sparks
```

## 提示

- 修改后务必 `niagara_compile`，验证系统是否有效
- 修改前先用 `niagara_describe_system` 查看当前状态
- 发射器名称在系统内必须唯一