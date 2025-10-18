"""
ODKey USB Configuration Interface

This module provides USB system configuration functionality for ODKey devices,
including program upload and download operations.
"""

from .odkey_config import ODKeyConfig, ODKeyUploadError, main

__all__ = ["ODKeyConfig", "ODKeyUploadError", "main"]
