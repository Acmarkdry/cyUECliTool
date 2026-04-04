## 1. ContextStore 核心类

- [x] 1.1 创建 `Python/ue_editor_mcp/context.py`，定义 `ContextStore` 类骨架（`__init__`, `_context_dir` 路径计算，`.context/` 目录自动创建）
- [x] 1.2 实现 `_load_session()` / `_save_session()`：读写 `session.json`，包含 `{session_id, status, started_at, ended_at, ue_connection, op_count, previous_session}`
- [x] 1.3 实现会话生命周期：启动时检测上次 session 状态，标记 abnormal/ended_normally，创建新 session UUID
- [x] 1.4 实现 `shutdown()` 方法：将 session status 设为 `"ended"`，写入 `session.json`
- [x] 1.5 实现原子写入辅助函数 `_atomic_write_json(path, data)`：写临时文件 → rename
- [x] 1.6 实现文件读取容错 `_safe_read_json(path, default)`：try/except + warning 日志

## 2. 操作历史

- [x] 2.1 实现 `_load_history()` / `_append_history(entry)`：JSONL 追加写入 `history.jsonl`
- [x] 2.2 实现 `_truncate_history(max_entries=500)`：启动时检查行数，截断旧条目
- [x] 2.3 实现 `record_operation(tool, action_id, params, success, result, duration_ms)` 公开方法：构建摘要 → 追加
- [x] 2.4 实现 `_summarize_params(params)` 辅助函数：提取关键字段，code 截断到 80 字符
- [x] 2.5 实现 `_summarize_result(result, success)` 辅助函数：成功提取关键值，失败提取错误消息
- [x] 2.6 实现 `get_history(limit=20)` 查询方法：返回最近 N 条（max 100）

## 3. 工作集

- [x] 3.1 实现 `_load_workset()` / `_save_workset()`：读写 `workset.json`
- [x] 3.2 定义 `ASSET_PARAM_KEYS` 常量列表
- [x] 3.3 实现 `track_assets(params)` 方法：从 params 中提取资产路径，更新 workset 条目（first_seen, last_op, last_op_time, op_count）
- [x] 3.4 实现 `_cleanup_stale_workset(days=7)` 方法：移除超过 7 天未操作的条目
- [x] 3.5 实现 `get_workset()` 查询方法：返回完整工作集
- [x] 3.6 实现 `clear()` 方法：清空 workset 和 history，重置 op_count

## 4. UE 连接状态监控

- [x] 4.1 阅读 `Python/ue_editor_mcp/connection.py`，理解 `PersistentUnrealConnection` 的心跳和断连逻辑
- [x] 4.2 在 `PersistentUnrealConnection` 中添加 `on_state_change` 回调属性（可选 callable）
- [x] 4.3 在连接/断连/心跳失败路径中调用回调：`on_state_change(new_state, old_state, timestamp)`
- [x] 4.4 区分正常断连（`"disconnected"`）和异常断连（`"crashed"`）
- [x] 4.5 在 ContextStore 中实现 `_on_ue_state_change(new_state, old_state, ts)` 回调处理：更新 session.json 的 `ue_connection` 字段
- [x] 4.6 崩溃时立即持久化当前状态（workset + last_op）到 session.json 的 `crash_context`

## 5. ue_context MCP 工具

- [x] 5.1 在 `server_unified.py` 的 TOOLS 列表中添加 `ue_context` Tool 定义（action enum + limit 参数）
- [x] 5.2 在 `_handle_tool` 中添加 `ue_context` 分发逻辑
- [x] 5.3 实现 `action="resume"` 处理：调用 ContextStore 的 resume 方法，组装 previous_session + ue_connection + workset + recent_ops
- [x] 5.4 实现 `action="status"` 处理：返回当前会话实时状态
- [x] 5.5 实现 `action="history"` 处理：调用 get_history(limit)
- [x] 5.6 实现 `action="workset"` 处理：调用 get_workset()
- [x] 5.7 实现 `action="clear"` 处理：调用 clear()

## 6. 集成与钩子

- [x] 6.1 在 `server_unified.py` 启动时实例化 `ContextStore`，传入 `.context/` 路径
- [x] 6.2 在 `_handle_tool` 的每次调用后，自动调用 `context_store.record_operation()` 和 `context_store.track_assets()`
- [x] 6.3 将 `ContextStore._on_ue_state_change` 注册为 `PersistentUnrealConnection.on_state_change` 回调
- [x] 6.4 在 MCP server 退出时调用 `context_store.shutdown()`（atexit 或 signal handler）
- [x] 6.5 将 `.context/` 添加到 `.gitignore`

## 7. 文档与测试

- [x] 7.1 更新 `README.md`：工具数量从 10 更新为 11，新增 ue_context 说明
- [x] 7.2 更新 `docs/architecture.md`：新增 Context Layer 段落和架构图更新
- [x] 7.3 更新 `server_unified.py` 顶部文档字符串：工具数量更新
- [x] 7.4 创建 `tests/test_context.py`：测试 ContextStore 的会话管理、历史记录、工作集追踪、文件持久化容错
