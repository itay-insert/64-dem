#ifndef DEM_KEY_CODES_H
#define DEM_KEY_CODES_H

// Navigation bytes in the read_key(true, ...) stream.
enum {
    DEM_KEY_UP = 0x80,
    DEM_KEY_DOWN,
    DEM_KEY_RIGHT,
    DEM_KEY_LEFT
};

static inline unsigned char dem_arrow_key(unsigned char set1)
{
    switch (set1) {
        case 0x48: return DEM_KEY_UP;
        case 0x50: return DEM_KEY_DOWN;
        case 0x4D: return DEM_KEY_RIGHT;
        case 0x4B: return DEM_KEY_LEFT;
        default: return 0;
    }
}

#endif
