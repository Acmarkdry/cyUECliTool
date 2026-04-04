## Context

当前 `server_unified.py` 是一个纯无状态的 MCP 服务器：接收 AI 的工具调用 → 转发到 UE C++ 插件 → 返回结果。除了一个 TCP 连接对象（`PersistentUnrealConnection`）和日志环形缓冲区外，不维护任何跨调用状态。

AI 端（Claude/Copilot）将所有操作结果放在自己的上下文窗口中，依赖窗口内容来"记住"工作状态。当对话结束或 UE 崩溃时，这些信息就丢失了。

本设计在 Python MCP 服务器层引入一个 `ContextStore`，嵌入 `server_unified.py` 进程，使用文件持久化，充当"有记忆的中间人"。

## Goals / Non-Goals

**Goals:**
- AI 新开对话时，一次 `ue_context(action="resume")` 调用即可恢复上次工作上下文
- UE 崩溃时，中间层自动感知并记录崩溃状态，AI 下次查询立即知道
- 操作历史自动追加（无需 AI 额外调用），支持回溯最近 N 次操作
- 工作集（当前涉及的资产路径）自动从操作参数中提取和维护
- 进程重启后上下文可从文件恢复

**Non-Goals:**
- 不做完整资产快照（变量列表、节点图等）— AI 可以随时 `describe_full`
- 不做操作撤销/回滚 — 这需要 UE C++ 端的 Transaction 支持
- 不做多 AI 会话冲突检测 — 留给未来迭代
- 不做主动推送 — MCP 协议是请求-响应模式，被动查询足够
- 不修改 C++ 插件 — 纯 Python 层变更

## Decisions

### D1: 架构位置 — 嵌入 server_unified.py 进程

**选择**：ContextStore 作为 Python 类实例化在 server_unified.py 中，与 MCP 服务器同生命周期。

**备选**：
- 独立进程/服务 → 增加部署复杂度，需要 IPC，用户体验变差
- 浏览器端 localStorage → 不适用于 stdio MCP

**理由**：用户已经只需运行一个 Python 进程，不应增加额外运维负担。文件持久化解决进程重启问题。

### D2: 持久化方案 — .context/ 目录 + JSON 文件

**选择**：
```
.context/
├── session.json      # 当前会话元数据 + UE 连接状态
├── history.jsonl     # 操作历史（append-only，每行一条）
└── workset.json      # 当前工作集（资产路径 → 最后操作）
```

**备选**：
- SQLite → 过重，增加依赖
- 内存 only → 进程重启就丢
- 单个大 JSON → history 追加写性能差

**理由**：JSONL 适合 append-only 的操作日志；JSON 适合小文件的原子覆写；无额外依赖。

### D3: 操作历史记录粒度 — 摘要级

**选择**：每条历史记录包含 `{timestamp, tool, action_id, params_summary, success, result_summary, duration_ms}`。

- `params_summary`：从 params 中提取关键字段（blueprint_name、code 的前 80 字符等），不存全量
- `result_summary`：成功时提取关键返回值，失败时记录错误消息
- 上限：保留最近 500 条，超过时截断旧记录

**理由**：全量 params/result 太大（尤其 `ue_python_exec` 的 code 可能很长），摘要足够 AI 恢复上下文。

### D4: 工作集追踪 — 从参数自动提取

**选择**：在 `_handle_tool` 返回后，从 params 中检查已知的资产路径字段（`blueprint_name`、`material_name`、`asset_path`、`widget_name` 等），自动添加到 workset。

```python
ASSET_PARAM_KEYS = [
    "blueprint_name", "material_name", "asset_path", 
    "widget_name", "anim_blueprint", "mapping_context",
]
```

每个 workset 条目：`{path, first_seen, last_op, last_op_time, op_count, status}`

**理由**：零额外调用，不需要 AI 显式"注册"资产。

### D5: UE 连接状态监控 — 复用心跳 + 断连回调

**选择**：`PersistentUnrealConnection` 已有心跳机制。扩展它：
- 正常 `close` → 状态设为 `disconnected`
- 心跳超时/TCP 异常 → 状态设为 `crashed`
- 重连成功 → 状态设为 `alive`，标记 `recovered_from_crash: true`（如果上次是 crash）

通过回调函数通知 ContextStore 更新 `session.json`。

**理由**：复用已有基础设施，不增加额外心跳线程。

### D6: ue_context 工具接口设计

```
ue_context(action="resume")
→ 返回上次会话摘要、UE 连接状态、工作集、最近 10 条操作

ue_context(action="status") 
→ 返回当前会话实时状态（UE 连接、操作计数、工作集大小）

ue_context(action="history", limit=20)
→ 返回最近 N 条操作历史

ue_context(action="workset")
→ 返回完整工作集列表

ue_context(action="clear")
→ 清空工作集和历史，开始新的干净会话
```

**理由**：单个工具 + action 参数模式，与 `ue_async_run` 一致，避免工具数量膨胀。

### D7: 会话生命周期

```
MCP Server 启动
  → 读取 .context/session.json
  → 如果存在上次 session 且非 "ended"
    → 标记为 "previous_session_abnormal"（进程被杀）
  → 创建新 session（新 UUID）
  → 加载 history.jsonl 和 workset.json

AI 调用 ue_context(action="resume")
  → 返回上次 session 信息 + 当前 session 信息

MCP Server 正常退出
  → session 状态设为 "ended"
  → 写入 session.json
```

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| history.jsonl 无限增长 | 启动时检查行数，超过 500 条截断保留最近 500 条 |
| 文件写入并发冲突（多个工具并行调用） | MCP stdio 模式实际是串行处理请求，无真正并发；但仍用 threading.Lock 保护 |
| .context/ 文件损坏 | 读取时 try/except，损坏则重建空状态，记录 warning |
| workset 积累太多过期资产 | resume 时检查 first_seen，超过 7 天未操作的自动移除 |
| ue_python_exec 的 code 参数很长，摘要截断后丢失信息 | 摘要只用于 AI 快速恢复认知，详细代码 AI 自己的对话上下文里有 |
| PersistentUnrealConnection 的断连回调时机不精确 | 心跳间隔已是 10s，崩溃感知延迟最多 10s，可接受 |
