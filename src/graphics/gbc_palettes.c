#include <gb/cgb.h>
#include "gbc_palettes.h"


const uint16_t menu_pal[4] = {
    RGB8(20, 20, 40), RGB8(100, 100, 150), RGB8(200, 200, 255), RGB8(255, 255, 255)
};

const uint16_t gbc_sprite_palettes[32] = {
    // 0: Player (Outline: Black, Primary: Blue, Secondary: Green)
    RGB8(255, 255, 255), RGB8(0, 255, 255), RGB8(125, 255, 0), RGB8(0, 0, 0),
    // 1: Practice checkpoints, growth portal (Green inside: colour 2, White outside: colour 3)
    RGB8(255, 255, 255), RGB8(0, 0, 0), RGB8(125, 255, 0), RGB8(255, 255, 255),
    // 2: Normal Gravity (Outline: Black, Primary: Teal, Secondary: Teal)
    RGB8(255, 255, 255), RGB8(0, 0, 0), RGB8(0, 255, 255), RGB8(0, 255, 255),
    // 3: Inverted Gravity, yellow pads / orb, coins (Outline: Black, colour 2: orange (the coin's
    //    star, the yellow orb's shading; the other users only use colour 3), colour 3: gold)
    RGB8(255, 255, 255), RGB8(0, 0, 0), RGB8(225, 120, 10), RGB8(255, 220, 0),
    // 4: Ship (Outline: Black, Primary: Pink, Secondary: Pink)
    RGB8(255, 255, 255), RGB8(0, 0, 0), RGB8(255, 100, 255), RGB8(255, 100, 255),
    // 5: Ball (Outline: Black, Primary: Red, Secondary: Red)
    RGB8(255, 255, 255), RGB8(0, 0, 0), RGB8(255, 0, 0), RGB8(255, 0, 0),
    // 6: Mirror portals (outline black; entrance ring orange #f7b62a = colour 2, exit ring
    //    blue #3c8cff = colour 3: on CGB the exit tiles are loaded recoloured 2 -> 3)
    RGB8(255, 255, 255), RGB8(0, 0, 0), RGB8(247, 182, 42), RGB8(60, 140, 255),
    // 7: HUD text, same colours as the pause menu's "PAUSED": the % display
    RGB8(255, 255, 255), RGB8(0, 0, 0), RGB8(180, 215, 255), RGB8(255, 255, 255)
};
