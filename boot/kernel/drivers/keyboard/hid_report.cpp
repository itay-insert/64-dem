#include "drivers/keyboard/hid.hpp"


extern GlobalKeyboard *globalkeyboard;

static bool contains(const hid_keyboard_report &report, u8 key) {
    for (u8 value : report.keys) if (value == key) return true;
    return false;
}

void HIDK::process_report(const hid_keyboard_report &incoming) {
    // Rollover/ErrorUndefined/POSTFail do not describe a valid held-key set.
    for (u8 value : incoming.keys) if (value >= 1 && value <= 3) return;
    for (u8 value : previous.keys)
        if (value && !contains(incoming, value)) key(value, false, incoming.modifiers);
    for (u8 bit = 0; bit < 8; ++bit)
        if ((previous.modifiers ^ incoming.modifiers) & (1u << bit))
            key(0xe0 + bit, incoming.modifiers & (1u << bit), incoming.modifiers);
    for (u8 i = 0; i < 6; ++i) {
        const u8 value = incoming.keys[i];
        bool duplicate = false;
        for (u8 j = 0; j < i; ++j) if (incoming.keys[j] == value) duplicate = true;
        if (value && !duplicate && !contains(previous, value)) key(value, true, incoming.modifiers);
    }
    previous = incoming;
}

void HIDK::key(u8 usage, bool pressed, u8 modifiers) {
    // HID usages 0x04..0x53 -> PC Set 1. Bit 8 denotes an E0 prefix.
    static const u16 set1[] = {
        0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
        0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c,
        0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,
        0x1c,0x01,0x0e,0x0f,0x39,0x0c,0x0d,0x1a,0x1b,0x2b,0x2b,0x27,
        0x28,0x29,0x33,0x34,0x35,0x3a,
        0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x57,0x58,
        0,0x46,0,0x152,0x147,0x149,0x153,0x14f,0x151,0x14d,0x14b,0x150,0x148,0x45
    };
    static const u16 modifier_codes[] = {0x1d,0x2a,0x38,0x15b,0x11d,0x36,0x138,0x15c};
    static const u16 keypad[] = {0x135,0x37,0x4a,0x4e,0x11c,0x4f,0x50,0x51,0x4b,0x4c,0x4d,0x47,0x48,0x49,0x52,0x53};
    u16 code = 0;
    if (usage >= 4 && usage <= 0x53) code = set1[usage - 4];
    else if (usage >= 0x54 && usage <= 0x63) code = keypad[usage - 0x54];
    else if (usage == 0x64) code = 0x56;
    else if (usage == 0x65) code = 0x15d;
    else if (usage >= 0xe0 && usage <= 0xe7) code = modifier_codes[usage - 0xe0];
    auto append = [this](u8 byte) {
        if (!globalkeyboard)
            return;

        globalkeyboard->proccess_scancode(byte);
    };
    if (usage == 0x46) { // Print Screen
        append(0xe0); append(pressed ? 0x2a : 0xb7);
        append(0xe0); append(pressed ? 0x37 : 0xaa);
    } else if (usage == 0x48) { // Pause has a make sequence only.
        const u8 pause[] = {0xe1,0x1d,0x45,0xe1,0x9d,0xc5};
        if (pressed) for (u8 byte : pause) append(byte);
    } else if (code) {
        if (code & 0x100) append(0xe0);
        append((code & 0xff) | (pressed ? 0 : 0x80));
    }
    char character = 0;
    if (pressed) {
        if (usage == 0x39) caps_lock = !caps_lock;
        if (usage == 0x53) num_lock = !num_lock;
        const bool shift = modifiers & 0x22;
        if (usage >= 4 && usage <= 0x1d)
            character = ((shift ^ caps_lock) ? 'A' : 'a') + usage - 4;
        else if (usage >= 0x1e && usage <= 0x27)
            character = (shift ? "!@#$%^&*()" : "1234567890")[usage - 0x1e];
        else {
            switch (usage) {
            case 0x28: case 0x58: character = '\n'; break;
            case 0x29: character = 0x1b; break;
            case 0x2a: character = '\b'; break;
            case 0x2b: character = '\t'; break;
            case 0x2c: character = ' '; break;
            case 0x2d: character = shift ? '_' : '-'; break;
            case 0x2e: character = shift ? '+' : '='; break;
            case 0x2f: character = shift ? '{' : '['; break;
            case 0x30: character = shift ? '}' : ']'; break;
            case 0x31: case 0x64: character = shift ? '|' : '\\'; break;
            case 0x32: character = shift ? '~' : '#'; break;
            case 0x33: character = shift ? ':' : ';'; break;
            case 0x34: character = shift ? '"' : '\''; break;
            case 0x35: character = shift ? '~' : '`'; break;
            case 0x36: character = shift ? '<' : ','; break;
            case 0x37: character = shift ? '>' : '.'; break;
            case 0x38: character = shift ? '?' : '/'; break;
            case 0x54: character = '/'; break;
            case 0x55: character = '*'; break;
            case 0x56: character = '-'; break;
            case 0x57: character = '+'; break;
            default:
                if (usage >= 0x59 && usage <= 0x63 && (num_lock ^ shift))
                    character = "1234567890."[usage - 0x59];
            }
        }
        if (character) {
            if (!globalkeyboard)
                return;

            globalkeyboard->proccess_ascii(character);
        }
    }
    
}
