#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

extern uint8_t setting_music_enabled;
extern uint8_t setting_sfx_enabled;
extern uint8_t setting_dmg_gradient;
extern uint8_t setting_show_bg_enabled;
extern uint8_t setting_parallax_enabled;
extern uint8_t setting_effects_enabled;
extern uint8_t setting_show_percent;
extern uint8_t setting_old_ship_cam;   // ship camera: 0 = corridor, 1 = the cube dead zone
extern uint8_t setting_auto_checkpoints;
extern uint8_t setting_furnace_music;  // every song with a Furnace version uses it (else only SM, BoT, PG)

#endif // SETTINGS_H
