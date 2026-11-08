#pragma bank 122
#include "assets.h"

BANKREF(test_sp)

// Extracted 1 objects from SP layer
const SpDef test_sp[] = {
    {2032, 880, 15},
    {0xFFFF, 0, 0}
};

// Extracted 1 gameplay-critical objects for DMG mode
const SpDef test_sp_dmg[] = {
    {2032, 880, 15},
    {0xFFFF, 0, 0}
};
