/* mainquit.c - add QUIT after the title's five main-menu lines.
 * The mq_items mark copies the items and descriptions to a title-allocated
 * block before the list widget is built; mq_select consumes A on QUIT.
 * TAKEDAWN_QUIT_MENU=0 suppresses it, =1 also lists it headless. */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "platform/recomp_env.h"
#include "platform/recomp_patch.h"
#include "platform/recomp_restart.h"
#include "recomp_types.h"
#include "region.h"
#include "mainquit.h"
#include "onlinebrand.h"
#include "onlinedesc.h"
#include "headfont.h"

#if !defined(_WIN32) || defined(XBOXRECOMP_WIN_VULKAN)
#define SDL_MAIN_HANDLED
#include <SDL.h>
#define MQ_HAVE_SDL 1
#endif

extern RECOMP_TLS uint32_t g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);          /* the title's heap: (size, tag), stdcall */

#define MQ_M16(va) (*(uint16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define MQ_M32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define MQ_PAGE       BO3_VA(0x00559390u, 0x005596B0u, 0x00559750u)
#define MQ_PAIR       (MQ_PAGE + 0x14)   /* cursor, count */
#define MQ_ITEMS      (MQ_PAGE + 0x18)
#define MQ_DESCS      (MQ_PAGE + 0x40)
#define MQ_N          5u                 /* the title's lines */
#define MQ_ITEM_SIZE  8u
#define MQ_HEAP_TAG   0x54414B51u        /* "TAKQ" */
#define MQ_ARG_ITEMS  0x14u              /* from esp at the mark: the widget's items argument */
#define MQ_ARG_DESCS  0x2Cu              /* the local that holds the descriptions */

static const char k_label[]      = "QUIT";
static const char k_desc[]       = "CLOSE THE GAME";

/* One block: six items and descriptions, then QUIT's label and text. */
#define MQ_LINES      (MQ_N + 1)
#define MQ_DESC_AT    (MQ_LINES * MQ_ITEM_SIZE)
#define MQ_QUIT_LABEL (MQ_DESC_AT + MQ_LINES * 4u)
#define MQ_QUIT_TEXT  (MQ_QUIT_LABEL + (uint32_t)sizeof k_label * 2u)
#define MQ_BLOCK_SIZE (MQ_QUIT_TEXT + (uint32_t)sizeof k_desc * 2u)

static uint32_t s_block;
static int s_logged_bad;

static int mq_on(void)
{
#if defined(MQ_HAVE_SDL)
    const char *v = getenv("TAKEDAWN_QUIT_MENU");

    if (v && *v == '0')
        return 0;
    if (v && *v == '1')                 /* tests: listed headless too */
        return 1;
    return !recomp_env_on(RENV_HEADLESS);
#else
    return 0;
#endif
}

static uint32_t mq_block(void)
{
    if (!s_block) {
        uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, i;

        PUSH32(g_esp, MQ_HEAP_TAG);
        PUSH32(g_esp, MQ_BLOCK_SIZE);
        PUSH32(g_esp, 0);               /* the return address its ret takes */
        BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
        s_block = g_eax;
        g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
        if (!s_block)
            return 0;
        for (i = 0; i < sizeof k_label; i++)
            MQ_M16(s_block + MQ_QUIT_LABEL + i * 2) = (uint16_t)(uint8_t)k_label[i];
        for (i = 0; i < sizeof k_desc; i++)
            MQ_M16(s_block + MQ_QUIT_TEXT + i * 2) = (uint16_t)(uint8_t)k_desc[i];
    }
    return s_block;
}

static void mq_bad(const char *what)
{
    if (!s_logged_bad) {
        s_logged_bad = 1;
        fprintf(stderr, "[MENU] main menu: our lines not listed, %s\n", what);
    }
}

/* After the count is stored (0x77F27): edi the page's pair, the widget's
 * arguments already pushed. */
static void mq_items(void)
{
    uint32_t b, i;
    int lines, quick, quit;

    if (g_edi == MQ_PAIR && g_xbox_mem_offset) {
        uint32_t d;
        bo3_onlinebrand_apply();
        bo3_headfont_apply();
        /* TAKEDAWN ONLINE's description says what is on the server
         * (onlinedesc.c); the copy below takes it with the others */
        if (MQ_M16(MQ_PAIR + 2) == MQ_N && (d = bo3_onlinedesc_main()) != 0)
            MQ_M32(MQ_DESCS + 3u * 4u) = d;
    }
    if (g_edi != MQ_PAIR || !g_xbox_mem_offset || !mq_on())
        return;
    if (MQ_M16(MQ_PAIR + 2) != MQ_N) {
        mq_bad("the page's count is not five");
        return;
    }
    if (MQ_M32(g_esp + MQ_ARG_ITEMS) != MQ_ITEMS || MQ_M32(g_esp + MQ_ARG_DESCS) != MQ_DESCS) {
        mq_bad("the widget's arguments are not where the page leaves them");
        return;
    }
    if (!(b = mq_block()))
        return;
    for (i = 0; i < MQ_N; i++) {
        MQ_M32(b + i * MQ_ITEM_SIZE) = MQ_M32(MQ_ITEMS + i * MQ_ITEM_SIZE);
        MQ_M32(b + i * MQ_ITEM_SIZE + 4) = MQ_M32(MQ_ITEMS + i * MQ_ITEM_SIZE + 4);
        MQ_M32(b + MQ_DESC_AT + i * 4) = MQ_M32(MQ_DESCS + i * 4);
    }
    bo3_mainquit_layout(0, &lines, &quick, &quit);
    MQ_M32(b + (uint32_t)quit * MQ_ITEM_SIZE) = b + MQ_QUIT_LABEL;
    MQ_M32(b + (uint32_t)quit * MQ_ITEM_SIZE + 4) = 0;
    MQ_M32(b + MQ_DESC_AT + (uint32_t)quit * 4) = b + MQ_QUIT_TEXT;
    MQ_M16(MQ_PAIR + 2) = (uint16_t)lines;
    MQ_M32(g_esp + MQ_ARG_ITEMS) = b;
    MQ_M32(g_esp + MQ_ARG_DESCS) = b + MQ_DESC_AT;
    fprintf(stderr, "[MENU] main menu: QUIT listed\n");
}

/* The game is closed as a click on the window's close button would: see the
 * header. Any thread. */
static void mq_quit(void)
{
#if defined(MQ_HAVE_SDL)
    SDL_Event q;

    recomp_quit_request("the main menu's QUIT");
    memset(&q, 0, sizeof q);
    q.type = SDL_QUIT;
    SDL_PushEvent(&q);
#endif
}

/* After sub_00017790 (A taken, al the answer; edi the page). */
static void mq_select(void)
{
    int lines, quick, quit;

    if (g_edi != MQ_PAGE || !g_xbox_mem_offset || !(g_eax & 0xFFu))
        return;
    bo3_mainquit_layout(0, &lines, &quick, &quit);
    if (MQ_M16(MQ_PAIR + 2) != (uint16_t)lines)
        return;
    if (MQ_M16(MQ_PAIR) != (uint16_t)quit)
        return;
    g_eax &= ~0xFFu;
    fprintf(stderr, "[MENU] main menu: QUIT chosen\n");
    mq_quit();
}

void bo3_mainquit_start(void)
{
    recomp_patch_on_mark("mq_items", mq_items);
    recomp_patch_on_mark("mq_select", mq_select);
}
