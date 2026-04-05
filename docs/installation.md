# 安装与配置

## 环境要求

- Unreal Engine 5.6+（内置 Python 3.11+，无需另行安装）
- Visual Studio 2022
- 任意兼容 MCP 的客户端（Claude Desktop、VS Code + Copilot、Cursor 等）

## 第 1 步：编译 C++ 插件

插件位于 `Plugins/UECliTool/`，编译编辑器目标：

```
Engine\Build\BatchFiles\Build.bat <ProjectName>Editor Win64 Development <Project>.uproject -waitmutex
```

## 第 2 步：Python 环境配置

**PowerShell（推荐，使用 UE 内置 Python）：**
```powershell
cd Plugins/UECliTool
.\setup_mcp.ps1
```

脚本自动完成：
1. 检测 UE 内置 Python（`Engine/Binaries/ThirdParty/Python3/Win64/python.exe`）
2. 在 `Python/.venv` 创建虚拟环境并安装 `mcp` 包
3. 在项目根目录生成 `.vscode/mcp.json`

<details>
<summary>手动配置</summary>

```bash
cd Plugins/UECliTool/Python
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

`.vscode/mcp.json`：
```jsonc
{
  "servers": {
    "ue-cli-tool": {
      "command": "./Plugins/UECliTool/Python/.venv/Scripts/python.exe",
      "args": ["-m", "ue_cli_tool.server"],
      "env": { "PYTHONPATH": "./Plugins/UECliTool/Python" }
    }
  }
}
```
</details>

## 第 3 步：启动

1. 在 Unreal Editor 中打开项目（插件自动在端口 55558 启动 TCP 服务器）
2. 打开 VS Code — `ue-cli-tool` 通过 stdio 启动并连接
3. 使用 AI 客户端通过 `ue_cli` / `ue_query` 两个工具控制编辑器

## MCP 客户端配置（Claude Desktop）

```json
{
  "mcpServers": {
    "ue-cli-tool": {
      "command": "ue-cli-tool"
    }
  }
}
```

需要先 `pip install -e .` 安装包。
