/* nodash.c - no way out to the Xbox Dashboard (see nodash.h).
 *
 * Where the title leaves for the dashboard (NTSC-U addresses; the PAL and
 * PAL-ENSV twins are in config/pal*\/patches.json, tools/region/xbemap.py):
 *
 *   SELECT GAMERTAG (page 0x55FBDC, sub_000B3020): rows 0 CREATE NEW ACCOUNT
 *     and 1 MANAGE ACCOUNTS, then the gamertags. A on row 0 / 1 (0xB376A)
 *     sets [page+0x380] = 0 / 1 and opens the dashboard question
 *     (sub_000B4520); its YES (sub_000B4590) is XLaunchNewImage(NULL, reason
 *     7 / 9), and [page+0x380] = 2 / 3 (the sign-in error boxes, sub_000B3C40)
 *     are sub_0031AAD1 (XOnline's update launch) and sub_00210CC0
 *     (XLaunchNewImage, reason 6 / 7 / 8).
 *   The sign-in error state (flow +0x948, sub_000B50C0): its box's A on an
 *     error the title does not know (not -0x38, -0x35, -2, -0xC) is
 *     XLaunchNewImage(NULL, reason 6: network settings).
 *   The memory unit check at boot (flow +0x4F0, sub_00070CB0): step 4's YES
 *     is XLaunchNewImage(NULL, reason 0).
 *
 * XLaunchNewImage (sub_001CFF88) goes through XOnline's launch hook
 * (sub_0031B0EF: y:\xboxdash.xbe, then '\Device\CdRom0;dashupdate.xbe') to
 * sub_001D00AB and HalReturnToFirmware(2), and the toolkit ended the
 * process there (kernel_bridge.c bridge_HalReturnToFirmware,
 * kernel_hal.c xbox_HalReturnToFirmware: ExitProcess(0)). The player saw the
 * game vanish.
 *
 * The marks (config/patches.json):
 *   nd_list   after the second fixed row's flag is written, before the list
 *             widget is built (0xB34A2 in sub_000B3340, 0xB35D4 in
 *             sub_000B34F0): the gamertags move up into rows 0.., the count
 *             and the cursor with them. The widget draws what it is given.
 *   nd_pick   after `mov ax, [ebp+0x2CC]` of the A (0xB36DC): the cursor two
 *             more, in ax and in the page, so the title's own row test, name
 *             copy and gamertag index read the gamertag the player sees. The
 *             page keeps that cursor: the A releases the list widget at once
 *             (sub_000B3840), and the sign-in state reads it after the page
 *             (sub_0006E1B0: [page+0x2CC] 0, the account row, is EA's CREATE
 *             NEW ACCOUNT state; a gamertag row is CONNECTING). The list is
 *             built afresh (nd_list again) whenever the page comes back. Up
 *             and down need nothing: the title moves within the count.
 *   nd_answer after the question's `call sub_000CD2A0` (0xB45A6): YES is NO
 *             (sub_00063B00, the title's way back to its page). YES / NO are
 *             4 / 5 on NTSC-U, 5 / 6 on the European discs (0xB47AB PAL).
 *   nd_signin after the last flag write before `cmp eax, -0xC` (0xB518C):
 *             eax -0xC, so an unknown error takes the known errors' way
 *             (0xB50F2: flow +0x260, as the box's B does).
 *   nd_mu     after the memory unit box's `call sub_000D1660` (0x70CD6):
 *             step 4's YES is the box's other answer (back to step 3).
 * The toolkit refuses a dashboard launch as well (HalReturnToFirmware
 * returns an error to the title), for any path not listed here. */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "nodash.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static int users_ok(const uint8_t *page)
{
    int32_t n = (int32_t)rd32(page + NODASH_USERS);
    return n >= 1 && (uint32_t)n <= NODASH_MAX_USERS;
}

int bo3_nodash_list(uint8_t *page)
{
    uint32_t n;
    uint16_t cur;
    if (!users_ok(page))
        return 0;
    n = rd32(page + NODASH_USERS);
    if (rd16(page + NODASH_COUNT) != n + NODASH_FIXED)
        return 0;                       /* not the title's fresh list */
    memmove(page + NODASH_ITEMS, page + NODASH_ITEMS + NODASH_FIXED * 8u, n * 8u);
    memset(page + NODASH_ITEMS + n * 8u, 0, NODASH_FIXED * 8u);
    cur = rd16(page + NODASH_CURSOR);
    wr16(page + NODASH_COUNT, (uint16_t)n);
    wr16(page + NODASH_CURSOR, (uint16_t)(cur >= NODASH_FIXED && cur - NODASH_FIXED < n ? cur - NODASH_FIXED : 0));
    return 1;
}

int bo3_nodash_hidden(const uint8_t *page)
{
    return users_ok(page) && rd16(page + NODASH_COUNT) == rd32(page + NODASH_USERS) &&
           rd16(page + NODASH_CURSOR) < rd32(page + NODASH_USERS);
}

int bo3_nodash_pick(uint8_t *page)
{
    if (!bo3_nodash_hidden(page))
        return 0;
    wr16(page + NODASH_CURSOR, (uint16_t)(rd16(page + NODASH_CURSOR) + NODASH_FIXED));
    return 1;
}

uint32_t bo3_nodash_answer(uint32_t answer, uint32_t yes)
{
    return answer == yes ? yes + 1u : answer;
}

uint32_t bo3_nodash_mu_answer(uint32_t answer, uint32_t step)
{
    return (answer == 0 && step == 4) ? 1 : answer;
}

#ifndef BO3_NODASH_TEST
#include "platform/recomp_patch.h"
#include "recomp_types.h"
#include "region.h"

extern RECOMP_TLS uint32_t g_eax, g_esi;
extern ptrdiff_t g_xbox_mem_offset;

#define ND_PAGE BO3_VA(0x0055FBDCu, 0x0055FEFCu, 0x0055FF9Cu)
#define ND_HP(va) ((uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

static void nd_list(void)
{
    if (!g_xbox_mem_offset)
        return;
    if (bo3_nodash_list(ND_HP(ND_PAGE)))
        fprintf(stderr, "[NODASH] gamertag page: %u gamertag(s) listed, no account lines\n",
                (unsigned)rd32(ND_HP(ND_PAGE) + NODASH_USERS));
}

static void nd_pick(void)
{
    if (!g_xbox_mem_offset)
        return;
    if (bo3_nodash_pick(ND_HP(ND_PAGE)))
        g_eax = (g_eax & 0xFFFF0000u) | rd16(ND_HP(ND_PAGE) + NODASH_CURSOR);
}

static void nd_answer(void)
{
    uint32_t a = bo3_nodash_answer(g_eax, BO3_VA(NODASH_ANSWER_YES_NTSC, NODASH_ANSWER_YES_PAL, NODASH_ANSWER_YES_PAL));
    if (a != g_eax) {
        fprintf(stderr, "[NODASH] dashboard question: YES taken as NO (there is no Xbox Dashboard)\n");
        g_eax = a;
    }
}

static void nd_signin(void)
{
    fprintf(stderr, "[NODASH] sign-in error 0x%08X: the box's way back, not the dashboard\n", g_eax);
    g_eax = 0xFFFFFFF4u;                /* -0xC: a known error, 0xB50F2 */
}

static void nd_mu(void)
{
    uint32_t a;
    if (!g_xbox_mem_offset)
        return;
    a = bo3_nodash_mu_answer(g_eax, *(uint32_t *)ND_HP(g_esi + 8));
    if (a != g_eax) {
        fprintf(stderr, "[NODASH] memory unit question: YES taken as the way back (there is no Xbox Dashboard)\n");
        g_eax = a;
    }
}

/* TAKEDAWN_NODASH_TEST (tests only, tools/headless/nodash_scene.sh): "rows"
 * keeps the title's account rows, so a scene can choose one and see the
 * question's YES stay in the game; "toolkit" switches every mark here off, so
 * the title's XLaunchNewImage runs and the toolkit's refusal is what keeps the
 * game up. */
void bo3_nodash_start(void)
{
    const char *t = getenv("TAKEDAWN_NODASH_TEST");
    int rows = t && (!strcmp(t, "rows") || !strcmp(t, "toolkit"));
    int all = t && !strcmp(t, "toolkit");
    if (t && *t)
        fprintf(stderr, "[NODASH] test: %s (account rows %s, dashboard answers %s)\n", t,
                rows ? "listed" : "hidden", all ? "as the title has them" : "kept in the game");
    if (!rows) {
        recomp_patch_on_mark("nd_list", nd_list);
        recomp_patch_on_mark("nd_pick", nd_pick);
    }
    if (!all) {
        recomp_patch_on_mark("nd_answer", nd_answer);
        recomp_patch_on_mark("nd_signin", nd_signin);
        recomp_patch_on_mark("nd_mu", nd_mu);
    }
}
#endif
