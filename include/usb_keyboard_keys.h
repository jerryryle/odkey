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

/**
 * @brief HID Key Codes Enumeration
 * 
 * All standard HID key codes organized by category.
 * Values match the USB HID Usage Tables specification.
 */
typedef enum {
    // ============================================================================
    // ALPHABETIC KEYS
    // ============================================================================
    KEY_A = 0x04,
    KEY_B = 0x05,
    KEY_C = 0x06,
    KEY_D = 0x07,
    KEY_E = 0x08,
    KEY_F = 0x09,
    KEY_G = 0x0A,
    KEY_H = 0x0B,
    KEY_I = 0x0C,
    KEY_J = 0x0D,
    KEY_K = 0x0E,
    KEY_L = 0x0F,
    KEY_M = 0x10,
    KEY_N = 0x11,
    KEY_O = 0x12,
    KEY_P = 0x13,
    KEY_Q = 0x14,
    KEY_R = 0x15,
    KEY_S = 0x16,
    KEY_T = 0x17,
    KEY_U = 0x18,
    KEY_V = 0x19,
    KEY_W = 0x1A,
    KEY_X = 0x1B,
    KEY_Y = 0x1C,
    KEY_Z = 0x1D,

    // ============================================================================
    // NUMERIC KEYS
    // ============================================================================
    KEY_1 = 0x1E,
    KEY_2 = 0x1F,
    KEY_3 = 0x20,
    KEY_4 = 0x21,
    KEY_5 = 0x22,
    KEY_6 = 0x23,
    KEY_7 = 0x24,
    KEY_8 = 0x25,
    KEY_9 = 0x26,
    KEY_0 = 0x27,

    // ============================================================================
    // SPECIAL KEYS
    // ============================================================================
    KEY_ENTER = 0x28,
    KEY_ESCAPE = 0x29,
    KEY_BACKSPACE = 0x2A,
    KEY_TAB = 0x2B,
    KEY_SPACE = 0x2C,
    KEY_MINUS = 0x2D,
    KEY_EQUAL = 0x2E,
    KEY_LEFTBRACE = 0x2F,
    KEY_RIGHTBRACE = 0x30,
    KEY_BACKSLASH = 0x31,
    KEY_NONUS_HASH = 0x32,
    KEY_SEMICOLON = 0x33,
    KEY_APOSTROPHE = 0x34,
    KEY_GRAVE = 0x35,
    KEY_COMMA = 0x36,
    KEY_DOT = 0x37,
    KEY_SLASH = 0x38,
    KEY_CAPSLOCK = 0x39,

    // ============================================================================
    // FUNCTION KEYS
    // ============================================================================
    KEY_F1 = 0x3A,
    KEY_F2 = 0x3B,
    KEY_F3 = 0x3C,
    KEY_F4 = 0x3D,
    KEY_F5 = 0x3E,
    KEY_F6 = 0x3F,
    KEY_F7 = 0x40,
    KEY_F8 = 0x41,
    KEY_F9 = 0x42,
    KEY_F10 = 0x43,
    KEY_F11 = 0x44,
    KEY_F12 = 0x45,

    // ============================================================================
    // LOCK KEYS
    // ============================================================================
    KEY_SCROLLLOCK = 0x47,
    KEY_PAUSE = 0x48,

    // ============================================================================
    // INSERT/DELETE CLUSTER
    // ============================================================================
    KEY_INSERT = 0x49,
    KEY_HOME = 0x4A,
    KEY_PAGEUP = 0x4B,
    KEY_DELETE = 0x4C,
    KEY_END = 0x4D,
    KEY_PAGEDOWN = 0x4E,

    // ============================================================================
    // ARROW KEYS
    // ============================================================================
    KEY_RIGHT = 0x4F,
    KEY_LEFT = 0x50,
    KEY_DOWN = 0x51,
    KEY_UP = 0x52,

    // ============================================================================
    // NUMPAD KEYS
    // ============================================================================
    KEY_NUMLOCK = 0x53,
    KEY_KP_SLASH = 0x54,
    KEY_KP_ASTERISK = 0x55,
    KEY_KP_MINUS = 0x56,
    KEY_KP_PLUS = 0x57,
    KEY_KP_ENTER = 0x58,
    KEY_KP_1 = 0x59,
    KEY_KP_2 = 0x5A,
    KEY_KP_3 = 0x5B,
    KEY_KP_4 = 0x5C,
    KEY_KP_5 = 0x5D,
    KEY_KP_6 = 0x5E,
    KEY_KP_7 = 0x5F,
    KEY_KP_8 = 0x60,
    KEY_KP_9 = 0x61,
    KEY_KP_0 = 0x62,
    KEY_KP_DOT = 0x63,
    KEY_NONUS_BACKSLASH = 0x64,

    // ============================================================================
    // APPLICATION KEYS
    // ============================================================================
    KEY_APPLICATION = 0x65,

    // ============================================================================
    // EXTENDED FUNCTION KEYS (F13-F24)
    // ============================================================================
    KEY_F13 = 0x68,
    KEY_F14 = 0x69,
    KEY_F15 = 0x6A,
    KEY_F16 = 0x6B,
    KEY_F17 = 0x6C,
    KEY_F18 = 0x6D,
    KEY_F19 = 0x6E,
    KEY_F20 = 0x6F,
    KEY_F21 = 0x70,
    KEY_F22 = 0x71,
    KEY_F23 = 0x72,
    KEY_F24 = 0x73,

    // ============================================================================
    // MENU KEY
    // ============================================================================
    KEY_MENU = 0x76,

    // ============================================================================
    // INTERNATIONAL KEYS
    // ============================================================================
    KEY_HENKAN = 0x8A,
    KEY_MUHENKAN = 0x8B,
    KEY_KATAKANAHIRAGANA = 0x8C,
    KEY_HANGEUL = 0x90,
    KEY_HANJA = 0x91,

    // ============================================================================
    // SYSTEM KEYS
    // ============================================================================
    KEY_SYSTEM_POWER = 0x81,
    KEY_SYSTEM_SLEEP = 0x82,
    KEY_SYSTEM_WAKE = 0x83,

    // ============================================================================
    // MODIFIER KEYS
    // ============================================================================
    KEY_LEFTCTRL = 0xE0,
    KEY_LEFTSHIFT = 0xE1,
    KEY_LEFTALT = 0xE2,
    KEY_LEFTMETA = 0xE3,
    KEY_RIGHTCTRL = 0xE4,
    KEY_RIGHTSHIFT = 0xE5,
    KEY_RIGHTALT = 0xE6,
    KEY_RIGHTMETA = 0xE7,

    // ============================================================================
    // MEDIA KEYS
    // ============================================================================
    KEY_MEDIA_PLAY_PAUSE = 0xE8,
    KEY_MEDIA_STOP = 0xE9,
    KEY_MEDIA_PREVIOUS = 0xEA,
    KEY_MEDIA_NEXT = 0xEB,
    KEY_MEDIA_VOLUME_UP = 0xEC,
    KEY_MEDIA_VOLUME_DOWN = 0xED,
    KEY_MEDIA_MUTE = 0xEE,
    KEY_MEDIA_EJECT = 0xB3,
    KEY_MEDIA_RECORD = 0xB4,
    KEY_MEDIA_REWIND = 0xB5,
    KEY_MEDIA_FAST_FORWARD = 0xB6,

    // ============================================================================
    // CONSUMER KEYS
    // ============================================================================
    KEY_CALCULATOR = 0xA1,
    KEY_MYCOMPUTER = 0xA2,
    KEY_WWW_SEARCH = 0xA3,
    KEY_WWW_HOME = 0xA4,
    KEY_WWW_BACK = 0xA5,
    KEY_WWW_FORWARD = 0xA6,
    KEY_WWW_STOP = 0xA7,
    KEY_WWW_REFRESH = 0xA8,
    KEY_WWW_FAVORITES = 0xA9,
    KEY_MAIL = 0xAA,
    KEY_COMPOSE = 0xAB,
    KEY_BROWSER_BACK = 0xAC,
    KEY_BROWSER_FORWARD = 0xAD,
    KEY_BROWSER_REFRESH = 0xAE,
    KEY_BROWSER_STOP = 0xAF,
    KEY_BROWSER_SEARCH = 0xB0,
    KEY_BROWSER_FAVORITES = 0xB1,
    KEY_BROWSER_HOME = 0xB2,
    KEY_GAME = 0xB7,
    KEY_CHAT = 0xB8,
    KEY_ZOOM = 0xB9,
    KEY_PRESENTATION = 0xBA,
    KEY_SPREADSHEET = 0xBB,
    KEY_LANGUAGE = 0xBC
} hid_keycode_t;

/**
 * @brief Keyboard Modifier Bits
 * 
 * These are bit flags that can be combined using bitwise OR.
 * Used for modifier keys in HID reports.
 */
typedef enum {
    KEY_MODIFIER_LEFTCTRL   = (1UL << 0), ///< Left Control
    KEY_MODIFIER_LEFTSHIFT  = (1UL << 1), ///< Left Shift
    KEY_MODIFIER_LEFTALT    = (1UL << 2), ///< Left Alt
    KEY_MODIFIER_LEFTGUI    = (1UL << 3), ///< Left Windows/Command
    KEY_MODIFIER_RIGHTCTRL  = (1UL << 4), ///< Right Control
    KEY_MODIFIER_RIGHTSHIFT = (1UL << 5), ///< Right Shift
    KEY_MODIFIER_RIGHTALT   = (1UL << 6), ///< Right Alt
    KEY_MODIFIER_RIGHTGUI   = (1UL << 7)  ///< Right Windows/Command
} hid_key_modifiers_t;


#ifdef __cplusplus
}
#endif

#endif // USB_KEYBOARD_KEYS_H