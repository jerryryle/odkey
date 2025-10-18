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

CMD_PROGRAM_WRITE_START = 0x20
CMD_PROGRAM_WRITE_CHUNK = 0x21
CMD_PROGRAM_WRITE_FINISH = 0x22
CMD_PROGRAM_READ_START = 0x23
CMD_PROGRAM_READ_CHUNK = 0x24

# USB HID constants
RAW_HID_REPORT_SIZE = 64
DATA_PAYLOAD_SIZE = 60  # 60 bytes of data payload (bytes 4-63)
USB_VID = 0x303A
USB_PID = 0x4008


class ODKeyUploadError(Exception):
    """Exception raised for ODKey upload errors"""

    pass


class ODKeyConfig:
    """ODKey USB system configuration interface using Raw HID"""

    def __init__(self, device_path: Optional[str] = None):
        """
        Initialize the ODKey configuration interface

        Args:
            device_path: Optional path to specific HID device
        """
        self.device: Optional[Any] = None
        self.device_path = device_path
        self.interface_num = 1  # Raw HID interface (Interface 1 in firmware)
        self.usb_vid = USB_VID
        self.usb_pid = USB_PID

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
                        return True, bytes(response[1:])
                    elif response_id == RESP_ERROR:
                        return False, bytes(response[1:])
                    else:
                        print(f"Unexpected response ID: 0x{response_id:02X}")
                        return False, b""
                time.sleep(0.01)  # Small delay to avoid busy waiting

            print("Timeout waiting for response")
            return False, b""

        except Exception as e:
            print(f"Error sending command: {e}")
            return False, b""

    def upload_program(self, program_data: bytes) -> bool:
        """
        Upload a program to the device

        Args:
            program_data: Compiled program bytecode

        Returns:
            True if upload successful, False otherwise
        """
        if not self.device:
            raise ODKeyUploadError("Device not connected")

        program_size = len(program_data)
        print(f"Uploading program ({program_size} bytes)...")

        # Step 1: Send WRITE_START command
        print("Starting write session...")
        size_data = struct.pack("<I", program_size)  # 32-bit little-endian
        success, response = self.send_command(CMD_PROGRAM_WRITE_START, size_data)
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
            success, response = self.send_command(CMD_PROGRAM_WRITE_CHUNK, chunk_data)
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
        success, response = self.send_command(CMD_PROGRAM_WRITE_FINISH, size_data)
        if not success:
            print("Failed to finish write session")
            return False

        print("Program uploaded successfully!")
        return True

    def download_program(self) -> bytes:
        """
        Download a program from the device

        Returns:
            Program bytecode as bytes

        Raises:
            ODKeyUploadError: If download fails
        """
        if not self.device:
            raise ODKeyUploadError("Device not connected")

        print("Downloading program...")

        # Step 1: Send READ_START command
        print("Starting read session...")
        success, response = self.send_command(CMD_PROGRAM_READ_START, b"")
        if not success:
            raise ODKeyUploadError("Failed to start read session")

        # Parse program size from response (bytes 4-7)
        if len(response) < 7:
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
            success, response = self.send_command(CMD_PROGRAM_READ_CHUNK, b"")
            if not success:
                raise ODKeyUploadError(f"Failed to read chunk {chunk_count + 1}")

            # Extract chunk data (bytes 4-63)
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
    config = ODKeyConfig(args.device_path)
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
