#!/usr/bin/env python3
"""
ODKey CLI - Unified command-line interface for ODKey development tools

This CLI provides access to all ODKey development functionality including
compilation, disassembly, and USB upload.
"""

import argparse
import sys
from pathlib import Path
from typing import Any, Union

from .config.odkey_config_http import ODKeyConfigHttp
from .config.odkey_config_usb import ODKeyConfigUsb, ODKeyUploadError
from .odkeyscript.odkeyscript_compiler import CompileError, Compiler
from .odkeyscript.odkeyscript_disassembler import disassemble


# Helper functions
def create_config(args: Any) -> Union[ODKeyConfigUsb, ODKeyConfigHttp]:
    """Create and configure the appropriate config object"""
    if args.interface == "http":
        return ODKeyConfigHttp(args.host, args.port, args.api_key)
    else:  # usb
        return ODKeyConfigUsb(args.device_path, args.vid, args.pid)


def add_device_args(parser: argparse.ArgumentParser) -> None:
    """Add common device connection arguments"""
    parser.add_argument(
        "--vid",
        type=lambda x: int(x, 0),
        default=0x303A,
        help="USB Vendor ID (default: 0x303A)",
    )
    parser.add_argument(
        "--pid",
        type=lambda x: int(x, 0),
        default=0x4008,
        help="USB Product ID (default: 0x4008)",
    )
    parser.add_argument("--device-path", help="Specific HID device path to use")
    parser.add_argument(
        "--interface",
        "-i",
        choices=["usb", "http"],
        default="usb",
        help="Communication interface (default: usb)",
    )
    parser.add_argument(
        "--host",
        default="odkey.local",
        help="Device hostname or IP address (default: odkey.local, for http interface)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=80,
        help="HTTP port (default: 80, for http interface)",
    )
    parser.add_argument(
        "--api-key", help="API key for authentication (for http interface)"
    )


def add_target_args(parser: argparse.ArgumentParser, default: str = "ram") -> None:
    """Add target selection arguments"""
    parser.add_argument(
        "--target",
        choices=["ram", "flash"],
        default=default,
        help=f"Program target (default: {default})",
    )


def load_program_data(input_path: Path) -> bytes:
    """Load program data from .odk or .bin file"""
    if input_path.suffix.lower() == ".odk":
        print(f"Compiling ODKeyScript source: {input_path}")
        try:
            with open(input_path, "r", encoding="utf-8") as f:
                source = f.read()

            compiler = Compiler()
            program_data = compiler.compile(source)
            print(f"Compiled to {len(program_data)} bytes")
            return program_data

        except CompileError as e:
            print(f"Compilation error at line {e.line}, column {e.column}: {e.message}")
            raise
        except Exception as e:
            print(f"Compilation failed: {e}")
            raise
    elif input_path.suffix.lower() == ".bin":
        print(f"Loading pre-compiled bytecode: {input_path}")
        try:
            with open(input_path, "rb") as f:
                program_data = f.read()
            print(f"Loaded {len(program_data)} bytes")
            return program_data
        except Exception as e:
            print(f"Error loading bytecode: {e}")
            raise
    else:
        print(f"Error: Unsupported file type '{input_path.suffix}'")
        print("Supported types: .odk (ODKeyScript source), .bin (compiled bytecode)")
        raise ValueError(f"Unsupported file type: {input_path.suffix}")


def parse_nvs_value(value: str, value_type: str, file_path: Path | None = None) -> str | int | bytes:
    """Parse NVS value based on type"""
    if value_type == "string":
        return value
    elif value_type in ["u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64"]:
        try:
            return int(value)
        except ValueError:
            raise ValueError(f"'{value}' is not a valid integer")
    elif value_type == "blob":
        if file_path:
            try:
                with open(file_path, "rb") as f:
                    return f.read()
            except Exception as e:
                raise Exception(f"Error reading file: {e}")
        else:
            try:
                return bytes.fromhex(value)
            except ValueError:
                raise ValueError(f"'{value}' is not valid hex data")
    else:
        raise ValueError(f"Invalid type '{value_type}'")


def check_program_size(program_data: bytes, target: str) -> None:
    """Check if program size is within limits for the target"""
    if target == "ram":
        max_size = 8 * 1024  # 8KB
    else:  # flash
        max_size = 1024 * 1024  # 1MB

    if len(program_data) > max_size:
        print(f"Error: Program too large for {target} ({len(program_data)} bytes)")
        print(f"Maximum size for {target}: {max_size} bytes")
        raise ValueError(f"Program too large for {target}")


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
    try:
        program_data = load_program_data(args.input)
        check_program_size(program_data, args.target)
        
        config = create_config(args)
        
        try:
            if args.interface == "usb" and not config.find_device():
                return 1

            if not config.upload_program(program_data, target=args.target):
                return 1

            print("Upload completed successfully!")

            # Execute program if requested
            if args.execute:
                print(f"Executing {args.target.upper()} program...")
                if not config.execute_program(target=args.target):
                    print("Execution failed!")
                    return 1
                print("Execution started successfully!")

            return 0

        except Exception as e:
            print(f"Upload failed: {e}")
            return 1
        finally:
            config.close()
            
    except Exception as e:
        print(f"Error: {e}")
        return 1


def download_command(args: Any) -> int:
    """Handle the download command"""
    config = create_config(args)
    
    try:
        if args.interface == "usb" and not config.find_device():
            return 1

        # Download program
        try:
            program_data = config.download_program(target=args.target)
            if program_data is None:
                return 1
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


def execute_command(args: Any) -> int:
    """Handle the execute command"""
    config = create_config(args)
    
    try:
        if args.interface == "usb" and not config.find_device():
            return 1

        # Execute program
        if not config.execute_program(target=args.target):
            return 1

        print("Execution started successfully!")
        return 0

    except Exception as e:
        print(f"Execution failed: {e}")
        return 1
    finally:
        config.close()


def nvs_set_command(args: Any) -> int:
    """Handle the nvs-set command"""
    config = create_config(args)
    
    try:
        if args.interface == "usb" and not config.find_device():
            return 1

        # Parse value based on type
        try:
            parsed_value = parse_nvs_value(args.value, args.type, args.file)
            success = config.nvs_set(args.key, args.type, parsed_value)
        except (ValueError, Exception) as e:
            print(f"Error: {e}")
            return 1

        if not success:
            print("Failed to set NVS value")
            return 1

        return 0

    except Exception as e:
        print(f"NVS set failed: {e}")
        return 1
    finally:
        config.close()


def nvs_get_command(args: Any) -> int:
    """Handle the nvs-get command"""
    config = create_config(args)
    
    try:
        if args.interface == "usb" and not config.find_device():
            return 1

        # Get value
        try:
            success, type_name, value = config.nvs_get(args.key)
            if not success:
                return 1
        except ODKeyUploadError as e:
            print(f"NVS get failed: {e}")
            return 1

        # Display or save value
        if args.output:
            # Save to file
            try:
                if type_name == "blob":
                    with open(args.output, "wb") as f:
                        f.write(value)
                else:
                    with open(args.output, "w", encoding="utf-8") as f:
                        f.write(str(value))
                print(f"Value saved to {args.output}")
            except Exception as e:
                print(f"Error saving file: {e}")
                return 1
        else:
            # Display value
            print(f"Type: {type_name}")
            if type_name == "blob" and isinstance(value, bytes):
                print(f"Value: {len(value)} bytes")
                print(f"Hex: {bytes(value).hex()}")
            else:
                print(f"Value: {value}")

        return 0

    except Exception as e:
        print(f"NVS get failed: {e}")
        return 1
    finally:
        config.close()


def nvs_delete_command(args: Any) -> int:
    """Handle the nvs-delete command"""
    config = create_config(args)
    
    try:
        if args.interface == "usb" and not config.find_device():
            return 1

        # Delete key
        try:
            success = config.nvs_delete(args.key)
            if not success:
                return 1
        except ODKeyUploadError as e:
            print(f"NVS delete failed: {e}")
            return 1

        return 0

    except Exception as e:
        print(f"NVS delete failed: {e}")
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
  %(prog)s upload program.odk                  # Upload to RAM (default)
  %(prog)s upload program.odk --execute        # Upload to RAM and execute
  %(prog)s upload program.odk --target flash   # Upload to flash
  %(prog)s download                            # Download from RAM (default)
  %(prog)s download --target flash             # Download from flash
  %(prog)s execute                             # Execute RAM program (default)
  %(prog)s execute --target flash              # Execute flash program
  %(prog)s nvs-set wifi_ssid "MyNetwork"       # Set a string value
  %(prog)s nvs-set http_port 80 --type u16     # Set an integer value
  %(prog)s nvs-set cert --file cert.pem --type blob  # Set blob from file
  %(prog)s nvs-get wifi_ssid                   # Get a value
  %(prog)s nvs-get cert --output cert.pem      # Get and save to file
  %(prog)s nvs-delete wifi_ssid                # Delete a key
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
    add_device_args(upload_parser)
    add_target_args(upload_parser, default="ram")
    upload_parser.add_argument(
        "--execute",
        action="store_true",
        help="Execute program after upload",
    )
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
    add_device_args(download_parser)
    add_target_args(download_parser, default="ram")

    # Execute command
    execute_parser = subparsers.add_parser(
        "execute", help="Execute program on ODKey device"
    )
    add_target_args(execute_parser, default="ram")
    add_device_args(execute_parser)

    # NVS set command
    nvs_set_parser = subparsers.add_parser("nvs-set", help="Set a value in NVS storage")
    nvs_set_parser.add_argument("key", help="NVS key (max 15 characters)")
    nvs_set_parser.add_argument("value", help="Value to set")
    nvs_set_parser.add_argument(
        "--type",
        choices=[
            "u8",
            "i8",
            "u16",
            "i16",
            "u32",
            "i32",
            "u64",
            "i64",
            "string",
            "blob",
        ],
        default="string",
        help="Data type (default: string)",
    )
    nvs_set_parser.add_argument(
        "--file", type=Path, help="Read blob data from file (for blob type)"
    )
    add_device_args(nvs_set_parser)

    # NVS get command
    nvs_get_parser = subparsers.add_parser(
        "nvs-get", help="Get a value from NVS storage"
    )
    nvs_get_parser.add_argument("key", help="NVS key (max 15 characters)")
    nvs_get_parser.add_argument("--output", "-o", type=Path, help="Save value to file")
    add_device_args(nvs_get_parser)

    # NVS delete command
    nvs_delete_parser = subparsers.add_parser(
        "nvs-delete", help="Delete a key from NVS storage"
    )
    nvs_delete_parser.add_argument("key", help="NVS key (max 15 characters)")
    add_device_args(nvs_delete_parser)

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
    elif args.command == "execute":
        return execute_command(args)
    elif args.command == "nvs-set":
        return nvs_set_command(args)
    elif args.command == "nvs-get":
        return nvs_get_command(args)
    elif args.command == "nvs-delete":
        return nvs_delete_command(args)
    elif args.command == "list-devices":
        return list_devices_command(args)
    else:
        print(f"Unknown command: {args.command}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
