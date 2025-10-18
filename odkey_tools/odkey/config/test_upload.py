#!/usr/bin/env python3
"""
Test script for ODKey USB upload functionality

This script creates a simple test program and uploads it to verify the upload process.
"""

import sys
import tempfile
from pathlib import Path

from .odkey_upload import ODKeyUploader, compile_odkeyscript


def create_test_program() -> str:
    """Create a simple test ODKeyScript program"""
    return """
# Simple test program for ODKey
# This program types "Hello from ODKey!" and presses Enter

press_time 50
type "Hello from ODKey!"
press ENTER
pause 1000
type "Upload test successful!"
press ENTER
"""


def main() -> int:
    """Main test function"""
    print("ODKey Upload Test")
    print("================")

    # Create test program
    test_source = create_test_program()
    print("Created test program:")
    print(test_source)

    # Compile the test program
    try:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".odk", delete=False) as f:
            f.write(test_source)
            temp_file = Path(f.name)

        print("\nCompiling test program...")
        bytecode = compile_odkeyscript(temp_file)
        print(f"Compiled to {len(bytecode)} bytes")

        # Clean up temp file
        temp_file.unlink()

    except Exception as e:
        print(f"Compilation failed: {e}")
        return 1

    # Test device connection
    print("\nTesting device connection...")
    uploader = ODKeyUploader()

    try:
        if not uploader.find_device():
            print(
                "Device not found. Make sure your ODKey is connected and in programming mode."
            )
            return 1

        print("Device found! Testing upload...")

        # Test upload
        if uploader.upload_program(bytecode):
            print("Upload test successful!")
            print("Check your ODKey device - it should have typed the test message.")
            return 0
        else:
            print("Upload test failed!")
            return 1

    except Exception as e:
        print(f"Upload test failed with error: {e}")
        return 1
    finally:
        uploader.close()


if __name__ == "__main__":
    sys.exit(main())
