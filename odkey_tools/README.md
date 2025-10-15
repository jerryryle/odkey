# ODKey Tools

A comprehensive Python toolkit for developing and uploading ODKeyScript programs to ODKey devices.

## Features

- **ODKeyScript Compiler**: Compiles ODKeyScript source code to bytecode
- **USB Upload Tool**: Uploads programs to ODKey devices over USB using Raw HID
- **Disassembler**: Disassembles compiled bytecode for debugging
- **Test Tools**: Built-in testing utilities

## Installation

This project uses `uv` for dependency management. Install with:

```bash
# Install the project and dependencies
uv sync
```

## Usage

After installation, the following commands are available:

### Compile ODKeyScript

```bash
# Compile a .odk source file to bytecode
odkey-compile program.odk program.bin
```

### Upload to Device

```bash
# Upload an ODKeyScript source file (compiles automatically)
odkey-upload program.odk

# Upload pre-compiled bytecode
odkey-upload program.bin

# List available HID devices
odkey-upload --list-devices

# Specify custom device VID/PID
odkey-upload --vid 0x1234 --pid 0x5678 program.odk
```

### Disassemble Bytecode

```bash
# Disassemble compiled bytecode for debugging
odkey-disassemble program.bin
```

### Test Tools

```bash
# Run compiler tests
odkey-test

# Test upload functionality
odkey-test-upload
```

## Development

### Project Structure

```
odkey_tools/
├── odkeyscript/           # Main package
│   ├── odkeyscript_compiler.py    # ODKeyScript compiler
│   ├── odkeyscript_disassembler.py # Bytecode disassembler
│   ├── odkey_upload.py            # USB upload tool
│   ├── test_compiler.py           # Compiler tests
│   └── test_upload.py             # Upload tests
├── sample.odk            # Sample ODKeyScript program
├── pyproject.toml        # Project configuration
└── README.md            # This file
```

### Running Tests

```bash
# Run all tests
uv run pytest

# Run specific test file
uv run python -m odkeyscript.test_compiler
uv run python -m odkeyscript.test_upload
```

### Development Setup

```bash
# Install development dependencies
uv sync --group dev

# Run linting
uv run flake8 odkeyscript/

# Run type checking
uv run mypy odkeyscript/
```

## ODKeyScript Language

ODKeyScript is a simple scripting language for creating keyboard macros. See the main project documentation for language details.

### Example Program

```odk
# Simple greeting macro
press_time 50
type "Hello, World!"
press ENTER
pause 1000
type "This is ODKeyScript!"
press ENTER
```

## USB Upload Protocol

The upload tool communicates with ODKey devices using USB Raw HID with the following protocol:

### Commands

- `CMD_WRITE_START (0x10)`: Start write session with program size
- `CMD_WRITE_CHUNK (0x11)`: Send 64-byte data chunk  
- `CMD_WRITE_FINISH (0x12)`: Finish write session with program size

### Responses

- `RESP_OK (0x20)`: Command successful
- `RESP_ERROR (0x21)`: Command failed

### Data Format

- All packets are 64 bytes
- First byte is the report ID
- Remaining 63 bytes contain command data
- Program size is sent as 32-bit little-endian integer
- Data chunks are padded to 63 bytes with zeros

## Troubleshooting

### Device Not Found

1. Make sure the ODKey device is connected via USB
2. Check that the device is in programming mode
3. Verify the correct VID/PID using `odkey-upload --list-devices`
4. Ensure proper USB drivers are installed

### Upload Fails

1. Check that the program size is within limits (1MB max)
2. Verify the device is not in use by another application
3. Try disconnecting and reconnecting the device
4. Check device logs for error messages

### Compilation Errors

1. Verify the ODKeyScript syntax is correct
2. Check that all referenced keys are valid
3. Ensure proper nesting of repeat blocks
4. Validate string literals are properly quoted

## Dependencies

- `hidapi`: For USB HID communication
- `setuptools`: For package building
- `wheel`: For wheel distribution

## License

This project is part of the ODKey project and follows the same license terms.
