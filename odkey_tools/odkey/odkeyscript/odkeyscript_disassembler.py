#!/usr/bin/env python3
"""
ODKeyScript Bytecode Disassembler

Disassembles ODKeyScript bytecode back to a human-readable format.
"""

import sys
from typing import List


class Opcode:
    """ODKeyScript Virtual Machine Opcodes"""

    KEYDN = 0x10
    KEYUP = 0x11
    KEYUP_ALL = 0x12
    WAIT = 0x13
    SET_COUNTER = 0x14
    DEC = 0x15
    JNZ = 0x16


# Key mappings (reverse lookup)
KEY_NAMES = {
    0x04: "A",
    0x05: "B",
    0x06: "C",
    0x07: "D",
    0x08: "E",
    0x09: "F",
    0x0A: "G",
    0x0B: "H",
    0x0C: "I",
    0x0D: "J",
    0x0E: "K",
    0x0F: "L",
    0x10: "M",
    0x11: "N",
    0x12: "O",
    0x13: "P",
    0x14: "Q",
    0x15: "R",
    0x16: "S",
    0x17: "T",
    0x18: "U",
    0x19: "V",
    0x1A: "W",
    0x1B: "X",
    0x1C: "Y",
    0x1D: "Z",
    0x1E: "1",
    0x1F: "2",
    0x20: "3",
    0x21: "4",
    0x22: "5",
    0x23: "6",
    0x24: "7",
    0x25: "8",
    0x26: "9",
    0x27: "0",
    0x28: "ENTER",
    0x29: "ESCAPE",
    0x2A: "BACKSPACE",
    0x2B: "TAB",
    0x2C: "SPACE",
    0x2D: "MINUS",
    0x2E: "EQUAL",
    0x2F: "LEFTBRACE",
    0x30: "RIGHTBRACE",
    0x31: "BACKSLASH",
    0x32: "NONUS_HASH",
    0x33: "SEMICOLON",
    0x34: "APOSTROPHE",
    0x35: "GRAVE",
    0x36: "COMMA",
    0x37: "DOT",
    0x38: "SLASH",
    0x39: "CAPSLOCK",
    0x3A: "F1",
    0x3B: "F2",
    0x3C: "F3",
    0x3D: "F4",
    0x3E: "F5",
    0x3F: "F6",
    0x40: "F7",
    0x41: "F8",
    0x42: "F9",
    0x43: "F10",
    0x44: "F11",
    0x45: "F12",
    0x47: "SCROLLLOCK",
    0x48: "PAUSE",
    0x49: "INSERT",
    0x4A: "HOME",
    0x4B: "PAGEUP",
    0x4C: "DELETE",
    0x4D: "END",
    0x4E: "PAGEDOWN",
    0x4F: "RIGHT",
    0x50: "LEFT",
    0x51: "DOWN",
    0x52: "UP",
    0x53: "NUMLOCK",
    0x54: "KP_SLASH",
    0x55: "KP_ASTERISK",
    0x56: "KP_MINUS",
    0x57: "KP_PLUS",
    0x58: "KP_ENTER",
    0x59: "KP_1",
    0x5A: "KP_2",
    0x5B: "KP_3",
    0x5C: "KP_4",
    0x5D: "KP_5",
    0x5E: "KP_6",
    0x5F: "KP_7",
    0x60: "KP_8",
    0x61: "KP_9",
    0x62: "KP_0",
    0x63: "KP_DOT",
    0x65: "APPLICATION",
    0x76: "MENU",
    0x81: "POWER",
    0x82: "SLEEP",
    0x83: "WAKE",
    0x8A: "HENKAN",
    0x8B: "MUHENKAN",
    0x8C: "KATAKANAHIRAGANA",
    0x90: "HANGEUL",
    0x91: "HANJA",
    0xE0: "LEFTCTRL",
    0xE1: "LEFTSHIFT",
    0xE2: "LEFTALT",
    0xE3: "LEFTMETA",
    0xE4: "RIGHTCTRL",
    0xE5: "RIGHTSHIFT",
    0xE6: "RIGHTALT",
    0xE7: "RIGHTMETA",
    0xE8: "MEDIA_PLAY_PAUSE",
    0xE9: "MEDIA_STOP",
    0xEA: "MEDIA_PREVIOUS",
    0xEB: "MEDIA_NEXT",
    0xEC: "MEDIA_VOLUME_UP",
    0xED: "MEDIA_VOLUME_DOWN",
    0xEE: "MEDIA_MUTE",
}

# Modifier mappings (reverse lookup)
MODIFIER_NAMES = {
    0x01: "M_LEFTCTRL",
    0x02: "M_LEFTSHIFT",
    0x04: "M_LEFTALT",
    0x08: "M_LEFTGUI",
    0x10: "M_RIGHTCTRL",
    0x20: "M_RIGHTSHIFT",
    0x40: "M_RIGHTALT",
    0x80: "M_RIGHTGUI",
}


def bytes_to_uint16(data: bytes, offset: int) -> int:
    """Convert 2 bytes to 16-bit integer (little-endian)"""
    return data[offset] | (data[offset + 1] << 8)


def bytes_to_uint32(data: bytes, offset: int) -> int:
    """Convert 4 bytes to 32-bit integer (little-endian)"""
    return (
        data[offset]
        | (data[offset + 1] << 8)
        | (data[offset + 2] << 16)
        | (data[offset + 3] << 24)
    )


def format_modifiers(modifier_byte: int) -> str:
    """Format modifier byte as string"""
    if modifier_byte == 0:
        return ""

    modifiers = []
    for bit, name in MODIFIER_NAMES.items():
        if modifier_byte & bit:
            modifiers.append(name)

    return " ".join(modifiers)


def format_keys(keys: List[int]) -> str:
    """Format key codes as string"""
    key_names = []
    for key in keys:
        if key in KEY_NAMES:
            key_names.append(KEY_NAMES[key])
        else:
            key_names.append(f"0x{key:02X}")

    return " ".join(key_names)


def disassemble(bytecode: bytes) -> List[str]:
    """Disassemble bytecode to human-readable format"""
    instructions = []
    pc = 0

    while pc < len(bytecode):
        opcode = bytecode[pc]
        pc += 1

        if opcode == Opcode.KEYDN:
            if pc + 2 > len(bytecode):
                instructions.append(f"0x{pc-1:04X}: KEYDN (incomplete)")
                break

            modifier = bytecode[pc]
            count = bytecode[pc + 1]
            pc += 2

            if pc + count > len(bytecode):
                instructions.append(f"0x{pc-3:04X}: KEYDN (incomplete)")
                break

            keys = [bytecode[pc + i] for i in range(count)]
            pc += count

            mod_str = format_modifiers(modifier)
            key_str = format_keys(keys)

            if mod_str and key_str:
                instructions.append(f"0x{pc-count-3:04X}: KEYDN {mod_str} {key_str}")
            elif mod_str:
                instructions.append(f"0x{pc-count-3:04X}: KEYDN {mod_str}")
            elif key_str:
                instructions.append(f"0x{pc-count-3:04X}: KEYDN {key_str}")
            else:
                instructions.append(f"0x{pc-count-3:04X}: KEYDN")

        elif opcode == Opcode.KEYUP:
            if pc + 2 > len(bytecode):
                instructions.append(f"0x{pc-1:04X}: KEYUP (incomplete)")
                break

            modifier = bytecode[pc]
            count = bytecode[pc + 1]
            pc += 2

            if pc + count > len(bytecode):
                instructions.append(f"0x{pc-3:04X}: KEYUP (incomplete)")
                break

            keys = [bytecode[pc + i] for i in range(count)]
            pc += count

            mod_str = format_modifiers(modifier)
            key_str = format_keys(keys)

            if mod_str and key_str:
                instructions.append(f"0x{pc-count-3:04X}: KEYUP {mod_str} {key_str}")
            elif mod_str:
                instructions.append(f"0x{pc-count-3:04X}: KEYUP {mod_str}")
            elif key_str:
                instructions.append(f"0x{pc-count-3:04X}: KEYUP {key_str}")
            else:
                instructions.append(f"0x{pc-count-3:04X}: KEYUP")

        elif opcode == Opcode.KEYUP_ALL:
            instructions.append(f"0x{pc-1:04X}: KEYUP_ALL")

        elif opcode == Opcode.WAIT:
            if pc + 2 > len(bytecode):
                instructions.append(f"0x{pc-1:04X}: WAIT (incomplete)")
                break

            ms = bytes_to_uint16(bytecode, pc)
            pc += 2
            instructions.append(f"0x{pc-3:04X}: WAIT {ms}")

        elif opcode == Opcode.SET_COUNTER:
            if pc + 3 > len(bytecode):
                instructions.append(f"0x{pc-1:04X}: SET_COUNTER (incomplete)")
                break

            index = bytecode[pc]
            value = bytes_to_uint16(bytecode, pc + 1)
            pc += 3
            instructions.append(f"0x{pc-4:04X}: SET_COUNTER {index} {value}")

        elif opcode == Opcode.DEC:
            if pc >= len(bytecode):
                instructions.append(f"0x{pc-1:04X}: DEC (incomplete)")
                break

            index = bytecode[pc]
            pc += 1
            instructions.append(f"0x{pc-2:04X}: DEC {index}")

        elif opcode == Opcode.JNZ:
            if pc + 4 > len(bytecode):
                instructions.append(f"0x{pc-1:04X}: JNZ (incomplete)")
                break

            address = bytes_to_uint32(bytecode, pc)
            pc += 4
            instructions.append(f"0x{pc-5:04X}: JNZ 0x{address:04X}")

        else:
            instructions.append(f"0x{pc-1:04X}: UNKNOWN_OPCODE 0x{opcode:02X}")

    return instructions


def main() -> None:
    """Main function for command-line usage"""
    if len(sys.argv) != 2:
        print("Usage: python disassemble.py <bytecode.bin>")
        sys.exit(1)

    input_file = sys.argv[1]

    try:
        with open(input_file, "rb") as f:
            bytecode = f.read()

        instructions = disassemble(bytecode)

        print(f"Disassembly of {input_file} ({len(bytecode)} bytes):")
        print("=" * 50)

        for instruction in instructions:
            print(instruction)

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
