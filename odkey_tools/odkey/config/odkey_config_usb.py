#!/usr/bin/env python3
"""
ODKey USB Configuration Interface

Provides USB system configuration functionality for ODKey devices over Raw HID.
"""

import argparse
import struct
import sys
import time
from pathlib import Path
from typing import Any, Optional, Tuple

try:
    import hid
except ImportError:
    print("Error: hidapi library not found. Install with: pip install hidapi")
    sys.exit(1)

from ..odkeyscript.odkeyscript_compiler import CompileError, Compiler

# Protocol constants (matching the ESP32 firmware)
RESP_OK = 0x10
RESP_ERROR = 0x11

# Flash program commands
CMD_FLASH_PROGRAM_WRITE_START = 0x20
CMD_FLASH_PROGRAM_WRITE_CHUNK = 0x21
CMD_FLASH_PROGRAM_WRITE_FINISH = 0x22
CMD_FLASH_PROGRAM_READ_START = 0x23
CMD_FLASH_PROGRAM_READ_CHUNK = 0x24
CMD_FLASH_PROGRAM_EXECUTE = 0x25

# RAM program commands
CMD_RAM_PROGRAM_WRITE_START = 0x26
CMD_RAM_PROGRAM_WRITE_CHUNK = 0x27
CMD_RAM_PROGRAM_WRITE_FINISH = 0x28
CMD_RAM_PROGRAM_READ_START = 0x29
CMD_RAM_PROGRAM_READ_CHUNK = 0x2A
CMD_RAM_PROGRAM_EXECUTE = 0x2B
CMD_NVS_SET_START = 0x30
CMD_NVS_SET_DATA = 0x31
CMD_NVS_SET_FINISH = 0x32
CMD_NVS_GET_START = 0x33
CMD_NVS_GET_DATA = 0x34
CMD_NVS_DELETE = 0x35

# NVS type constants (matching ESP-IDF nvs.h)
NVS_TYPE_U8 = 0x01
NVS_TYPE_I8 = 0x11
NVS_TYPE_U16 = 0x02
NVS_TYPE_I16 = 0x12
NVS_TYPE_U32 = 0x04
NVS_TYPE_I32 = 0x14
NVS_TYPE_U64 = 0x08
NVS_TYPE_I64 = 0x18
NVS_TYPE_STR = 0x21
NVS_TYPE_BLOB = 0x42

# Type name to byte mapping
TYPE_TO_BYTE = {
    "u8": NVS_TYPE_U8,
    "i8": NVS_TYPE_I8,
    "u16": NVS_TYPE_U16,
    "i16": NVS_TYPE_I16,
    "u32": NVS_TYPE_U32,
    "i32": NVS_TYPE_I32,
    "u64": NVS_TYPE_U64,
    "i64": NVS_TYPE_I64,
    "string": NVS_TYPE_STR,
    "blob": NVS_TYPE_BLOB,
}

# Byte to type name mapping
BYTE_TO_TYPE = {v: k for k, v in TYPE_TO_BYTE.items()}

# USB HID constants
RAW_HID_REPORT_SIZE = 64
DATA_PAYLOAD_SIZE = 60  # 60 bytes of data payload (bytes 4-63)
USB_VID = 0x303A
USB_PID = 0x4008

# Program size limits (matching firmware)
PROGRAM_FLASH_MAX_SIZE = 1024 * 1024  # 1MB
PROGRAM_RAM_MAX_SIZE = 8 * 1024  # 8KB


class ODKeyUploadError(Exception):
    """Exception raised for ODKey upload errors"""

    pass


class ODKeyConfigUsb:
    """ODKey USB system configuration interface using Raw HID"""

    def __init__(self, device_path: Optional[str] = None, vid: int = USB_VID, pid: int = USB_PID):
        """
        Initialize the ODKey configuration interface

        Args:
            device_path: Optional path to specific HID device
            vid: USB Vendor ID (default: USB_VID)
            pid: USB Product ID (default: USB_PID)
        """
        self.device: Optional[Any] = None
        self.device_path = device_path
        self.interface_num = 1  # Raw HID interface (Interface 1 in firmware)
        self.usb_vid = vid
        self.usb_pid = pid

    def find_device(self) -> bool:
        """
        Find and connect to the ODKey device

        Returns:
            True if device found and connected, False otherwise
        """
        try:
            # List all HID devices
            devices = hid.enumerate()

            # Look for ODKey device
            target_device = None
            for device in devices:
                # Check if this looks like our device
                # Note: You'll need to update these values to match your actual device
                if (
                    device["vendor_id"] == self.usb_vid
                    and device["product_id"] == self.usb_pid
                    and device["interface_number"] == self.interface_num
                ):
                    target_device = device
                    break

            if not target_device:
                print("ODKey device not found. Make sure:")
                print("1. Device is connected via USB")
                print("2. Device is in programming mode")
                print("3. Device drivers are installed")
                return False

            # Open the device
            self.device = hid.device()
            if self.device:
                self.device.open_path(target_device["path"])

            print(
                f"Connected to ODKey device: {target_device['manufacturer_string']} {target_device['product_string']}"
            )
            return True

        except Exception as e:
            print(f"Error finding device: {e}")
            return False

    def send_command(self, command: int, data: bytes) -> Tuple[bool, bytes]:
        """
        Send a command to the device and wait for response

        Args:
            command: Command code for the command
            data: Command data (will be placed in bytes 4-63)

        Returns:
            Tuple of (success, response_data)
        """
        if not self.device:
            raise ODKeyUploadError("Device not connected")

        # Construct the command payload (64 bytes)
        payload = bytearray(RAW_HID_REPORT_SIZE)
        payload[0] = command  # Command code in first byte
        # Bytes 1-3 reserved for future use
        payload[4 : 4 + len(data)] = data  # Data payload in bytes 4-63

        # Wrap with HID report ID (hidapi requirement)
        hid_packet = bytearray(1 + RAW_HID_REPORT_SIZE)
        hid_packet[0] = 0  # Report ID (stripped by hidapi before sending to device)
        hid_packet[1:] = payload  # Actual command payload

        try:
            # Send command (hidapi expects bytes, not bytearray)
            self.device.write(bytes(hid_packet))

            # Wait for response (with timeout)
            timeout = 5.0  # 5 second timeout
            start_time = time.time()

            while time.time() - start_time < timeout:
                # Try to read response (no report ID needed for our protocol)
                response = self.device.read(RAW_HID_REPORT_SIZE)
                if response and len(response) > 0:
                    response_id = response[0]
                    if response_id == RESP_OK:
                        return True, bytes(response)
                    elif response_id == RESP_ERROR:
                        return False, bytes(response)
                    else:
                        print(f"Unexpected response ID: 0x{response_id:02X}")
                        return False, b""
                time.sleep(0.01)  # Small delay to avoid busy waiting

            print("Timeout waiting for response")
            return False, b""

        except Exception as e:
            print(f"Error sending command: {e}")
            return False, b""

    def upload_program(self, program_data: bytes, target: str = "flash") -> bool:
        """
        Upload a program to the device

        Args:
            program_data: Compiled program bytecode
            target: Program target ("flash" or "ram")

        Returns:
            True if upload successful, False otherwise
        """
        if not self.device:
            raise ODKeyUploadError("Device not connected")

        program_size = len(program_data)

        # Validate target
        if target not in ["flash", "ram"]:
            raise ODKeyUploadError(f"Invalid target: {target}")

        # Validate size based on target
        max_size = PROGRAM_FLASH_MAX_SIZE if target == "flash" else PROGRAM_RAM_MAX_SIZE
        if program_size > max_size:
            raise ODKeyUploadError(
                f"Program too large for {target}: {program_size} bytes (max: {max_size})"
            )

        print(f"Uploading program to {target.upper()} ({program_size} bytes)...")

        # Select command codes based on target
        if target == "flash":
            cmd_start = CMD_FLASH_PROGRAM_WRITE_START
            cmd_chunk = CMD_FLASH_PROGRAM_WRITE_CHUNK
            cmd_finish = CMD_FLASH_PROGRAM_WRITE_FINISH
        else:  # ram
            cmd_start = CMD_RAM_PROGRAM_WRITE_START
            cmd_chunk = CMD_RAM_PROGRAM_WRITE_CHUNK
            cmd_finish = CMD_RAM_PROGRAM_WRITE_FINISH

        # Step 1: Send WRITE_START command
        print("Starting write session...")
        size_data = struct.pack("<I", program_size)  # 32-bit little-endian
        success, response = self.send_command(cmd_start, size_data)
        if not success:
            print("Failed to start write session")
            return False

        # Step 2: Send data in 60-byte chunks
        bytes_sent = 0
        chunk_count = 0

        while bytes_sent < program_size:
            # Calculate chunk size (60 bytes of data payload)
            chunk_size = min(DATA_PAYLOAD_SIZE, program_size - bytes_sent)
            chunk_data = program_data[bytes_sent : bytes_sent + chunk_size]

            # Pad chunk to 60 bytes if needed
            if len(chunk_data) < DATA_PAYLOAD_SIZE:
                chunk_data = chunk_data + b"\x00" * (
                    DATA_PAYLOAD_SIZE - len(chunk_data)
                )

            print(f"Sending chunk {chunk_count + 1} ({chunk_size} bytes)...")
            success, response = self.send_command(cmd_chunk, chunk_data)
            if not success:
                print(f"Failed to send chunk {chunk_count + 1}")
                return False

            bytes_sent += chunk_size
            chunk_count += 1

            # Progress indicator
            progress = (bytes_sent / program_size) * 100
            print(f"Progress: {progress:.1f}% ({bytes_sent}/{program_size} bytes)")

        # Step 3: Send WRITE_FINISH command
        print("Finishing write session...")
        success, response = self.send_command(cmd_finish, size_data)
        if not success:
            print("Failed to finish write session")
            return False

        print("Program uploaded successfully!")
        return True

    def download_program(self, target: str = "flash") -> bytes:
        """
        Download a program from the device

        Args:
            target: Program target ("flash" or "ram")

        Returns:
            Program bytecode as bytes

        Raises:
            ODKeyUploadError: If download fails
        """
        if not self.device:
            raise ODKeyUploadError("Device not connected")

        # Validate target
        if target not in ["flash", "ram"]:
            raise ODKeyUploadError(f"Invalid target: {target}")

        print(f"Downloading {target.upper()} program...")

        # Select command codes based on target
        if target == "flash":
            cmd_start = CMD_FLASH_PROGRAM_READ_START
            cmd_chunk = CMD_FLASH_PROGRAM_READ_CHUNK
        else:  # ram
            cmd_start = CMD_RAM_PROGRAM_READ_START
            cmd_chunk = CMD_RAM_PROGRAM_READ_CHUNK

        # Step 1: Send READ_START command
        print("Starting read session...")
        success, response = self.send_command(cmd_start, b"")
        if not success:
            raise ODKeyUploadError("Failed to start read session")

        # Parse program size from response (bytes 4-7 as sent by firmware)
        if len(response) < 8:
            raise ODKeyUploadError("Invalid response from device")

        program_size = struct.unpack("<I", response[4:8])[0]  # 32-bit little-endian
        print(f"Program size: {program_size} bytes")

        if program_size == 0:
            raise ODKeyUploadError("No program stored on device")

        # Step 2: Read data in 60-byte chunks
        program_data = bytearray()
        bytes_received = 0
        chunk_count = 0

        while bytes_received < program_size:
            print(f"Reading chunk {chunk_count + 1}...")
            success, response = self.send_command(cmd_chunk, b"")
            if not success:
                raise ODKeyUploadError(f"Failed to read chunk {chunk_count + 1}")

            # Extract chunk data (bytes 4-63 as sent by firmware)
            chunk_data = response[4:64]

            # Calculate how many bytes we actually need from this chunk
            bytes_needed = min(60, program_size - bytes_received)
            program_data.extend(chunk_data[:bytes_needed])

            bytes_received += bytes_needed
            chunk_count += 1

            # Progress indicator
            progress = (bytes_received / program_size) * 100
            print(f"Progress: {progress:.1f}% ({bytes_received}/{program_size} bytes)")

        print("Program downloaded successfully!")
        # Truncate to exact program size (chunks are padded to 60 bytes)
        return bytes(program_data[:program_size])

    def execute_program(self, target: str = "flash") -> bool:
        """
        Execute a program on the device

        Args:
            target: Program target ("flash" or "ram")

        Returns:
            True if execution successful, False otherwise
        """
        if not self.device:
            raise ODKeyUploadError("Device not connected")

        # Validate target
        if target not in ["flash", "ram"]:
            raise ODKeyUploadError(f"Invalid target: {target}")

        # Select command code based on target
        cmd = (
            CMD_FLASH_PROGRAM_EXECUTE if target == "flash" else CMD_RAM_PROGRAM_EXECUTE
        )

        print(f"Executing {target.upper()} program...")
        success, response = self.send_command(cmd, b"")

        if success:
            print(f"{target.upper()} program execution started")
        else:
            print(f"Failed to execute {target} program")

        return success

    def nvs_set_int(self, key: str, value: int, type_str: str) -> None:
        """
        Set an integer value in NVS

        Args:
            key: NVS key (max 15 characters)
            value: Integer value
            type_str: Type string (u8, i8, u16, i16, u32, i32, u64, i64)

        Raises:
            ODKeyUploadError: If operation fails
        """
        if not self.device:
            raise ODKeyUploadError("Device not connected")

        if len(key) > 15:
            raise ODKeyUploadError("Key too long (max 15 characters)")

        if type_str not in TYPE_TO_BYTE:
            raise ODKeyUploadError(f"Invalid type: {type_str}")

        type_byte = TYPE_TO_BYTE[type_str]

        # Encode value based on type
        if type_str in ["u8", "i8"]:
            value_bytes = struct.pack("<B", value & 0xFF)
        elif type_str in ["u16", "i16"]:
            value_bytes = struct.pack("<H", value & 0xFFFF)
        elif type_str in ["u32", "i32"]:
            value_bytes = struct.pack("<I", value & 0xFFFFFFFF)
        elif type_str in ["u64", "i64"]:
            value_bytes = struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF)
        else:
            raise ODKeyUploadError(f"Invalid integer type: {type_str}")

        # Send SET_START command
        start_data = bytearray(25)  # type(1) + length(4) + key(16) + padding(4)
        start_data[0] = type_byte
        start_data[1:5] = struct.pack("<I", len(value_bytes))
        start_data[5 : 5 + len(key)] = key.encode("utf-8")

        success, response = self.send_command(CMD_NVS_SET_START, bytes(start_data))
        if not success:
            raise ODKeyUploadError("Failed to start NVS set operation")

        # Send value data (integers are small, so single chunk)
        if len(value_bytes) > 0:
            success, response = self.send_command(CMD_NVS_SET_DATA, value_bytes)
            if not success:
                raise ODKeyUploadError("Failed to send NVS value data")

        # Send SET_FINISH command
        success, response = self.send_command(CMD_NVS_SET_FINISH, b"")
        if not success:
            raise ODKeyUploadError("Failed to finish NVS set operation")

        print(f"NVS set completed: {key} = {value} ({type_str})")

    def nvs_set_str(self, key: str, value: str) -> None:
        """
        Set a string value in NVS

        Args:
            key: NVS key (max 15 characters)
            value: String value

        Raises:
            ODKeyUploadError: If operation fails
        """
        if not self.device:
            raise ODKeyUploadError("Device not connected")

        if len(key) > 15:
            raise ODKeyUploadError("Key too long (max 15 characters)")

        # Encode as UTF-8 + null terminator
        value_bytes = value.encode("utf-8") + b"\x00"

        if len(value_bytes) > 1024:
            raise ODKeyUploadError("Value too large (max 1024 bytes)")

        # Send SET_START command
        start_data = bytearray(25)  # type(1) + length(4) + key(16) + padding(4)
        start_data[0] = NVS_TYPE_STR
        start_data[1:5] = struct.pack("<I", len(value_bytes))
        start_data[5 : 5 + len(key)] = key.encode("utf-8")

        success, response = self.send_command(CMD_NVS_SET_START, bytes(start_data))
        if not success:
            raise ODKeyUploadError("Failed to start NVS set operation")

        # Send value data in chunks
        bytes_sent = 0
        while bytes_sent < len(value_bytes):
            chunk_size = min(60, len(value_bytes) - bytes_sent)
            chunk_data = value_bytes[bytes_sent : bytes_sent + chunk_size]

            success, response = self.send_command(CMD_NVS_SET_DATA, chunk_data)
            if not success:
                raise ODKeyUploadError("Failed to send NVS value data")

            bytes_sent += chunk_size

        # Send SET_FINISH command
        success, response = self.send_command(CMD_NVS_SET_FINISH, b"")
        if not success:
            raise ODKeyUploadError("Failed to finish NVS set operation")

        print(f"NVS set completed: {key} = '{value}' (string)")

    def nvs_set_blob(self, key: str, value: bytes) -> None:
        """
        Set a blob value in NVS

        Args:
            key: NVS key (max 15 characters)
            value: Binary data

        Raises:
            ODKeyUploadError: If operation fails
        """
        if not self.device:
            raise ODKeyUploadError("Device not connected")

        if len(key) > 15:
            raise ODKeyUploadError("Key too long (max 15 characters)")

        if len(value) > 1024:
            raise ODKeyUploadError("Value too large (max 1024 bytes)")

        # Send SET_START command
        start_data = bytearray(25)  # type(1) + length(4) + key(16) + padding(4)
        start_data[0] = NVS_TYPE_BLOB
        start_data[1:5] = struct.pack("<I", len(value))
        start_data[5 : 5 + len(key)] = key.encode("utf-8")

        success, response = self.send_command(CMD_NVS_SET_START, bytes(start_data))
        if not success:
            raise ODKeyUploadError("Failed to start NVS set operation")

        # Send value data in chunks
        bytes_sent = 0
        while bytes_sent < len(value):
            chunk_size = min(60, len(value) - bytes_sent)
            chunk_data = value[bytes_sent : bytes_sent + chunk_size]

            success, response = self.send_command(CMD_NVS_SET_DATA, chunk_data)
            if not success:
                raise ODKeyUploadError("Failed to send NVS value data")

            bytes_sent += chunk_size

        # Send SET_FINISH command
        success, response = self.send_command(CMD_NVS_SET_FINISH, b"")
        if not success:
            raise ODKeyUploadError("Failed to finish NVS set operation")

        print(f"NVS set completed: {key} = {len(value)} bytes (blob)")

    def nvs_set(self, key: str, value_type: str, value: Any) -> bool:
        """
        Set an NVS value (unified interface)

        Args:
            key: NVS key name
            value_type: Type of value (u8, i8, u16, i16, u32, i32, u64, i64, string, blob)
            value: Value to set

        Returns:
            True if set successful, False otherwise
        """
        try:
            if value_type == "string":
                self.nvs_set_str(key, value)
                return True
            elif value_type == "blob":
                self.nvs_set_blob(key, value)
                return True
            elif value_type in ["u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64"]:
                self.nvs_set_int(key, value, value_type)
                return True
            else:
                print(f"Error: Invalid type '{value_type}'")
                return False
        except ODKeyUploadError as e:
            print(f"NVS set failed: {e}")
            return False

    def nvs_get(self, key: str) -> Tuple[bool, Optional[str], Any]:
        """
        Get an NVS value (unified interface)

        Args:
            key: NVS key name

        Returns:
            Tuple of (success, type, value)
        """
        if not self.device:
            print("Device not connected")
            return False, None, None

        if len(key) > 15:
            print("Key too long (max 15 characters)")
            return False, None, None

        try:
            # Send GET_START command
            start_data = bytearray(20)  # key(16) + padding(4)
            start_data[0 : len(key)] = key.encode("utf-8")

            success, response = self.send_command(CMD_NVS_GET_START, bytes(start_data))
            if not success:
                print("Failed to start NVS get operation")
                return False, None, None

            # Parse type and size from first response
            if len(response) < 9:
                print("Invalid response from device")
                return False, None, None

            value_type = response[4]
            value_size = struct.unpack("<I", response[5:9])[0]

            if value_type not in BYTE_TO_TYPE:
                print(f"Unknown value type: 0x{value_type:02X}")
                return False, None, None

            type_name = BYTE_TO_TYPE[value_type]

            # Read value data
            value_data = bytearray()
            bytes_received = 0

            # First chunk (55 bytes after type/size)
            if value_size > 0:
                first_chunk_size = min(55, value_size)
                if len(response) >= 9 + first_chunk_size:
                    value_data.extend(response[9 : 9 + first_chunk_size])
                    bytes_received = first_chunk_size

            # Subsequent chunks
            while bytes_received < value_size:
                success, response = self.send_command(CMD_NVS_GET_DATA, b"")
                if not success:
                    print("Failed to get NVS value data")
                    return False, None, None

                chunk_size = min(60, value_size - bytes_received)
                if len(response) >= 4 + chunk_size:
                    value_data.extend(response[4 : 4 + chunk_size])
                    bytes_received += chunk_size

            # Decode value based on type
            if type_name in ["u8", "i8"]:
                decoded_value = struct.unpack("<B", value_data)[0]
                if type_name == "i8":
                    decoded_value = struct.unpack("<b", value_data)[0]
            elif type_name in ["u16", "i16"]:
                decoded_value = struct.unpack("<H", value_data)[0]
                if type_name == "i16":
                    decoded_value = struct.unpack("<h", value_data)[0]
            elif type_name in ["u32", "i32"]:
                decoded_value = struct.unpack("<I", value_data)[0]
                if type_name == "i32":
                    decoded_value = struct.unpack("<i", value_data)[0]
            elif type_name in ["u64", "i64"]:
                decoded_value = struct.unpack("<Q", value_data)[0]
                if type_name == "i64":
                    decoded_value = struct.unpack("<q", value_data)[0]
            elif type_name == "string":
                decoded_value = value_data.decode("utf-8").rstrip("\x00")
            elif type_name == "blob":
                decoded_value = bytes(value_data)
            else:
                print(f"Unknown type: {type_name}")
                return False, None, None

            print(f"NVS get completed: {key} = {decoded_value} ({type_name})")
            return True, type_name, decoded_value

        except Exception as e:
            print(f"NVS get failed: {e}")
            return False, None, None

    def nvs_delete(self, key: str) -> bool:
        """
        Delete an NVS key (unified interface)

        Args:
            key: NVS key name

        Returns:
            True if deletion successful, False otherwise
        """
        if not self.device:
            print("Device not connected")
            return False

        if len(key) > 15:
            print("Key too long (max 15 characters)")
            return False

        try:
            # Send DELETE command
            delete_data = bytearray(20)  # key(16) + padding(4)
            delete_data[0 : len(key)] = key.encode("utf-8")

            success, response = self.send_command(CMD_NVS_DELETE, bytes(delete_data))
            if not success:
                print("Failed to delete NVS key")
                return False

            print(f"NVS delete completed: {key}")
            return True

        except Exception as e:
            print(f"NVS delete failed: {e}")
            return False

    def close(self) -> None:
        """Close the device connection"""
        if self.device:
            self.device.close()
            self.device = None


def compile_odkeyscript(source_file: Path) -> bytes:
    """
    Compile ODKeyScript source file to bytecode

    Args:
        source_file: Path to .odk source file

    Returns:
        Compiled bytecode
    """
    try:
        with open(source_file, "r", encoding="utf-8") as f:
            source = f.read()

        compiler = Compiler()
        bytecode = compiler.compile(source)

        print(f"Compiled {source_file.name} ({len(bytecode)} bytes)")
        return bytes(bytecode)

    except CompileError as e:
        print(f"Compilation error at line {e.line}, column {e.column}: {e.message}")
        raise
    except Exception as e:
        print(f"Error reading source file: {e}")
        raise


def main() -> int:
    """Main function"""
    parser = argparse.ArgumentParser(
        description="Upload ODKeyScript programs to ODKey device over USB",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s program.odk                    # Compile and upload .odk file
  %(prog)s program.bin                    # Upload pre-compiled .bin file
  %(prog)s --list-devices                 # List available HID devices
  %(prog)s --vid 0x1234 --pid 0x5678 program.odk  # Specify device VID/PID
        """,
    )

    parser.add_argument(
        "input_file",
        nargs="?",
        type=Path,
        help="Input file (.odk source or .bin bytecode)",
    )

    parser.add_argument(
        "--list-devices",
        action="store_true",
        help="List available HID devices and exit",
    )

    parser.add_argument(
        "--vid",
        type=lambda x: int(x, 0),  # Allow hex input
        default=USB_VID,
        help=f"USB Vendor ID (default: 0x{USB_VID:04X})",
    )

    parser.add_argument(
        "--pid",
        type=lambda x: int(x, 0),  # Allow hex input
        default=USB_PID,
        help=f"USB Product ID (default: 0x{USB_PID:04X})",
    )

    parser.add_argument("--device-path", help="Specific HID device path to use")

    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Enable verbose output"
    )

    args = parser.parse_args()

    # Update constants if VID/PID specified
    usb_vid = args.vid
    usb_pid = args.pid

    # List devices if requested
    if args.list_devices:
        print("Available HID devices:")
        try:
            devices = hid.enumerate()
            for i, device in enumerate(devices):
                print(
                    f"{i}: {device['manufacturer_string']} {device['product_string']}"
                )
                print(
                    f"   VID: 0x{device['vendor_id']:04X}, PID: 0x{device['product_id']:04X}"
                )
                print(f"   Interface: {device['interface_number']}")
                print(f"   Path: {device['path'].decode('utf-8', errors='replace')}")
                print()
        except Exception as e:
            print(f"Error listing devices: {e}")
        return 0

    # Check if input file specified
    if not args.input_file:
        parser.error(
            "Input file is required (use --list-devices to see available devices)"
        )

    if not args.input_file.exists():
        print(f"Error: Input file '{args.input_file}' does not exist")
        return 1

    # Determine if we need to compile
    if args.input_file.suffix.lower() == ".odk":
        print(f"Compiling ODKeyScript source: {args.input_file}")
        try:
            program_data = compile_odkeyscript(args.input_file)
        except Exception as e:
            print(f"Compilation failed: {e}")
            return 1
    elif args.input_file.suffix.lower() == ".bin":
        print(f"Loading pre-compiled bytecode: {args.input_file}")
        try:
            with open(args.input_file, "rb") as f:
                program_data = f.read()
            print(f"Loaded {len(program_data)} bytes")
        except Exception as e:
            print(f"Error loading bytecode: {e}")
            return 1
    else:
        print(f"Error: Unsupported file type '{args.input_file.suffix}'")
        print("Supported types: .odk (ODKeyScript source), .bin (compiled bytecode)")
        return 1

    # Check program size
    max_size = 1024 * 1024  # 1MB (PROGRAM_STORAGE_MAX_SIZE from firmware)
    if len(program_data) > max_size:
        print(f"Error: Program too large ({len(program_data)} bytes)")
        print(f"Maximum size: {max_size} bytes")
        return 1

    # Connect to device and upload
    config = ODKeyConfigUsb(args.device_path)
    # Update the device search parameters
    config.usb_vid = usb_vid
    config.usb_pid = usb_pid

    try:
        if not config.find_device():
            return 1

        if not config.upload_program(program_data):
            return 1

        print("Upload completed successfully!")
        return 0

    except Exception as e:
        print(f"Upload failed: {e}")
        return 1
    finally:
        config.close()


if __name__ == "__main__":
    sys.exit(main())
