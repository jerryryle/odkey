#!/usr/bin/env python3
"""
Test script for ODKeyScript compiler
"""

from .odkeyscript_compiler import CompileError, Compiler


def test_compiler() -> None:
    """Test the ODKeyScript compiler with various examples"""

    compiler = Compiler()

    # Test cases
    test_cases = [
        {
            "name": "Simple key press",
            "source": "press A",
            "description": "Press and release the A key",
        },
        {
            "name": "Key with modifier",
            "source": "press M_LEFTSHIFT A",
            "description": "Press Shift+A",
        },
        {
            "name": "Multiple keys",
            "source": "keydn M_LEFTSHIFT A B C\npause 100\nkeyup",
            "description": "Hold Shift+A+B+C for 100ms, then release all",
        },
        {
            "name": "Type string",
            "source": 'type "Hello World!"',
            "description": "Type a string with automatic shift handling",
        },
        {
            "name": "Repeat loop",
            "source": "repeat 3 {\n    press ENTER\n    pause 50\n}",
            "description": "Press Enter 3 times with 50ms pause between",
        },
        {
            "name": "Press time setting",
            "source": "press_time 100\npress A\npress_time 30\npress B",
            "description": "Set different press times",
        },
        {
            "name": "Interkey time setting",
            "source": 'press_time 50\ninterkey_time 100\ntype "Hi"',
            "description": "Set interkey time for delays between keystrokes",
        },
        {
            "name": "Nested repeat",
            "source": "repeat 2 {\n    press A\n    repeat 3 {\n        press B\n    }\n}",
            "description": "Nested repeat loops",
        },
    ]

    print("ODKeyScript Compiler Test Results")
    print("=" * 50)

    for i, test_case in enumerate(test_cases, 1):
        print(f"\n{i}. {test_case['name']}")
        print(f"   Description: {test_case['description']}")
        print(f"   Source: {test_case['source']}")

        try:
            bytecode = compiler.compile(test_case["source"])
            print(f"   ✅ Success: {len(bytecode)} bytes")
            print(f"   Bytecode: {' '.join(f'{b:02x}' for b in bytecode)}")
        except CompileError as e:
            print(
                f"   ❌ Compile Error: {e.message} at line {e.line}, column {e.column}"
            )
        except Exception as e:
            print(f"   ❌ Error: {e}")

    # Test error cases
    print("\n\nError Test Cases")
    print("=" * 50)

    error_cases = [
        {
            "name": "Unknown key",
            "source": "press UNKNOWN_KEY",
            "expected": "Unknown key error",
        },
        {
            "name": "Too many keys",
            "source": "keydn A B C D E F G",
            "expected": "Too many keys error",
        },
        {
            "name": "Repeat count too small",
            "source": "repeat -1 { press A }",
            "expected": "Invalid repeat count error",
        },
        {
            "name": "Repeat count too large",
            "source": "repeat 65536 { press A }",
            "expected": "Repeat count too large error",
        },
        {
            "name": "Unclosed repeat block",
            "source": "repeat 3 { press A",
            "expected": "Unclosed block error",
        },
        {
            "name": "Too many nested loops",
            "source": "repeat 1 { " * 257 + "press A" + " }" * 257,
            "expected": "Too many nested loops error",
        },
        {
            "name": "Invalid interkey_time value",
            "source": "interkey_time -1",
            "expected": "interkey_time must be between 0 and 65535",
        },
        {
            "name": "Interkey_time too large",
            "source": "interkey_time 65536",
            "expected": "interkey_time must be between 0 and 65535",
        },
    ]

    for i, test_case in enumerate(error_cases, 1):
        print(f"\n{i}. {test_case['name']}")
        print(f"   Source: {test_case['source']}")
        print(f"   Expected: {test_case['expected']}")

        try:
            bytecode = compiler.compile(test_case["source"])
            print(f"   ❌ Unexpected success: {len(bytecode)} bytes")
        except CompileError as e:
            print(f"   ✅ Expected error: {e.message}")
        except Exception as e:
            print(f"   ❌ Unexpected error: {e}")


if __name__ == "__main__":
    test_compiler()
