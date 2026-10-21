#pragma once


#include "drivers/keyboard/keyboard.hpp"

class PS2K : public keyboard {

    public:
        int scancode_top;
        int ascii_top;
        char vendor[4];
        char buffer[256];
        char ascii[256];
        void init() override;
};

bool ps2_controller_init();