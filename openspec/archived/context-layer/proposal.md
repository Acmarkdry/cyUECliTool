## Why

MCP 协议是无状态的请求-响应模式：每次 AI 调用工具都是独立的，调用结果仅存在于 AI 的上下文窗口中。这导致三个实际问题：

1. **AI 新开对话时失忆** — 前一个会话做了什么、做到哪里，完全丢失，需要重新 `describe_full` 等操作来重建认知
2. **UE 崩溃无感知** — AI 不知道 UE 已经崩了，下次调用才发现 TCP 断了，之前未保存的操作状态也丢失
3. **重复上下文浪费 token** — AI 每次调用都要在 prompt 中重复"我正在编辑 BP_Player"等已知信息

在 `server_unified.py` 中引入一个持久化的上下文层（Context Layer），让它充当"有记忆的中间人"，可以解决这三个问题。

## What Changes

- **新增 `ContextStore` 类**：在 `server_unified.py` 所在包中实现，维护会话状态、操作历史、工作集、UE 连接状态
- **新增 `ue_context` MCP 工具**：AI 通过此工具查询/管理上下文（resume 恢复上下文、status 查状态、clear 清理）
- **文件持久化**：上下文数据写入 `.context/` 目录（session.json、history.jsonl、workset.json），进程重启后可恢复
- **UE 连接监控**：后台心跳线程检测 TCP 连接状态变化（正常断开 vs 崩溃），自动更新上下文中的连接状态
- **操作历史自动记录**：每次 `_handle_tool` 成功/失败后，自动将操作摘要追加到上下文中
- **工作集自动追踪**：从操作参数中提取资产路径（blueprint_name、material_name 等），自动维护当前工作集

## Capabilities

### New Capabilities
- `context-store`: 上下文存储引擎 — 会话管理、操作历史、工作集追踪、文件持久化
- `context-tool`: MCP 工具接口 — ue_context 工具的 resume/status/clear 动作定义及分发逻辑
- `connection-monitor`: UE 连接监控 — 后台心跳检测、崩溃感知、连接状态转换

### Modified Capabilities
<!-- 无现有 spec 需要修改 -->

## Impact

- **Python/ue_editor_mcp/server_unified.py** — 新增 ue_context 工具、操作记录钩子
- **Python/ue_editor_mcp/context.py** — 新文件，ContextStore 类
- **Python/ue_editor_mcp/connection.py** — 可能需要扩展心跳/断连回调
- **.context/** — 新目录，需加入 .gitignore
- **工具总数** — 从 10 增至 11
- **无 Breaking Change** — 纯增量，现有工具行为不变
