# Proposal: CLI-Native Interface + Full Rename

## Summary

对项目进行彻底重构：
1. **全面重命名** — `cyUEEditorMCP` → `cyUECliTool`，涵盖仓库名、C++ 模块、Python 包、所有代码引用
2. **CLI 原生接口** — 用 AI 天然擅长的 CLI 格式替代冗长的 JSON tool 接口，12 个 MCP tool 收敛为 1 个主力 `ue_cli` tool
3. **删除已过时代码** — 移除 `script.py`（上一轮的自定义 DSL 尝试）、清理冗余

## Motivation

### 为什么做 CLI 原生接口

当前 AI 通过 MCP 操作 Unreal 时，每个操作都需要生成冗长的 JSON：

```json
ue_actions_run(action_id="blueprint.add_component", params={
  "blueprint_name": "BP_Enemy", "component_type": "StaticMeshComponent",
  "component_name": "Mesh", "location": [0, 0, 0]
})
```

**~150 tokens，AI 的输出 token 是最贵的资源。**

而 AI 的训练数据充满了 CLI 命令（bash, git, docker, kubectl, aws...），它天然擅长这种格式：

```
add_component_to_blueprint BP_Enemy --component_type StaticMeshComponent --component_name Mesh --location [0,0,0]
```

**~40 tokens，减少 73%。** 且完全不需要教 AI 任何新语法。

### 为什么要重命名

- "MCP" 是底层协议名，用户不关心
- 项目的核心价值是 **CLI 式地控制 UE Editor**，名字应体现这一点
- `cyUECliTool` 直观表达了 "用 CLI 操作 UE 的工具"

## Goals

- **G1**: 所有文件/目录/引用从 `UEEditorMCP`/`ue_editor_mcp` 重命名为 `UECliTool`/`ue_cli_tool`
- **G2**: 新增 `ue_cli` MCP tool — 接受 CLI 文本，一站式执行单条/多条命令
- **G3**: 删除旧的 12 个 MCP tool（`ue_actions_list`, `ue_actions_run`, `ue_batch`, `ue_script`, etc.）
- **G4**: CLI 解析器从 `ActionRegistry` 的 `input_schema.required` 自动推导位置参数，零手写映射
- **G5**: 支持 `@target` 上下文继承减少重复
- **G6**: 更新 README、docs、GitHub Actions CI、pyproject 等所有文档/配置
- **G7**: 所有测试通过，GitHub Actions CI 绿色

## Non-Goals

- 不修改 C++ 侧的 MCPServer/MCPBridge 底层 TCP 通信协议
- C++ 模块内部的 .cpp/.h 文件名/类名暂时可以保留（UE 模块重命名风险高），只改模块外部引用
- 不改 TCP 端口号（55558）

## Approach

### Part A: 全面重命名

**目录/文件重命名**:
- `Plugins/UEEditorMCP/` → `Plugins/UECliTool/` （UE 插件根目录）
- `Source/UEEditorMCP/` → `Source/UECliTool/`
- `UEEditorMCP.uplugin` → `UECliTool.uplugin`
- `Python/ue_editor_mcp/` → `Python/ue_cli_tool/`
- 所有 `import ue_editor_mcp` → `import ue_cli_tool`
- `pyproject.toml` 的 package name
- `.github/workflows/ci.yml` 中的路径引用
- `README.md` 全文替换
- `docs/*.md` 全文替换

**C++ 模块名**:
- `.uplugin` 中的 Module Name
- `.Build.cs` 文件名和类名
- `IMPLEMENT_MODULE` 宏中的模块名

**Python 包内部**:
- 所有 `from ue_editor_mcp.xxx import` → `from ue_cli_tool.xxx import`
- `server_unified.py` 中的 `Server("ue-editor-mcp")` → `Server("ue-cli-tool")`
- `ue_bridge.py` 中的导入路径

### Part B: CLI 原生接口

**新增 `ue_cli` MCP tool**:

```python
Tool(
    name="ue_cli",
    description="Execute Unreal Editor commands using CLI syntax. ..."
    inputSchema={
        "type": "object",
        "properties": {
            "command": {
                "type": "string",
                "description": "CLI command(s), one per line. Use @target for context."
            }
        },
        "required": ["command"]
    }
)
```

AI 调用示例：
```
ue_cli(command="@BP_Enemy\ncreate_blueprint --parent_class Actor\nadd_component_to_blueprint --component_type StaticMeshComponent --component_name Mesh\ncompile_blueprint")
```

**CLI 解析器** (`cli_parser.py`):

```
<command> [positional_args...] [--flag value ...]
```

- 命令名 = C++ command name（从 `ActionRegistry.get_by_command()` 查找）
- 位置参数 = `input_schema.required` 字段的顺序（从 registry 自动推导，零手写）
- `--flag value` = 可选参数名（AI 天然理解这个格式）
- `@target` = 自动注入为 `blueprint_name` / `material_name` / `widget_name`
- 多行 = batch 执行（一次 TCP round-trip）
- `#` 开头 = 注释

**自动参数推导示例**:

Registry 定义：
```python
ActionDef(
    command="add_component_to_blueprint",
    input_schema={
        "required": ["blueprint_name", "component_type", "component_name"]
    }
)
```

CLI 调用（context = @BP_Enemy）：
```
add_component_to_blueprint StaticMeshComponent Mesh
```
解析器自动：
1. `@BP_Enemy` → 填充 `blueprint_name`
2. 第 1 个位置参数 → `component_type`（required 中排除已被 context 填充的，取下一个）
3. 第 2 个位置参数 → `component_name`

**删除旧 tool**:

删除以下 12 个 MCP tool 的定义和 handler：
- `ue_actions_list`
- `ue_actions_run`
- `ue_actions_discover`
- `ue_patch`
- `ue_batch`
- `ue_resources_read`
- `ue_logs_tail`
- `ue_skills`
- `ue_search`
- `ue_context`
- `ue_script`
- `ue_async_run`

替换为 2 个 tool：
- `ue_cli` — 执行命令（覆盖 run/batch/script/patch 的功能）
- `ue_query` — 查询型操作（覆盖 list/discover/search/logs/context/resources 的功能）

### Part C: 文档/CI 更新

**README.md 重写**:
- 新项目名 + 描述
- CLI 语法文档 + 示例
- 安装/配置说明更新
- 删除旧的 JSON tool 文档

**docs/ 更新**:
- `architecture.md` — 新架构图
- `development.md` — CLI 开发指南
- 新增 `cli-reference.md` — 完整的 CLI 命令参考

**GitHub Actions**:
- `ci.yml` 更新路径引用
- 确保 `black --check` / `pytest` / `schema contract` 全绿
- 确保 push 后 CI 通过

**测试**:
- 新增 `test_cli_parser.py` — CLI 解析器单元测试（对标原 `test_script.py` 的 42 个 case，重写为 CLI 格式）
- 更新 `test_schema_contract.py` — 适配新包名
- 更新 `test_context.py` — 适配新包名
- 更新 `test_skills.py` — 适配新包名
- 删除 `test_script.py`（旧 DSL 测试）

## Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| C++ UE 模块重命名编译失败 | 高 | 先改外部引用，.Build.cs 和 IMPLEMENT_MODULE 小心处理，本地编译验证 |
| Python 包 import 路径遗漏 | 中 | grep 全局搜索 `ue_editor_mcp`，零遗漏替换 |
| GitHub repo rename 断链 | 低 | GitHub 自动重定向旧 URL |
| AI 不知道 ue_cli 的用法 | 低 | tool description 足够清晰 + 内置 help 命令 |
| 删除旧 tool 导致现有 AI 对话断裂 | 低 | 这是 breaking change，用户已确认不需要向后兼容 |

## Success Criteria

- [ ] 仓库内零个 `ue_editor_mcp` 字符串残留（git grep 验证）
- [ ] MCP tool 从 12 个减至 2 个（`ue_cli` + `ue_query`）
- [ ] CLI 解析器零手写映射表（全部从 ActionRegistry 自动推导）
- [ ] 所有测试通过
- [ ] GitHub Actions CI 绿色
- [ ] README 完整反映新架构
- [ ] `black --check` 通过
