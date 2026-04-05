# 动作域参考

## ue_python_exec

`ue_python_exec` 是一个 MCP 工具，可以在 Unreal 的嵌入式 Python 环境中执行任意代码。
它替代了 45+ 原有 C++ 动作（Actor 管理、蓝图创建/编译、材质操作、视口控制、PIE 等）。

```python
# 示例：列出所有关卡 Actor
import unreal
_result = [a.get_name() for a in unreal.EditorLevelLibrary.get_all_level_actors()]
```

设置 `_result = <value>` 返回数据。详见 [python-api skill](../Python/ue_cli_tool/skills/python-api.md)。

## 动作域（核心）

| 域 | 数量 | 说明 | 示例 ID |
|----|------|------|--------|
| `python.*` | 1 | Python 代码执行（替代 45+ C++ 动作） | `python.exec` |
| `blueprint.*` | 2 | 蓝图内省与完整快照（创建/编译→Python） | `blueprint.get_summary`、`blueprint.describe_full` |
| `graph.*` | 18 | 图连线、检视、注释、补丁、折叠重构 | `graph.connect_nodes`、`graph.describe`、`graph.apply_patch` |
| `node.*` | 19 | 蓝图图节点创建 | `node.add_event`、`node.add_function_call`、`node.add_branch` |
| `variable.*` | 8 | 变量增删改查、默认值、元数据 | `variable.create`、`variable.add_getter`、`variable.set_default` |
| `function.*` | 4 | 函数创建、管理与重构 | `function.create`、`function.call`、`function.delete`、`function.rename` |
| `dispatcher.*` | 4 | 事件派发器管理 | `dispatcher.create`、`dispatcher.call`、`dispatcher.bind` |
| `layout.*` | 4 | 节点自动布局 | `layout.auto_selected`、`layout.auto_subtree`、`layout.auto_blueprint` |
| `macro.*` | 1 | 宏管理 | `macro.rename` |
| `material.*` | 14 | 材质分析、诊断、布局（创建/编译→Python） | `material.get_summary`、`material.auto_layout`、`material.diagnose` |
| `widget.*` | 21 | UMG 控件蓝图（24 种类型）+ MVVM | `widget.create`、`widget.add_component`、`widget.mvvm_add_viewmodel` |
| `input.*` | 4 | 增强输入系统 | `input.create_action`、`input.create_mapping_context` |
| `animgraph.*` | 18 | AnimGraph 读取/创建/修改/编译 | `animgraph.list_graphs`、`animgraph.create_blueprint`、`animgraph.compile` |
| `editor.*` | 8 | 日志、缩略图、源码控制 diff（Actor/PIE→Python） | `editor.get_logs`、`editor.diff_against_depot`、`editor.is_ready` |

## AI 工作流

### CLI 语法（推荐）

```
ue_cli(command="@BP_Player\nadd_blueprint_variable Speed --variable_type Float\nadd_blueprint_event_node ReceiveBeginPlay\ncompile_blueprint")
```

### 查询

```
ue_query(query="help add_blueprint_variable")
ue_query(query="search material")
ue_query(query="skills")
```

## 编译诊断

### 蓝图

- 通过 `compile_blueprint` CLI 命令编译
- 详细日志：`ue_query(query="logs --source editor --category LogBlueprint")`

### 材质

- 使用 `material_diagnose` 检查常见问题
- 详细日志：`ue_query(query="logs --source editor --category LogMaterial")`
