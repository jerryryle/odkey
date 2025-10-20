# ODKey Tools

A comprehensive Python toolkit for developing and managing ODKeyScript programs on ODKey devices.

## Features

- **ODKeyScript Compiler**: Compiles ODKeyScript source code to bytecode
- **Unified CLI**: Single command-line interface for all operations
- **Dual Interface Support**: USB (Raw HID) and WiFi (HTTP) communication
- **Program Management**: Upload and download device programs
- **NVS Storage**: Read, write, and delete configuration values
- **Disassembler**: Disassembles compiled bytecode for debugging
- **Device Discovery**: List and connect to available devices

## Installation

This project uses `uv` for dependency management. Install with:

```bash
# Install the project and dependencies
uv sync
```

## Usage

After installation, use the unified CLI with the following commands:

### Compile ODKeyScript

```bash
# Compile a .odk source file to bytecode
uv run odkey compile program.odk program.bin
```

### Upload Programs

```bash
# Upload an ODKeyScript source file (compiles automatically)
uv run odkey upload program.odk

# Upload pre-compiled bytecode
uv run odkey upload program.bin

# Upload via WiFi (HTTP interface)
uv run odkey upload program.odk --interface wifi --host odkey.local --api-key your-api-key

# Upload via WiFi with custom hostname/IP
uv run odkey upload program.odk --interface wifi --host 192.168.1.100 --api-key your-api-key
uv run odkey upload program.odk --interface wifi --host my-odkey-device.local --api-key your-api-key

# Upload with custom USB device VID/PID
uv run odkey upload program.odk --vid 0x303A --pid 0x4008
```

### Download Programs

```bash
# Download program from device
uv run odkey download --output program.bin

# Download and disassemble
uv run odkey download --disassemble

# Download via WiFi
uv run odkey download --interface wifi --host odkey.local --api-key your-api-key

# Download via WiFi with custom hostname/IP
uv run odkey download --interface wifi --host 192.168.1.100 --api-key your-api-key
uv run odkey download --interface wifi --host my-odkey-device.local --api-key your-api-key
```

### NVS Storage Management

```bash
# Set configuration values
uv run odkey nvs-set wifi_ssid "MyNetwork"
uv run odkey nvs-set http_port 80 --type u16
uv run odkey nvs-set cert --file cert.pem --type blob

# Set configuration values via WiFi
uv run odkey nvs-set wifi_ssid "MyNetwork" --interface wifi --host 192.168.1.100 --api-key your-api-key
uv run odkey nvs-set http_port 80 --type u16 --interface wifi --host my-odkey.local --api-key your-api-key

# Get configuration values
uv run odkey nvs-get wifi_ssid
uv run odkey nvs-get cert --output cert.pem

# Get configuration values via WiFi
uv run odkey nvs-get wifi_ssid --interface wifi --host 192.168.1.100 --api-key your-api-key

# Delete configuration keys
uv run odkey nvs-delete wifi_ssid
uv run odkey nvs-delete wifi_ssid --interface wifi --host my-odkey.local --api-key your-api-key
```

### Device Management

```bash
# List available HID devices
uv run odkey list-devices

# Disassemble compiled bytecode
uv run odkey disassemble program.bin
```

## Development

### Project Structure

```
odkey_tools/
├── odkey/                    # Main package
│   ├── cli.py               # Unified command-line interface
│   ├── config/              # Configuration interfaces
│   │   ├── __init__.py
│   │   ├── odkey_config_usb.py    # USB (Raw HID) interface
│   │   └── odkey_config_http.py   # HTTP (WiFi) interface
│   └── odkeyscript/         # ODKeyScript tools
│       ├── odkeyscript_compiler.py    # ODKeyScript compiler
│       └── odkeyscript_disassembler.py # Bytecode disassembler
├── sample.odk              # Sample ODKeyScript program
├── hello.odk               # Hello world example
├── pyproject.toml          # Project configuration
└── README.md              # This file
```

### Running Tests

```bash
# Run all tests
uv run pytest

# Test CLI functionality
uv run odkey --help
```

### Development Setup

```bash
# Install development dependencies
uv sync --group dev

# Run linting
uv run flake8 odkey/

# Run type checking
uv run mypy odkey/
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

## Communication Interfaces

### USB Interface (Raw HID)

The USB interface communicates with ODKey devices using Raw HID with the following protocol:

#### Commands
- `CMD_PROGRAM_WRITE_START (0x20)`: Start write session with program size
- `CMD_PROGRAM_WRITE_CHUNK (0x21)`: Send 60-byte data chunk  
- `CMD_PROGRAM_WRITE_FINISH (0x22)`: Finish write session with program size
- `CMD_PROGRAM_READ_START (0x23)`: Start read session
- `CMD_PROGRAM_READ_CHUNK (0x24)`: Read 60-byte data chunk
- `CMD_NVS_SET_START (0x30)`: Start NVS set operation
- `CMD_NVS_SET_DATA (0x31)`: Send NVS data chunk
- `CMD_NVS_SET_FINISH (0x32)`: Finish NVS set operation
- `CMD_NVS_GET_START (0x33)`: Start NVS get operation
- `CMD_NVS_GET_DATA (0x34)`: Get NVS data chunk
- `CMD_NVS_DELETE (0x35)`: Delete NVS key

#### Responses
- `RESP_OK (0x10)`: Command successful
- `RESP_ERROR (0x11)`: Command failed

#### Data Format
- All packets are 64 bytes
- First byte is the report ID
- Bytes 1-3 are reserved
- Bytes 4-63 contain command data (60 bytes payload)
- Program size is sent as 32-bit little-endian integer
- Data chunks are padded to 60 bytes with zeros

### HTTP Interface (WiFi)

The HTTP interface communicates with ODKey devices over WiFi using REST API endpoints:

#### Endpoints
- `POST /api/program`: Upload program bytecode
- `GET /api/program`: Download program bytecode
- `DELETE /api/program`: Delete program
- `GET /api/status`: Check device status
- `POST /api/nvs/{key}`: Set NVS value
- `GET /api/nvs/{key}`: Get NVS value
- `DELETE /api/nvs/{key}`: Delete NVS key

#### Authentication
- **Required** API key authentication via `Authorization: Bearer <token>` header
- API key must be configured on the device via NVS storage
- Use the USB interface to configure the initial API Key. You can then change it via USB or HTTP.
- All HTTP operations require a valid API key.
- If no API Key is configured on the device, all HTTP operations will be rejected as unauthorized.

## Troubleshooting

### Device Not Found (USB)

1. Make sure the ODKey device is connected via USB
2. Check that the device is in programming mode
3. Verify the correct VID/PID using `uv run odkey list-devices`
4. Ensure proper USB drivers are installed

### Device Not Found (WiFi)

1. Ensure the device is connected to WiFi
2. Check that the device hostname/IP is correct
3. Verify the device is running the HTTP service
4. Test connectivity with `ping odkey.local`
5. Ensure you have the correct API key configured on the device

### Upload/Download Fails

1. Check that the program size is within limits (1MB max)
2. Verify the device is not in use by another application
3. For USB: Try disconnecting and reconnecting the device
4. For WiFi: Check network connectivity and device status
5. Check device logs for error messages

### NVS Operations Fail

1. Verify the key name is valid (max 15 characters)
2. Check that the value type matches the data
3. Ensure the value size is within limits (1024 bytes max)
4. For blob types, verify the data is valid binary

### Compilation Errors

1. Verify the ODKeyScript syntax is correct
2. Check that all referenced keys are valid
3. Ensure proper nesting of repeat blocks
4. Validate string literals are properly quoted

## Dependencies

- `hidapi`: For USB HID communication
- `requests`: For HTTP communication (WiFi interface)
- `setuptools`: For package building
- `wheel`: For wheel distribution

## License

This project is part of the ODKey project and follows the same license terms.
