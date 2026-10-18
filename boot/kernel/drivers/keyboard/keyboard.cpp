#include "uint_definitions.h"
#include "drivers/keyboard/keyboard.hpp"
#include "drivers/keyboard/ps2.hpp"

GlobalKeyboard::GlobalKeyboard() {
    index = 0;
    keyboards[0] = new PS2K;
    keyboards[0]->init();


}