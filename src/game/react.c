/*
 * react.c - the quick reactions (BO-352, step 7). See react.h.
 *
 * The input is taken at ctl_pad (controls.c calls bo3_react_pad on the menu
 * pad, after the title has read the state and before it uses it): a reaction
 * press is sent on the toolkit's TKRX side channel and cleared from the pad,
 * so the title never sees it. Only on the game lobby (LT + d-pad; the plain
 * d-pad moves the player-list cursor) and the results screen (the d-pad
 * alone); never while driving. The keys 1..5 are ids 0..4, whatever screen
 * the reaction is on.
 *
 * The display drains the channel on the title's thread each online frame
 * (online.c's lobby_watch calls bo3_react_tick): the newest reaction is kept
 * as "NAME: PHRASE" for 3 s, where lane B's lobby_line (the game lobby's
 * top message line) shows it through bo3_react_ticker_text. Behind
 * online.reactions_bubble (off until the row layout is scene-tested), the
 * lobby bubble instead points the sender's row name (screen +0x135C, a
 * 6-pointer array, STEP0-NOTES 0.3) at our own UTF-16 block for 3 s, then
 * puts the title's pointer back, each lobby frame (bo3_react_lobby_frame).
 * The title's own strings are never written.
 */
#include "region.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "react.h"
#include "online.h"                 /* bo3_online_lobby_now */
#include "stay.h"                   /* bo3_stay_peer_left */
#include "onlinescreens.h"          /* bo3_online_screen_for (the shared screen table) */
#include "recomp_types.h"
#include "keyboard.h"               /* xbox_FramebufferKeyDown */
#include "xinput_xbox.h"            /* XBOX_GAMEPAD, button flags */

extern ptrdiff_t g_xbox_mem_offset;
extern const char *xbox_SettingsText(const char *key, int *locked);
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);   /* the title's heap: (size, tag), stdcall */

#define RX_HEAP_TAG 0x54454E58u     /* "XNET" */
#define RX_ROWS     0x135Cu          /* the lobby screen's 6 name pointers */

#define RX_M32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define RX_FLOW   BO3_VA(0x00567170u, 0x00567490u, 0x00567530u)
#define RX_PAGE   BO3_VA(0x00557A70u, 0x00557D90u, 0x00557E30u)
#define RX_NPEERS BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u)
#define RX_PEERS  BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u)
/* a title peer record ([RX_PEERS + 4k], 0 for this console): its address in
 * host byte order (the reaction's title_ip is in network order) and its
 * gamertag, ASCII, null-terminated */
#define RX_PEER_INA  0x24u
#define RX_PEER_NAME 0x4B8u
#define RX_NAME_MAX  16

static int s_on;                    /* online.reactions: show them */
static int s_held;                  /* the reaction id held since the last frame, -1 none */

static unsigned long long react_ms(void)
{
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
#endif
}

/* the newest received reaction, for the ticker (bo3_react_ticker_text) */
static char s_ticker[64];
static unsigned long long s_ticker_until;   /* 0 = none */

/* the bubble (online.reactions_bubble): one row's name pointer swapped for
 * our UTF-16 text; off until the row layout is scene-tested */
static bo3_react_bubble s_bubble = { -1, 0, 0 };
static uint32_t s_bubble_block;             /* the title-heap UTF-16 block */
static uint32_t s_bubble_scr;               /* the screen the bubble is for */
static int s_bubble_on;
static uint32_t s_bubble_pending_id;        /* a reaction to resolve, 0 none */
static int s_bubble_pending_value;

static int ok_va(uint32_t va) { return va >= 0x10000u && va < 0x04000000u; }

/* a 0x40-byte block on the title's heap (menuentry.c's pattern), for the
 * bubble's UTF-16 text: the title's font reads the pointer in guest memory */
static uint32_t rx_alloc(uint32_t size)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, p;
    PUSH32(g_esp, RX_HEAP_TAG);
    PUSH32(g_esp, size);
    PUSH32(g_esp, 0);
    BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
    p = g_eax;
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
    if (p)
        memset((void *)XBOX_PTR(p), 0, size);
    return p;
}

/* the reaction screen now: the game lobby, the results page, or none */
static int react_screen(void)
{
    uint32_t cur, page;
    int sc;
    if (!g_xbox_mem_offset)
        return -1;
    if (bo3_online_lobby_now(NULL, NULL, NULL))
        return OB_SC_LOBBY;
    cur = RX_M32(RX_FLOW + 4);
    page = RX_M32(RX_PAGE);
    sc = bo3_online_screen_for(ok_va(cur) ? cur - RX_FLOW : 0, page);
    return sc == OB_SC_RESULTS ? OB_SC_RESULTS : -1;
}

/* the peer table's index of the player at title_ip, -1 none. The record's
 * +8 is not the address (a small id); +0x24 is, byte-swapped. */
static int react_row_of(uint32_t title_ip)
{
    uint32_t np = RX_M32(RX_NPEERS), k;
    uint32_t ina = (title_ip >> 24) | ((title_ip >> 8) & 0xFF00u) | ((title_ip << 8) & 0xFF0000u) | (title_ip << 24);
    for (k = 0; k < np && k < 8; k++) {
        uint32_t P = RX_M32(RX_PEERS + 4 * k);
        if (ok_va(P) && RX_M32(P + RX_PEER_INA) == ina)
            return (int)k;
    }
    return -1;
}

/* the sender's gamertag from its title peer record; "" when the address is
 * not a player's. (The peer table's order is not the lobby's: on a guest the
 * host is entry 0 and this console an empty entry, so the record's own name
 * is read, not the lobby view's row.) */
static void react_peer_name(uint32_t title_ip, char *out, int n)
{
    int row = react_row_of(title_ip), i;
    const char *nm;
    out[0] = 0;
    if (row < 0 || n <= 0)
        return;
    nm = (const char *)((uintptr_t)g_xbox_mem_offset + RX_M32(RX_PEERS + 4u * (uint32_t)row) + RX_PEER_NAME);
    for (i = 0; i < n - 1 && i < RX_NAME_MAX - 1 && nm[i]; i++)
        out[i] = (char)(nm[i] >= 0x20 && nm[i] < 0x7F ? nm[i] : '?');
    out[i] = 0;
}

void bo3_react_start(void)
{
    const char *v = getenv("RECOMP_ONLINE_REACTIONS");
    const char *b = getenv("RECOMP_ONLINE_REACTIONS_BUBBLE");
    if (!v || !*v)
        v = xbox_SettingsText("online.reactions", NULL);
    if (!b || !*b)
        b = xbox_SettingsText("online.reactions_bubble", NULL);
    s_on = bo3_react_setting_on(v);
    s_bubble_on = b && *b && !strcmp(b, "on");
    s_held = -1;
    if (!s_on)
        xnet_hle_log("reactions: hidden (online.reactions = off)");
}

int bo3_react_pad(void *g)
{
    XBOX_GAMEPAD *pad = (XBOX_GAMEPAD *)g;
    int screen = react_screen(), id = -1, dir = -1, i;

    if (screen < 0 || !bo3_react_active_screen(screen)) {
        s_held = -1;
        return 0;
    }
    /* the d-pad, with LT held in the lobby (the plain d-pad is the cursor's) */
    if (pad->wButtons & XBOX_GAMEPAD_DPAD_UP)
        dir = 0;
    else if (pad->wButtons & XBOX_GAMEPAD_DPAD_RIGHT)
        dir = 1;
    else if (pad->wButtons & XBOX_GAMEPAD_DPAD_LEFT)
        dir = 2;
    else if (pad->wButtons & XBOX_GAMEPAD_DPAD_DOWN)
        dir = 3;
    if (dir >= 0 && (screen == OB_SC_RESULTS ||
                     pad->bAnalogButtons[XBOX_BUTTON_LTRIGGER] > XBOX_ANALOG_BUTTON_THRESHOLD))
        id = bo3_react_dpad_id(screen, dir);
    /* the keys 1..5 are ids 0..4, whatever screen */
    for (i = 0; i < XR_REACT_N && id < 0; i++)
        if (xbox_FramebufferKeyDown('1' + i))
            id = i;
    if (id < 0) {
        s_held = -1;
        return 0;
    }
    if (id == s_held)
        return 0;               /* held: one reaction per press */
    s_held = id;
    if (!xnet_hle_react_send(XR_REACT_REACTION, id)) {
        s_held = -1;            /* not taken (offline, or the send bucket) */
        return 0;
    }
    /* the title never sees the press: a d-pad reaction clears the d-pad (the
     * cursor in the lobby, the menus on results); the keys are no button of
     * the title's, so a key reaction clears nothing */
    if (dir >= 0)
        pad->wButtons &= (uint16_t)~(XBOX_GAMEPAD_DPAD_UP | XBOX_GAMEPAD_DPAD_DOWN |
                                     XBOX_GAMEPAD_DPAD_LEFT | XBOX_GAMEPAD_DPAD_RIGHT);
    return 1;
}

void bo3_react_tick(void)
{
    uint32_t title_ip;
    int kind, value;
    xnet_hle_react_tick();          /* the due second copy (xnet_hle.c) */
    while (xnet_hle_react_get(&title_ip, &kind, &value)) {
        char name[16];
        if (kind == XR_REACT_LEFT) {
            /* a player quit on its results (stay.c): not a phrase, never hidden */
            react_peer_name(title_ip, name, sizeof name);
            bo3_stay_peer_left(name, value);
            continue;
        }
        if (kind != XR_REACT_REACTION || !s_on)
            continue;               /* the countdown is step 5a (lane B) */
        /* the bubble takes it in the lobby (off until scene-tested); the
         * ticker text everywhere else */
        if (s_bubble_on && bo3_online_lobby_now(NULL, NULL, NULL)) {
            s_bubble_pending_id = title_ip;
            s_bubble_pending_value = value;
            continue;
        }
        react_peer_name(title_ip, name, sizeof name);
        bo3_react_upper_ascii(name);
        if (name[0])
            snprintf(s_ticker, sizeof s_ticker, "%s: %s", name, bo3_react_phrase(value));
        else
            snprintf(s_ticker, sizeof s_ticker, "%s", bo3_react_phrase(value));
        s_ticker_until = react_ms() + 3000;
    }
}

int bo3_react_ticker_text(char *out, int n)
{
    if (!s_ticker_until || react_ms() >= s_ticker_until) {
        s_ticker_until = 0;
        return 0;
    }
    if (out && n > 0)
        snprintf(out, (size_t)n, "%s", s_ticker);
    return 1;
}

/* "NAME: PHRASE" (upper-case ASCII) into the bubble's UTF-16 block */
static void bubble_block_build(uint32_t title_ip, int value)
{
    char name[16], text[40];
    uint16_t *w = (uint16_t *)XBOX_PTR(s_bubble_block);
    int i;
    react_peer_name(title_ip, name, sizeof name);
    bo3_react_upper_ascii(name);
    if (name[0])
        snprintf(text, sizeof text, "%s: %s", name, bo3_react_phrase(value));
    else
        snprintf(text, sizeof text, "%s", bo3_react_phrase(value));
    for (i = 0; text[i] && i < 31; i++)
        w[i] = (uint16_t)(uint8_t)text[i];
    w[i] = 0;
}

void bo3_react_lobby_frame(uint32_t scr)
{
    uint64_t now = react_ms();
    int row, st;
    uint32_t orig;

    if (!s_bubble_on || !ok_va(scr))
        return;
    /* a new lobby screen: the title rebuilt its rows, so a bubble left from
     * the last one is forgotten (its pointer may be freed) */
    if (s_bubble.row >= 0 && s_bubble_scr != scr) {
        s_bubble.row = -1;
        s_bubble.until = 0;
    }
    /* resolve a pending reaction into a bubble on this screen */
    if (s_bubble_pending_id) {
        uint32_t id = s_bubble_pending_id;
        s_bubble_pending_id = 0;
        row = react_row_of(id);
        if (row >= 0 && !(s_bubble.row >= 0 && s_bubble_scr == scr)) {
            if (!s_bubble_block)
                s_bubble_block = rx_alloc(0x40);
            if (s_bubble_block) {
                orig = RX_M32(scr + RX_ROWS + 4u * (uint32_t)row);
                bubble_block_build(id, s_bubble_pending_value);
                bo3_react_bubble_begin(&s_bubble, row, orig, now + 3000);
                s_bubble_scr = scr;
            }
        }
    }
    st = bo3_react_bubble_step(&s_bubble, now, &row, &orig);
    if (st == BO3_REACT_BUBBLE_SWAP)
        RX_M32(scr + RX_ROWS + 4u * (uint32_t)row) = s_bubble_block;
    else if (st == BO3_REACT_BUBBLE_RESTORE)
        RX_M32(scr + RX_ROWS + 4u * (uint32_t)row) = orig;
}
