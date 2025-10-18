#!/usr/bin/env python3
"""
ODKey CLI - Unified command-line interface for ODKey development tools

This CLI provides access to all ODKey development functionality including
compilation, disassembly, and USB upload.
"""

import argparse
import sys
from pathlib import Path
from typing import Any

from .config.odkey_config import ODKeyConfig, ODKeyUploadError
from .odkeyscript.odkeyscript_compiler import CompileError, Compiler
from .odkeyscript.odkeyscript_disassembler import disassemble


def compile_command(args: Any) -> int:
    """Handle the compile command"""
    try:
        with open(args.input, "r", encoding="utf-8") as f:
            source = f.read()

        compiler = Compiler()
        bytecode = compiler.compile(source)

        with open(args.output, "wb") as f:
            f.write(bytecode)

        print(f"Compiled {args.input} to {args.output} ({len(bytecode)} bytes)")
        return 0

    except CompileError as e:
        print(f"Compilation error at line {e.line}, column {e.column}: {e.message}")
        return 1
    except Exception as e:
        print(f"Error: {e}")
        return 1


def disassemble_command(args: Any) -> int:
    """Handle the disassemble command"""
    try:
        with open(args.input, "rb") as f:
            bytecode = f.read()

        disassembly_lines = disassemble(bytecode)
        for line in disassembly_lines:
            print(line)
        return 0

    except Exception as e:
        print(f"Error: {e}")
        return 1


def upload_command(args: Any) -> int:
    """Handle the upload command"""
    # Determine if we need to compile
    if args.input.suffix.lower() == ".odk":
        print(f"Compiling ODKeyScript source: {args.input}")
        try:
            with open(args.input, "r", encoding="utf-8") as f:
                source = f.read()

            compiler = Compiler()
            program_data = compiler.compile(source)
            print(f"Compiled to {len(program_data)} bytes")

        except CompileError as e:
            print(f"Compilation error at line {e.line}, column {e.column}: {e.message}")
            return 1
        except Exception as e:
            print(f"Compilation failed: {e}")
            return 1
    elif args.input.suffix.lower() == ".bin":
        print(f"Loading pre-compiled bytecode: {args.input}")
        try:
            with open(args.input, "rb") as f:
                program_data = f.read()
            print(f"Loaded {len(program_data)} bytes")
        except Exception as e:
            print(f"Error loading bytecode: {e}")
            return 1
    else:
        print(f"Error: Unsupported file type '{args.input.suffix}'")
        print("Supported types: .odk (ODKeyScript source), .bin (compiled bytecode)")
        return 1

    # Check program size
    max_size = 1024 * 1024  # 1MB
    if len(program_data) > max_size:
        print(f"Error: Program too large ({len(program_data)} bytes)")
        print(f"Maximum size: {max_size} bytes")
        return 1

    # Upload to device
    config = ODKeyConfig(args.device_path)
    config.usb_vid = args.vid
    config.usb_pid = args.pid

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


def download_command(args: Any) -> int:
    """Handle the download command"""
    # Connect to device
    config = ODKeyConfig(args.device_path)
    config.usb_vid = args.vid
    config.usb_pid = args.pid

    try:
        if not config.find_device():
            return 1

        # Download program
        try:
            program_data = config.download_program()
        except ODKeyUploadError as e:
            print(f"Download failed: {e}")
            return 1

        # Save to file if specified
        if args.output:
            try:
                with open(args.output, "wb") as f:
                    f.write(program_data)
                print(f"Program saved to {args.output}")
            except Exception as e:
                print(f"Error saving file: {e}")
                return 1

        # Disassemble if requested
        if args.disassemble:
            print("\nDisassembly:")
            print("=" * 50)
            try:
                disassembly_lines = disassemble(program_data)
                for line in disassembly_lines:
                    print(line)
            except Exception as e:
                print(f"Error disassembling: {e}")
                return 1

        return 0

    except Exception as e:
        print(f"Download failed: {e}")
        return 1
    finally:
        config.close()


def list_devices_command(args: Any) -> int:
    """Handle the list-devices command"""
    try:
        import hid

        devices = hid.enumerate()

        print("Available HID devices:")
        for i, device in enumerate(devices):
            print(f"{i}: {device['manufacturer_string']} {device['product_string']}")
            print(
                f"   VID: 0x{device['vendor_id']:04X}, PID: 0x{device['product_id']:04X}"
            )
            print(f"   Interface: {device['interface_number']}")
            print(f"   Path: {device['path'].decode('utf-8', errors='replace')}")
            print()
        return 0

    except Exception as e:
        print(f"Error listing devices: {e}")
        return 1


def main() -> int:
    """Main CLI entry point"""
    parser = argparse.ArgumentParser(
        description="ODKey development tools - compile, disassemble, and upload ODKeyScript programs",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s compile program.odk program.bin     # Compile ODKeyScript to bytecode
  %(prog)s disassemble program.bin             # Disassemble bytecode to text
  %(prog)s upload program.odk                  # Compile and upload to device
  %(prog)s upload program.bin                  # Upload pre-compiled bytecode
  %(prog)s download --output program.bin       # Download and save program
  %(prog)s download --disassemble              # Download and display disassembly
  %(prog)s list-devices                        # List available HID devices
        """,
    )

    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # Compile command
    compile_parser = subparsers.add_parser(
        "compile", help="Compile ODKeyScript source to bytecode"
    )
    compile_parser.add_argument("input", type=Path, help="Input .odk source file")
    compile_parser.add_argument("output", type=Path, help="Output .bin bytecode file")

    # Disassemble command
    disassemble_parser = subparsers.add_parser(
        "disassemble", help="Disassemble bytecode to text"
    )
    disassemble_parser.add_argument("input", type=Path, help="Input .bin bytecode file")

    # Upload command
    upload_parser = subparsers.add_parser(
        "upload", help="Upload program to ODKey device"
    )
    upload_parser.add_argument(
        "input", type=Path, help="Input file (.odk source or .bin bytecode)"
    )
    upload_parser.add_argument(
        "--vid",
        type=lambda x: int(x, 0),
        default=0x303A,
        help="USB Vendor ID (default: 0x1234)",
    )
    upload_parser.add_argument(
        "--pid",
        type=lambda x: int(x, 0),
        default=0x4008,
        help="USB Product ID (default: 0x5678)",
    )
    upload_parser.add_argument("--device-path", help="Specific HID device path to use")
    upload_parser.add_argument(
        "--verbose", "-v", action="store_true", help="Enable verbose output"
    )

    # Download command
    download_parser = subparsers.add_parser(
        "download", help="Download program from ODKey device"
    )
    download_parser.add_argument(
        "--output", "-o", type=Path, help="Output file to save program (.bin)"
    )
    download_parser.add_argument(
        "--disassemble",
        "-d",
        action="store_true",
        help="Display disassembly of downloaded program",
    )
    download_parser.add_argument(
        "--vid",
        type=lambda x: int(x, 0),
        default=0x303A,
        help="USB Vendor ID (default: 0x303A)",
    )
    download_parser.add_argument(
        "--pid",
        type=lambda x: int(x, 0),
        default=0x4008,
        help="USB Product ID (default: 0x4008)",
    )
    download_parser.add_argument(
        "--device-path", help="Specific HID device path to use"
    )

    # List devices command
    subparsers.add_parser("list-devices", help="List available HID devices")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    # Route to appropriate command handler
    if args.command == "compile":
        return compile_command(args)
    elif args.command == "disassemble":
        return disassemble_command(args)
    elif args.command == "upload":
        return upload_command(args)
    elif args.command == "download":
        return download_command(args)
    elif args.command == "list-devices":
        return list_devices_command(args)
    else:
        print(f"Unknown command: {args.command}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
