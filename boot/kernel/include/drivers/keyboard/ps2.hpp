#pragma once


#include "drivers/keyboard/keyboard.hpp"

class PS2K : public keyboard {

    public:
        void init() override;
};

bool ps2_controller_init();