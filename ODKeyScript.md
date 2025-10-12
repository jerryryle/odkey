# ODKeyScript Reference
The ODKey can be reprogrammed to perform custom macro sequences that are written in ODKeyScript. This file documents that language and the virtual machine that executes it.

ODKeyScript compilers should fail on the first error encountered.

## Comments
The `#` character denotes a comment. It and everything after it on the same line is ignored.
Example:
```
# This is a comment
KEYDN M_LEFTSHIFT A B C D E F # Presses left shift and abcdef keys all together
```

## Commands
All commands are case sensitive. They can appear anywhere on a line, but a line may only contain one command.

### press_time
The `press_time` command sets the time, in milliseconds that keys are "pressed" for commands such as `type`. This can be set more than once within a script and it applies to all relevant commands that come after it. Note that very short press times may be unreliable, depending upon the OS and application. ODKey defaults the press time to 30ms.
Example:
```
press_time 30
press ENTER # Short press of enter
press_time 100
press A # Longer press of enter
```

### pause
The `pause` command pauses execution for the specified number of milliseconds.
```
pause 100
```

### keydn
The `keydn` command "presses" up to 6 keys and any number of modifier keys. The keys and modifiers follow the `keydn` command and may be specified in any order. The keys will appear in the HID report in the order specified (the modifiers are sent as bitmasks and thus have no order). If more than 6 keys are specified, the script will fail to compile.
Example:
```
keydn M_LEFTSHIFT A B C D E F
pause 100
keyup
```

### keyup
The `keyup` command "releases" up to 6 keys and any number of modifier keys. The keys and modifiers follow the `keyup` command and may be specified in any order. The keys will appear in the HID report in the order specified (the modifiers are sent as bitmasks and thus have no order). If more than 6 keys are specified, the script will fail to compile. Any keys that were not previously "pressed" with `keydn` will have no effect. A bare `keyup` command with no modifiers or keys will release everything that was previously pressed.
Example:
```
keyup M_LEFTSHIFT A B C D E F
keyup # release all keys
```

### press
The `press` command presses and releases a single key, with any number of optional modifiers. The keypress time can be changed with the `press_time` command. 
Example:
```
press M_LEFTSHIFT A
press ENTER
press M_LEFTSHIFT
```

### type
The `type` command sends a sequence of alphanumeric key presses/releases to "type" a string. Trailing spaces are omitted. The string must be separated from the `type` command by at least one space, but all whitespace before the first alphanumeric character is ignored. All trailing whitespace is also ignored. The keypress time can be changed with the `press_time` command. The `type` command will infer and automatically add shift modifiers when needed, so you can type characters and symbols as you expect to see them.
Example:
```
type The quick brown fox jumps over the lazy dog!
press ENTER
```

### repeat
The `repeat` command repeats, the specified number of times, all commands that follow it inside braces. You can nest `repeat` commands up to 256 levels deep.
Example:
```
repeat 3 {
    press ENTER

    repeat 100 {
        press DOWN
    }
}

press ENTER # This command is not repeated.
```


## Key definitions
This section defines the key names you can use with commands such as `keydn` and `press`. They are case sensitive.

### Modifiers
M_LEFTCTRL M_LEFTSHIFT M_LEFTALT M_LEFTGUI
M_RIGHTCTRL M_RIGHTSHIFT M_RIGHTALT M_RIGHTGUI

### Alphanumeric Keys
Alphanumeric keys are specified as they appear on a keyboard:
A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
1 2 3 4 5 6 7 8 9 0
` - = [ ] \ ; ' , . /

### Special Keys
ENTER
ESCAPE
BACKSPACE DELETE
TAB SPACE
NONUS_HASH
CAPSLOCK SCROLLLOCK NUMLOCK
PAUSE
INSERT
HOME
PAGEUP PAGEDOWN
END
APPLICATION
MENU

### Numpad Keys
KP_SLASH
KP_ASTERISK
KP_MINUS
KP_PLUS
KP_ENTER
KP_1 KP_2 KP_3 KP_4 KP_5 KP_6 KP_7 KP_8 KP_9 KP_0
KP_DOT
NONUS_BACKSLASH

### Arrow Keys
RIGHT LEFT DOWN UP

### Function Keys
F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12
F13 F14 F15 F16 F17 F18 F19 F20 F21 F22 F23 F24

### International Keys
HENKAN MUHENKAN KATAKANAHIRAGANA HANGEUL HANJA

### System Keys
POWER SLEEP WAKE

### Modifier Keys
Typically, you would use the `M_` modifiers, which are sent as modifier bitmasks in the HID report. These versions will send the modifier as a key in the HID report's key data.
LEFTCTRL LEFTSHIFT LEFTALT LEFTMETA
RIGHTCTRL RIGHTSHIFT RIGHTALT RIGHTMETA

### Media Keys
MEDIA_PLAY_PAUSE MEDIA_STOP
MEDIA_PREVIOUS MEDIA_NEXT 
MEDIA_VOLUME_UP MEDIA_VOLUME_DOWN MEDIA_MUTE
MEDIA_EJECT
MEDIA_RECORD
MEDIA_REWIND MEDIA_FAST_FORWARD

### Consumer Keys
CALCULATOR
MYCOMPUTER
WWW_SEARCH WWW_HOME WWW_BACK WWW_FORWARD WWW_STOP WWW_REFRESH WWW_FAVORITES
MAIL
COMPOSE
BROWSER_BACK BROWSER_FORWARD BROWSER_REFRESH BROWSER_STOP BROWSER_SEARCH BROWSER_FAVORITES BROWSER_HOME
GAME
CHAT
ZOOM
PRESENTATION
SPREADSHEET
LANGUAGE


# ODKeyScript Virtual Machine
The ODKeyScript virtual machine executes bytecode generated by the compiler.

## Virtual Machine
The VM executes opcodes out of a program memory array, which has a 32-bit "address" space, addressed in bytes. The addresses are zero-based indexes into the array. Any opcodes that modify the program counter will be bounds-checked by the VM implementation and, if the program counter runs off the end of the program memory array, the VM will raise an error and halt.

The program memory array is bounded by the size of the program currently loaded into it. It may reside in RAM or FLASH as long as it can be accessed via a base pointer, indexed with a zero-based program counter.

The VM maintains the following state:
- **Program Counter (PC)**: 32 bit address of current instruction
- **Zero Flag**: 1 bit flag that's set when an instruction results in zero
- **Counter State**: 512 byte counter space (256 2-byte counter variables)
- **Key State**: 6 byte array to track currently-pressed keys
- **Modifier State**: 8 bit bitmap tracking currently-pressed modifiers


## Opcode Definitions
Each opcode is a single byte, followed by any required operands.
```
0x10: KEYDN <mod> <count> <keys> # Press keys with modifiers
                                 # <mod>: 1-byte modifier bitmask
                                 # <count>: 1-byte count of key codes
                                 # <keys>: 0-6 bytes of key codes

0x11: KEYUP <mod> <count> <keys> # Release keys with modifiers
                                 # <mod>: 1-byte modifier bitmask  
                                 # <count>: 1-byte count of key codes
                                 # <keys>: 0-6 bytes of key codes

0x12: KEYUP_ALL                  # Release all currently pressed keys

0x13: WAIT <ms>                  # Wait for a specified number milliseconds
                                 # <ms> 2-byte number of milliseconds to wait

0x14: SET_COUNTER <index> <val>  # Set a counter variable to a value
                                 # <index>: 1-byte index into the counter state space
                                 # <value>: 2-byte value to set the counter to

0x15: DEC <index>                # Decrement a counter variable and set the Zero Flag if the new value is zero
                                 # <index>: 1-byte index into the counter state space

0x16: JNZ <address>              # Set the Program Counter to the specified address if the Zero Flag is not set
                                 # <address> 4-byte address, which is an index into the program's byte array in memory
```
