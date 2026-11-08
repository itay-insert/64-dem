#pragma once
#include "uint_definitions.h"

class keyboard {
    public:
        char vendor[4] = {};
        virtual void init() {}
};


class GlobalKeyboard {
    public:
        keyboard *keyboards[2] = {};
        int scancode_top = 0;
        int ascii_top = 0;
        char buffer[256] = {};
        char ascii[256] = {};
        void proccess_scancode(u8 scancode);
        void proccess_ascii(char ch);
        GlobalKeyboard();
};