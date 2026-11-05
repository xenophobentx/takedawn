/*
 * race_module.h - "is this module a race": one place for the game modules
 * a race runs in, so the controls gate, high refresh and the autopilot
 * cannot drift apart again (BO-134).
 *
 * The current module is [0x4D5370]. A race runs in the game module (vtable
 * 0x3A9ACC, world + 0x2D430) offline, and in the online game module
 * (0x3A9E5C, world + 0x2D7C8) in an Xbox Live race. The front end is
 * 0x3A9E7C. The two-player (split-screen) stage module is 0x3A939C (world +
 * 0x2D5F8; verified in BO-310, takedawn PR #102, which found the controls
 * gate treating split-screen races as menus).
 */
#ifndef BO3_RACE_MODULE_H
#define BO3_RACE_MODULE_H

#include <stdint.h>

#include "region.h"

#define BO3_MODULE_CUR        BO3_VA(0x004D5370u, 0x004D5690u, 0x004D5730u)   /* the current module */
#define BO3_VT_GAME           BO3_VA(0x003A9ACCu, 0x003AA078u, 0x003AA118u)   /* the game module (offline races) */
#define BO3_VT_GAME_ONLINE    BO3_VA(0x003A9E5Cu, 0x003AA054u, 0x003AA0F4u)   /* the online game module (Xbox Live races) */
#define BO3_VT_GAME_2P        BO3_VA(0x003A939Cu, 0x003A9BDCu, 0x003A9C7Cu)   /* the two-player stage module (split screen) */
#define BO3_VT_FRONT_END      BO3_VA(0x003A9E7Cu, 0x003AA098u, 0x003AA138u)   /* the front end */

/* vt: the vtable word of the current module */
static inline int bo3_race_module_vt(uint32_t vt)
{
    return vt == BO3_VT_GAME || vt == BO3_VT_GAME_ONLINE || vt == BO3_VT_GAME_2P;
}

#endif
