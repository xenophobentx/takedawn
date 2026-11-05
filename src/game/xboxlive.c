/* xboxlive.c - the four-line TAKEDAWN ONLINE page and its code entry.
 * xl_items replaces the title's list before the widget is built. xl_select
 * dispatches our actions or maps Browse and QUICK PLAY to the title's Quick
 * Match case 0 (QUICK PLAY arms quickplay.c, which adds the no-game end).
 * CREATE GAME goes straight into the title's create state (quickplay.c).
 * While the navigator is active, its synthetic selections map to the original
 * Quick Match and Lobby Select cases; they must not start the navigator again.
 *
 * JOIN WITH CODE (step 5) turns the same page into a code entry
 * (codeentry.h): the page is built again (sub_000A6140) with six lines of
 * ours, the code and the letter grid, the page title JOIN WITH CODE (xl_head)
 * and the description line under the list (xl_desc, onlinedesc.c) saying
 * what to do. The title's list moves the cursor up and down; the rest of the
 * entry's presses are read and cleared at the pad (bo3_xboxlive_pad), so the
 * title never sees them, and typing changes only our lines' text. A on the
 * complete code is the title's own select: the title's Quick Match runs with
 * the code (quickplay.c, bo3_quickplay_begin_join), which joins the game by
 * itself; with no open game of that code it leaves by the page's own Back and
 * the entry is up again with the code and "NO OPEN GAME WITH CODE ...".
 * B with nothing typed leaves the entry through the page's own select exit
 * (the fields its A writes), and the select builds the four lines again.
 * The page is always built as a whole, as the title builds it, never one
 * widget on its own (crash0211, see rebuild()). */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "platform/recomp_patch.h"
#include "recomp_types.h"
#include "region.h"
#include "xboxlive.h"
#include "codeentry.h"
#include "onlinedesc.h"
#include "onlinebot.h"
#include "onlinenav.h"
#include "quickplay.h"
#include "online.h"
#include "xnet_hle.h"
#include "xinput_xbox.h"
#include "statustext.h"
#include "languages.h"

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
extern RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
extern ptrdiff_t g_xbox_mem_offset;
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);          /* the title's heap: (size, tag), stdcall */
extern void BO3_SUB(sub_001B99D0, sub_001B9CE0, sub_001B9D80)(void);
extern void BO3_SUB(sub_000FC9F0, sub_000FCD50, sub_000FCDF0)(void);
extern void BO3_SUB(sub_000A6140, sub_000A6490, sub_000A6530)(void);         /* the Xbox Live page's set-up (cdecl, page on the stack) */
extern void BO3_SUB(sub_000CA090, sub_000CA390, sub_000CA430)(void);         /* the description widget's set-up */
extern void BO3_SUB(sub_000CAD90, sub_000CB090, sub_000CB130)(void);         /* ... and its release (ecx the widget) */

#define XL_M8(va) (*(uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define XL_M16(va) (*(uint16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define XL_M32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define XL_PAGE       BO3_VA(0x0055DF50u, 0x0055E270u, 0x0055E310u)
#define XL_PAIR       (XL_PAGE + 4)    /* cursor, count */
#define XL_ITEMS      (XL_PAGE + 8)
#define XL_MAX        8u               /* room for the entry and the normal list */
#define XL_ITEM_SIZE  8u
#define XL_HEAP_TAG   0x54414B4Cu      /* "TAKL" */
#define XL_DESC_W     BO3_VA(0x00550C38u, 0x00550F58u, 0x00550FF8u)   /* the description widget */
#define XL_DESC_SCALE BO3_VA(0x003B17E4u, 0x003B1B04u, 0x003B1BA4u)   /* the float its set-ups pass in xmm0 */
#define XL_CUR_PAGE   BO3_VA(0x00557A70u, 0x00557D90u, 0x00557E30u)
#define XL_MGR_PHASE  BO3_VA(0x00557A64u, 0x00557D84u, 0x00557E24u)
#define XL_MGR_CODE   (XL_MGR_PHASE + 4u)
#define XL_MGR_CTX    (XL_MGR_PHASE + 8u)
#define XL_MGR_FLAG   BO3_VA(0x0055927Fu, 0x0055959Fu, 0x0055963Fu)
#define XL_MGR_FADE   BO3_VA(0x00557A5Cu, 0x00557D7Cu, 0x00557E1Cu)
#define XL_FADE_SRC   BO3_VA(0x00557838u, 0x00557B58u, 0x00557BF8u)
#define XL_FLOW       BO3_VA(0x00567170u, 0x00567490u, 0x00567530u)
#define XL_PAD_INDEX  BO3_VA(0x004AED44u, 0x004AF064u, 0x004AF104u)

static const char k_quick[]  = "QUICK PLAY";
static const char k_create[] = "CREATE GAME";
static const char k_join[]   = "JOIN WITH CODE";
static const char k_browse[] = "BROWSE GAMES";

#define XL_QUICK_LABEL  (XL_MAX * XL_ITEM_SIZE)
#define XL_CREATE_LABEL (XL_QUICK_LABEL + (uint32_t)sizeof k_quick * 2u)
#define XL_JOIN_LABEL   (XL_CREATE_LABEL + (uint32_t)sizeof k_create * 2u)
#define XL_BROWSE_LABEL (XL_JOIN_LABEL + (uint32_t)sizeof k_join * 2u)
#define XL_ROW_UNITS    32u
#define XL_ROW_BASE     (XL_BROWSE_LABEL + (uint32_t)sizeof k_browse * 2u)
#define XL_ROW_LABEL(r) (XL_ROW_BASE + (uint32_t)(r) * XL_ROW_UNITS * 2u)
#define XL_BLOCK_SIZE   (XL_ROW_LABEL(CE_ROWS))

static int xl_online_path_page(uint32_t page);
static uint32_t s_block;
static int s_listed;
static int s_logged_bad;
static int s_picker;                    /* the page shows the code entry */
static int s_rebuilding;                /* rebuild() is running the page's set-up */
static int s_leave;                     /* the entry's B asked to leave: the select builds the four lines */
static int s_entry_cursor;              /* where the list's cursor goes when the entry is built */
static int s_page_cursor = -1;          /* where it goes when the four lines are built next */
static bo3_ce s_ce;                     /* what is typed */
static int s_shown_row = -1;            /* the cursor row the lines were last drawn for */
static uint8_t s_held;                  /* the entry's buttons down at the last pad read */
static int s_a_pass;                    /* this A press goes to the title (the join) */
static uint8_t s_swallow;               /* buttons the title must not see until they are let go */

/* ---- the game code the lobby sees (xnet_hle_set_code) ----
 * QUICK PLAY and BROWSE GAMES use the code of the System page's Game code row
 * (online.code; usually none: public games); CREATE GAME makes a new code of
 * its own, JOIN WITH CODE uses the one typed. The row's code is taken again
 * whenever the code is not the one this file set last (the overlay changed it). */
static char s_cfg_code[24], s_last_set[24];
static int s_cfg_seen;
/* CREATE GAME's own code, "" when the code is another (bo3_xboxlive_gcre_public);
 * s_gcre_public is what the lobby send's tag reads (another thread) */
static char s_create_code[24];
static volatile int s_gcre_public;

int bo3_xboxlive_gcre_public_now(void) { return s_gcre_public; }

static void xl_code(const char *code)
{
    const char *now = xnet_hle_code();
    if (!s_cfg_seen || strcmp(now, s_last_set)) {
        snprintf(s_cfg_code, sizeof s_cfg_code, "%s", now);
        s_cfg_seen = 1;
    }
#ifdef XNET_HLE_HAS_CODE_QUIET
    /* our own codes: the menus say what they did, not the overlay */
    xnet_hle_set_code_quiet(code != NULL);
#endif
    xnet_hle_set_code(code ? code : s_cfg_code);
    snprintf(s_last_set, sizeof s_last_set, "%s", xnet_hle_code());
    s_create_code[0] = 0;
    s_gcre_public = 0;
}

/* a game's code ends with the game (bo3_xboxlive_code_over); the code of the
 * System page's Game code row stays */
static void xl_code_end(void)
{
    uint32_t st = XL_M32(XL_FLOW + 4u);
    int ours = s_last_set[0] && strcmp(s_last_set, s_cfg_code) && !strcmp(s_last_set, xnet_hle_code());
    if (bo3_xboxlive_code_over(st - XL_FLOW, ours)) {
        fprintf(stderr, "[MENU] online: the game's code %s is over\n", s_last_set);
        xl_code(NULL);
    }
    /* the overlay changed the code: that one makes a private game again */
    if (s_gcre_public && !bo3_xboxlive_gcre_public(s_create_code, xnet_hle_code()))
        s_gcre_public = 0;
}

static uint32_t xl_rand(void)
{
    static uint32_t n;
    struct timespec ts;
    uint32_t h = 2166136261u;
    uint8_t b[16];
    size_t i;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    memcpy(b, &ts.tv_nsec, 4);
    memcpy(b + 4, &ts.tv_sec, 4);
    n++;
    memcpy(b + 8, &n, 4);
    memcpy(b + 12, &s_block, 4);
    for (i = 0; i < sizeof b; i++)
        h = (h ^ b[i]) * 16777619u;
    return h ^ (h >> 15) ^ (uint32_t)time(NULL);
}

static void xl_put(uint32_t at, const char *t, uint32_t units)
{
    uint32_t i;
    for (i = 0; t[i] && i + 1 < units; i++)
        XL_M16(at + i * 2) = (uint16_t)(uint8_t)t[i];
    XL_M16(at + i * 2) = 0;
}

static uint32_t xl_block(void)
{
    if (!s_block) {
        uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;

        PUSH32(g_esp, XL_HEAP_TAG);
        PUSH32(g_esp, XL_BLOCK_SIZE);
        PUSH32(g_esp, 0);               /* the return address its ret takes */
        BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
        s_block = g_eax;
        g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
        if (!s_block)
            return 0;
        xl_put(s_block + XL_QUICK_LABEL, k_quick, sizeof k_quick);
        xl_put(s_block + XL_CREATE_LABEL, k_create, sizeof k_create);
        xl_put(s_block + XL_JOIN_LABEL, k_join, sizeof k_join);
        xl_put(s_block + XL_BROWSE_LABEL, k_browse, sizeof k_browse);
    }
    return s_block;
}

static void xl_bad(const char *what)
{
    if (!s_logged_bad) {
        s_logged_bad = 1;
        fprintf(stderr, "[MENU] Xbox Live: our lines not listed, %s\n", what);
    }
}

/* the entry's lines, for the list's cursor row: the text of our labels
 * changes in place (the list draws them from there) */
static void xl_render_entry(int row)
{
    char t[XL_ROW_UNITS];
    int r;
    for (r = 0; r < CE_ROWS; r++) {
        bo3_ce_line(&s_ce, r, row, t, sizeof t);
        xl_put(s_block + XL_ROW_LABEL(r), t, XL_ROW_UNITS);
    }
    s_shown_row = row;
}

/* build the page again (sub_000A6140), as the normal list or the entry.
 * Re-running the whole set-up is a page build like the title's own: it does
 * not lose front-end nodes the way a bare re-set of one widget does
 * (crash0211). Measured on int044: JOIN WITH CODE opened and closed 20 times,
 * the front-end root's pool (12 free) and the shared pool (71 free) flat and
 * the ticker on the same node at every return to the page. */
static void rebuild(int picker)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, b = g_ebx, si = g_esi, di = g_edi, bp = g_ebp;

    s_picker = picker;
    s_rebuilding = 1;
    PUSH32(g_esp, XL_PAGE);             /* the page (sub_000A6140's stack arg) */
    PUSH32(g_esp, 0);                   /* its ret 4 pops this and the arg */
    BO3_SUB(sub_000A6140, sub_000A6490, sub_000A6530)();
    s_rebuilding = 0;
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp; g_ebx = b; g_esi = si; g_edi = di; g_ebp = bp;
}

/* The entry, opened (JOIN WITH CODE) or opened again (a join came back) */
static void xl_entry_open(const char *keep, const char *msg, int cursor)
{
    char k[CE_LEN + 1];
    snprintf(k, sizeof k, "%s", keep ? keep : "");     /* keep may be s_ce's own code */
    bo3_ce_reset(&s_ce, k);
    bo3_onlinedesc_entry_msg(msg);
    s_entry_cursor = cursor;
    s_held = 0xFF;                      /* a button still down from before is not a press */
    s_a_pass = 0;
}

/* After the page title's text is pushed (0xA619F, sub_000A6140): the build's
 * first point. A build the title made itself (the player came back to the
 * page) is the four lines, unless a join came back to the entry. */
static void xl_head(void)
{
    if (!g_xbox_mem_offset || !xl_block())
        return;
    if (!s_rebuilding) {
        int r = bo3_quickplay_join_return();
        s_picker = 0;
        if (r == QP_JOIN_BROWSE_EMPTY) {
            /* BO-372: BROWSE GAMES found no game: the line says so, the
             * cursor goes to QUICK PLAY (CREATE GAME is right below it) */
            bo3_onlinedesc_page_msg("NO GAMES RIGHT NOW - QUICK PLAY OR CREATE GAME");
            s_page_cursor = XL_QUICK;
            fprintf(stderr, "[MENU] online: BROWSE GAMES found no game, the cursor on QUICK PLAY\n");
            r = QP_JOIN_NONE;
        } else if (r == QP_JOIN_BROWSE_BACK) {
            s_page_cursor = XL_BROWSE;  /* B on the list: the line it came from */
            r = QP_JOIN_NONE;
        }
        if (r != QP_JOIN_NONE) {
            char m[96];
            m[0] = 0;
            if (r == QP_JOIN_MISSED)
                snprintf(m, sizeof m, "NO OPEN GAME WITH CODE %s", s_ce.code);
            else if (r == QP_JOIN_REFUSED)          /* BO-364: it started (or closed) as we joined */
                snprintf(m, sizeof m, "GAME %s IS NO LONGER OPEN", s_ce.code);
            else if (r == QP_JOIN_REMOVED)
                snprintf(m, sizeof m, "THE HOST REMOVED YOU FROM THAT GAME");
            else if (r == QP_JOIN_RACE_GONE || r == QP_JOIN_RACE_FULL) {
                /* BO-365: the code's game was racing and the wait for it ended */
                extern const char *xbox_SettingsText(const char *key, int *locked);
                int lang = bo3_language_index(xbox_SettingsText("game.language", NULL));
                snprintf(m, sizeof m, bo3_statustext(lang >= 0 ? lang : BO3_LANG_ENGLISH,
                                                     r == QP_JOIN_RACE_FULL ? BO3_TXT_RACEWAIT_CODE_FULL
                                                                            : BO3_TXT_RACEWAIT_CODE_GONE), s_ce.code);
            }
            xl_entry_open(s_ce.code, m, CE_ROW_CODE);
            s_picker = 1;
            fprintf(stderr, "[MENU] online: the code entry again (%s)\n",
                    r == QP_JOIN_MISSED ? "no open game with the code" : r == QP_JOIN_REFUSED ? "the game refused the join"
                    : r == QP_JOIN_REMOVED ? "its host removed us" : r == QP_JOIN_RACE_GONE ? "the race waited for ended, nobody made it again"
                    : r == QP_JOIN_RACE_FULL ? "the game waited for is full" : "the search was cancelled");
        }
    }
    if (s_picker)
        XL_M32(g_esp) = s_block + XL_JOIN_LABEL;    /* the page is titled as the line chosen */
}

/* After the last item is built (0xA629B): eax the page's pair, ecx the items. */
static void xl_items(void)
{
    uint32_t pair = g_eax, items = g_ecx, b, n, i;

    if (pair - 4 != XL_PAGE || items - 8 != XL_PAGE || !g_xbox_mem_offset)
        return;
    if (!(b = xl_block()))
        return;
    if (s_picker) {
        for (i = 0; i < CE_ROWS; i++) {
            XL_M32(b + i * XL_ITEM_SIZE) = b + XL_ROW_LABEL(i);
            XL_M32(b + i * XL_ITEM_SIZE + 4) = 0;
        }
        XL_M16(pair + 2) = CE_ROWS;
        XL_M16(pair) = (uint16_t)bo3_xboxlive_clamp(s_entry_cursor, CE_ROWS);
        xl_render_entry(XL_M16(pair));
        g_ecx = b;
        s_listed = 1;
        fprintf(stderr, "[MENU] online: the code entry is up (%s)\n", s_ce.code);
        return;
    }
    n = XL_M16(pair + 2);
    if (n < 5 || n > 6) {
        xl_bad("the page's count is not five or six");
        /* our lines are not listed: a cursor left on one of them from the
         * last visit must not point past the title's own list */
        XL_M16(pair) = (uint16_t)bo3_xboxlive_clamp((int)XL_M16(pair), (int)n);
        s_listed = 0;
        return;
    }
    const uint32_t labels[] = { XL_QUICK_LABEL, XL_CREATE_LABEL, XL_JOIN_LABEL, XL_BROWSE_LABEL };
    for (i = 0; i < XL_LINE_COUNT; i++) {
        XL_M32(b + i * XL_ITEM_SIZE) = b + labels[i];
        XL_M32(b + i * XL_ITEM_SIZE + 4) = 0;
    }
    XL_M16(pair + 2) = XL_LINE_COUNT;
    if (s_page_cursor >= 0) {
        XL_M16(pair) = (uint16_t)s_page_cursor;
        s_page_cursor = -1;
    }
    XL_M16(pair) = (uint16_t)bo3_xboxlive_clamp((int)XL_M16(pair), XL_LINE_COUNT);
    s_listed = 1;
    g_ecx = b;
    fprintf(stderr, "[MENU] online: four lines listed\n");
}

/* Right after the list widget is built (0xA62C5): the page gets the title's
 * description line under the list, as the main menu and the Quick Match page
 * set it up (sub_000CA090: eax the widget, ebx the descriptions, the list's
 * pair and a flag on the stack, xmm0 the title's constant), with ours: the
 * server's counts and what the line does, or the entry's hint and message.
 * A widget left up is released first, the title's own way (sub_00064580 at
 * 0x64647: sub_000CAD90 when +0xA is set). */
static void xl_desc(void)
{
    uint32_t descs, a = g_eax, b = g_ebx, c = g_ecx, d = g_edx, sp = g_esp, si = g_esi, di = g_edi, bp = g_ebp, seh = g_seh_ebp;
    RecompXmm x0 = g_xmm0, x1 = g_xmm1, x2 = g_xmm2, x3 = g_xmm3, x4 = g_xmm4, x5 = g_xmm5, x6 = g_xmm6, x7 = g_xmm7;

    if (!g_xbox_mem_offset || !s_listed || !xnet_hle_started())
        return;
    if (!(descs = bo3_onlinedesc_page(s_picker)))
        return;
    if (XL_M8(XL_DESC_W + 0x0Au)) {
        fprintf(stderr, "[MENU] online: the description line was still up (descriptions %08X): released first\n",
                XL_M32(XL_DESC_W + 0x10u));
        g_ecx = XL_DESC_W;
        PUSH32(g_esp, 0);
        BO3_SUB(sub_000CAD90, sub_000CB090, sub_000CB130)();
    }
    g_esp = sp;
    g_xmm0.f[0] = *(float *)((uintptr_t)g_xbox_mem_offset + XL_DESC_SCALE);
    PUSH32(g_esp, 0);                   /* the flag the main menu passes */
    PUSH32(g_esp, XL_PAIR);
    PUSH32(g_esp, 0);                   /* the return address its ret 8 takes */
    g_eax = XL_DESC_W;
    g_ebx = descs;
    BO3_SUB(sub_000CA090, sub_000CA390, sub_000CA430)();
    g_eax = a; g_ebx = b; g_ecx = c; g_edx = d; g_esp = sp; g_esi = si; g_edi = di; g_ebp = bp; g_seh_ebp = seh;
    g_xmm0 = x0; g_xmm1 = x1; g_xmm2 = x2; g_xmm3 = x3; g_xmm4 = x4; g_xmm5 = x5; g_xmm6 = x6; g_xmm7 = x7;
}

/* The cursor read has completed; rewrite only eax, never the saved cursor. */
static void xl_select(void)
{
    uint32_t cur = g_eax;

    if (!g_xbox_mem_offset || !s_block)
        return;
    if (s_picker) {
        if (s_leave) {
            /* the entry's B with nothing typed: the four lines again, the
             * cursor on JOIN WITH CODE */
            s_leave = 0;
            XL_M16(XL_PAIR) = XL_JOIN;
            rebuild(0);
            XL_M16(XL_PAIR) = XL_JOIN;
            g_eax = 6;
            fprintf(stderr, "[MENU] online: the code entry is left\n");
            return;
        }
        /* A on the complete code: the title's Quick Match with the code */
        if (cur == CE_ROW_CODE && bo3_ce_complete(&s_ce)) {
            fprintf(stderr, "[MENU] online: JOIN %s (title case 0)\n", s_ce.code);
            xl_code(s_ce.code);
            bo3_quickplay_begin_join();
            g_eax = 0;
            return;
        }
        g_eax = 6;                      /* anything else: the title's switch is a no-op */
        return;
    }
    if (!s_listed)
        return;
    /* BO-372: a line chosen ends the page's message ("no games right now"):
     * the page that comes back after it says what is true then */
    bo3_onlinedesc_page_msg(NULL);
    if (bo3_onlinenav_active()) {
        g_eax = (uint32_t)bo3_xboxlive_case((int)cur, 1);
        return;
    }
    if (cur == XL_QUICK) {
        /* the title's own Quick Match (case 0) searches, joins by itself and
         * ends on its car select; quickplay.c adds the no-game end */
        fprintf(stderr, "[MENU] online: QUICK PLAY chosen (title case 0)\n");
        xl_code(NULL);
        bo3_quickplay_begin();
        g_eax = 0;
        return;
    }
    if (cur == XL_CREATE) {
        char code[CE_LEN + 1];
        /* a game of our own with a new code: a public game, listed for
         * everyone, that a friend also finds with the code (TKCODE with
         * TKPUBLIC=1, online.c's lobby tag); the lobby's top line shows the
         * code (lobbystatus.c) */
        bo3_ce_make_code(xl_rand(), code);
        if (bo3_onlinebot_code())       /* the test navigator's, so its guests can type it */
            snprintf(code, sizeof code, "%s", bo3_onlinebot_code());
        fprintf(stderr, "[MENU] online: CREATE GAME chosen (code %s, a public game)\n", code);
        xl_code(code);
        snprintf(s_create_code, sizeof s_create_code, "%s", code);
        s_gcre_public = bo3_xboxlive_gcre_public(s_create_code, xnet_hle_code());
        bo3_quickplay_create_game();
        g_eax = 6;
        return;
    }
    if (cur == XL_JOIN) {
        fprintf(stderr, "[MENU] online: JOIN WITH CODE chosen\n");
        xl_entry_open(NULL, NULL, CE_ROW_GRID);
        rebuild(1);
        g_eax = 6;
        return;
    }
    if (cur == XL_BROWSE) {
        /* BO-372: the title's Quick Match page as a list of every game
         * (quickplay.c, bo3_quickplay_begin_browse) */
        fprintf(stderr, "[MENU] online: BROWSE GAMES chosen (title case 0, the list)\n");
        xl_code(NULL);
        bo3_quickplay_begin_browse();
    }
    g_eax = (uint32_t)bo3_xboxlive_case((int)cur, 0);
}

/* the entry's B with nothing typed: the page's own select exit, as its A
 * writes it (sub_000A63D0 at 0xA6454..0xA652A: code 1, the pad, the fade,
 * phase 3); the page manager fades it out and hands the state the select,
 * where xl_select builds the four lines */
static void xl_entry_leave(void)
{
    if (XL_M32(XL_MGR_PHASE) != 5u)
        return;
    s_leave = 1;
    s_swallow = s_held;                 /* B (and anything else down) stays ours until let go */
    XL_M32(XL_MGR_CODE) = 1u;
    XL_M32(XL_MGR_CTX) = (uint32_t)(int32_t)(int8_t)XL_M8(XL_PAD_INDEX);
    XL_M8(XL_MGR_FLAG) = 0;
    XL_M32(XL_MGR_FADE) = XL_M32(XL_FADE_SRC);
    XL_M32(XL_MGR_PHASE) = 3u;
}

/* The pad rewrite point (controls.c chains this in at ctl_pad). While the
 * code entry is up, A, B, X, left and right are the entry's: a fresh press is
 * applied (codeentry.h) and every one of them is cleared, so the title sees
 * none of them, except an A begun on the complete code (the title's select:
 * the join). Up and down stay the title's (its list moves the cursor). */
void bo3_xboxlive_pad(uint32_t port, struct XBOX_GAMEPAD *g)
{
    uint8_t now = 0, fresh;
    int row, act, k;
    static const struct { uint8_t bit; int btn; } keys[] = {
        { 1, CE_BTN_A }, { 2, CE_BTN_B }, { 4, CE_BTN_X }, { 8, CE_BTN_LEFT }, { 16, CE_BTN_RIGHT },
    };
    if (!g_xbox_mem_offset || port != 2u || !g)
        return;
    /* Friends is not a Takedawn Online feature: Y does nothing on the online
     * path's pages (where the prompt is hidden, xl_bar / xl_prompt) */
    if (g->bAnalogButtons[XBOX_BUTTON_Y] >= XBOX_ANALOG_BUTTON_THRESHOLD) {
        uint32_t page = XL_M32(XL_CUR_PAGE);
        if (xl_online_path_page(page) || page == XL_PAGE)
            g->bAnalogButtons[XBOX_BUTTON_Y] = 0;
    }
    if (bo3_quickplay_room_pending())
        g->bAnalogButtons[XBOX_BUTTON_A] = 0;   /* the mode page's Continue waits for the room */
    {   /* BO-372: BROWSE GAMES takes A on a racing or full game, and B on its list */
        int m = bo3_quickplay_browse_pad(g->bAnalogButtons[XBOX_BUTTON_A] >= XBOX_ANALOG_BUTTON_THRESHOLD,
                                         g->bAnalogButtons[XBOX_BUTTON_B] >= XBOX_ANALOG_BUTTON_THRESHOLD);
        if (m & 1) g->bAnalogButtons[XBOX_BUTTON_A] = 0;
        if (m & 2) g->bAnalogButtons[XBOX_BUTTON_B] = 0;
    }
    if (g->bAnalogButtons[XBOX_BUTTON_A] >= XBOX_ANALOG_BUTTON_THRESHOLD) now |= 1;
    if (g->bAnalogButtons[XBOX_BUTTON_B] >= XBOX_ANALOG_BUTTON_THRESHOLD) now |= 2;
    if (g->bAnalogButtons[XBOX_BUTTON_X] >= XBOX_ANALOG_BUTTON_THRESHOLD) now |= 4;
    if (g->wButtons & XBOX_GAMEPAD_DPAD_LEFT) now |= 8;
    if (g->wButtons & XBOX_GAMEPAD_DPAD_RIGHT) now |= 16;
    if (!s_picker || s_leave || XL_M32(XL_CUR_PAGE) != XL_PAGE) {
        s_swallow &= now;               /* let go: the title has the button back */
        s_held = now;
        if (s_swallow & 1) g->bAnalogButtons[XBOX_BUTTON_A] = 0;
        if (s_swallow & 2) g->bAnalogButtons[XBOX_BUTTON_B] = 0;
        if (s_swallow & 4) g->bAnalogButtons[XBOX_BUTTON_X] = 0;
        return;
    }
    row = XL_M16(XL_PAIR);
    fresh = (uint8_t)(now & ~s_held);
    s_held = now;
    if (!(now & 1))
        s_a_pass = 0;
    /* only while the page is up and still (phase 5), never into a fade */
    for (k = 0; k < 5 && XL_M32(XL_MGR_PHASE) == 5u; k++) {
        if (!(fresh & keys[k].bit))
            continue;
        act = bo3_ce_press(&s_ce, keys[k].btn, row);
        switch (act) {
        case CE_ACT_TYPE:
            if (bo3_ce_complete(&s_ce)) {
                XL_M16(XL_PAIR) = CE_ROW_CODE;  /* five letters: the cursor to JOIN */
                row = CE_ROW_CODE;
            }
            /* fall through */
        case CE_ACT_DEL:
            bo3_onlinedesc_entry_msg(NULL);     /* a message goes once the code changes */
            break;
        case CE_ACT_JOIN:
            s_a_pass = 1;
            break;
        case CE_ACT_LEAVE:
            xl_entry_leave();
            break;
        }
        if (act != CE_ACT_NONE)
            fprintf(stderr, "[MENU] code entry: %s on line %d -> %s\n",
                    keys[k].btn == CE_BTN_A ? "A" : keys[k].btn == CE_BTN_B ? "B" : keys[k].btn == CE_BTN_X ? "X" :
                    keys[k].btn == CE_BTN_LEFT ? "left" : "right", row, s_ce.code);
    }
    xl_render_entry(row);
    if (!s_a_pass)
        g->bAnalogButtons[XBOX_BUTTON_A] = 0;
    g->bAnalogButtons[XBOX_BUTTON_B] = 0;
    g->bAnalogButtons[XBOX_BUTTON_X] = 0;
    g->wButtons &= (WORD)~(XBOX_GAMEPAD_DPAD_LEFT | XBOX_GAMEPAD_DPAD_RIGHT);
}

int bo3_xboxlive_entry_view(char *code, size_t n, int *col)
{
    if (!g_xbox_mem_offset || !s_picker || s_leave || XL_M32(XL_CUR_PAGE) != XL_PAGE)
        return 0;
    snprintf(code, n, "%s", s_ce.code);
    *col = s_ce.col;
    return 1;
}

/* every flip: the entry's lines follow the list's cursor (the title moves it
 * with up and down), and the description line its counts and messages */
void bo3_xboxlive_flip(void)
{
    if (!g_xbox_mem_offset)
        return;
    if (s_picker && s_block && XL_M32(XL_CUR_PAGE) == XL_PAGE && XL_M16(XL_PAIR + 2) == CE_ROWS &&
        XL_M16(XL_PAIR) != s_shown_row) {
        fprintf(stderr, "[MENU] code entry: cursor on line %d\n", XL_M16(XL_PAIR));
        xl_render_entry(XL_M16(XL_PAIR));
    }
    xl_code_end();
    bo3_onlinedesc_flip();
}

/* At the online page's prompt-bar call: the first stack argument is the
 * prompt object, the second is the flags. Bit 0x1000000 asks for Sign out;
 * mask 0x1 asks for localized Back; mask 0x40 adds Friends. Validate before
 * changing, preserving the remaining Select and Change prompts. */
static void xl_prompt(void)
{
    if (!g_xbox_mem_offset) return;
    uint32_t object = BO3_VA(0x00550CB0u, 0x00550FD0u, 0x00551070u);
    if (XL_M32(g_esp) == object)
        XL_M32(g_esp + 4) = bo3_xboxlive_prompt_flags(XL_M32(g_esp + 4));
}

/* After sub_000D4700 (the front end's prompt bar) loaded its flags into eax:
 * on the online path's pages the Friends prompt goes, as on the online page. */
static int xl_online_path_page(uint32_t page)
{
    return page == BO3_VA(0x005628E0u, 0x00562C00u, 0x00562CA0u) ||     /* Quick Match */
           page == BO3_VA(0x00561100u, 0x00561420u, 0x005614C0u) ||     /* lobby select */
           page == BO3_VA(0x00564490u, 0x005647B0u, 0x00564850u) ||     /* the room */
           page == BO3_VA(0x0055DF8Cu, 0x0055E2ACu, 0x0055E34Cu) ||     /* create: name */
           page == BO3_VA(0x00564900u, 0x00564C20u, 0x00564CC0u) ||     /* create: mode */
           page == BO3_VA(0x0055E07Cu, 0x0055E39Cu, 0x0055E43Cu);       /* creating */
}
static void xl_bar(void)
{
    if (!g_xbox_mem_offset) return;
    if (XL_M32(g_esp + 4) != BO3_VA(0x00550CB0u, 0x00550FD0u, 0x00551070u)) return;
    g_eax = bo3_xboxlive_bar_flags(xl_online_path_page(XL_M32(BO3_VA(0x00557A70u, 0x00557D90u, 0x00557E30u))), g_eax);
}

/* After `push 0xF8535FC2` in sub_0006DAC0's leave case (the lobby state's
 * way back to the room): the hash is the top of the stack. A player who left
 * the game (B, Yes) goes to the online page, through the same transition. */
static void xl_leave(void)
{
    uint8_t st = 0;
    uint32_t lo, to;
    int alive;
    if (!g_xbox_mem_offset) return;
    lo = XL_M32(g_esp);
    alive = bo3_online_lobby_now(&st, NULL, NULL);
    /* the screen may have stopped by now (the fade, the lobby's answer) */
    if (!alive && bo3_online_lobby_leaving()) {
        alive = 1;
        st = XL_LOBBY_LEAVING;
    }
    fprintf(stderr, "[MENU] leaving the game lobby: target %08X, lobby screen %d state 0x%02X\n", lo, alive, st);
    to = bo3_xboxlive_leave_target(lo, alive, st);
    if (to != lo) {
        XL_M32(g_esp) = to;
        fprintf(stderr, "[MENU] leaving the game lobby: to the online page\n");
    }
}

/* After `push 0xF8535FC2` in sub_0006DE20: the game went from under the player
 * (the host left) and the lobby's message box was answered. The room is no
 * page of the online path: back on the online page, as after leaving. */
static void xl_gone(void)
{
    if (!g_xbox_mem_offset || XL_M32(g_esp) != XL_FLOW_ROOM_LO)
        return;
    XL_M32(g_esp) = XL_FLOW_ONLINE_LO;
    fprintf(stderr, "[MENU] the game is gone: to the online page\n");
}

/* After `push 0xF8535FC2` in the create state's Back case (0x6D254): the hash
 * is the top of the stack. A game CREATE GAME started from the online page
 * goes back there (and the online page ends the code, xl_code_end). */
static void xl_cr_back(void)
{
    uint32_t lo, to;
    if (!g_xbox_mem_offset) return;
    lo = XL_M32(g_esp);
    to = bo3_xboxlive_create_target(lo, bo3_quickplay_create_direct());
    if (to != lo) {
        XL_M32(g_esp) = to;
        s_page_cursor = XL_CREATE;
        fprintf(stderr, "[MENU] online: CREATE GAME left, back to the online page\n");
    }
}

/* The title's B check, before it constructs the sign-out question. The
 * writes and cleanup call are the Yes branch of sub_000A65B0: the normal
 * page exit then dispatches event 3 through the title's flow state machine.
 * Picker B is left to the existing picker/page flow. */
static void xl_back(void)
{
    if (!g_xbox_mem_offset || !bo3_xboxlive_back_direct(g_edi == XL_PAGE, s_picker, (g_eax & 0xffu) != 0, 1))
        return;
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;
    uint32_t b = g_ebx, si = g_esi, di = g_edi, bp = g_ebp, seh = g_seh_ebp;
    g_ecx = BO3_VA(0x0057BCFCu, 0x0057C00Cu, 0x0057C0ACu);
    PUSH32(g_esp, 0);
    BO3_SUB(sub_001B99D0, sub_001B9CE0, sub_001B9D80)();
    int allowed = g_eax != 0;
    if (allowed) {
        XL_M8(BO3_VA(0x005A375Au, 0x005A3A6Au, 0x005A3B0Au)) = 0;
        g_edi = BO3_VA(0x00572980u, 0x00572C90u, 0x00572D30u);
        PUSH32(g_esp, 0);
        BO3_SUB(sub_000FC9F0, sub_000FCD50, sub_000FCDF0)();
        XL_M8(BO3_VA(0x00567820u, 0x00567B40u, 0x00567BE0u)) = 0;
        XL_M8(BO3_VA(0x00567822u, 0x00567B42u, 0x00567BE2u)) = 0;
        XL_M8(BO3_VA(0x0055927Fu, 0x0055959Fu, 0x0055963Fu)) = 0;
        XL_M8(BO3_VA(0x00411572u, 0x004118A2u, 0x00411942u)) = 1;
        XL_M32(BO3_VA(0x00557A68u, 0x00557D88u, 0x00557E28u)) = 3;
        XL_M32(BO3_VA(0x00557A5Cu, 0x00557D7Cu, 0x00557E1Cu)) =
            XL_M32(BO3_VA(0x00557838u, 0x00557B58u, 0x00557BF8u));
        XL_M32(BO3_VA(0x00557A64u, 0x00557D84u, 0x00557E24u)) = 4;
        a &= ~0xffu;
        fprintf(stderr, "[MENU] online: Back uses sign-out Yes path\n");
    }
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
    g_ebx = b; g_esi = si; g_edi = di; g_ebp = bp; g_seh_ebp = seh;
}

/* After `mov [edi + 4], eax` in sub_00078160 (the main menu's A; eax the
 * line, flags still from `cmp eax, 3`), before its `cmp byte [notice shown], 0`.
 * On XBOX LIVE the title would set the byte, open the notice (string slot
 * 0xF88 on NTSC-U, 0xA0A / 0xA0B on the European discs, by the 50/60 Hz byte)
 * and leave by sub_000780D0 when it is closed; with the byte already set it
 * leaves at once by the same writes (0x781F6). Nothing else reads the byte. */
#define XL_NOTICE_SHOWN BO3_VA(0x0057C3D5u, 0x0057C6E5u, 0x0057C785u)
static void xl_notice(void)
{
    if (!g_xbox_mem_offset || !bo3_xboxlive_notice_skip(g_eax, XL_M8(XL_NOTICE_SHOWN)))
        return;
    XL_M8(XL_NOTICE_SHOWN) = 1;
    fprintf(stderr, "[MENU] main menu: Xbox Live notice skipped\n");
}

void bo3_xboxlive_start(void)
{
    recomp_patch_on_mark("xl_notice", xl_notice);
    recomp_patch_on_mark("xl_prompt", xl_prompt);
    recomp_patch_on_mark("xl_back", xl_back);
    recomp_patch_on_mark("xl_bar", xl_bar);
    recomp_patch_on_mark("xl_leave", xl_leave);
    recomp_patch_on_mark("xl_gone", xl_gone);
    recomp_patch_on_mark("cr_back", xl_cr_back);
    recomp_patch_on_mark("xl_head", xl_head);
    recomp_patch_on_mark("xl_items", xl_items);
    recomp_patch_on_mark("xl_desc", xl_desc);
    recomp_patch_on_mark("xl_select", xl_select);
    bo3_quickplay_start();
}
