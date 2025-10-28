# ODKey - A USB HID macro key, with a fun WiFi backdoor

You can find an explanation for this device's existence in [ABOUT.md](/ABOUT.md).

The ODKey is a hardware device featuring a single button (a [NovelKeys Big Switch](https://novelkeys.com/collections/switches/products/the-big-switch-series)), a USB interface, and a WiFi interface.

When plugged into a computer, the ODKey enumerates as a USB HID keyboard. You can press the ODKey's button to run a configurable script that sends keyboard events to the connected computer. See [ODKeyScript.md](ODKeyScript.md) for documentation on the scripting language used to send keyboard events.

The ODKey incorporates an [Adafruit Feather ESP32-S2](https://www.adafruit.com/product/5000) board. Those familiar with this product will note that, besides the USB interface, the ESP32-S2 sports a WiFi interface. And, of course, the ODKey offers WiFi functionality.

You can use both USB and WiFi to configure the ODKey and run programs.

## Overview of functionality

* The Button
    * A push/release of the ODKey button runs the current flash program (more on this below)
    * If the ODKey is held, it will repeatedly run the flash program as long as it remains held. While held, the ODKey will run the program completion, wait a configurable amount of time (as deteremined by the `button_repeat` setting), and then run the program again, etc. Once the button is released, any currently-running program will finish and then program exection will stop.
    * Pressing the ODKey button will not interrupt a running program. If you have uploaded a very long program and executed it by pressing the button, the only way to halt it is to physically unplug the ODKey.
    * The ODKey will not enqueue presses. When you press/release the button, two things can happen:
        * The ODKey is idle and begins executing the flash program
        * The ODKey is already executing a program and ignores the new button press.
* ODKeyScript Programs
    * ODKeyScript Programs are compiled locally using the ODKey Tools. Compiled bytecode is uploaded to the device via USB or WiFi.
    * An ODKeyScript Program can be uploaded to flash via USB or WiFi. When the button is pressed, it runs the program currently stored in flash. Flash is non-volatile storage and persists when the device is unplugged/re-plugged or owtherise rebooted.
    * An ODKeyScript Program can be uploaded to RAM and executed immediately. This is an ephemeral program meant for testing and/or remote control of a computer.
    * See [ODKeyScript.md](ODKeyScript.md) for documentation of theODKeyScript language used to write programs.
* Configuration
    * The ODKey can be configured via USB and/or WiFi.

## Setting up the ODKey Tools on macOS

### Prerequisites

1. **Install Homebrew** (if not already installed):
   See the [Homebrew installation guide](https://brew.sh/#install).

2. **Install Python 3.12+**:
   ```bash
   # Install Python 3.12+ via Homebrew
   brew install python@3.12
   ```

3. **Install uv**:
   ```bash
   # Install uv (Python package manager) via Homebrew
   brew install uv
   ```

4. **Verify installations**:
   ```bash
   python3 --version  # Should show Python 3.12.x or higher
   uv --version       # Should show uv version
   ```

### Clone and Setup the Repository

1. **Clone the ODKey repository**:
   ```bash
   git clone git@github.com:jerryryle/odkey.git
   ```

2. **Navigate to the tools directory**:
   ```bash
   cd odkey/odkey_tools
   ```

3. **Install dependencies and setup the project**:
   ```bash
   # Install project dependencies
   uv sync
   
   # Verify the CLI is working
   uv run odkey --help
   ```

## Using the ODKey Tools
### Device Interfaces
#### USB
The USB interface provides direct communication with the ODKey device using a custom HID protocol. Use this to perform initial configuration before WiFi is set up.

#### WiFi
The WiFi interface provides access to the ODKey device via an HTTP API. This allows you to control and configure the device without a physical USB connection. By default, the ODKey device appears on the network as `odkey.local` via mDNS.

The WiFi credentials, hostname, http port, and API key are all configurable. Use the USB interface for initial setup or if you don't otherwise have WiFi connectivity.

### Device Configuration

The ODKey device stores configuration values in NVS (Non-Volatile Storage) flash memory. You can read, write, and delete these configuration keys using the CLI tools.

#### Supported Configuration Keys

| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `wifi_ssid` | string | WiFi network name (SSID) to connect to | Empty (WiFi not enabled) |
| `wifi_pw` | string | WiFi network password | Empty (WiFi not enabled) |
| `mdns_hostname` | string | mDNS hostname for network discovery | "odkey" |
| `mdns_instance` | string | mDNS instance name for device identification | "ODKey Device" |
| `http_port` | u16 | HTTP server port for WiFi API access | 80 |
| `http_api_key` | string | API key for HTTP authentication | Empty (API disabled) |
| `button_debounce` | u32 | Button debounce time in milliseconds | 50ms |
| `button_repeat` | u32 | Button repeat delay in milliseconds | 225ms |

- **WiFi Configuration**: `wifi_ssid` and `wifi_pw` control which WiFi network the device connects to. If not set, the device operates in USB-only mode.
- **mDNS Discovery**: `mdns_hostname` sets the device's network hostname (e.g., "odkey.local"). `mdns_instance` sets the friendly name shown in network discovery tools.
- **HTTP Server**: `http_port` sets the port for the WiFi API server. `http_api_key` enables authentication for all HTTP operations.
- **Button Behavior**: `button_debounce` prevents false triggers from electrical noise when the button is pressed. `button_repeat` controls how long to wait before re-running the program while the button is held.

#### Configuration Commands

```bash
# Set/get WiFi credentials
## USB
uv run odkey nvs-set wifi_ssid "MyWiFiNetwork"
uv run odkey nvs-set wifi_pw "MyWiFiPassword"

uv run odkey nvs-get wifi_ssid
uv run odkey nvs-get wifi_pw

## HTTP
uv run odkey nvs-set wifi_ssid "MyWiFiNetwork" --interface http --host odkey.local --api-key your-api-key
uv run odkey nvs-set wifi_pw "MyWiFiPassword" --interface http --host odkey.local --api-key your-api-key

uv run odkey nvs-get wifi_ssid --interface http --host odkey.local --api-key your-api-key
uv run odkey nvs-get wifi_pw --interface http --host odkey.local --api-key your-api-key

# Set/get mDNS configuration
## USB
uv run odkey nvs-set mdns_hostname "my-odkey"
uv run odkey nvs-set mdns_instance "My ODKey Device"

uv run odkey nvs-get mdns_hostname
uv run odkey nvs-get mdns_instance

## HTTP
uv run odkey nvs-set mdns_hostname "my-odkey" --interface http --host odkey.local --api-key your-api-key
uv run odkey nvs-set mdns_instance "My ODKey Device" --interface http --host odkey.local --api-key your-api-key

uv run odkey nvs-get mdns_hostname --interface http --host odkey.local --api-key your-api-key
uv run odkey nvs-get mdns_instance --interface http --host odkey.local --api-key your-api-key

# Set/get HTTP server configuration
## USB
uv run odkey nvs-set http_port 8080 --type u16
uv run odkey nvs-set http_api_key "my-secret-api-key"

uv run odkey nvs-get http_port
uv run odkey nvs-get http_api_key

## HTTP
uv run odkey nvs-set http_port 8080 --type u16 --interface http --host odkey.local --api-key your-api-key
uv run odkey nvs-set http_api_key "my-secret-api-key" --interface http --host odkey.local --api-key your-api-key

uv run odkey nvs-get http_port --interface http --host odkey.local --api-key your-api-key
uv run odkey nvs-get http_api_key --interface http --host odkey.local --api-key your-api-key

# Set/get button behavior
## USB
uv run odkey nvs-set button_debounce 100 --type u32
uv run odkey nvs-set button_repeat 500 --type u32

uv run odkey nvs-get button_debounce
uv run odkey nvs-get button_repeat

## HTTP
uv run odkey nvs-set button_debounce 100 --type u32 --interface http --host odkey.local --api-key your-api-key
uv run odkey nvs-set button_repeat 500 --type u32 --interface http --host odkey.local --api-key your-api-key

uv run odkey nvs-get button_debounce --interface http --host odkey.local --api-key your-api-key
uv run odkey nvs-get button_repeat --interface http --host odkey.local --api-key your-api-key

# Delete configuration keys
## USB
uv run odkey nvs-delete wifi_ssid # disables WiFi
uv run odkey nvs-delete http_api_key # disables HTTP API

## HTTP
uv run odkey nvs-delete wifi_ssid --interface http --host odkey.local --api-key your-api-key # disables WiFi
uv run odkey nvs-delete http_api_key --interface http --host odkey.local --api-key your-api-key # disables HTTP API
```

### Program upload/execution

When you push the ODKey's button, it runs a program stored in flash. You can update this program over the USB/HTTP interfaces. The ODKey has a little less than 1MB reserved for the flash program.

You can also upload a temporary program over USB/HTTP to the ODKey's RAM and execute it immediately. The largest program you can upload to RAM is 8KB.

#### Compile and disassemble programs
The ODKey Tools include a compiler to compile ODKeyScript and a disassembler that takes a compiled program and outputs the ODKeyScript Virtual Machine opcodes. Note that the ODKey Tools upload command can automatically compile an ODKeyScript file for you before uploading it, so you do not need to invoke the compiler yourself.

```bash
# Compile an ODKeyScript program to bytecode
uv run odkey compile scripts/sample.odk sample.bin

# Disassemble the compiled bytecode to see the opcodes
uv run odkey disassemble sample.bin
```

#### Upload a Program
```bash
# Upload an ODKeyScript source file (compiles automatically)
## RAM
uv run odkey upload program.odk
## Flash
uv run odkey upload --target flash scripts/sample.odk

# Upload pre-compiled bytecode
## RAM
uv run odkey upload program.bin
## flash
uv run odkey upload --target flash program.bin
```

#### Upload and execute a program
```bash
# Upload ODKeyScript source and execute immediately (compiles automatically)
## RAM
uv run odkey upload --execute scripts/sample.odk
## flash
uv run odkey upload --execute --target flash scripts/sample.odk
```

#### Execute a program
```bash
# Executes a previously-uploaded program
## RAM
uv run odkey execute
## flash
uv run odkey execute --target flash
```

#### Download a Program
```bash
# Download program
## RAM
uv run odkey download --output program.bin
## flash
uv run odkey download --target flash --output program.bin

# Download program and disassemble it
## RAM
uv run odkey download --disassemble
## flash
uv run odkey download --target flash --disassemble
```

### Log Management

The ODKey device maintains a 32KB ring buffer in PSRAM that captures all ESP32 log output (ESP_LOGI, ESP_LOGE, etc.). Logs continue to be sent to the serial port as normal while also being captured to the ring buffer for download.

#### Download Logs
```bash
# Download logs from device (displays to console)
uv run odkey log

# Save logs to file
uv run odkey log --output logs.txt

# Download via HTTP interface
uv run odkey log --interface http --host odkey.local --api-key key
```

#### Clear Log Buffer
```bash
# Clear the log buffer on the device
uv run odkey log-clear

# Clear via HTTP interface
uv run odkey log-clear --interface http --host odkey.local --api-key key
```
