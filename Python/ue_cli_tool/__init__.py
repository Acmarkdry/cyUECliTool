# coding: utf-8
"""CLI-first AI interface for controlling Unreal Engine Editor."""

__version__ = "0.6.0"
__author__ = "zolnoor"

from .connection import PersistentUnrealConnection
from .cli import main

__all__ = ["PersistentUnrealConnection", "main", "__version__"]
