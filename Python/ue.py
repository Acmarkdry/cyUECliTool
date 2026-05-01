#!/usr/bin/env python3
# coding: utf-8
"""Small wrapper for the CLI-first UE runtime."""

from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
	sys.path.insert(0, HERE)

from ue_cli_tool.cli import main


if __name__ == "__main__":
	raise SystemExit(main())
