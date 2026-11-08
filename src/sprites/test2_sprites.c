#pragma bank 123
#include "assets.h"

BANKREF(test2_sp)

// Extracted 6 objects from SP layer
const SpDef test2_sp[] = {
    {2304, 960, 65},
    {2336, 960, 66},
    {2368, 960, 65},
    {2400, 960, 66},
    {2432, 944, 73},
    {4080, 880, 15},
    {0xFFFF, 0, 0}
};

// Extracted 1 gameplay-critical objects for DMG mode
const SpDef test2_sp_dmg[] = {
    {4080, 880, 15},
    {0xFFFF, 0, 0}
};
