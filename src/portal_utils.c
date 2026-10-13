#include <gb/gb.h>
#include "assets.h"
#include "player.h"
#include "gameplay.h"

// ID Mappings from SP Layer
#define OBJ_CUBE_PORTAL   0
#define OBJ_SHIP_PORTAL   1
#define OBJ_ORB_BLUE      5
#define OBJ_ORB_PINK      6
#define OBJ_GRAVITY_DOWN  8
#define OBJ_GRAVITY_UP    9
#define OBJ_PAD_YELLOW    10
#define OBJ_ORB_YELLOW    11
#define OBJ_PAD_YELLOW_UP 12
#define OBJ_PAD_BLUE      13
#define OBJ_PAD_BLUE_UP   14
#define OBJ_PAD_PINK      37
#define OBJ_LEVEL_END     15
#define OBJ_MIRROR_PORTAL 126
#define OBJ_MIRROR_EXIT   121

extern SpCache active_sp;   // the only cache (see sp_draw.c)

void sp_cache_load(uint8_t sp_bank, const SpDef *sp_list, uint16_t cam_px,
                   SpCache *cache_arg, uint16_t *stream_idx, uint16_t map_h) {
    uint8_t count = 0;
    uint8_t save_bank = _current_bank;
    uint16_t idx = *stream_idx;
    uint16_t ahead = cam_px + 176u;
    const SpDef *sp;
    (void)cache_arg; (void)map_h;

    if (sp_bank == 0 || sp_list == 0) return;
    SWITCH_ROM(sp_bank);
    while (count < MAX_ACTIVE_SP_OBJECTS && active_sp.active[count]) count++;

    // Walk the list with a pointer (indexing 5-byte SpDefs recomputes idx * 5 per access)
    for (sp = &sp_list[idx]; sp->x != 0xFFFF; sp++, idx++) {
        uint16_t object_x = sp->x;

        // sorted by x: the rest is further ahead
        if (object_x > ahead) break;

        if (object_x + 48u < cam_px) continue;

        uint8_t obj_id = sp->obj;

        // DMG never draws decorations or ground colour triggers: skip them here
        if (_cpu != CGB_TYPE) {
            if ((obj_id >= 38 && obj_id < 64) || obj_id == 74 || (obj_id >= 192 && obj_id <= 239)) continue;
        }

        // cache nearly full: keep room for the gameplay objects
        if (count >= MAX_ACTIVE_SP_OBJECTS - 8 && ((obj_id >= 38 && obj_id < 64) || obj_id == 74)) continue;

        if (count >= MAX_ACTIVE_SP_OBJECTS) break;

        active_sp.obj[count] = obj_id;
        active_sp.px[count] = object_x;
        active_sp.py[count] = sp->y;
        active_sp.active[count] = 1;
        active_sp.activated[count] = 0;
        count++;
    }
    *stream_idx = idx;
    SWITCH_ROM(save_bank);
}
