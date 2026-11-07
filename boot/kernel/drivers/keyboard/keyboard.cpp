#include "uint_definitions.h"
#include "drivers/keyboard/keyboard.hpp"
#include "drivers/keyboard/ps2.hpp"
#include "drivers/keyboard/hid.hpp"

GlobalKeyboard::GlobalKeyboard() {
    index = 0;
    keyboards[0] = new PS2K;
    keyboards[0]->init();

    keyboards[1] = new HIDK;
    keyboards[1]->init();
    
}