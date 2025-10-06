#ifndef USB_KEYBOARD_KEYS_H
#define USB_KEYBOARD_KEYS_H

/**
 * @file usb_keyboard_keys.h
 * @brief HID Key Codes for USB Keyboard
 * 
 * This file contains all the standard HID key codes that can be used
 * with the USB keyboard module. These codes follow the USB HID Usage
 * Tables for Consumer Page (0x0C) and Keyboard/Keypad Page (0x07).
 */

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ALPHABETIC KEYS
// ============================================================================
#define KEY_A     0x04
#define KEY_B     0x05
#define KEY_C     0x06
#define KEY_D     0x07
#define KEY_E     0x08
#define KEY_F     0x09
#define KEY_G     0x0A
#define KEY_H     0x0B
#define KEY_I     0x0C
#define KEY_J     0x0D
#define KEY_K     0x0E
#define KEY_L     0x0F
#define KEY_M     0x10
#define KEY_N     0x11
#define KEY_O     0x12
#define KEY_P     0x13
#define KEY_Q     0x14
#define KEY_R     0x15
#define KEY_S     0x16
#define KEY_T     0x17
#define KEY_U     0x18
#define KEY_V     0x19
#define KEY_W     0x1A
#define KEY_X     0x1B
#define KEY_Y     0x1C
#define KEY_Z     0x1D

// ============================================================================
// NUMERIC KEYS
// ============================================================================
#define KEY_1     0x1E
#define KEY_2     0x1F
#define KEY_3     0x20
#define KEY_4     0x21
#define KEY_5     0x22
#define KEY_6     0x23
#define KEY_7     0x24
#define KEY_8     0x25
#define KEY_9     0x26
#define KEY_0     0x27

// ============================================================================
// FUNCTION KEYS
// ============================================================================
#define KEY_F1    0x3A
#define KEY_F2    0x3B
#define KEY_F3    0x3C
#define KEY_F4    0x3D
#define KEY_F5    0x3E
#define KEY_F6    0x3F
#define KEY_F7    0x40
#define KEY_F8    0x41
#define KEY_F9    0x42
#define KEY_F10   0x43
#define KEY_F11   0x44
#define KEY_F12   0x45

// ============================================================================
// SPECIAL KEYS
// ============================================================================
#define KEY_ENTER     0x28
#define KEY_ESCAPE    0x29
#define KEY_BACKSPACE 0x2A
#define KEY_TAB       0x2B
#define KEY_SPACE     0x2C
#define KEY_MINUS     0x2D
#define KEY_EQUAL     0x2E
#define KEY_LEFTBRACE 0x2F
#define KEY_RIGHTBRACE 0x30
#define KEY_BACKSLASH 0x31
#define KEY_SEMICOLON 0x33
#define KEY_APOSTROPHE 0x34
#define KEY_GRAVE     0x35
#define KEY_COMMA     0x36
#define KEY_DOT       0x37
#define KEY_SLASH     0x38
#define KEY_CAPSLOCK  0x39

// ============================================================================
// ARROW KEYS
// ============================================================================
#define KEY_UP    0x52
#define KEY_DOWN  0x51
#define KEY_LEFT  0x50
#define KEY_RIGHT 0x4F

// ============================================================================
// MODIFIER KEYS
// ============================================================================
#define KEY_LEFTCTRL   0xE0
#define KEY_LEFTSHIFT  0xE1
#define KEY_LEFTALT    0xE2
#define KEY_LEFTMETA   0xE3
#define KEY_RIGHTCTRL  0xE4
#define KEY_RIGHTSHIFT 0xE5
#define KEY_RIGHTALT   0xE6
#define KEY_RIGHTMETA  0xE7

// ============================================================================
// MEDIA KEYS
// ============================================================================
#define KEY_MEDIA_PLAY_PAUSE    0xE8
#define KEY_MEDIA_STOP          0xE9
#define KEY_MEDIA_PREVIOUS      0xEA
#define KEY_MEDIA_NEXT         0xEB
#define KEY_MEDIA_VOLUME_UP     0xEC
#define KEY_MEDIA_VOLUME_DOWN   0xED
#define KEY_MEDIA_MUTE          0xEE

// ============================================================================
// SYSTEM KEYS
// ============================================================================
#define KEY_SYSTEM_POWER        0x81
#define KEY_SYSTEM_SLEEP        0x82
#define KEY_SYSTEM_WAKE         0x83

// ============================================================================
// NUMPAD KEYS
// ============================================================================
#define KEY_NUMLOCK    0x53
#define KEY_KP_SLASH   0x54
#define KEY_KP_ASTERISK 0x55
#define KEY_KP_MINUS   0x56
#define KEY_KP_PLUS    0x57
#define KEY_KP_ENTER   0x58
#define KEY_KP_1       0x59
#define KEY_KP_2       0x5A
#define KEY_KP_3       0x5B
#define KEY_KP_4       0x5C
#define KEY_KP_5       0x5D
#define KEY_KP_6       0x5E
#define KEY_KP_7       0x5F
#define KEY_KP_8       0x60
#define KEY_KP_9       0x61
#define KEY_KP_0       0x62
#define KEY_KP_DOT     0x63

// ============================================================================
// UNCOMMON BUT USEFUL KEYS
// ============================================================================

// International keys
#define KEY_NONUS_HASH       0x32
#define KEY_NONUS_BACKSLASH   0x64

// Japanese keys
#define KEY_HENKAN           0x8A
#define KEY_MUHENKAN         0x8B
#define KEY_KATAKANAHIRAGANA  0x8C

// Korean keys
#define KEY_HANGEUL          0x90
#define KEY_HANJA            0x91

// Lock keys
#define KEY_SCROLLLOCK       0x47
#define KEY_PAUSE            0x48

// Insert/Delete cluster
#define KEY_INSERT           0x49
#define KEY_HOME             0x4A
#define KEY_PAGEUP           0x4B
#define KEY_DELETE           0x4C
#define KEY_END              0x4D
#define KEY_PAGEDOWN         0x4E

// Function keys F13-F24 (less common)
#define KEY_F13              0x68
#define KEY_F14              0x69
#define KEY_F15              0x6A
#define KEY_F16              0x6B
#define KEY_F17              0x6C
#define KEY_F18              0x6D
#define KEY_F19              0x6E
#define KEY_F20              0x6F
#define KEY_F21              0x70
#define KEY_F22              0x71
#define KEY_F23              0x72
#define KEY_F24              0x73

// Application keys
#define KEY_APPLICATION      0x65
#define KEY_MENU             0x76

// Calculator keys
#define KEY_CALCULATOR       0xA1
#define KEY_MYCOMPUTER       0xA2
#define KEY_WWW_SEARCH       0xA3
#define KEY_WWW_HOME         0xA4
#define KEY_WWW_BACK         0xA5
#define KEY_WWW_FORWARD      0xA6
#define KEY_WWW_STOP         0xA7
#define KEY_WWW_REFRESH      0xA8
#define KEY_WWW_FAVORITES    0xA9

// Email keys
#define KEY_MAIL             0xAA
#define KEY_COMPOSE          0xAB

// Browser keys
#define KEY_BROWSER_BACK     0xAC
#define KEY_BROWSER_FORWARD  0xAD
#define KEY_BROWSER_REFRESH  0xAE
#define KEY_BROWSER_STOP     0xAF
#define KEY_BROWSER_SEARCH   0xB0
#define KEY_BROWSER_FAVORITES 0xB1
#define KEY_BROWSER_HOME     0xB2

// Media keys (extended)
#define KEY_MEDIA_EJECT      0xB3
#define KEY_MEDIA_RECORD     0xB4
#define KEY_MEDIA_REWIND     0xB5
#define KEY_MEDIA_FAST_FORWARD 0xB6

// Gaming keys
#define KEY_GAME             0xB7
#define KEY_CHAT             0xB8
#define KEY_ZOOM             0xB9

// Presentation keys
#define KEY_PRESENTATION     0xBA
#define KEY_SPREADSHEET      0xBB

// Language keys
#define KEY_LANGUAGE         0xBC

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

/**
 * @brief Check if a key code is a modifier key
 * @param key_code The key code to check
 * @return true if it's a modifier key, false otherwise
 */
#define IS_MODIFIER_KEY(key_code) ((key_code) >= 0xE0 && (key_code) <= 0xE7)

/**
 * @brief Check if a key code is a media key
 * @param key_code The key code to check
 * @return true if it's a media key, false otherwise
 */
#define IS_MEDIA_KEY(key_code) ((key_code) >= 0xE8 && (key_code) <= 0xEE)

/**
 * @brief Check if a key code is a system key
 * @param key_code The key code to check
 * @return true if it's a system key, false otherwise
 */
#define IS_SYSTEM_KEY(key_code) ((key_code) >= 0x81 && (key_code) <= 0x83)

#ifdef __cplusplus
}
#endif

#endif // USB_KEYBOARD_KEYS_H
