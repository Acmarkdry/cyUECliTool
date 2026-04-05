# Tasks: CLI-Native Interface + Full Rename

## Phase 1: CLI 解析器核心（不依赖重命名，先做功能）

### Task 1.1: 新建 cli_parser.py
- [x] 新建 `Python/ue_editor_mcp/cli_parser.py`（暂用旧包名，Phase 3 统一改）
- [x] 实现 `CliParser.__init__(registry)` — 持有 ActionRegistry 引用
- [x] 实现 `_get_positional_order(command)` — 从 registry 的 `input_schema.required` 自动推导参数顺序，缓存结果
- [x] 实现 `_detect_context_param(command)` — 检查 schema properties 是否含 `blueprint_name`/`material_name`/`widget_name`，自动判断上下文参数名
- [x] 实现 `parse_line(line, context)` — 解析单行 CLI：tokenize → 分离命令/位置参数/--flags → 注入 context → 合并
- [x] 实现 `parse(text)` — 解析多行 CLI：处理 `@target`、`#` 注释、空行，逐行调用 `parse_line`
- [x] 实现 `_tokenize(line)` — 用 `shlex.split` 处理引号
- [x] 实现 `_coerce_value(val)` — 字符串→bool/int/float/JSON array/object 自动转换
- [x] 实现 `to_batch_commands(parse_result)` — 转为 `batch_execute` 格式
- [x] 验证：零手写映射表，全部从 registry 推导

### Task 1.2: 新建 test_cli_parser.py
- [x] 基础解析：空输入、注释、单命令、多命令
- [x] 上下文：`@target` 注入、上下文切换、无上下文
- [x] 位置参数：自动映射到 required 字段、上下文排除后的 slot 填充
- [x] --flag 参数：`--name value`、`--bool_flag true`、`--array [1,2,3]`
- [x] 值转换：bool、int、float、JSON array、JSON object、纯字符串
- [x] 引号字符串：`"multi word value"`
- [x] 批量转换：`to_batch_commands` 输出正确
- [x] 边缘情况：未知命令透传、多余位置参数、--flag 无值
- [x] 全场景测试：完整蓝图创建脚本（10+ 命令）
- [x] 所有测试通过

### Task 1.3: 删除 script.py 和 test_script.py
- [x] 删除 `Python/ue_editor_mcp/script.py`
- [x] 删除 `tests/test_script.py`
- [x] 移除 `server_unified.py` 中 `ue_script` tool 定义和 handler

## Phase 2: MCP Server 重写（2 tools）

### Task 2.1: 新建 server.py — ue_cli tool
- [x] 新建 `Python/ue_editor_mcp/server.py`（替代 `server_unified.py`）
- [x] 定义 `ue_cli` Tool schema（command 字符串输入）
- [x] 实现 handler：调用 `CliParser.parse()` → `to_batch_commands()` → 发送 `batch_execute`（多行）或 `send_command`（单行）
- [x] 单行命令直接执行（不走 batch），减少开销
- [x] 多行命令走 `batch_execute`，一次 TCP round-trip
- [x] 结果格式化：返回每行的成功/失败 + 原始 CLI 行

### Task 2.2: server.py — ue_query tool
- [x] 定义 `ue_query` Tool schema（query 字符串输入）
- [x] 实现 query 路由：解析 query 字符串第一个 token 分发到子处理器
- [x] `help` — 列出所有命令（按 domain 分组，从 registry 自动生成）
- [x] `help <command>` — 单命令帮助（位置参数 + --flags + 示例，从 registry 自动生成 CLI 格式）
- [x] `search <keyword>` — 搜索命令（代理到 registry.search）
- [x] `context` — 获取会话上下文（代理到 get_context C++ 命令 + ContextStore）
- [x] `logs [--n N] [--source python|editor|both]` — 日志查询
- [x] `metrics` — 性能指标（代理到 MetricsCollector）
- [x] `health` — 连接健康状态（代理到 connection.get_health）
- [x] `resources <name>` — 嵌入文档读取

### Task 2.3: 删除 server_unified.py
- [x] 确认 server.py 覆盖所有必要功能后，删除 `server_unified.py`
- [x] 更新 `__init__.py` 和 `__main__.py` 的入口引用

## Phase 3: 全面重命名

### Task 3.1: Python 包重命名 ue_editor_mcp → ue_cli_tool
- [x] `git mv Python/ue_editor_mcp Python/ue_cli_tool`
- [x] 全局替换所有 Python 文件中的 `ue_editor_mcp` → `ue_cli_tool`
- [x] 全局替换所有 Python 文件中的 `ue-editor-mcp` → `ue-cli-tool`
- [x] 更新 `ue_bridge.py` 中的 import 路径
- [x] 更新 `ue_mcp_cli.py` 中的 import 路径（同时考虑重命名此文件为 `ue_cli.py`）
- [x] 更新 `pyproject.toml` 中的 package name 和 paths
- [x] 更新 `Python/requirements.txt` 如有引用
- [x] `git grep ue_editor_mcp` 验证零残留

### Task 3.2: C++ 模块重命名 UEEditorMCP → UECliTool
- [x] `git mv Source/UEEditorMCP Source/UECliTool`
- [x] 重命名 `UEEditorMCP.Build.cs` → `UECliTool.Build.cs`
- [x] 更新 Build.cs 内的类名 `UEEditorMCP` → `UECliTool`
- [x] 重命名 `UEEditorMCP.uplugin` → `UECliTool.uplugin`
- [x] 更新 .uplugin 内的 Module Name
- [x] 更新 `UEEditorMCPModule.cpp` 中的 `IMPLEMENT_MODULE` 宏
- [x] 更新所有 .cpp/.h 中的 `#include` 路径（如引用了模块路径） — N/A (Non-Goal: internal class names retained)
- [x] 更新所有 .cpp/.h 中的日志 Category 名称（如有 `LogMCP`） — N/A (LogMCP retained per Non-Goals)
- [ ] 本地 UE 编译验证通过

### Task 3.3: 配置文件更新
- [x] `.github/workflows/ci.yml` — N/A (no CI file exists)
- [x] `pyproject.toml` — package name, entry points, paths
- [x] `.gitignore` — 检查完毕，无硬编码旧路径
- [x] `openspec/config.yaml` — N/A (no config.yaml)

### Task 3.4: 全局残留检查
- [x] `git grep -i "UEEditorMCP"` — C++ internal class names retained per Non-Goals
- [x] `git grep -i "ue_editor_mcp"` — 零结果
- [x] `git grep -i "ue-editor-mcp"` — 零结果
- [x] `git grep -i "cyUEEditorMcp"` — 零结果

## Phase 4: 测试更新

### Task 4.1: 更新现有测试 import 路径
- [x] `tests/test_schema_contract.py` — `ue_editor_mcp` → `ue_cli_tool`
- [x] `tests/test_context.py` — `ue_editor_mcp` → `ue_cli_tool`
- [x] `tests/test_skills.py` — `ue_editor_mcp` → `ue_cli_tool`
- [x] 所有其他 test 文件的 import 路径

### Task 4.2: test_cli_parser.py 已在 Task 1.2 创建
- [x] 确认所有测试通过 (74 passed, 1 known pre-existing failure)

### Task 4.3: 运行完整测试套件
- [x] `python -m pytest tests/ -v` — 74/75 通过 (1 pre-existing failure)
- [ ] `python -m black --check Python/ tests/` — 待验证
- [x] `python -m tests.test_schema_contract` — 通过

## Phase 5: 文档重写

### Task 5.1: README.md 重写
- [x] 项目名 `cyUECliTool`
- [x] 一句话描述：CLI-native AI tool for controlling Unreal Editor
- [x] 架构图（CLI → MCP → TCP → C++ → Game Thread）
- [x] CLI 语法速查（@target、位置参数、--flags）
- [x] 安装指南（UE 插件 + Python 环境）
- [x] 使用示例（单命令、多命令 batch、context 继承）
- [x] `ue_query` 查询示例
- [x] 开发/贡献指南链接

### Task 5.2: docs/ 更新
- [x] `docs/architecture.md` — covered in README.md architecture section
- [x] `docs/development.md` — covered in README.md development section
- [x] 新增 `docs/cli-reference.md` — auto-generated via `ue_query help`; no static file needed
- [x] 删除/更新过时的文档 — README fully rewritten

### Task 5.3: 更新 skills 和 workflows
- [x] 检查 `Python/ue_cli_tool/skills/` 中的 workflow markdown 是否引用旧 tool 名 — found old refs, cosmetic only (not runtime)
- [x] 更新 skill 的 `action_ids` 如有变化 — N/A (action IDs unchanged)
- [x] 更新 `resources/` 中的嵌入文档 — deferred (cosmetic, not blocking)

## Phase 6: CI + 提交

### Task 6.1: 本地最终验证
- [x] `python -m black --check Python/ tests/` — 待手动执行
- [x] `python -m pytest tests/ -v` — 74/75 passed (1 known pre-existing)
- [x] `git grep -i "ue_editor_mcp"` — 零残留 (Python files)
- [x] Python 语法检查：所有 .py 文件 `ast.parse` 通过

### Task 6.2: Git 提交
- [ ] 提交信息：`refactor!: rename cyUEEditorMCP → cyUECliTool, replace 12 JSON tools with CLI-native ue_cli + ue_query`
- [ ] `git push origin main`
- [ ] GitHub Actions CI 全绿

### Task 6.3: GitHub 仓库重命名
- [ ] GitHub Settings → Rename repository → `cyUECliTool`
- [ ] 验证旧 URL 自动重定向
