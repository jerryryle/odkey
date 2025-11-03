"""
ODKey USB Configuration Interface

This module provides USB system configuration functionality for ODKey devices,
including program upload and download operations.
"""

# Import constants first (no dependencies)
from .constants import (
                        PROGRAM_FLASH_MAX_SIZE,
                        PROGRAM_FLASH_PAGE_SIZE,
                        PROGRAM_RAM_MAX_SIZE,
)

# Then import other modules
from .odkey_config_http import ODKeyConfigHttp
from .odkey_config_usb import ODKeyConfigUsb, ODKeyUploadError, main

__all__ = [
    "ODKeyConfigUsb",
    "ODKeyConfigHttp",
    "ODKeyUploadError",
    "main",
    "PROGRAM_FLASH_PAGE_SIZE",
    "PROGRAM_FLASH_MAX_SIZE",
    "PROGRAM_RAM_MAX_SIZE",
]
