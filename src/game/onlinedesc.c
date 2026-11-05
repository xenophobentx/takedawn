/*
 * onlinedesc.c - the description line under the menus' lists for Takedawn
 * Online (step 5): the main menu's TAKEDAWN ONLINE line ("RACE ONLINE - 12
 * PLAYERS ONLINE - 2 GAMES OPEN") and the online page's line, which the title
 * does not give that page (see onlinedesc.h).
 *
 * The line is the title's description widget (object 0x550C38, the one the
 * main menu and the Quick Match page set up with sub_000CA090): it draws the
 * text a description pointer names, for the list's cursor, and whenever its
 * byte +0x6D differs from the cursor it takes the pointer again and measures
 * it (0xCAB9A, sub_000C9870: a pointer and a width are stored, nothing is
 * allocated). The texts live in one title-heap block made once; each text
 * has two buffers: a new text goes into the one not shown, the description
 * pointer is moved to it and +0x6D is set to 0xFF, the value the widget's
 * own set-up leaves there, so the title takes and measures it on its next
 * frame. Nothing is ever written into a buffer the widget is drawing.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "recomp_types.h"
#include "region.h"
#include "onlinedesc.h"
#include "onlineinfo.h"
#include "xnet_hle.h"

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);   /* the title's heap: (size, tag), stdcall */

#define OD_M8(va)  (*(uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define OD_M16(va) (*(uint16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define OD_M32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define OD_WIDGET     BO3_VA(0x00550C38u, 0x00550F58u, 0x00550FF8u)
#define OD_W_ACTIVE   (OD_WIDGET + 0x0Au)
#define OD_W_DESCS    (OD_WIDGET + 0x10u)
#define OD_W_SHOWN    (OD_WIDGET + 0x6Du)
#define OD_MAIN_LINE  3u                      /* XBOX LIVE, now TAKEDAWN ONLINE */
#define OD_TAG        0x54414B44u             /* "TAKD" */

#define OD_UNITS      96u                     /* a text, UTF-16 with its NUL */
#define OD_ARRAY      0u                      /* the online page's descriptions: 8 pointers */
#define OD_WAIT_ARRAY 32u                     /* the Quick Match page's while it waits for a race (BO-365) */
#define OD_TEXTS      64u
#define OD_TEXT(slot, b) (OD_TEXTS + ((uint32_t)(slot) * 2u + (uint32_t)(b)) * OD_UNITS * 2u)
#define OD_SIZE       OD_TEXT(OD_SLOTS, 0)

enum { OD_MAIN, OD_QUICK, OD_CREATE, OD_JOIN, OD_BROWSE, OD_ENTRY, OD_WAIT, OD_SLOTS };

static uint32_t s_block;
static int s_cur[OD_SLOTS];
static char s_text[OD_SLOTS][OD_UNITS];
static int s_entry;                         /* the page shows the code entry */
static char s_entry_msg[OD_UNITS];          /* its message ("" = the hint) */
static uint32_t s_wait_prev;                /* the descriptions the widget had before the wait line */
static char s_page_msg[OD_UNITS];           /* BO-372: the four lines' message ("" none) ... */
static unsigned long long s_page_msg_until; /* ... until then (monotonic ms) */
#define OD_PAGE_MSG_MS 8000ull

static unsigned long long od_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}

void bo3_onlinedesc_page_msg(const char *msg)
{
    snprintf(s_page_msg, sizeof s_page_msg, "%s", msg ? msg : "");
    s_page_msg_until = msg && msg[0] ? od_ms() + OD_PAGE_MSG_MS : 0;
}

static uint32_t od_block(void)
{
    if (!s_block && g_xbox_mem_offset) {
        uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;
        PUSH32(g_esp, OD_TAG);
        PUSH32(g_esp, OD_SIZE);
        PUSH32(g_esp, 0);
        BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
        s_block = g_eax;
        g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
        if (s_block)
            memset((void *)((uintptr_t)g_xbox_mem_offset + s_block), 0, OD_SIZE);
    }
    return s_block;
}

static uint32_t od_buf(int slot, int b) { return s_block + OD_TEXT(slot, b); }

/* the buffer that shows slot's text now */
static uint32_t od_now(int slot) { return od_buf(slot, s_cur[slot]); }

static int od_is(int slot, uint32_t p) { return s_block && (p == od_buf(slot, 0) || p == od_buf(slot, 1)); }

/* slot's text becomes t: into the buffer not shown; 1 when it changed */
static int od_set(int slot, const char *t)
{
    uint32_t to;
    size_t i;
    if (!strcmp(s_text[slot], t) && s_text[slot][0])
        return 0;
    snprintf(s_text[slot], sizeof s_text[slot], "%s", t);
    s_cur[slot] ^= 1;
    to = od_now(slot);
    for (i = 0; s_text[slot][i] && i < OD_UNITS - 1; i++) {
        unsigned char c = (unsigned char)s_text[slot][i];
        OD_M16(to + 2u * (uint32_t)i) = (uint16_t)(c >= 0x20 && c < 0x7F ? c : '?');
    }
    OD_M16(to + 2u * (uint32_t)i) = 0;
    return 1;
}

static const char *const k_hint[] = {
    "", "ONE PRESS AND YOU RACE", "YOUR OWN GAME WITH A CODE", "A FRIEND'S GAME BY ITS CODE", "EVERY GAME: OPEN, RACING, FULL",
};
#define OD_ENTRY_HINT "TYPE YOUR FRIEND'S CODE"     /* short: the line holds it still (a long one scrolls) */

/* every text as it should read now; 1 when one changed */
static int od_refresh(void)
{
    char counts[80], t[OD_UNITS];
    int on = -1, wt = -1, have, mode = xnet_hle_on(), k, changed = 0, rc = -1;
    have = mode == 1 && bo3_onlineinfo_latest(&on, &wt);
#ifdef XNET_HLE_HAS_RACEWAIT
    rc = have ? xnet_hle_lobby_racing() : -1;      /* the room record's count: signed in only */
#endif
    bo3_onlineinfo_counts_racing(counts, sizeof counts, mode, have, on, wt, rc);
    bo3_onlineinfo_menu_line(t, sizeof t, counts);
    changed |= od_set(OD_MAIN, t);
    if (s_page_msg[0] && od_ms() >= s_page_msg_until)
        s_page_msg[0] = 0;
    for (k = OD_QUICK; k <= OD_BROWSE; k++) {
        if (s_page_msg[0])
            snprintf(t, sizeof t, "%s", s_page_msg);
        else
            bo3_onlineinfo_page_line(t, sizeof t, counts,
                                     k == OD_BROWSE ? bo3_onlineinfo_browse_hint(k_hint[k], have, wt, rc) : k_hint[k]);
        changed |= od_set(k, t);
    }
    changed |= od_set(OD_ENTRY, s_entry_msg[0] ? s_entry_msg : OD_ENTRY_HINT);
    return changed;
}

uint32_t bo3_onlinedesc_main(void)
{
    if (!xnet_hle_on() || !od_block())
        return 0;
    bo3_onlineinfo_kick();
    od_refresh();
    return od_now(OD_MAIN);
}

uint32_t bo3_onlinedesc_page(int entry)
{
    uint32_t a;
    int k;
    if (!od_block())
        return 0;
    s_entry = entry;
    bo3_onlineinfo_kick();
    od_refresh();
    a = s_block + OD_ARRAY;
    for (k = 0; k < 8; k++)
        OD_M32(a + 4u * (uint32_t)k) = entry ? od_now(OD_ENTRY) : od_now(k < 4 ? OD_QUICK + k : OD_QUICK);
    return a;
}

/* BO-365: the Quick Match page's line while it waits for a race. The page
 * set the widget up with its own one-text array (0x46889C, the pair after it:
 * cursor 0, count 1; traced, PLAN.md section 10); the widget draws entry
 * [cursor] of the array at +0x10 and takes it again when +0x6D is 0xFF. So
 * the line is ours by pointing +0x10 at an array whose entries all name the
 * wait text, and the title's again by putting its array back. The title's
 * own array is never written. */
void bo3_onlinedesc_wait_line(const char *t)
{
    uint32_t a, cur;
    int k, changed;
    if (!g_xbox_mem_offset || !od_block())
        return;
    a = s_block + OD_WAIT_ARRAY;
    cur = OD_M32(OD_W_DESCS);
    if (!t || !t[0]) {
        if (s_wait_prev && cur == a) {
            OD_M32(OD_W_DESCS) = s_wait_prev;
            OD_M8(OD_W_SHOWN) = 0xFF;
        }
        s_wait_prev = 0;
        return;
    }
    if (!OD_M8(OD_W_ACTIVE))
        return;
    changed = od_set(OD_WAIT, t);
    if (cur != a) {
        /* the page's own array: in the image, or in the title's heap (the RAM alias) */
        if (cur < 0x10000u || (cur >= 0x04000000u && (cur < 0x80000000u || cur >= 0x84000000u))) {
            static uint32_t said;
            if (said != cur) {
                said = cur;
                fprintf(stderr, "[MENU] the bottom line's descriptions %08X: not ours to change\n", cur);
            }
            return;
        }
        s_wait_prev = cur;
        OD_M32(OD_W_DESCS) = a;
        changed = 1;
    }
    for (k = 0; k < 8; k++)
        OD_M32(a + 4u * (uint32_t)k) = od_now(OD_WAIT);
    if (changed)
        OD_M8(OD_W_SHOWN) = 0xFF;
}

void bo3_onlinedesc_entry_msg(const char *msg)
{
    snprintf(s_entry_msg, sizeof s_entry_msg, "%s", msg ? msg : "");
}

/* TAKEDAWN_DESC_TRACE=1: at every page change, the widget's state and how
 * many nodes its layer of the front end's pool (0x557A20) has in use: a
 * line set up and never released would show as a count that grows */
static void od_trace(void)
{
    static int on = -1;
    static uint32_t last_page;
    uint32_t page, tab, head, n = 0;
    if (on < 0) {
        const char *e = getenv("TAKEDAWN_DESC_TRACE");
        on = e && *e == '1';
    }
    page = OD_M32(BO3_VA(0x00557A70u, 0x00557D90u, 0x00557E30u));
    if (!on || page == last_page)
        return;
    last_page = page;
    tab = OD_M32(BO3_VA(0x00557A20u, 0x00557D40u, 0x00557DE0u) + 0x2Cu);
    if (tab >= 0x10000u && tab < 0x04000000u) {
        head = OD_M32(tab + (uint32_t)(int16_t)OD_M16(OD_WIDGET + 8u) * 16u + 8u);
        while (head >= 0x10000u && head < 0x04000000u && n < 1000) {
            n++;
            head = OD_M32(head);
        }
    }
    fprintf(stderr, "[MENU] desc trace: page %08X, line %s, descriptions %08X, layer nodes in use %u\n", page,
            OD_M8(OD_W_ACTIVE) ? "up" : "down", OD_M32(OD_W_DESCS), n);
}

void bo3_onlinedesc_flip(void)
{
    uint32_t arr, a;
    int k, changed = 0;
    if (g_xbox_mem_offset)
        od_trace();
    if (!g_xbox_mem_offset || !s_block || !OD_M8(OD_W_ACTIVE))
        return;
    arr = OD_M32(OD_W_DESCS);
    if (arr < 0x10000u || arr >= 0x04000000u)
        return;
    a = s_block + OD_ARRAY;
    if (arr == a) {
        bo3_onlineinfo_want(1);
        if (!od_refresh())
            return;
        for (k = 0; k < 8; k++) {
            uint32_t want = s_entry ? od_now(OD_ENTRY) : od_now(k < 4 ? OD_QUICK + k : OD_QUICK);
            if (OD_M32(a + 4u * (uint32_t)k) != want) {
                OD_M32(a + 4u * (uint32_t)k) = want;
                changed = 1;
            }
        }
    } else if (od_is(OD_MAIN, OD_M32(arr + 4u * OD_MAIN_LINE))) {
        bo3_onlineinfo_want(1);
        if (!od_refresh())
            return;
        if (OD_M32(arr + 4u * OD_MAIN_LINE) != od_now(OD_MAIN)) {
            OD_M32(arr + 4u * OD_MAIN_LINE) = od_now(OD_MAIN);
            changed = 1;
        }
    }
    if (changed)
        OD_M8(OD_W_SHOWN) = 0xFF;   /* the widget takes the pointer again on its next frame */
}
