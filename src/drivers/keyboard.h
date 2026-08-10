#ifndef _DRIVERS_KEYBOARD_H
#define _DRIVERS_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KEYBOARD_DATA_PORT      0x60
#define KEYBOARD_STATUS_PORT    0x64
#define KEYBOARD_COMMAND_PORT   0x64

#define KEYBOARD_IRQ            1
#define KEYBOARD_INT_VECTOR     0x21

#define KEYBOARD_STATUS_OUTPUT_FULL  0x01
#define KEYBOARD_STATUS_INPUT_EMPTY  0x02

#define SCANCODE_SET_1  1
#define SCANCODE_SET_2  2
#define SCANCODE_SET_3  3

#define SCANCODE_EXTENDED  0xE0
#define SCANCODE_RELEASE   0xF0

#define SCANCODE_UP    0x48
#define SCANCODE_DOWN  0x50
#define SCANCODE_LEFT  0x4B
#define SCANCODE_RIGHT 0x4D

#define SCANCODE_F1   0x3B
#define SCANCODE_F2   0x3C
#define SCANCODE_F3   0x3D
#define SCANCODE_F4   0x3E
#define SCANCODE_F5   0x3F
#define SCANCODE_F6   0x40
#define SCANCODE_F7   0x41
#define SCANCODE_F8   0x42
#define SCANCODE_F9   0x43
#define SCANCODE_F10  0x44
#define SCANCODE_F11  0x57
#define SCANCODE_F12  0x58

#define SCANCODE_LSHIFT         0x2A
#define SCANCODE_RSHIFT         0x36
#define SCANCODE_LSHIFT_RELEASE 0xAA
#define SCANCODE_RSHIFT_RELEASE 0xB6
#define SCANCODE_LCTRL          0x1D
#define SCANCODE_RCTRL          0x9D
#define SCANCODE_LALT           0x38
#define SCANCODE_RALT           0xB8
#define SCANCODE_CAPSLOCK       0x3A
#define SCANCODE_NUMLOCK        0x45
#define SCANCODE_SCROLLLOCK     0x46

typedef enum {
    KEY_NONE = 0,
    KEY_ESC = 1,
    KEY_1 = 2, KEY_2 = 3, KEY_3 = 4, KEY_4 = 5, KEY_5 = 6,
    KEY_6 = 7, KEY_7 = 8, KEY_8 = 9, KEY_9 = 10, KEY_0 = 11,
    KEY_MINUS = 12, KEY_EQUALS = 13, KEY_BACKSPACE = 14,
    KEY_TAB = 15,
    KEY_Q = 16, KEY_W = 17, KEY_E = 18, KEY_R = 19, KEY_T = 20,
    KEY_Y = 21, KEY_U = 22, KEY_I = 23, KEY_O = 24, KEY_P = 25,
    KEY_LEFTBRACE = 26, KEY_RIGHTBRACE = 27, KEY_ENTER = 28,
    KEY_LEFTCTRL = 29,
    KEY_A = 30, KEY_S = 31, KEY_D = 32, KEY_F = 33, KEY_G = 34,
    KEY_H = 35, KEY_J = 36, KEY_K = 37, KEY_L = 38,
    KEY_SEMICOLON = 39, KEY_QUOTE = 40, KEY_TILDE = 41,
    KEY_LEFTSHIFT = 42, KEY_BACKSLASH = 43,
    KEY_Z = 44, KEY_X = 45, KEY_C = 46, KEY_V = 47,
    KEY_B = 48, KEY_N = 49, KEY_M = 50,
    KEY_COMMA = 51, KEY_DOT = 52, KEY_SLASH = 53,
    KEY_RIGHTSHIFT = 54,
    KEY_STAR = 55,
    KEY_LEFTALT = 56, KEY_SPACE = 57,
    KEY_CAPSLOCK = 58,
    KEY_F1 = 59, KEY_F2 = 60, KEY_F3 = 61, KEY_F4 = 62,
    KEY_F5 = 63, KEY_F6 = 64, KEY_F7 = 65, KEY_F8 = 66,
    KEY_F9 = 67, KEY_F10 = 68,
    KEY_NUMLOCK = 69, KEY_SCROLLLOCK = 70,

    // 扩展键（>= 128，避免与普通键冲突）
    KEY_UP    = 128,
    KEY_DOWN  = 129,
    KEY_LEFT  = 130,
    KEY_RIGHT = 131,
    KEY_HOME  = 132,
    KEY_END   = 133,
    KEY_PAGEUP   = 134,
    KEY_PAGEDOWN = 135,
    KEY_INSERT   = 136,
    KEY_DELETE   = 137,

    KEY_KP_ENTER  = 138,
    KEY_KP_SLASH  = 139,
    KEY_KP_STAR   = 140,
    KEY_KP_MINUS  = 141,
    KEY_KP_PLUS   = 142,
    KEY_KP_DOT    = 143,
    KEY_KP_0 = 144, KEY_KP_1 = 145, KEY_KP_2 = 146,
    KEY_KP_3 = 147, KEY_KP_4 = 148, KEY_KP_5 = 149,
    KEY_KP_6 = 150, KEY_KP_7 = 151, KEY_KP_8 = 152,
    KEY_KP_9 = 153,

    KEY_RIGHTALT = 154,
    KEY_RIGHTCTRL = 155,

    KEY_F11 = 156, KEY_F12 = 157,

    KEY_PRINTSCREEN = 158,
    KEY_PAUSE       = 159,

    KEY_LEFTGUI  = 160,
    KEY_RIGHTGUI = 161,
    KEY_APPS     = 162,

    KEY_SLEEP = 163,
    KEY_POWER = 164,
    KEY_WAKE  = 165,

    KEY_PREVTRACK  = 166,
    KEY_NEXTTRACK  = 167,
    KEY_MUTE       = 168,
    KEY_CALCULATOR = 169,
    KEY_PLAY       = 170,
    KEY_STOP       = 171,
    KEY_VOLUMEDOWN = 172,
    KEY_VOLUMEUP   = 173,

    KEY_WWW_HOME     = 174,
    KEY_WWW_SEARCH   = 175,
    KEY_WWW_FAV      = 176,
    KEY_WWW_REFRESH  = 177,
    KEY_WWW_STOP     = 178,
    KEY_WWW_FORWARD  = 179,
    KEY_WWW_BACK     = 180,
    KEY_MYCOMPUTER   = 181,
    KEY_EMAIL        = 182,
    KEY_MEDIASELECT  = 183
} KeyCode;

typedef struct {
    KeyCode key;
    char ascii;
    bool pressed;
    bool shift;
    bool ctrl;
    bool alt;
    bool capsLock;
} KeyEvent;

void keyboardInit(void);
char keyboardGetChar(void);
bool keyboardHasChar(void);
void keyboardSetHandler(void (*handler)(KeyEvent*));
void keyboardSetScanCodeSet(uint8_t set);

#endif