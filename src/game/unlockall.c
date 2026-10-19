/*
 * unlockall.c - the overlay's "Unlock everything" cheat (BO-131): every car,
 * event and crash junction open for this session, without writing the
 * player's career.
 *
 * Where the title keeps what is open: not in the profile but in arrays it
 * derives from it. sub_0001BE60 (PS2 CB3Profile::Evaluate; its part
 * sub_0001C340 clears and refills the event arrays from the profile's medals
 * and points) rebuilds them whenever the profile changes (a load, an event's
 * result, the garage), and the menus and the car select read only them:
 *
 *   0x44CFC8  0x43 bytes   a car is open           count 0x44D154
 *   0x44D00B  0x14 bytes   (signature takedowns)   count 0x44D148
 *   0x44D01F  0x49 bytes   a race event is open    count 0x44D15C
 *   0x44D068  100 bytes    a crash junction is open  count 0x44D160
 *   0x44D0CC  0x24 bytes   a track is offered in Single Event (Race and
 *                          Road Rage Region/Track lists; Random draws from
 *                          these)                        count 0x44D164
 *   0x44D158 = 0x14, 0x44D23C = 10                 (as the title's own sets them)
 *   0x44D4B8  0x162 bytes  the copy of 0x44CFC8.. the title compares with to
 *                          announce what is new ("new car" and the like)
 *
 * The Single Event lists are not the events array: sub_0001C9D0 (called from
 * sub_0001BE60 after sub_0001C340) opens a track there only for a race event
 * that is open AND has a medal in the profile (profile + 0x386 + event > 0),
 * plus track 4 as the starting track. A fresh profile therefore offers only
 * the US starter tracks, and the event arrays alone do not widen it; the
 * menu builder sub_0008C920 lists a region only if one of its tracks is
 * set in 0x44D0CC. The title's own "Unlock All" below never touched it (it
 * sets the medals, which this must not). Opening the 0x24 bytes here
 * puts every region and track in the lists, again in memory only.
 *
 * The title has an "Unlock All" of its own on a debug page (sub_0001BFD0,
 * strings at 0x3AC3A8): it fills these and also writes the profile (a gold
 * medal in every event, every car marked won, the stats at 1000), which a
 * save would keep. This writes only the arrays, the way that function does,
 * and the announced copy with them so the title does not announce every car
 * at once: the "ua_eval" mark (sub_0001BE60 just before its ret, 0x1BFC4)
 * keeps what the title computed and puts the open set in its place each
 * time; off puts the title's last computed set back.
 *
 * The overlay switches it (xbox_OverlaySetCheats; its row shows only with
 * RECOMP_DEBUG=cheats, a tester's tool; BO3_UNLOCK_ALL=1 below needs no
 * row) and, while it is on, sends
 * the title's saves to a copy of the save (overlay_ui.cpp): the profile is
 * not written by this, and what the player wins in a race opened by it is
 * kept or thrown away when the cheat goes off.
 */
#include "region.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "platform/recomp_patch.h"
#include "recomp_types.h"
#ifdef XBOXRECOMP_HAVE_OVERLAY
#include "xbox_settings.h"
#endif

extern ptrdiff_t g_xbox_mem_offset;

#define UA_P(va)   ((uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define UA_M32(va) (*(uint32_t *)UA_P(va))

#define UA_BLOCK      BO3_VA(0x0044CFC8u, 0x0044D2E8u)     /* the arrays and their counts */
#define UA_BLOCK_END  BO3_VA(0x0044D240u, 0x0044D560u)
#define UA_ANNOUNCED  BO3_VA(0x0044D4B8u, 0x0044D7D8u)     /* the copy of UA_BLOCK .. + 0x162 */
#define UA_ANN_SIZE   0x162u

static volatile int s_on;
static int s_have_real;
static uint8_t s_real[UA_BLOCK_END - UA_BLOCK];
static uint8_t s_real_ann[UA_ANN_SIZE];

static void ua_keep_real(void)
{
    memcpy(s_real, UA_P(UA_BLOCK), sizeof s_real);
    memcpy(s_real_ann, UA_P(UA_ANNOUNCED), sizeof s_real_ann);
    s_have_real = 1;
}

/* What sub_0001BFD0 writes outside the profile. */
static void ua_open_all(void)
{
    memset(UA_P(BO3_VA(0x0044D01Fu, 0x0044D33Fu)), 1, 0x49);
    UA_M32(BO3_VA(0x0044D15Cu, 0x0044D47Cu)) = 0x49;
    memset(UA_P(BO3_VA(0x0044D068u, 0x0044D388u)), 1, 100);
    UA_M32(BO3_VA(0x0044D160u, 0x0044D480u)) = 100;
    memset(UA_P(BO3_VA(0x0044CFC8u, 0x0044D2E8u)), 1, 0x43);
    UA_M32(BO3_VA(0x0044D154u, 0x0044D474u)) = 0x43;
    UA_M32(BO3_VA(0x0044D23Cu, 0x0044D55Cu)) = 10;
    UA_M32(BO3_VA(0x0044D158u, 0x0044D478u)) = 0x14;
    UA_M32(BO3_VA(0x0044D148u, 0x0044D468u)) = 0x14;
    memset(UA_P(BO3_VA(0x0044D00Bu, 0x0044D32Bu)), 1, 0x14);
    memset(UA_P(BO3_VA(0x0044D0CCu, 0x0044D3ECu)), 1, 0x24);
    UA_M32(BO3_VA(0x0044D164u, 0x0044D484u)) = 0x24;
    memcpy(UA_P(UA_ANNOUNCED), UA_P(UA_BLOCK), UA_ANN_SIZE);
}

static void ua_eval(void)
{
    if (!s_on || !g_xbox_mem_offset)
        return;
    ua_keep_real();
    ua_open_all();
}

static int ua_get(void) { return s_on; }

static void ua_set(int on)
{
    if (!g_xbox_mem_offset || on == s_on)
        return;
    if (on) {
        ua_keep_real();
        ua_open_all();
        s_on = 1;
        fprintf(stderr, "[CHEAT] unlock everything: on (cars, events and junctions open; the profile is not written)\n");
    } else {
        s_on = 0;
        if (s_have_real) {
            memcpy(UA_P(UA_BLOCK), s_real, sizeof s_real);
            memcpy(UA_P(UA_ANNOUNCED), s_real_ann, sizeof s_real_ann);
        }
        fprintf(stderr, "[CHEAT] unlock everything: off (the career's own set is back)\n");
    }
}

#ifdef XBOXRECOMP_HAVE_OVERLAY
static const xbox_cheat k_cheats[] = {
    { "Unlock everything",
      "Opens every car, race and crash junction for this session. Your save is not changed. "
      "When you switch it off you choose whether to keep what you won. "
      "Fair warning: the career is a lot more fun earned.",
      ua_get, ua_set },
};
#endif

void bo3_unlock_all_start(void)
{
    recomp_patch_on_mark("ua_eval", ua_eval);
#ifdef XBOXRECOMP_HAVE_OVERLAY
    xbox_OverlaySetCheats(k_cheats, (int)(sizeof k_cheats / sizeof k_cheats[0]));
#endif
    /* A scripted run asks for it from the start: BO3_UNLOCK_ALL=1 (no save
     * copy then: give the run a scratch save, RECOMP_SAVE_DIR). */
    {
        const char *e = getenv("BO3_UNLOCK_ALL");
        if (e && *e == '1')
            s_on = 1;
    }
}
