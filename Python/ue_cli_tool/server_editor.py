"""
UE Editor MCP Server 鈥?level, viewport, and asset management (18 tools).

Covers: actor CRUD, transforms, viewport control, save, assets, blueprint summary, auto-layout.
"""

from .server_factory import create_mcp_server
from .tools import editor

server, main = create_mcp_server("ue-cli-tool", [editor])

if __name__ == "__main__":
    main()
