# Tasks: CLI-Native Interface + Full Rename

## Phase 1: CLI 解析器核心（不依赖重命名，先做功能）

### Task 1.1: 新建 cli_parser.py
- [ ] 新建 `Python/ue_editor_mcp/cli_parser.py`（暂用旧包名，Phase 3 统一改）
- [ ] 实现 `CliParser.__init__(registry)` — 持有 ActionRegistry 引用
- [ ] 实现 `_get_positional_order(command)` — 从 registry 的 `input_schema.required` 自动推导参数顺序，缓存结果
- [ ] 实现 `_detect_context_param(command)` — 检查 schema properties 是否含 `blueprint_name`/`material_name`/`widget_name`，自动判断上下文参数名
- [ ] 实现 `parse_line(line, context)` — 解析单行 CLI：tokenize → 分离命令/位置参数/--flags → 注入 context → 合并
- [ ] 实现 `parse(text)` — 解析多行 CLI：处理 `@target`、`#` 注释、空行，逐行调用 `parse_line`
- [ ] 实现 `_tokenize(line)` — 用 `shlex.split` 处理引号
- [ ] 实现 `_coerce_value(val)` — 字符串→bool/int/float/JSON array/object 自动转换
- [ ] 实现 `to_batch_commands(parse_result)` — 转为 `batch_execute` 格式
- [ ] 验证：零手写映射表，全部从 registry 推导

### Task 1.2: 新建 test_cli_parser.py
- [ ] 基础解析：空输入、注释、单命令、多命令
- [ ] 上下文：`@target` 注入、上下文切换、无上下文
- [ ] 位置参数：自动映射到 required 字段、上下文排除后的 slot 填充
- [ ] --flag 参数：`--name value`、`--bool_flag true`、`--array [1,2,3]`
- [ ] 值转换：bool、int、float、JSON array、JSON object、纯字符串
- [ ] 引号字符串：`"multi word value"`
- [ ] 批量转换：`to_batch_commands` 输出正确
- [ ] 边缘情况：未知命令透传、多余位置参数、--flag 无值
- [ ] 全场景测试：完整蓝图创建脚本（10+ 命令）
- [ ] 所有测试通过

### Task 1.3: 删除 script.py 和 test_script.py
- [ ] 删除 `Python/ue_editor_mcp/script.py`
- [ ] 删除 `tests/test_script.py`
- [ ] 移除 `server_unified.py` 中 `ue_script` tool 定义和 handler

## Phase 2: MCP Server 重写（2 tools）

### Task 2.1: 新建 server.py — ue_cli tool
- [ ] 新建 `Python/ue_editor_mcp/server.py`（替代 `server_unified.py`）
- [ ] 定义 `ue_cli` Tool schema（command 字符串输入）
- [ ] 实现 handler：调用 `CliParser.parse()` → `to_batch_commands()` → 发送 `batch_execute`（多行）或 `send_command`（单行）
- [ ] 单行命令直接执行（不走 batch），减少开销
- [ ] 多行命令走 `batch_execute`，一次 TCP round-trip
- [ ] 结果格式化：返回每行的成功/失败 + 原始 CLI 行

### Task 2.2: server.py — ue_query tool
- [ ] 定义 `ue_query` Tool schema（query 字符串输入）
- [ ] 实现 query 路由：解析 query 字符串第一个 token 分发到子处理器
- [ ] `help` — 列出所有命令（按 domain 分组，从 registry 自动生成）
- [ ] `help <command>` — 单命令帮助（位置参数 + --flags + 示例，从 registry 自动生成 CLI 格式）
- [ ] `search <keyword>` — 搜索命令（代理到 registry.search）
- [ ] `context` — 获取会话上下文（代理到 get_context C++ 命令 + ContextStore）
- [ ] `logs [--n N] [--source python|editor|both]` — 日志查询
- [ ] `metrics` — 性能指标（代理到 MetricsCollector）
- [ ] `health` — 连接健康状态（代理到 connection.get_health）
- [ ] `resources <name>` — 嵌入文档读取

### Task 2.3: 删除 server_unified.py
- [ ] 确认 server.py 覆盖所有必要功能后，删除 `server_unified.py`
- [ ] 更新 `__init__.py` 和 `__main__.py` 的入口引用

## Phase 3: 全面重命名

### Task 3.1: Python 包重命名 ue_editor_mcp → ue_cli_tool
- [ ] `git mv Python/ue_editor_mcp Python/ue_cli_tool`
- [ ] 全局替换所有 Python 文件中的 `ue_editor_mcp` → `ue_cli_tool`
- [ ] 全局替换所有 Python 文件中的 `ue-editor-mcp` → `ue-cli-tool`
- [ ] 更新 `ue_bridge.py` 中的 import 路径
- [ ] 更新 `ue_mcp_cli.py` 中的 import 路径（同时考虑重命名此文件为 `ue_cli.py`）
- [ ] 更新 `pyproject.toml` 中的 package name 和 paths
- [ ] 更新 `Python/requirements.txt` 如有引用
- [ ] `git grep ue_editor_mcp` 验证零残留

### Task 3.2: C++ 模块重命名 UEEditorMCP → UECliTool
- [ ] `git mv Source/UEEditorMCP Source/UECliTool`
- [ ] 重命名 `UEEditorMCP.Build.cs` → `UECliTool.Build.cs`
- [ ] 更新 Build.cs 内的类名 `UEEditorMCP` → `UECliTool`
- [ ] 重命名 `UEEditorMCP.uplugin` → `UECliTool.uplugin`
- [ ] 更新 .uplugin 内的 Module Name
- [ ] 更新 `UEEditorMCPModule.cpp` 中的 `IMPLEMENT_MODULE` 宏
- [ ] 更新所有 .cpp/.h 中的 `#include` 路径（如引用了模块路径）
- [ ] 更新所有 .cpp/.h 中的日志 Category 名称（如有 `LogMCP`）
- [ ] 本地 UE 编译验证通过

### Task 3.3: 配置文件更新
- [ ] `.github/workflows/ci.yml` — 更新所有路径引用
- [ ] `pyproject.toml` — package name, entry points, paths
- [ ] `.gitignore` — 检查是否有硬编码旧路径
- [ ] `openspec/config.yaml` — 如有项目名引用

### Task 3.4: 全局残留检查
- [ ] `git grep -i "UEEditorMCP"` — 零结果（除了 git history）
- [ ] `git grep -i "ue_editor_mcp"` — 零结果
- [ ] `git grep -i "ue-editor-mcp"` — 零结果
- [ ] `git grep -i "cyUEEditorMcp"` — 零结果（除了 git history/archived）

## Phase 4: 测试更新

### Task 4.1: 更新现有测试 import 路径
- [ ] `tests/test_schema_contract.py` — `ue_editor_mcp` → `ue_cli_tool`
- [ ] `tests/test_context.py` — `ue_editor_mcp` → `ue_cli_tool`
- [ ] `tests/test_skills.py` — `ue_editor_mcp` → `ue_cli_tool`
- [ ] 所有其他 test 文件的 import 路径

### Task 4.2: test_cli_parser.py 已在 Task 1.2 创建
- [ ] 确认所有测试通过

### Task 4.3: 运行完整测试套件
- [ ] `python -m pytest tests/ -v` — 全部通过
- [ ] `python -m black --check Python/ tests/` — 全部通过
- [ ] `python -m tests.test_schema_contract` — 通过（已知的历史问题除外）

## Phase 5: 文档重写

### Task 5.1: README.md 重写
- [ ] 项目名 `cyUECliTool`
- [ ] 一句话描述：CLI-native AI tool for controlling Unreal Editor
- [ ] 架构图（CLI → MCP → TCP → C++ → Game Thread）
- [ ] CLI 语法速查（@target、位置参数、--flags）
- [ ] 安装指南（UE 插件 + Python 环境）
- [ ] 使用示例（单命令、多命令 batch、context 继承）
- [ ] `ue_query` 查询示例
- [ ] 开发/贡献指南链接

### Task 5.2: docs/ 更新
- [ ] `docs/architecture.md` — 新架构图 + 模块说明
- [ ] `docs/development.md` — CLI parser 开发指南
- [ ] 新增 `docs/cli-reference.md` — 命令参考（或说明如何用 `ue_query help` 自动获取）
- [ ] 删除/更新过时的文档

### Task 5.3: 更新 skills 和 workflows
- [ ] 检查 `Python/ue_cli_tool/skills/` 中的 workflow markdown 是否引用旧 tool 名
- [ ] 更新 skill 的 `action_ids` 如有变化
- [ ] 更新 `resources/` 中的嵌入文档

## Phase 6: CI + 提交

### Task 6.1: 本地最终验证
- [ ] `python -m black --check Python/ tests/` — 通过
- [ ] `python -m pytest tests/ -v` — 全部通过
- [ ] `git grep -i "ue_editor_mcp"` — 零残留
- [ ] Python 语法检查：所有 .py 文件 `ast.parse` 通过

### Task 6.2: Git 提交
- [ ] 提交信息：`refactor!: rename cyUEEditorMCP → cyUECliTool, replace 12 JSON tools with CLI-native ue_cli + ue_query`
- [ ] `git push origin main`
- [ ] GitHub Actions CI 全绿

### Task 6.3: GitHub 仓库重命名
- [ ] GitHub Settings → Rename repository → `cyUECliTool`
- [ ] 验证旧 URL 自动重定向
