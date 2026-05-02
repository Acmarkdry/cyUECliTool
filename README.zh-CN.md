# cyUECliTool

[English](README.md) | [中文](README.zh-CN.md)

用于控制 Unreal Engine Editor 的 CLI-first AI 工具。

v0.6.0 将 PowerShell launcher 和 Codex skill 作为主入口。Codex 和其他智能体应调用项目根目录的 `.\ue.ps1`，复杂 Python 或多行 CLI DSL 写入文件后执行，并默认通过 daemon-backed CLI 操作 Unreal Editor，而不是直接编写 MCP tool JSON。

MCP 支持仍然保留，但只作为 legacy 兼容路径。

## 快速开始

```powershell
# 1. 编译 UE 项目，确保 editor plugin 可用。

# 2. 安装/初始化 Python 环境。
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\setup_mcp.ps1

# 3. 启动 Unreal Editor。C++ bridge 默认监听 55558。
D:\UnrealEngine5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe `
  D:\UnrealGame\Lyra_56\Lyra_56.uproject -MCPPort=55558

# 4. 安装项目根 launcher，并使用 CLI-first runtime。
.\scripts\install_project_launchers.ps1 -ProjectRoot D:\UnrealGame\Lyra_56
cd D:\UnrealGame\Lyra_56
.\ue.ps1 version
.\ue.ps1 query health
.\ue.ps1 run "get_context"
```

`run`、`query`、`py` 和 `doctor` 默认会自动启动 daemon。

## Codex Skill

插件内置可复用 skill：`skills/unreal-ue-cli`。推荐用脚本把 Codex skill 链接到插件内的 canonical skill：

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\scripts\link_codex_skill.ps1
```

这样插件内 skill 和 `C:\Users\<you>\.codex\skills\unreal-ue-cli` 可以保持一致，后续只维护一份。

## 架构

```text
Codex / user
  -> ue.ps1 run/query/py/doctor
  -> local Python daemon on 127.0.0.1:55559
  -> PersistentUnrealConnection
  -> Unreal Editor C++ bridge on 127.0.0.1:55558
  -> MCPBridge / FEditorAction handlers
```

C++ bridge 和 action classes 继续保留。关键变化是模型边界：智能体输出 CLI 命令和脚本文件，而不是 MCP JSON。

## CLI 用法

单条命令：

```powershell
.\ue.ps1 run "create_blueprint BP_Player --parent_class Character"
```

多行 CLI DSL 使用文件，避免 PowerShell 引号问题：

```powershell
New-Item -ItemType Directory -Force .\.codex\tmp | Out-Null
@'
@BP_Player
add_blueprint_variable Health --variable_type Float
compile_blueprint
'@ | Set-Content -Encoding UTF8 .\.codex\tmp\task.uecli
.\ue.ps1 run --file .\.codex\tmp\task.uecli
```

Unreal Python 使用 `py`/`python` 子命令，绕开 run DSL parser：

```powershell
@'
import unreal
_result = unreal.SystemLibrary.get_engine_version()
'@ | Set-Content -Encoding UTF8 .\.codex\tmp\task.py
.\ue.ps1 py --json --file .\.codex\tmp\task.py
```

`_result` 是结构化返回值；`print()` 只用于日志。如果把同一份数据既 `print()` 又赋给 `_result`，CLI 会同时显示 `Stdout` 和 `Return value`，这是两个不同输出通道。

查询帮助和诊断：

```powershell
.\ue.ps1 version
.\ue.ps1 doctor
.\ue.ps1 daemon status
.\ue.ps1 query help
.\ue.ps1 query "help create_blueprint"
.\ue.ps1 query "search material"
.\ue.ps1 query "logs --n 50 --source editor"
```

## 输出模式

默认输出是适合人和模型阅读的文本：

```text
OK exec_python
Return value: {"engine":"5.6.1-0+UE5"}
```

脚本和测试使用 `--json`：

```powershell
.\ue.ps1 py --json --file .\.codex\tmp\task.py
```

底层调试使用 `--raw`：

```powershell
.\ue.ps1 run "get_context" --raw
```

## 项目配置

`ue_mcp_config.yaml` 会从项目树加载：

```yaml
engine_root: D:/UnrealEngine5/UnrealEngine
project_root: D:/UnrealGame/Lyra_56
tcp_port: 55558
daemon_port: 55559
auto_start_daemon: true
```

同时运行多个 editor 实例时，请使用不同的 `tcp_port` 和 `daemon_port`。

## Legacy MCP

legacy MCP server 仍可用：

```powershell
.\Python\.venv\Scripts\python.exe -m ue_cli_tool.server
```

它暴露旧的 `ue_cli` 和 `ue_query` 两个工具，仅供尚未迁移的客户端使用。新开发应面向 `.\ue.ps1`。

## 开发

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\Python\.venv\Scripts\python.exe -m pytest Python\tests -q
.\Python\.venv\Scripts\python.exe -m pytest tests -q
```

## 文档

| 文档 | 说明 |
|------|------|
| [Installation](docs/installation.md) | CLI-first 安装和故障排查 |
| [Architecture](docs/architecture.md) | 技术架构、C++ server、事件系统和协议 |
| [Actions](docs/actions.md) | Action domain reference |
| [Development](docs/development.md) | 添加 action、测试和 commandlet mode |
| [CLI-first Migration](docs/cli-first-migration.md) | 迁移状态和兼容性说明 |
| [GitHub Actions Runner](docs/github-actions-runner.md) | Self-hosted Windows runner 配置 |
| [Changelog](CHANGELOG.md) | 版本更新记录 |

## Credits

由 Acmarkdry 维护，并由 Codex 辅助开发。

Based on [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP) (MIT License).

## License

MIT
