#pragma once


class keyboard {
    public:
        int scancode_top = 0;
        char vendor[4];
        char buffer[256];
        char ascii[256];
        virtual void init();
};


class GlobalKeyboard {
    public:
        keyboard *keyboards[2];
        int index;
        GlobalKeyboard();
};