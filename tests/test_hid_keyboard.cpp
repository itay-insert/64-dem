#include <assert.h>
#include <stdio.h>
#include "drivers/keyboard/hid.hpp"
#include "drivers/keyboard/ps2.hpp"
#include "drivers/acpiPM.hpp"
GlobalKeyboard *globalkeyboard = nullptr;
u64 acpiadd = 0;
// Hardware discovery is excluded from this report decoder test.
void HIDK::init() {}
void PS2K::init() {}
void AcpiPM::AcpiReset() { assert(false); }
void AcpiPM::AcpiShutDown() { assert(false); }
int main() {
    GlobalKeyboard buffers;
    globalkeyboard = &buffers;
    HIDK hid;
    GlobalKeyboard *base = globalkeyboard;
    hid.process_report({0, 0, {4}}); // A press
    assert(base->scancode_top == 1 && u8(base->buffer[0]) == 0x1e);
    assert(base->ascii_top == 1 && base->ascii[0] == 'a');
    hid.process_report({0, 0, {4}}); // Held, no duplicate press
    assert(base->scancode_top == 1);
    hid.process_report({0, 0, {1,1,1,1,1,1}}); // Rollover preserves held state
    assert(base->scancode_top == 1);
    hid.process_report({0, 0, {}});
    assert(u8(base->buffer[1]) == 0x9e && base->ascii_top == 1);
    hid.process_report({2, 0, {4,5}}); // Shift + A + B
    assert(base->ascii[1] == 'A' && base->ascii[2] == 'B');
    int count = base->scancode_top;
    hid.process_report({2, 0, {5,4}}); // Slots may be reordered
    assert(base->scancode_top == count);
    hid.process_report({0, 0, {}});
    hid.process_report({0, 0, {0x39}}); // Caps lock
    hid.process_report({0, 0, {}});
    hid.process_report({0, 0, {4,4}}); // Duplicate usage emits once
    assert(base->ascii[3] == 'A' && base->ascii_top == 4);
    hid.process_report({0, 0, {}});
    hid.process_report({2, 0, {4}}); // Caps + Shift => lowercase
    assert(base->ascii[4] == 'a');
    hid.process_report({0, 0, {}});
    count = base->scancode_top;
    hid.process_report({0, 0, {0x4f}}); // Right arrow, E0 4D
    assert(u8(base->buffer[count]) == 0xe0 && u8(base->buffer[count+1]) == 0x4d);
    assert(base->ascii_top == 5);
    hid.process_report({0, 0, {}});
    hid.process_report({0, 0, {0x53}}); // Num lock
    hid.process_report({0, 0, {0x59}});
    assert(base->ascii[5] == '1');
    hid.process_report({0, 0, {}});
    base->scancode_top = 255;
    base->ascii_top = 255;
    hid.process_report({0, 0, {4}});
    assert(base->scancode_top == 0 && base->ascii_top == 0);
    assert(base->ascii[255] == 'A');
    puts("HID keyboard report tests passed");
}
