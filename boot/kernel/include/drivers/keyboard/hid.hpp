#pragma once
#include "drivers/keyboard/keyboard.hpp"
#include "drivers/usb/usb.hpp"

struct hid_keyboard_report {
    u8 modifiers;
    u8 reserved;
    u8 keys[6];
};
static_assert(sizeof(hid_keyboard_report) == 8, "Boot keyboard report size");

class HIDK : public keyboard {
    usb_controller *controller = nullptr;
    hid_keyboard_report previous = {};
    bool caps_lock = false;
    bool num_lock = false;
    bool received_report = false;
    bool warned_short_report = false;
    static void completion(void *, const usb_transfer_result &, const u8 *);
    void key(u8 usage, bool pressed, u8 modifiers);
public:
    // Foreground decoder entry; accepts one validated boot-protocol report.
    void process_report(const hid_keyboard_report &);
    bool ready = false;
    void init() override;
};
