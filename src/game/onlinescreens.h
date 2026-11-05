/*
 * onlinescreens.h - the front-end screens the online navigator knows, by
 * (flow state offset, page) for all three discs (BO-352, step 1).
 *
 * onlinebot.c used to keep this table (NTSC-U only, the debug navigator).
 * The player navigator (onlinenav.c) must name a screen on the PAL and
 * PAL-ENSV builds too, so the table moved here with every page address
 * written three times. onlinebot.c uses it through bo3_online_screen_for,
 * which reads the build's region (region.h) as the rest of the port does.
 *
 * A screen is:
 *   flow      the front-end state machine's (0x567170) current state, as an
 *             offset from the machine's base. The machine's layout is the
 *             same program rebuilt, so the offsets do not change by region;
 *             only the absolute page objects do.
 *   ntsc/pal/ensv   the page object (the static the front end stores after
 *             sub_00063670), one per disc. 0 means "any page of that state"
 *             (the waits: loading, boot, race, results, autosave, points).
 *   sc        the screen (onlinebot.h's OB_SC_*).
 *
 * The first match wins, and the page-0 rows come first per state, so a page
 * the table names is found before a page-0 fallback of the same state.
 */
#ifndef BO3_ONLINESCREENS_H
#define BO3_ONLINESCREENS_H

#include <stdint.h>

typedef struct {
    uint32_t flow;              /* offset in the machine 0x567170 */
    uint32_t ntsc, pal, ensv;   /* the page object, one per disc; 0: any page */
    int sc;                     /* OB_SC_* (onlinebot.h) */
} bo3_online_screen;

extern const bo3_online_screen bo3_online_screens[];
extern const int bo3_online_screens_n;

/* the page object for one region (0 NTSC-U, 1 PAL, 2 PAL-ENSV) */
uint32_t bo3_online_screen_page_for(const bo3_online_screen *s, int region);
/* the page object for this build (region.h) */
uint32_t bo3_online_screen_page(const bo3_online_screen *s);

/* the screen of a front-end state and page, for one region (0/1/2) */
int bo3_online_screen_for_region(uint32_t flow, uint32_t page, int region);
/* the screen for this build's region */
int bo3_online_screen_for(uint32_t flow, uint32_t page);

#endif
