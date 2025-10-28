#!/usr/bin/env python3
"""
ODKey HTTP Configuration Interface

Provides HTTP-based system configuration functionality for ODKey devices over WiFi.
"""

import json
import sys
from typing import Any, Optional, Tuple

try:
    import requests
except ImportError:
    print("Error: requests library not found. Install with: pip install requests")
    sys.exit(1)


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


class ODKeyConfigHttp:
    """ODKey HTTP system configuration interface using REST API"""

    def __init__(self, host: str, port: int = 80, api_key: Optional[str] = None):
        """
        Initialize the ODKey HTTP configuration interface

        Args:
            host: Device hostname or IP address
            port: HTTP port (default: 80)
            api_key: Optional API key for authentication
        """
        self.host = host
        self.port = port
        self.api_key = api_key
        self.base_url = f"http://{host}:{port}"
        self.session = requests.Session()

        # Set up authentication headers if API key provided
        if self.api_key:
            self.session.headers.update({"Authorization": f"Bearer {self.api_key}"})

    def find_device(self) -> bool:
        """
        Test connection to the ODKey device (HTTP interface doesn't need device discovery)

        Returns:
            True if connection successful, False otherwise
        """
        try:
            # Try to ping the device with a simple request
            response = self.session.get(f"{self.base_url}/api/status", timeout=5)
            if response.status_code == 200:
                print(f"Connected to ODKey device at {self.host}:{self.port}")
                return True
            else:
                print(f"Device not responding: HTTP {response.status_code}")
                return False
        except requests.exceptions.RequestException as e:
            print(f"Connection failed: {e}")
            return False

    def upload_program(self, program_data: bytes, target: str = "flash") -> bool:
        """
        Upload a program to the ODKey device

        Args:
            program_data: Program bytecode data
            target: Program target ("flash" or "ram")

        Returns:
            True if upload successful, False otherwise
        """
        try:
            # Validate target
            if target not in ["flash", "ram"]:
                print(f"Error: Invalid target '{target}'")
                return False

            print(
                f"Uploading program to {target.upper()} on {self.host}:{self.port}..."
            )

            response = self.session.post(
                f"{self.base_url}/api/program/{target}",
                data=program_data,
                headers={"Content-Type": "application/octet-stream"},
                timeout=30,
            )

            if response.status_code == 200:
                print("Program uploaded successfully")
                return True
            else:
                print(f"Upload failed: HTTP {response.status_code}")
                if response.text:
                    print(f"Error: {response.text}")
                return False

        except requests.exceptions.RequestException as e:
            print(f"Upload failed: {e}")
            return False

    def download_program(self, target: str = "flash") -> Optional[bytes]:
        """
        Download the current program from the ODKey device

        Args:
            target: Program target ("flash" or "ram")

        Returns:
            Program bytecode data if successful, None otherwise
        """
        try:
            # Validate target
            if target not in ["flash", "ram"]:
                print(f"Error: Invalid target '{target}'")
                return None

            print(
                f"Downloading {target.upper()} program from {self.host}:{self.port}..."
            )

            response = self.session.get(
                f"{self.base_url}/api/program/{target}", timeout=30
            )

            if response.status_code == 200:
                print(
                    f"Program downloaded successfully ({len(response.content)} bytes)"
                )
                return bytes(response.content)
            elif response.status_code == 404:
                print("No program found on device")
                return None
            else:
                print(f"Download failed: HTTP {response.status_code}")
                if response.text:
                    print(f"Error: {response.text}")
                return None

        except requests.exceptions.RequestException as e:
            print(f"Download failed: {e}")
            return None

    def delete_program(self) -> bool:
        """
        Delete the current program from the ODKey device

        Returns:
            True if deletion successful, False otherwise
        """
        try:
            print(f"Deleting program from {self.host}:{self.port}...")

            response = self.session.delete(
                f"{self.base_url}/api/program/flash", timeout=30
            )

            if response.status_code == 200:
                print("Program deleted successfully")
                return True
            else:
                print(f"Delete failed: HTTP {response.status_code}")
                if response.text:
                    print(f"Error: {response.text}")
                return False

        except requests.exceptions.RequestException as e:
            print(f"Delete failed: {e}")
            return False

    def nvs_set(self, key: str, value_type: str, value: Any) -> bool:
        """
        Set an NVS value on the ODKey device

        Args:
            key: NVS key name
            value_type: Type of value (u8, i8, u16, i16, u32, i32, u64, i64, string, blob)
            value: Value to set

        Returns:
            True if set successful, False otherwise
        """
        try:
            if value_type == "blob":
                # Send binary data directly
                response = self.session.post(
                    f"{self.base_url}/api/nvs/{key}",
                    data=value,  # raw bytes
                    headers={"Content-Type": "application/octet-stream"},
                    timeout=30,
                )
            else:
                # Send JSON for other types
                payload = {"type": value_type, "value": value}
                response = self.session.post(
                    f"{self.base_url}/api/nvs/{key}", json=payload, timeout=30
                )

            if response.status_code == 200:
                print(f"NVS key '{key}' set successfully")
                return True
            else:
                print(f"Set failed: HTTP {response.status_code}")
                if response.text:
                    print(f"Error: {response.text}")
                return False

        except requests.exceptions.RequestException as e:
            print(f"Set failed: {e}")
            return False

    def nvs_get(self, key: str) -> Tuple[bool, Optional[str], Any]:
        """
        Get an NVS value from the ODKey device

        Args:
            key: NVS key name

        Returns:
            Tuple of (success, type, value)
        """
        try:
            response = self.session.get(f"{self.base_url}/api/nvs/{key}", timeout=30)

            if response.status_code == 200:
                # Check if it's binary data (blob type)
                content_type = response.headers.get("content-type", "")
                if content_type == "application/octet-stream":
                    # Binary blob data
                    print(
                        f"NVS key '{key}' retrieved successfully (blob, {len(response.content)} bytes)"
                    )
                    return True, "blob", response.content
                else:
                    # JSON response for other types
                    try:
                        data = response.json()
                        value_type = data.get("type", "unknown")
                        value = data.get("value")
                        print(
                            f"NVS key '{key}' retrieved successfully ({value_type}: {value})"
                        )
                        return True, value_type, value
                    except json.JSONDecodeError:
                        print(f"Invalid JSON response for key '{key}'")
                        return False, None, None
            elif response.status_code == 404:
                print(f"NVS key '{key}' not found")
                return False, None, None
            else:
                print(f"Get failed: HTTP {response.status_code}")
                if response.text:
                    print(f"Error: {response.text}")
                return False, None, None

        except requests.exceptions.RequestException as e:
            print(f"Get failed: {e}")
            return False, None, None

    def nvs_delete(self, key: str) -> bool:
        """
        Delete an NVS key from the ODKey device

        Args:
            key: NVS key name

        Returns:
            True if deletion successful, False otherwise
        """
        try:
            response = self.session.delete(f"{self.base_url}/api/nvs/{key}", timeout=30)

            if response.status_code == 200:
                print(f"NVS key '{key}' deleted successfully")
                return True
            else:
                print(f"Delete failed: HTTP {response.status_code}")
                if response.text:
                    print(f"Error: {response.text}")
                return False

        except requests.exceptions.RequestException as e:
            print(f"Delete failed: {e}")
            return False

    def execute_program(self, target: str = "flash") -> bool:
        """
        Execute a program on the device

        Args:
            target: Program target ("flash" or "ram")

        Returns:
            True if execution successful, False otherwise
        """
        try:
            # Validate target
            if target not in ["flash", "ram"]:
                print(f"Error: Invalid target '{target}'")
                return False

            endpoint = f"/api/program/{target}/execute"
            response = self.session.post(f"{self.base_url}{endpoint}", timeout=30)

            if response.status_code == 200:
                print(f"{target.upper()} program execution started")
                return True
            else:
                print(f"Execute failed: HTTP {response.status_code}")
                if response.text:
                    print(f"Error: {response.text}")
                return False
        except requests.exceptions.RequestException as e:
            print(f"Execute failed: {e}")
            return False

    def download_logs(self, file_handle: Any = None) -> None:
        """
        Download logs from the device via HTTP and stream to stdout or file
        
        Args:
            file_handle: Optional file handle to write to. If None, streams to stdout.
        """
        try:
            response = self.session.get(f"{self.base_url}/api/logs", timeout=30, stream=True)
            
            if response.status_code == 200:
                for chunk in response.iter_content(chunk_size=1024, decode_unicode=True):
                    if chunk:
                        if file_handle:
                            file_handle.write(chunk)
                            file_handle.flush()
                        else:
                            print(chunk, end="", flush=True)
            else:
                print(f"Log download failed: HTTP {response.status_code}")
                if response.text:
                    print(f"Error: {response.text}")

        except requests.exceptions.RequestException as e:
            print(f"Log download failed: {e}")
            return

    def clear_logs(self) -> bool:
        """
        Clear the log buffer on the device via HTTP

        Returns:
            True if successful, False otherwise
        """
        try:
            response = self.session.delete(f"{self.base_url}/api/logs", timeout=30)
            
            if response.status_code == 200:
                print("Log buffer cleared successfully")
                return True
            else:
                print(f"Log clear failed: HTTP {response.status_code}")
                if response.text:
                    print(f"Error: {response.text}")
                return False

        except requests.exceptions.RequestException as e:
            print(f"Log clear failed: {e}")
            return False

    def close(self) -> None:
        """Close the HTTP session"""
        self.session.close()
