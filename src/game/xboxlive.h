/*
 * xboxlive.h - CREATE GAME and JOIN WITH CODE lines in the title's Xbox Live
 * menu (BO-352, step 3), and the code-entry picker (step 4). The layout is
 * pure so tests/xboxlive can check it without the title's memory.
 */
#ifndef BO3_XBOXLIVE_H
#define BO3_XBOXLIVE_H

#include <stddef.h>
#include <stdint.h>

struct XBOX_GAMEPAD;            /* xinput_xbox.h; forward-declared (BO-352) */

enum { XL_QUICK, XL_CREATE, XL_JOIN, XL_BROWSE, XL_LINE_COUNT };
/* The main menu's A on its XBOX LIVE line (line 3) opens, once a boot, the
 * title's Xbox Live notice (a one-button message box: European players on a
 * different television standard, or on the European discs 50 Hz / 60 Hz
 * players, cannot be joined). It means nothing on Takedawn Online. The title
 * skips it itself when its "notice shown" byte is set, the byte the notice's
 * own branch sets; the mark sets it first on that line, and the title takes
 * its own second-visit path into the online flow (xboxlive.c, xl_notice). */
#define BO3_XL_MAIN_LIVE_LINE 3u
static inline int bo3_xboxlive_notice_skip(uint32_t line, uint8_t shown)
{
    return line == BO3_XL_MAIN_LIVE_LINE && !shown;
}
/* B on the online page exits only if its original input gate allows it;
 * the code picker retains its own B behavior. */
static inline int bo3_xboxlive_back_direct(int on_page, int picker, int b_pressed, int allowed)
{
    return on_page && !picker && b_pressed && allowed;
}
static inline uint32_t bo3_xboxlive_prompt_flags(uint32_t flags)
{
    return flags == 0x01000144u ? 0x00000105u : flags;
}
/* The prompt bar of the pages on the online path (Quick Match, lobby select,
 * the room, the create pages): the Y "Friends" prompt (bit 0x40) is dropped;
 * Takedawn Online has no friends list. Other pages keep their flags. */
static inline uint32_t bo3_xboxlive_bar_flags(int online_path_page, uint32_t flags)
{
    return online_path_page ? (flags & ~0x40u) : flags;
}
/* Leaving a game lobby (B, then Yes: the lobby screen's state byte is 0x0A,
 * "leaving") ends in the title's transition to the room. The player leaves
 * for the online page instead: the transition's target hash is that of state
 * +0x5B0 (F8512871; its high word 94414018 is the same as the room's). */
#define XL_FLOW_ROOM_LO   0xF8535FC2u
#define XL_FLOW_ONLINE_LO 0xF8512871u
#define XL_LOBBY_LEAVING  0x0Au
static inline uint32_t bo3_xboxlive_leave_target(uint32_t target_lo, int lobby_alive, unsigned lobby_state)
{
    return target_lo == XL_FLOW_ROOM_LO && lobby_alive && lobby_state == XL_LOBBY_LEAVING ? XL_FLOW_ONLINE_LO : target_lo;
}
/* The create state's Back ends in the same transition to the room. A game
 * CREATE GAME started from the online page goes back there instead. */
static inline uint32_t bo3_xboxlive_create_target(uint32_t target_lo, int direct)
{
    return target_lo == XL_FLOW_ROOM_LO && direct ? XL_FLOW_ONLINE_LO : target_lo;
}
/* The flow states a game's code does not outlive: the room (a race over, the
 * title's way back) and the online page. A code is set on the way into a game;
 * on these pages it would still filter JOIN GAME's list and be the code of
 * the room's own CREATE GAME. */
#define XL_FLOW_ST_ROOM   0x928u
#define XL_FLOW_ST_ONLINE 0x5B0u
static inline int bo3_xboxlive_code_over(uint32_t flow_state, int ours)
{
    return ours && (flow_state == XL_FLOW_ST_ROOM || flow_state == XL_FLOW_ST_ONLINE);
}
/* CREATE GAME's game is a public one that also has a code: the lobby lists
 * it for everyone and finds it with the code too (TKPUBLIC=1 on its gcre,
 * tools/lobby). That holds while the copy's code is still the one CREATE GAME
 * made (now: the code the lobby is sent); any other code (the System page's
 * Game code row, JOIN WITH CODE's) makes a private game, as before. Codes
 * compare without case, as the lobby does. */
static inline int bo3_xboxlive_gcre_public(const char *create_code, const char *now)
{
    size_t i;
    if (!create_code || !now || !create_code[0])
        return 0;
    for (i = 0; create_code[i] || now[i]; i++) {
        char a = create_code[i], b = now[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b)
            return 0;
    }
    return 1;
}
/* Returns the title case for navigator-driven selections and Browse. The
 * remaining selections are handled by the port (case 6 is the title no-op). */
static inline int bo3_xboxlive_case(int cursor, int navigator)
{
    if (navigator) return cursor == 0 ? 0 : cursor == 2 ? 2 : 6;
    return cursor == XL_BROWSE ? 0 : 6;
}

/* the list cursor, clamped to 0 .. count-1, so a cursor left past the last
 * line (by the select mark, or a rebuild to a shorter list) never indexes a
 * line the list does not have. */
static inline int bo3_xboxlive_clamp(int cursor, int count)
{
    if (count <= 0)
        return 0;
    if (cursor < 0)
        return 0;
    return cursor >= count ? count - 1 : cursor;
}

void bo3_xboxlive_start(void);
/* the picker's left/right, read and cleared at ctl_pad (controls.c chains it) */
void bo3_xboxlive_pad(uint32_t port, struct XBOX_GAMEPAD *gamepad);
/* the code entry as a player sees it, for the test navigator (onlinebot.c):
 * 1 while it is up; what is typed and the grid's column */
int  bo3_xboxlive_entry_view(char *code, size_t n, int *col);
/* every flip: the code entry's lines and the description line (onlinedesc.h) */
void bo3_xboxlive_flip(void);
/* the next gcre makes a public game with its code (bo3_xboxlive_gcre_public):
 * read by the lobby send's tag (online.c), on whichever thread sends */
int  bo3_xboxlive_gcre_public_now(void);

#endif
