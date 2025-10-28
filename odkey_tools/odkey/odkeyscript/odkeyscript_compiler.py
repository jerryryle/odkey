#!/usr/bin/env python3
"""
ODKeyScript Compiler

Compiles ODKeyScript source code into bytecode for the ODKey virtual machine.
"""

import sys
from dataclasses import dataclass
from enum import Enum
from typing import List, Tuple


class Opcode(Enum):
    """ODKeyScript Virtual Machine Opcodes"""

    KEYDN = 0x10
    KEYUP = 0x11
    KEYUP_ALL = 0x12
    WAIT = 0x13
    SET_COUNTER = 0x14
    DEC = 0x15
    JNZ = 0x16


class TokenType(Enum):
    """Token types for the lexer"""

    COMMAND = "COMMAND"
    KEY = "KEY"
    MODIFIER = "MODIFIER"
    NUMBER = "NUMBER"
    STRING = "STRING"
    BRACE_OPEN = "BRACE_OPEN"
    BRACE_CLOSE = "BRACE_CLOSE"
    COMMENT = "COMMENT"
    EOF = "EOF"


@dataclass
class Token:
    """Represents a token in the source code"""

    type: TokenType
    value: str
    line: int
    column: int


@dataclass
class CompileError(Exception):
    """Compilation error with location information"""

    message: str
    line: int
    column: int


class Lexer:
    """Lexical analyzer for ODKeyScript"""

    # Key mappings
    KEY_MAP = {
        # Alphanumeric keys
        "A": 0x04,
        "B": 0x05,
        "C": 0x06,
        "D": 0x07,
        "E": 0x08,
        "F": 0x09,
        "G": 0x0A,
        "H": 0x0B,
        "I": 0x0C,
        "J": 0x0D,
        "K": 0x0E,
        "L": 0x0F,
        "M": 0x10,
        "N": 0x11,
        "O": 0x12,
        "P": 0x13,
        "Q": 0x14,
        "R": 0x15,
        "S": 0x16,
        "T": 0x17,
        "U": 0x18,
        "V": 0x19,
        "W": 0x1A,
        "X": 0x1B,
        "Y": 0x1C,
        "Z": 0x1D,
        "1": 0x1E,
        "2": 0x1F,
        "3": 0x20,
        "4": 0x21,
        "5": 0x22,
        "6": 0x23,
        "7": 0x24,
        "8": 0x25,
        "9": 0x26,
        "0": 0x27,
        # Special keys
        "ENTER": 0x28,
        "ESCAPE": 0x29,
        "BACKSPACE": 0x2A,
        "TAB": 0x2B,
        "SPACE": 0x2C,
        "MINUS": 0x2D,
        "EQUAL": 0x2E,
        "LEFTBRACE": 0x2F,
        "RIGHTBRACE": 0x30,
        "BACKSLASH": 0x31,
        "NONUS_HASH": 0x32,
        "SEMICOLON": 0x33,
        "APOSTROPHE": 0x34,
        "GRAVE": 0x35,
        "COMMA": 0x36,
        "DOT": 0x37,
        "SLASH": 0x38,
        "CAPSLOCK": 0x39,
        # Function keys
        "F1": 0x3A,
        "F2": 0x3B,
        "F3": 0x3C,
        "F4": 0x3D,
        "F5": 0x3E,
        "F6": 0x3F,
        "F7": 0x40,
        "F8": 0x41,
        "F9": 0x42,
        "F10": 0x43,
        "F11": 0x44,
        "F12": 0x45,
        # Arrow keys
        "UP": 0x52,
        "DOWN": 0x51,
        "LEFT": 0x50,
        "RIGHT": 0x4F,
        # Numpad keys
        "NUMLOCK": 0x53,
        "KP_SLASH": 0x54,
        "KP_ASTERISK": 0x55,
        "KP_MINUS": 0x56,
        "KP_PLUS": 0x57,
        "KP_ENTER": 0x58,
        "KP_1": 0x59,
        "KP_2": 0x5A,
        "KP_3": 0x5B,
        "KP_4": 0x5C,
        "KP_5": 0x5D,
        "KP_6": 0x5E,
        "KP_7": 0x5F,
        "KP_8": 0x60,
        "KP_9": 0x61,
        "KP_0": 0x62,
        "KP_DOT": 0x63,
        # Other keys
        "SCROLLLOCK": 0x47,
        "PAUSE": 0x48,
        "INSERT": 0x49,
        "HOME": 0x4A,
        "PAGEUP": 0x4B,
        "DELETE": 0x4C,
        "END": 0x4D,
        "PAGEDOWN": 0x4E,
        "APPLICATION": 0x65,
        "MENU": 0x76,
        # International keys
        "HENKAN": 0x8A,
        "MUHENKAN": 0x8B,
        "KATAKANAHIRAGANA": 0x8C,
        "HANGEUL": 0x90,
        "HANJA": 0x91,
        # System keys
        "POWER": 0x81,
        "SLEEP": 0x82,
        "WAKE": 0x83,
        # Modifier keys (as regular keys)
        "LEFTCTRL": 0xE0,
        "LEFTSHIFT": 0xE1,
        "LEFTALT": 0xE2,
        "LEFTMETA": 0xE3,
        "RIGHTCTRL": 0xE4,
        "RIGHTSHIFT": 0xE5,
        "RIGHTALT": 0xE6,
        "RIGHTMETA": 0xE7,
        # Media keys
        "MEDIA_PLAY_PAUSE": 0xE8,
        "MEDIA_STOP": 0xE9,
        "MEDIA_PREVIOUS": 0xEA,
        "MEDIA_NEXT": 0xEB,
        "MEDIA_VOLUME_UP": 0xEC,
        "MEDIA_VOLUME_DOWN": 0xED,
        "MEDIA_MUTE": 0xEE,
        "MEDIA_EJECT": 0xB3,
        "MEDIA_RECORD": 0xB4,
        "MEDIA_REWIND": 0xB5,
        "MEDIA_FAST_FORWARD": 0xB6,
        # Consumer keys
        "CALCULATOR": 0xA1,
        "MYCOMPUTER": 0xA2,
        "WWW_SEARCH": 0xA3,
        "WWW_HOME": 0xA4,
        "WWW_BACK": 0xA5,
        "WWW_FORWARD": 0xA6,
        "WWW_STOP": 0xA7,
        "WWW_REFRESH": 0xA8,
        "WWW_FAVORITES": 0xA9,
        "MAIL": 0xAA,
        "COMPOSE": 0xAB,
        "BROWSER_BACK": 0xAC,
        "BROWSER_FORWARD": 0xAD,
        "BROWSER_REFRESH": 0xAE,
        "BROWSER_STOP": 0xAF,
        "BROWSER_SEARCH": 0xB0,
        "BROWSER_FAVORITES": 0xB1,
        "BROWSER_HOME": 0xB2,
        "GAME": 0xB7,
        "CHAT": 0xB8,
        "ZOOM": 0xB9,
        "PRESENTATION": 0xBA,
        "SPREADSHEET": 0xBB,
        "LANGUAGE": 0xBC,
    }

    # Modifier mappings
    MODIFIER_MAP = {
        "M_LEFTCTRL": 0x01,
        "M_LEFTSHIFT": 0x02,
        "M_LEFTALT": 0x04,
        "M_LEFTGUI": 0x08,
        "M_RIGHTCTRL": 0x10,
        "M_RIGHTSHIFT": 0x20,
        "M_RIGHTALT": 0x40,
        "M_RIGHTGUI": 0x80,
    }

    def __init__(self, source: str):
        self.source = source
        self.position = 0
        self.line = 1
        self.column = 1
        self.tokens: List[Token] = []
        self._tokenize()

    def _tokenize(self) -> None:
        """Tokenize the source code"""
        while self.position < len(self.source):
            char = self.source[self.position]

            if char.isspace():
                if char == "\n":
                    self.line += 1
                    self.column = 1
                else:
                    self.column += 1
                self.position += 1
            elif char == "#":
                self._tokenize_comment()
            elif char == "{":
                self.tokens.append(
                    Token(TokenType.BRACE_OPEN, char, self.line, self.column)
                )
                self.position += 1
                self.column += 1
            elif char == "}":
                self.tokens.append(
                    Token(TokenType.BRACE_CLOSE, char, self.line, self.column)
                )
                self.position += 1
                self.column += 1
            elif char == '"':
                self._tokenize_string()
            elif char.isdigit():
                self._tokenize_number()
            elif char.isalpha() or char == "_":
                self._tokenize_identifier()
            else:
                raise CompileError(
                    f"Unexpected character: {char}", self.line, self.column
                )

        self.tokens.append(Token(TokenType.EOF, "", self.line, self.column))

    def _tokenize_comment(self) -> None:
        """Tokenize a comment"""
        start_line = self.line
        start_column = self.column
        comment = ""

        while self.position < len(self.source) and self.source[self.position] != "\n":
            comment += self.source[self.position]
            self.position += 1
            self.column += 1

        self.tokens.append(Token(TokenType.COMMENT, comment, start_line, start_column))

    def _tokenize_string(self) -> None:
        """Tokenize a string literal with escape sequence support"""
        start_line = self.line
        start_column = self.column
        string = ""

        self.position += 1  # Skip opening quote
        self.column += 1

        while self.position < len(self.source) and self.source[self.position] != '"':
            if self.source[self.position] == "\\":
                self.position += 1
                self.column += 1
                if self.position < len(self.source):
                    # Handle escape sequences
                    escape_char = self.source[self.position]
                    if escape_char == 't':
                        string += '\t'
                    elif escape_char == 'n':
                        string += '\n'
                    elif escape_char == '\\':
                        string += '\\'
                    elif escape_char == '"':
                        string += '"'
                    else:
                        # Unknown escape sequence, treat as literal
                        string += '\\' + escape_char
                    self.position += 1
                    self.column += 1
                else:
                    # Backslash at end of string, treat as literal
                    string += '\\'
            else:
                string += self.source[self.position]
                self.position += 1
                self.column += 1

        if self.position >= len(self.source):
            raise CompileError("Unterminated string", start_line, start_column)

        self.position += 1  # Skip closing quote
        self.column += 1

        self.tokens.append(Token(TokenType.STRING, string, start_line, start_column))

    def _tokenize_number(self) -> None:
        """Tokenize a number"""
        start_line = self.line
        start_column = self.column
        number = ""

        while self.position < len(self.source) and self.source[self.position].isdigit():
            number += self.source[self.position]
            self.position += 1
            self.column += 1

        self.tokens.append(Token(TokenType.NUMBER, number, start_line, start_column))

    def _tokenize_identifier(self) -> None:
        """Tokenize an identifier (command, key, or modifier)"""
        start_line = self.line
        start_column = self.column
        identifier = ""

        while self.position < len(self.source) and (
            self.source[self.position].isalnum() or self.source[self.position] == "_"
        ):
            identifier += self.source[self.position]
            self.position += 1
            self.column += 1

        # Determine token type
        if identifier in [
            "press_time",
            "interkey_time",
            "keydn",
            "keyup",
            "press",
            "type",
            "repeat",
            "pause",
        ]:
            self.tokens.append(
                Token(TokenType.COMMAND, identifier, start_line, start_column)
            )
        elif identifier.startswith("M_"):
            self.tokens.append(
                Token(TokenType.MODIFIER, identifier, start_line, start_column)
            )
        else:
            self.tokens.append(
                Token(TokenType.KEY, identifier, start_line, start_column)
            )


class Compiler:
    """ODKeyScript compiler"""

    def __init__(self) -> None:
        self.bytecode: List[int] = []
        self.current_press_time: int = 30  # Default 30ms
        self.current_interkey_time: int = 30  # Default 30ms
        self.counter_index: int = 0
        self.max_counters: int = 256
        self.loop_stack: List[Tuple[int, int, int]] = (
            []
        )  # Stack of (counter_index, loop_start_address, loop_end_address)
        self.string_data: List[str] = []  # Store string data separately
        self.string_offset: int = 0

    def compile(self, source: str) -> bytes:
        """Compile ODKeyScript source to bytecode"""
        lexer = Lexer(source)
        self._compile_statements(lexer)
        return bytes(self.bytecode)

    def _compile_statements(self, lexer: Lexer) -> None:
        """Compile a sequence of statements"""
        while lexer.tokens and lexer.tokens[0].type != TokenType.EOF:
            if lexer.tokens[0].type == TokenType.COMMENT:
                lexer.tokens.pop(0)  # Skip comments
            elif lexer.tokens[0].type == TokenType.BRACE_CLOSE:
                # End of block, let caller handle it
                break
            else:
                self._compile_statement(lexer)

    def _compile_statement(self, lexer: Lexer) -> None:
        """Compile a single statement"""
        if not lexer.tokens:
            return

        token = lexer.tokens[0]

        if token.type == TokenType.COMMAND:
            if token.value == "press_time":
                self._compile_press_time(lexer)
            elif token.value == "interkey_time":
                self._compile_interkey_time(lexer)
            elif token.value == "keydn":
                self._compile_keydn(lexer)
            elif token.value == "keyup":
                self._compile_keyup(lexer)
            elif token.value == "press":
                self._compile_press(lexer)
            elif token.value == "type":
                self._compile_type(lexer)
            elif token.value == "repeat":
                self._compile_repeat(lexer)
            elif token.value == "pause":
                self._compile_pause(lexer)
            else:
                raise CompileError(
                    f"Unknown command: {token.value}", token.line, token.column
                )
        else:
            raise CompileError(
                f"Expected command, got {token.type}", token.line, token.column
            )

    def _compile_press_time(self, lexer: Lexer) -> None:
        """Compile press_time command"""
        lexer.tokens.pop(0)  # Remove 'press_time'

        if not lexer.tokens or lexer.tokens[0].type != TokenType.NUMBER:
            raise CompileError(
                "Expected number after press_time",
                lexer.tokens[0].line,
                lexer.tokens[0].column,
            )

        time_value = int(lexer.tokens[0].value)
        if time_value < 0 or time_value > 65535:
            raise CompileError(
                "press_time must be between 0 and 65535",
                lexer.tokens[0].line,
                lexer.tokens[0].column,
            )

        self.current_press_time = time_value
        lexer.tokens.pop(0)  # Remove number

    def _compile_interkey_time(self, lexer: Lexer) -> None:
        """Compile interkey_time command"""
        lexer.tokens.pop(0)  # Remove 'interkey_time'

        if not lexer.tokens or lexer.tokens[0].type != TokenType.NUMBER:
            raise CompileError(
                "Expected number after interkey_time",
                lexer.tokens[0].line,
                lexer.tokens[0].column,
            )

        time_value = int(lexer.tokens[0].value)
        if time_value < 0 or time_value > 65535:
            raise CompileError(
                "interkey_time must be between 0 and 65535",
                lexer.tokens[0].line,
                lexer.tokens[0].column,
            )

        self.current_interkey_time = time_value
        lexer.tokens.pop(0)  # Remove number

    def _compile_keydn(self, lexer: Lexer) -> None:
        """Compile keydn command"""
        lexer.tokens.pop(0)  # Remove 'keydn'

        modifiers = 0
        keys: List[int] = []

        # Parse modifiers and keys
        while lexer.tokens and lexer.tokens[0].type != TokenType.EOF:
            token = lexer.tokens[0]

            if token.type == TokenType.MODIFIER:
                if token.value in Lexer.MODIFIER_MAP:
                    modifiers |= Lexer.MODIFIER_MAP[token.value]
                else:
                    raise CompileError(
                        f"Unknown modifier: {token.value}", token.line, token.column
                    )
                lexer.tokens.pop(0)
            elif token.type == TokenType.KEY:
                if token.value in Lexer.KEY_MAP:
                    if len(keys) >= 6:
                        raise CompileError(
                            "Too many keys (maximum 6)", token.line, token.column
                        )
                    keys.append(Lexer.KEY_MAP[token.value])
                else:
                    raise CompileError(
                        f"Unknown key: {token.value}", token.line, token.column
                    )
                lexer.tokens.pop(0)
            else:
                break

        # Emit KEYDN opcode
        self.bytecode.append(Opcode.KEYDN.value)
        self.bytecode.append(modifiers)
        self.bytecode.append(len(keys))
        self.bytecode.extend(keys)

    def _compile_keyup(self, lexer: Lexer) -> None:
        """Compile keyup command"""
        lexer.tokens.pop(0)  # Remove 'keyup'

        # Check if it's a bare keyup (release all)
        if not lexer.tokens or lexer.tokens[0].type in [
            TokenType.EOF,
            TokenType.COMMENT,
        ]:
            self.bytecode.append(Opcode.KEYUP_ALL.value)
            return

        modifiers = 0
        keys: List[int] = []

        # Parse modifiers and keys
        while lexer.tokens and lexer.tokens[0].type != TokenType.EOF:
            token = lexer.tokens[0]

            if token.type == TokenType.MODIFIER:
                if token.value in Lexer.MODIFIER_MAP:
                    modifiers |= Lexer.MODIFIER_MAP[token.value]
                else:
                    raise CompileError(
                        f"Unknown modifier: {token.value}", token.line, token.column
                    )
                lexer.tokens.pop(0)
            elif token.type == TokenType.KEY:
                if token.value in Lexer.KEY_MAP:
                    if len(keys) >= 6:
                        raise CompileError(
                            "Too many keys (maximum 6)", token.line, token.column
                        )
                    keys.append(Lexer.KEY_MAP[token.value])
                else:
                    raise CompileError(
                        f"Unknown key: {token.value}", token.line, token.column
                    )
                lexer.tokens.pop(0)
            else:
                break

        # Emit KEYUP opcode
        self.bytecode.append(Opcode.KEYUP.value)
        self.bytecode.append(modifiers)
        self.bytecode.append(len(keys))
        self.bytecode.extend(keys)

    def _compile_press(self, lexer: Lexer) -> None:
        """Compile press command (keydn + wait + keyup)"""
        lexer.tokens.pop(0)  # Remove 'press'

        modifiers = 0
        keys: List[int] = []

        # Parse modifiers and keys
        while lexer.tokens and lexer.tokens[0].type != TokenType.EOF:
            token = lexer.tokens[0]

            if token.type == TokenType.MODIFIER:
                if token.value in Lexer.MODIFIER_MAP:
                    modifiers |= Lexer.MODIFIER_MAP[token.value]
                else:
                    raise CompileError(
                        f"Unknown modifier: {token.value}", token.line, token.column
                    )
                lexer.tokens.pop(0)
            elif token.type == TokenType.KEY:
                if token.value in Lexer.KEY_MAP:
                    if len(keys) >= 6:
                        raise CompileError(
                            "Too many keys (maximum 6)", token.line, token.column
                        )
                    keys.append(Lexer.KEY_MAP[token.value])
                else:
                    raise CompileError(
                        f"Unknown key: {token.value}", token.line, token.column
                    )
                lexer.tokens.pop(0)
            else:
                break

        if not keys:
            raise CompileError(
                "press command requires at least one key",
                lexer.tokens[0].line if lexer.tokens else 0,
                0,
            )

        # Emit KEYDN + WAIT + KEYUP + WAIT sequence
        self.bytecode.append(Opcode.KEYDN.value)
        self.bytecode.append(modifiers)
        self.bytecode.append(len(keys))
        self.bytecode.extend(keys)

        self.bytecode.append(Opcode.WAIT.value)
        self.bytecode.extend(self._uint16_to_bytes(self.current_press_time))

        self.bytecode.append(Opcode.KEYUP.value)
        self.bytecode.append(modifiers)
        self.bytecode.append(len(keys))
        self.bytecode.extend(keys)

        self.bytecode.append(Opcode.WAIT.value)
        self.bytecode.extend(self._uint16_to_bytes(self.current_interkey_time))

    def _compile_type(self, lexer: Lexer) -> None:
        """Compile type command"""
        lexer.tokens.pop(0)  # Remove 'type'

        if not lexer.tokens or lexer.tokens[0].type != TokenType.STRING:
            raise CompileError(
                "Expected string after type",
                lexer.tokens[0].line,
                lexer.tokens[0].column,
            )

        string = lexer.tokens[0].value
        lexer.tokens.pop(0)  # Remove string

        # For each character, emit KEYDN + WAIT + KEYUP + interkey_time
        for i, char in enumerate(string):
            if char == " ":
                # Space key
                self.bytecode.append(Opcode.KEYDN.value)
                self.bytecode.append(0)  # No modifiers
                self.bytecode.append(1)  # One key
                self.bytecode.append(Lexer.KEY_MAP["SPACE"])

                self.bytecode.append(Opcode.WAIT.value)
                self.bytecode.extend(self._uint16_to_bytes(self.current_press_time))

                self.bytecode.append(Opcode.KEYUP.value)
                self.bytecode.append(0)  # No modifiers
                self.bytecode.append(1)  # One key
                self.bytecode.append(Lexer.KEY_MAP["SPACE"])
            else:
                # Map character to key code and modifiers
                key_code, modifiers = self._char_to_keycode(char)

                self.bytecode.append(Opcode.KEYDN.value)
                self.bytecode.append(modifiers)
                self.bytecode.append(1)  # One key
                self.bytecode.append(key_code)

                self.bytecode.append(Opcode.WAIT.value)
                self.bytecode.extend(self._uint16_to_bytes(self.current_press_time))

                self.bytecode.append(Opcode.KEYUP.value)
                self.bytecode.append(modifiers)
                self.bytecode.append(1)  # One key
                self.bytecode.append(key_code)

            # Add interkey_time delay between keystrokes (except after the last character)
            if i < len(string) - 1:
                self.bytecode.append(Opcode.WAIT.value)
                self.bytecode.extend(self._uint16_to_bytes(self.current_interkey_time))

    def _compile_repeat(self, lexer: Lexer) -> None:
        """Compile repeat command"""
        lexer.tokens.pop(0)  # Remove 'repeat'

        if not lexer.tokens or lexer.tokens[0].type != TokenType.NUMBER:
            raise CompileError(
                "Expected number after repeat",
                lexer.tokens[0].line if lexer.tokens else 0,
                lexer.tokens[0].column if lexer.tokens else 0,
            )

        count = int(lexer.tokens[0].value)
        if count < 0 or count > 65535:
            raise CompileError(
                "repeat count must be between 0 and 65535",
                lexer.tokens[0].line,
                lexer.tokens[0].column,
            )

        lexer.tokens.pop(0)  # Remove number

        if not lexer.tokens:
            raise CompileError("Expected '{' after repeat count", 0, 0)

        next_token = lexer.tokens[0]
        if next_token.type != TokenType.BRACE_OPEN:
            raise CompileError(
                "Expected '{' after repeat count", next_token.line, next_token.column
            )

        lexer.tokens.pop(0)  # Remove '{'

        # Check for nested loop limit
        if len(self.loop_stack) >= self.max_counters:
            raise CompileError(
                f"Too many nested loops (maximum {self.max_counters})",
                lexer.tokens[0].line,
                lexer.tokens[0].column,
            )

        # Allocate counter
        counter_index = self.counter_index
        self.counter_index += 1

        # Set counter to repeat count
        self.bytecode.append(Opcode.SET_COUNTER.value)
        self.bytecode.append(counter_index)
        self.bytecode.extend(self._uint16_to_bytes(count))

        # Mark loop start
        loop_start = len(self.bytecode)
        self.loop_stack.append(
            (counter_index, loop_start, 0)
        )  # loop_end will be filled later

        # Compile loop body
        self._compile_statements(lexer)

        if not lexer.tokens:
            raise CompileError("Expected '}' to close repeat block", 0, 0)

        close_token = lexer.tokens[0]
        if close_token.type != TokenType.BRACE_CLOSE:
            raise CompileError(
                "Expected '}' to close repeat block",
                close_token.line,
                close_token.column,
            )

        lexer.tokens.pop(0)  # Remove '}'

        # Emit loop control
        self.bytecode.append(Opcode.DEC.value)
        self.bytecode.append(counter_index)

        # Emit conditional jump back to loop start
        self.bytecode.append(Opcode.JNZ.value)
        self.bytecode.extend(self._uint32_to_bytes(loop_start))

        # Update loop stack
        self.loop_stack.pop()

    def _compile_pause(self, lexer: Lexer) -> None:
        """Compile pause command"""
        lexer.tokens.pop(0)  # Remove 'pause'

        if not lexer.tokens or lexer.tokens[0].type != TokenType.NUMBER:
            raise CompileError(
                "Expected number after pause",
                lexer.tokens[0].line,
                lexer.tokens[0].column,
            )

        time_value = int(lexer.tokens[0].value)
        if time_value < 0 or time_value > 65535:
            raise CompileError(
                "pause time must be between 0 and 65535",
                lexer.tokens[0].line,
                lexer.tokens[0].column,
            )

        lexer.tokens.pop(0)  # Remove number

        # Emit WAIT opcode
        self.bytecode.append(Opcode.WAIT.value)
        self.bytecode.extend(self._uint16_to_bytes(time_value))

    def _char_to_keycode(self, char: str) -> Tuple[int, int]:
        """Convert character to keycode and modifiers"""
        char_upper = char.upper()

        # Direct mapping for letters
        if char_upper in Lexer.KEY_MAP:
            if char.isupper():
                return Lexer.KEY_MAP[char_upper], Lexer.MODIFIER_MAP["M_LEFTSHIFT"]
            else:
                return Lexer.KEY_MAP[char_upper], 0

        # Number mapping
        if char.isdigit():
            return Lexer.KEY_MAP[char], 0

        # Special whitespace character mapping
        whitespace_map = {
            "\t": "TAB",
            "\n": "ENTER",
        }
        
        if char in whitespace_map:
            key_name = whitespace_map[char]
            return Lexer.KEY_MAP[key_name], 0

        # Symbol mapping (simplified)
        symbol_map = {
            # Shifted number keys
            "!": ("1", "M_LEFTSHIFT"),
            "@": ("2", "M_LEFTSHIFT"),
            "#": ("3", "M_LEFTSHIFT"),
            "$": ("4", "M_LEFTSHIFT"),
            "%": ("5", "M_LEFTSHIFT"),
            "^": ("6", "M_LEFTSHIFT"),
            "&": ("7", "M_LEFTSHIFT"),
            "*": ("8", "M_LEFTSHIFT"),
            "(": ("9", "M_LEFTSHIFT"),
            ")": ("0", "M_LEFTSHIFT"),
            
            # Shifted punctuation
            "_": ("MINUS", "M_LEFTSHIFT"),
            "+": ("EQUAL", "M_LEFTSHIFT"),
            "{": ("LEFTBRACE", "M_LEFTSHIFT"),
            "}": ("RIGHTBRACE", "M_LEFTSHIFT"),
            "|": ("BACKSLASH", "M_LEFTSHIFT"),
            ":": ("SEMICOLON", "M_LEFTSHIFT"),
            "<": ("COMMA", "M_LEFTSHIFT"),
            ">": ("DOT", "M_LEFTSHIFT"),
            "?": ("SLASH", "M_LEFTSHIFT"),
            "~": ("GRAVE", "M_LEFTSHIFT"),
            '"': ("APOSTROPHE", "M_LEFTSHIFT"),
            
            # Unshifted punctuation
            "-": ("MINUS", None),
            "=": ("EQUAL", None),
            "[": ("LEFTBRACE", None),
            "]": ("RIGHTBRACE", None),
            "\\": ("BACKSLASH", None),
            ";": ("SEMICOLON", None),
            "'": ("APOSTROPHE", None),
            "`": ("GRAVE", None),
            ",": ("COMMA", None),
            ".": ("DOT", None),
            "/": ("SLASH", None),
        }

        if char in symbol_map:
            key_name, modifier_name = symbol_map[char]
            modifiers = 0
            if modifier_name:
                modifiers = Lexer.MODIFIER_MAP[modifier_name]
            return Lexer.KEY_MAP[key_name], modifiers

        # Default to space for unknown characters
        return Lexer.KEY_MAP["SPACE"], 0

    def _uint16_to_bytes(self, value: int) -> List[int]:
        """Convert 16-bit integer to little-endian bytes"""
        return [value & 0xFF, (value >> 8) & 0xFF]

    def _uint32_to_bytes(self, value: int) -> List[int]:
        """Convert 32-bit integer to little-endian bytes"""
        return [
            value & 0xFF,
            (value >> 8) & 0xFF,
            (value >> 16) & 0xFF,
            (value >> 24) & 0xFF,
        ]


def main() -> None:
    """Main function for command-line usage"""
    if len(sys.argv) != 3:
        print("Usage: python odkeyscript_compiler.py <input.odk> <output.bin>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    try:
        with open(input_file, "r") as f:
            source = f.read()

        compiler = Compiler()
        bytecode = compiler.compile(source)

        with open(output_file, "wb") as f:
            f.write(bytecode)

        print(f"Compiled {input_file} to {output_file} ({len(bytecode)} bytes)")

    except CompileError as e:
        print(f"Compilation error at line {e.line}, column {e.column}: {e.message}")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
