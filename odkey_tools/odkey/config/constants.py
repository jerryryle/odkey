"""
Shared constants for ODKey configuration

These constants match the firmware definitions in include/program.h
"""

# Program size limits (matching firmware constants in include/program.h)
PROGRAM_FLASH_PAGE_SIZE = 4096  # Flash page size in bytes
PROGRAM_FLASH_MAX_SIZE = (1024 * 1024) - PROGRAM_FLASH_PAGE_SIZE  # ~1MB
PROGRAM_RAM_MAX_SIZE = 1024 * 1024  # 1MB

