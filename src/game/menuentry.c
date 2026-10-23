/*
 * menuentry.c - interface.menu_entry: TAKEDAWN SETTINGS in the title's own
 * Settings menu (Driver Details > Settings), which opens the overlay
 * (BO-133). On by default; off leaves the menu as it was.
 *
 * The page: a menu state of the title's front-end flow whose object is the
 * static 0x55ABC4 (sub_000696D0 builds it on its "enter" action through
 * sub_00085170). The object is { +4 cursor (u16), +6 item count (u16),
 * +8 four items of 8 bytes: label pointer (UTF-16, from the string table
 * [[0x4D532C] + 0xC] + index * 4), two flag bytes, two spare }: GAMEPLAY
 * (string 2429), AUDIO (387), CONTROLLERS (174), EA GAMES TRAX (388). It
 * ends at +0x28: the next menu state's object starts at 0x55ABEC, so there
 * is no room for a fifth item in place. The items' address only goes to
 * the list widget (sub_000F0EA0, pushed from eax at 0x8522D), so the
 * "si_items" mark (after 0x85222, the fourth label stored; esi the page,
 * eax the items) copies the four into a block of our own on the title's
 * heap, adds the fifth, sets the count to 5 and hands the widget the block.
 * The cursor and count stay in the page, where the input handler
 * (sub_000852B0) reads them: down now reaches the fifth line.
 *
 * Its A: sub_000852B0 asks sub_00017790 (A taken on the menu player's
 * pad) and on yes starts the page's exit, after which the flow picks the
 * next page from the cursor (sub_000696D0, the table at 0x3EAF34 has four
 * entries). The "si_select" mark (after that call, at 0x853B4) turns the
 * answer to no when the cursor is on the fifth line and asks the overlay
 * to open: the title never sees the press, stays on its page, and the
 * overlay, once open, takes the pad and pauses the title as it does from
 * the hotkey. B, up and down stay the title's.
 *
 * The race's pause menu has no Settings page (Resume, Restart, Audio,
 * Controllers, Quit): the entry goes in there too, before QUIT (below).
 *
 * Only with an overlay to open: none without a window (headless) or with
 * RECOMP_OVERLAY=0, and then the menu is the title's.
 *
 * The label is ours (the string table has no such text), UTF-16 in the
 * same block, drawn by the title's widget in its own font.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "platform/recomp_env.h"
#include "platform/recomp_patch.h"
#include "recomp_types.h"
#ifdef XBOXRECOMP_HAVE_OVERLAY
#include "xbox_settings.h"
#endif

extern RECOMP_TLS uint32_t g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void sub_001D2879(void);          /* the title's heap: (size, tag), stdcall */

#define ME_M8(va)  (*(uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define ME_M16(va) (*(uint16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define ME_M32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define ME_PAGE       0x0055ABC4u     /* the Settings page's object */
#define ME_CURSOR     (ME_PAGE + 4)
#define ME_COUNT      (ME_PAGE + 6)
#define ME_ITEMS      (ME_PAGE + 8)
#define ME_N          4u              /* the title's items */
#define ME_ITEM_SIZE  8u
#define ME_HEAP_TAG   0x54414B45u     /* "TAKE" */

static const char k_label[] = "TAKEDAWN SETTINGS";
static uint32_t s_block;              /* 5 items, then the label */
static int s_listed;                  /* the Settings page last built lists our line (line 4) */

/* The MODS page (modsmenu.c) shares the two Settings marks: a mark name has
 * one callback, so ours call it after their own work. */
extern int bo3_modsmenu_mode(void);   /* 1: the page shows the MODS list */
extern void bo3_modsmenu_items(void);
extern void bo3_modsmenu_select(void);

static int me_on(void)
{
#ifdef XBOXRECOMP_HAVE_OVERLAY
    const char *v = recomp_env(RENV_MENU_ENTRY);
    return (!v || !*v || *v != '0') && xbox_OverlayAvailable();
#else
    return 0;
#endif
}

static uint32_t me_block(void)
{
    if (!s_block) {
        uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, size, lab, i;

        size = (ME_N + 1) * ME_ITEM_SIZE + (uint32_t)(sizeof k_label) * 2;
        PUSH32(g_esp, ME_HEAP_TAG);
        PUSH32(g_esp, size);
        PUSH32(g_esp, 0);               /* the return address its ret takes */
        sub_001D2879();
        s_block = g_eax;
        g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
        if (!s_block)
            return 0;
        lab = s_block + (ME_N + 1) * ME_ITEM_SIZE;
        for (i = 0; i < sizeof k_label; i++)
            ME_M16(lab + i * 2) = (uint16_t)(uint8_t)k_label[i];
    }
    return s_block;
}

static void me_items_body(void)
{
    uint32_t b, i;

    if (g_esi == ME_PAGE)
        s_listed = 0;
    if (g_esi != ME_PAGE || g_eax != ME_ITEMS || !g_xbox_mem_offset || !me_on())
        return;
    if (ME_M16(ME_COUNT) != ME_N || !(b = me_block()))
        return;
    for (i = 0; i < ME_N; i++) {
        ME_M32(b + i * ME_ITEM_SIZE) = ME_M32(ME_ITEMS + i * ME_ITEM_SIZE);
        ME_M32(b + i * ME_ITEM_SIZE + 4) = 0;   /* the flags the title clears (bl = 0) */
    }
    ME_M32(b + ME_N * ME_ITEM_SIZE) = b + (ME_N + 1) * ME_ITEM_SIZE;
    ME_M32(b + ME_N * ME_ITEM_SIZE + 4) = 0;
    ME_M16(ME_COUNT) = ME_N + 1;
    s_listed = 1;
    g_eax = b;                          /* pushed next as the widget's items */
    fprintf(stderr, "[MENU] Settings: TAKEDAWN SETTINGS listed\n");
}

static void me_items(void)
{
    me_items_body();
    bo3_modsmenu_items();               /* MODS after our line, or the MODS page's lines */
}

/* ── the pause menu ─────────────────────────────────────────────────
 *
 * The race's pause menu (RESUME, RESTART, AUDIO, CONTROLLERS, QUIT; one
 * more line in some events) is built by sub_00085F70 into the static
 * object 0x5593E4, for player 1 or 2 alike (split screen pauses the same
 * page for whoever pressed Start): +4 cursor, +6 count, +8 up to seven
 * items of 8 bytes (label, a "greyed" byte, a spare), +0x44 one action
 * code per item (0 resume, 1 restart, 2 the extra line, 3 audio,
 * 4 controllers, 5 quit), +0x60 a byte set for the other layout (seven
 * fixed lines, no action codes: left alone). There is room for a seventh
 * item and its code in place, so the "pe_items" mark (after the count is
 * stored, 0x8621A, ebx the page) moves QUIT one down and puts TAKEDAWN
 * SETTINGS in its place with a code of our own. The page's input
 * (sub_000863F0) switches on the code under the cursor when A is taken
 * (controller record + 0x4C) and does nothing for one it does not know: the
 * "pe_select" mark (0x863FC, esi the page, edi the record) asks the overlay
 * to open then. The title stays paused on its menu under the overlay (the
 * overlay pauses the host on top) and is still on it when the overlay
 * closes; B and Start are the title's as before. */
#define PE_PAGE      0x005593E4u
#define PE_CURSOR    (PE_PAGE + 4)
#define PE_COUNT     (PE_PAGE + 6)
#define PE_ITEMS     (PE_PAGE + 8)
#define PE_CODES     (PE_PAGE + 0x44)
#define PE_OTHER     (PE_PAGE + 0x60)
#define PE_MAX       7u
#define PE_QUIT      5u
#define PE_OURS      0x7Au           /* an action code the title does not have */

static void pe_items(void)
{
    uint32_t n, q, lab;

    if (g_ebx != PE_PAGE || !g_xbox_mem_offset || ME_M8(PE_OTHER) || !me_on())
        return;
    n = ME_M16(PE_COUNT);
    if (n < 2 || n >= PE_MAX || ME_M32(PE_CODES + (n - 1) * 4) != PE_QUIT || !me_block())
        return;
    lab = s_block + (ME_N + 1) * ME_ITEM_SIZE;
    q = n - 1;                          /* QUIT, one down */
    ME_M32(PE_ITEMS + n * ME_ITEM_SIZE) = ME_M32(PE_ITEMS + q * ME_ITEM_SIZE);
    ME_M32(PE_ITEMS + n * ME_ITEM_SIZE + 4) = ME_M32(PE_ITEMS + q * ME_ITEM_SIZE + 4);
    ME_M32(PE_CODES + n * 4) = PE_QUIT;
    ME_M32(PE_ITEMS + q * ME_ITEM_SIZE) = lab;
    ME_M32(PE_ITEMS + q * ME_ITEM_SIZE + 4) = 0;
    ME_M32(PE_CODES + q * 4) = PE_OURS;
    ME_M16(PE_COUNT) = (uint16_t)(n + 1);
    fprintf(stderr, "[MENU] pause: TAKEDAWN SETTINGS listed\n");
}

static void pe_select(void)
{
    uint32_t i;

    if (g_esi != PE_PAGE || !g_xbox_mem_offset || ME_M8(PE_OTHER) || !ME_M16(g_edi + 0x4C))
        return;
    i = ME_M16(PE_CURSOR);
    if (i >= PE_MAX || ME_M32(PE_CODES + i * 4) != PE_OURS)
        return;
#ifdef XBOXRECOMP_HAVE_OVERLAY
    if (xbox_OverlayRequestOpen())
        fprintf(stderr, "[MENU] pause: TAKEDAWN SETTINGS: opening the overlay\n");
#endif
}

static void me_select_body(void)
{
    if (!g_xbox_mem_offset || !(g_eax & 0xFFu) || ME_M16(ME_CURSOR) != ME_N
        || !s_listed || bo3_modsmenu_mode())
        return;
    /* Whatever happens, the title must not take this A: its exit would
     * look the fifth line up in a four-entry table. */
    g_eax &= ~0xFFu;
#ifdef XBOXRECOMP_HAVE_OVERLAY
    if (xbox_OverlayRequestOpen())
        fprintf(stderr, "[MENU] TAKEDAWN SETTINGS: opening the overlay\n");
#endif
}

static void me_select(void)
{
    me_select_body();
    bo3_modsmenu_select();              /* MODS, and the MODS page's lines */
}

void bo3_menu_entry_start(void)
{
    recomp_patch_on_mark("si_items", me_items);
    recomp_patch_on_mark("si_select", me_select);
    recomp_patch_on_mark("pe_items", pe_items);
    recomp_patch_on_mark("pe_select", pe_select);
}
