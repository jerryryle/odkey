"""
ODKeyScript Compiler Package

A Python compiler and tools for the ODKeyScript language.
"""

__version__ = "1.0.0"
__author__ = "ODKey Project"
__email__ = "odkey@example.com"

from .odkeyscript_compiler import Compiler, CompileError
from .disassemble import disassemble

__all__ = ["Compiler", "CompileError", "disassemble"]
