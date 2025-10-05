# ODKey - ESP32-S2 Feather Project

A PlatformIO project using ESP-IDF framework targeting the Adafruit ESP32-S2 Feather board.

## Hardware

- **Board**: Adafruit ESP32-S2 Feather
- **Microcontroller**: ESP32-S2
- **Framework**: ESP-IDF
- **Development Environment**: PlatformIO

## Features

- ESP32-S2 with WiFi and USB OTG capabilities
- PlatformIO build system with ESP-IDF framework
- Optimized for Adafruit Feather form factor
- Battery-powered operation support

## Development Setup

### Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) or [PlatformIO IDE](https://platformio.org/platformio-ide)
- ESP-IDF framework (automatically managed by PlatformIO)
- USB cable for programming and debugging

### Building and Uploading

```bash
# Build the project
pio run

# Upload to board
pio run --target upload

# Monitor serial output
pio device monitor

# Clean build
pio run --target clean
```

### Configuration

The project uses ESP-IDF's configuration system. Key configuration files:

- `sdkconfig` - Main ESP-IDF configuration
- `sdkconfig.adafruit_feather_esp32s2` - Board-specific configuration
- `platformio.ini` - PlatformIO project configuration

## Project Structure

```
├── src/                    # Source code
│   ├── main.c             # Main application
│   └── CMakeLists.txt     # Component build configuration
├── include/               # Header files
├── lib/                   # Custom libraries
├── test/                  # Unit tests
├── platformio.ini        # PlatformIO configuration
├── CMakeLists.txt         # Root CMake configuration
├── sdkconfig              # ESP-IDF configuration
└── .cursorrules           # Cursor IDE rules
```

## Development Guidelines

- Follow ESP-IDF coding standards
- Use ESP-IDF logging macros (ESP_LOGI, ESP_LOGW, ESP_LOGE)
- Implement proper error handling with `esp_err_t`
- Use FreeRTOS for task management
- Consider power consumption for battery operation

## Hardware Connections

Refer to the [Adafruit ESP32-S2 Feather pinout](https://learn.adafruit.com/adafruit-feather-esp32-s2/pinouts) for GPIO assignments and capabilities.

## License

[Add your license information here]

## Contributing

1. Follow the coding standards defined in `.cursorrules`
2. Test on actual hardware before submitting changes
3. Document any hardware-specific configurations
4. Use ESP-IDF logging for debugging information
