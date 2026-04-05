# 架构与技术细节

## 架构图

```
AI (Claude, GPT, etc.)
        │
        │  2 个 MCP 工具（stdio）
        │    ue_cli  — CLI 文本命令（执行）
        │    ue_query — 只读查询（help/search/logs/metrics）
        ▼
  server.py（CliParser + ActionRegistry 分发）
        │
        ├── CliParser（CLI 语法解析：@target、位置参数、--flags）
        │     └── 从 ActionRegistry.input_schema.required 自动推导参数映射
        │
        ├── ContextStore（.context/ 持久化）
        │     ├── session.json    — 会话元数据 + UE 连接状态
        │     ├── history.jsonl   — 操作历史（摘要级）
        │     └── workset.json    — 当前工作集（资产路径）
        │
        ├── TCP/JSON（端口 55558，持久连接，长度前缀帧）
        │         │
        │         ▼
        │   C++ 插件（FMCPServer → FMCPClientHandler）
        │         │
        │         ├── 快速路径（无需游戏线程）
        │         │     ├── ping/close
        │         │     ├── async_execute/get_task_result
        │         │     └── subscribe_events/poll_events/unsubscribe_events
        │         │
        │         ├── 游戏线程分发
        │         │     ├── FExecPythonAction → IPythonScriptPlugin → Unreal Python API
        │         │     ├── ~123 个 FEditorAction 子类 → 校验 → FScopedTransaction → 执行 → 自动保存
        │         │     └── FBatchExecuteAction → 原子事务回滚（transactional 模式）
        │         │
        │         ├── 异步路径
        │         │     └── SubmitAsyncTask → AsyncTask(GameThread) → 结果回写
        │         │
        │         └── 事件推送
        │               └── FMCPEventHub → 编辑器委托 → 每客户端事件队列 → poll 读取
        │
        └── Commandlet 模式（CLI/CI）
              └── UEEditorMCPCommandlet → MCPBridge → ActionHandlers
```

## MCP 工具（2 个）

| # | Tool | 功能 | 类型 |
|---|------|------|------|
| 1 | `ue_cli` | CLI 文本命令执行（支持多行批量、@target 上下文） | 写 |
| 2 | `ue_query` | 只读查询：help、search、context、logs、metrics、health、skills、resources、ping | 读 |

## CLI 语法

```bash
<command> [positional_args...] [--flag value ...]
@<target>     # 设置上下文（自动填充 blueprint_name/material_name/widget_name）
# comment     # 忽略
```

## C++ 服务器（`FMCPServer`）

- 监听 `127.0.0.1:55558`（仅限本地）
- 每个连接派生独立的 `FMCPClientHandler` 线程，最多 8 路并发
- **快速路径**：`ping`/`close`/`async_execute`/`get_task_result`/`subscribe_events`/`poll_events`/`unsubscribe_events` 直接在客户端线程处理
- 其余命令通过 `AsyncTask + FEvent` 分发到游戏线程
- 客户端超时：300 秒无活动后断开
- 启用 `SO_REUSEADDR`，避免编辑器重启时端口冲突

## 事件推送系统（`FMCPEventHub`）

- **架构**：编辑器委托 → `FMCPEventHub::EnqueueEvent` → 每客户端队列 → TCP poll 读取
- **支持事件类型**：`blueprint_compiled`、`asset_saved`/`asset_deleted`/`asset_renamed`、`pie_started`/`pie_ended`、`level_changed`、`selection_changed`、`undo_performed`
- **线程安全**：`FCriticalSection` 保护客户端队列，每客户端最多 500 个待处理事件

## 批量原子回滚

- `batch_execute` 支持 `transactional` 参数（默认 false）
- 当 `transactional=true` 时，所有子命令包裹在单个 `FScopedTransaction` 中
- 任何子命令失败时自动调用 `GEditor->UndoTransaction()` 回滚全部更改

## Python 执行引擎（`FExecPythonAction`）

- 通过 `IPythonScriptPlugin::Get()->ExecPythonCommand()` 执行代码
- `_result` 变量约定：脚本设置 `_result = <value>`，C++ 通过临时 JSON 文件读取
- 替代 45+ 原有 C++ 动作（Actor、蓝图、材质、视口、PIE 等）

## Undo/Redo 事务

- `FEditorAction::Execute()` 自动检测 `IsWriteAction()`，为写操作包裹 `FScopedTransaction`
- 事务描述：`"MCP: <ActionName>"`

## 异步执行基础设施

- `SubmitAsyncTask(command, params)` → 返回 UUID task_id
- `AsyncTask(ENamedThreads::GameThread)` 在游戏线程执行
- `GetTaskResult(task_id)` → pending/completed+result
- 过期清理：300 秒超时自动删除

## 通信协议

```
[4 字节：消息长度（大端序）] [UTF-8 JSON 载荷]
```

请求：
```json
{"type": "exec_python", "params": {"code": "import unreal; _result = unreal.SystemLibrary.get_engine_version()"}}
```

## 编辑器专属安全保障

| 层级 | 机制 | 效果 |
|------|------|------|
| `.uplugin` | `"Type": "Editor"` | UBT 对所有非编辑器目标跳过此模块 |
| `.Build.cs` | 依赖 `UnrealEd`、`BlueprintGraph`、`Kismet`、`UMGEditor`、`PythonScriptPlugin` 等 | 无法链接到游戏目标 |
| `.uplugin` | `"PlatformAllowList": ["Win64", "Mac", "Linux"]` | 仅限桌面编辑器平台 |

## 关键文件

| 文件 | 用途 |
|------|------|
| `Python/ue_cli_tool/server.py` | 2-tool MCP 服务器（ue_cli + ue_query） |
| `Python/ue_cli_tool/cli_parser.py` | CLI 语法解析器（@target、位置参数、--flags） |
| `Python/ue_cli_tool/registry/__init__.py` | ActionRegistry 类，关键字搜索引擎 |
| `Python/ue_cli_tool/registry/actions.py` | ~123 ActionDef 条目 + python.exec |
| `Python/ue_cli_tool/context.py` | ContextStore（会话、历史、工作集、UE 连接监控） |
| `Python/ue_cli_tool/connection.py` | PersistentUnrealConnection（TCP、心跳、自动重连） |
| `Python/ue_cli_tool/skills/__init__.py` | Skill 系统（按域分组的 action 目录） |
| `Source/UECliTool/Private/MCPServer.cpp` | TCP Accept + 快速路径 + 游戏线程分发 |
| `Source/UECliTool/Private/MCPBridge.cpp` | 动作处理器注册表 + 异步任务管理 |
| `Source/UECliTool/Private/MCPEventHub.cpp` | 事件推送系统（编辑器委托 → 客户端队列） |
| `Source/UECliTool/Private/Actions/PythonActions.cpp` | FExecPythonAction（Python 执行引擎） |
| `Source/UECliTool/Private/Actions/AnimGraphActions.cpp` | AnimGraph 全部 18 个 Action |
| `tests/test_cli_parser.py` | CLI 解析器测试（41 用例） |
| `tests/test_schema_contract.py` | Python ↔ C++ schema 一致性验证 |