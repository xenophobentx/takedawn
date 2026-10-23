/*
 * modsmenu.c - a MODS page in the title's own Settings menu (Driver Details >
 * Settings), for installed mods (modfs.h): each mod on or off from the next
 * start. Listed only when a mod folder is in use (TAKEDAWN_MODS, or Android's
 * mods folder: the player's own opt-in to mods); TAKEDAWN_MODS_MENU=0 hides
 * it. Otherwise the menu is the title's (and menuentry.c's), as before.
 *
 * Where: the Settings page object 0x55ABC4, built by sub_00085170 on the
 * Settings state's enter (menuentry.c describes it). MODS is one more line
 * of the title's list widget (sub_000F0EA0), after the title's four lines
 * and, when menuentry.c lists it, TAKEDAWN SETTINGS.
 *
 * The MODS page is the same page object, list widget, title, sounds and
 * transitions, built in a second mode:
 *   - A on MODS is the title's own A (its select sound, the page's exit),
 *     then the Settings state's event 5 (sub_000696D0), which would look the
 *     cursor up in its four-entry next-page table (0x3EAF34) and change
 *     state. The "mm_flow_sel" mark (after `push 0x65857D40`, 0x69733) makes
 *     that the state machine's own no-op (sub_000146E0 returns when the target
 *     is the current state: the pushed hash becomes the current state's, from
 *     the machine's table [0x567170 + 0xC], 16-byte entries {hash lo, hash hi,
 *     offset from the machine}) and builds the page again through
 *     sub_00085170 with the MODS lines and the title MODS: the page's own
 *     enter animation. Before the mark the title has already read the table
 *     entry for the cursor (a line past its four), but sub_000146E0 returns
 *     before using it when the target is the current state; so A is let
 *     through only when that hash is found, and on no other line from the
 *     fifth on (a line the title does not have, ours or menuentry.c's).
 *     Event 5 for line 3 (EA GAMES TRAX) does not reach the mark: the MODS
 *     page never lets A through on its line 3 either.
 *   - B on the MODS page is the title's own B (back sound, exit); event 5
 *     with result 3 would go to Driver Details: "mm_flow_back" (after
 *     `push 0xEF6E02D9`, 0x696F8) makes it the same no-op and builds the
 *     Settings list again, the cursor on MODS (where it is now: the overlay
 *     can turn menuentry.c's line on or off while the page is open).
 *   - A on a mod's line or APPLY (menuentry.c's si_select mark, after the A
 *     test, 0x853B4) is taken here, the title never starts its exit for it:
 *     the line acts and the title's select sound (sub_00141010, as at
 *     0x8542B) plays. Up, down and B stay the title's.
 * The list widget makes its line frames once, at set-up, at most 9
 * (WIDGET_MAX): six mods fit one page, more are shown four to a page with
 * PREVIOUS PAGE / NEXT PAGE, and a page change is the title's own A and exit,
 * then mm_flow_sel builds the next page.
 *
 * Choices are kept here, as chosen (whatever the mod list says later),
 * until APPLY, which saves them through bo3_modfs_set_enabled (the mod list,
 * written whole or not at all); they are used from the next start. Nothing
 * here reads or writes a file itself.
 * Every text shown, mod text included, goes through put_label: printable
 * ASCII in upper case (the title font's), anything else '?', cut to fit.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "platform/recomp_patch.h"
#include "recomp_types.h"
#include "modfs.h"

extern RECOMP_TLS uint32_t g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_esp, g_ebp, g_seh_ebp;
extern ptrdiff_t g_xbox_mem_offset;
extern void sub_001D2879(void);          /* the title's heap: (size, tag), stdcall */
extern void sub_00085170(void);          /* the Settings page's set-up: eax = the page */
extern void sub_00141010(void);          /* a front-end sound, stdcall 6, eax */

#define MM8(va)  (*(uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define MM16(va) (*(uint16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define MM32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define SP_PAGE      0x0055ABC4u
#define SP_CURSOR    (SP_PAGE + 4)
#define SP_COUNT     (SP_PAGE + 6)
#define SP_ITEMS     (SP_PAGE + 8)
#define FLOW         0x00567170u          /* the front end's state machine */
#define STRTAB       0x004D532Cu          /* [[STRTAB] + 0xC] = the string pointers */
#define TITLE_SLOT   0x6C4u               /* the Settings page's title string */
#define HEAP_TAG     0x4D4F4453u          /* "MODS" */

#define MAX_LINES    9                    /* WIDGET_MAX, and the Settings list (at most 6) */
#define LABEL_CH     48                   /* UTF-16 units per label, the terminator included */
#define WIDGET_MAX   9                    /* the list widget (0x5509F4) keeps two 9-entry arrays of line frames
                                           * (+0x14, +0x38); a tenth line would overwrite its next fields */
#define PAGE_ALL     6                    /* up to 6 mods: one page (6 + APPLY + details + info = 9) */
#define PAGE_MODS    4                    /* more: 4 a page + PREVIOUS + NEXT + 3 = 9 */
#define NAME_CH      24                   /* a longer name is cut to 22 and ".." */
#define TITLE_LINES  4                    /* the title's own Settings lines; its next-page table has 4 */

static uint32_t s_blk;                     /* items (MAX_LINES * 8), then labels, then the title */
static int s_hidden;                       /* TAKEDAWN_MODS_MENU=0, read at the start */
static int s_mode;                         /* 0 the Settings list, 1 the MODS page */
static int s_rebuilding;                   /* sub_00085170 called from here: the page asked for */
static int s_page_built;                   /* a page change: the cursor to its first mod */
static int s_mods_line = -1;               /* MODS in the Settings list */
static int s_first;                        /* the first mod shown */
static int s_nlines, s_apply_line, s_info_line, s_prev_line, s_next_line, s_detail_line = -1;
static char s_detail[LABEL_CH], s_info[LABEL_CH];
static int s_line_mod[MAX_LINES];
static int s_page_pending = -1;            /* the first mod of the page to build at the next event 5 */
static unsigned char s_touched[BO3_MODFS_MAX_MODS]; /* the player chose for it (s_choice) */
static unsigned char s_choice[BO3_MODFS_MAX_MODS];  /* on / off as chosen, whatever the mod list says */
static unsigned char s_saved[BO3_MODFS_MAX_MODS];   /* what the mod list says (read at each build and APPLY) */
static int s_save_failed;                  /* the last APPLY could not save every choice */

/* Made by the build (cmake/modsmenu_marks.cmake): 1 when the generated code
 * calls si_items, si_select, mm_flow_sel and mm_flow_back where
 * config/patches.json puts them. */
extern int bo3_modsmenu_gen_marks(void);
static int s_flow;                         /* both marks generated and reaching this file's functions */
static int s_probing, s_probed;            /* bo3_modsmenu_start calls each mark once: bits 1, 2 */

static int mm_on(void)
{
    return s_flow && g_xbox_mem_offset && !s_hidden && bo3_modfs_folder_count() > 0;
}

static uint32_t label_va(int i) { return s_blk + MAX_LINES * 8 + (uint32_t)i * LABEL_CH * 2; }
static uint32_t title_va(void)  { return s_blk + MAX_LINES * 8 + MAX_LINES * LABEL_CH * 2; }

/* Text for the title's font: printable ASCII in upper case; a UTF-8
 * character (or any other byte) is one '?'. At most n - 1 characters. */
static void shown(char *out, size_t n, const char *s)
{
    size_t k = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && k + 1 < n) {
        if (*p >= 0x20 && *p < 0x7F) {
            out[k++] = (char)(*p >= 'a' && *p <= 'z' ? *p - 'a' + 'A' : *p);
            p++;
        } else {
            out[k++] = '?';
            for (p++; (*p & 0xC0) == 0x80; p++)
                ;
        }
    }
    out[k] = 0;
}

static void put_label(uint32_t va, const char *s)
{
    char t[LABEL_CH];
    int i;
    shown(t, sizeof t, s);
    for (i = 0; t[i]; i++)
        MM16(va + (uint32_t)i * 2) = (uint8_t)t[i];
    MM16(va + (uint32_t)i * 2) = 0;
}

static uint32_t mm_block(void)
{
    if (!s_blk) {
        uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;
        uint32_t size = MAX_LINES * 8 + MAX_LINES * LABEL_CH * 2 + 16 * 2;
        PUSH32(g_esp, HEAP_TAG);
        PUSH32(g_esp, size);
        PUSH32(g_esp, 0);                   /* the return address its ret takes */
        sub_001D2879();
        s_blk = g_eax;
        g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
        if (s_blk)
            put_label(title_va(), "MODS");
    }
    return s_blk;
}

/* The title's front-end select sound: what A plays (0x85415..0x8542B). */
static void play_select(void)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;
    PUSH32(g_esp, 0);
    PUSH32(g_esp, MM8(0x411560) ? 1u : 0u);
    PUSH32(g_esp, 0x3F800000u);
    PUSH32(g_esp, 0x3ECCCCCDu);
    PUSH32(g_esp, 0xB701A7E2u);
    PUSH32(g_esp, 0xA9E00000u);
    PUSH32(g_esp, 0);
    g_eax = 0;
    sub_00141010();
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
}

/* ---- the mods and the choices ---- */

static int nmods(void)
{
    int n = bo3_modfs_mod_count();
    return n < BO3_MODFS_MAX_MODS ? n : BO3_MODFS_MAX_MODS;
}

/* What the mod list says now (it may have become readable since). */
static void read_saved(void)
{
    for (int i = 0; i < nmods(); i++) {
        const bo3_mod *m = bo3_modfs_mod(i);
        s_saved[i] = (unsigned char)(m->usable ? bo3_modfs_enabled_next(m->manifest.id) : m->enabled);
    }
}

static int chosen(int i)           /* on at the next start, with the player's choice */
{
    return s_touched[i] ? s_choice[i] : s_saved[i];
}

static int changes_unsaved(void)   /* 1: APPLY would change the mod list */
{
    for (int i = 0; i < nmods(); i++)
        if (bo3_modfs_mod(i)->usable && chosen(i) != s_saved[i])
            return 1;
    return 0;
}

static int restart_needed(void)    /* 1: the mod list differs from what this start used */
{
    for (int i = 0; i < nmods(); i++)
        if (bo3_modfs_mod(i)->usable && s_saved[i] != bo3_modfs_mod(i)->enabled)
            return 1;
    return 0;
}

/* Another mod with the same id (both refused by modfs.c), or NULL. */
static const bo3_mod *shared_id(const bo3_mod *m)
{
    if (m->usable || !m->manifest.id[0])
        return NULL;
    for (int i = 0; i < nmods(); i++) {
        const bo3_mod *o = bo3_modfs_mod(i);
        if (o != m && !strcmp(o->manifest.id, m->manifest.id))
            return o;
    }
    return NULL;
}

/* The last part of a host path (never the whole of it on screen). */
static const char *last_part(const char *path)
{
    const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
    const char *f = a > b ? a : b;
    return f ? f + 1 : path;
}

/* ON / OFF, a star when the next start differs from this one; or why a mod
 * cannot be used. */
static const char *state_word(int i)
{
    const bo3_mod *m = bo3_modfs_mod(i);
    if (!m->usable)
        return shared_id(m) ? "SAME ID" : "INVALID";
    if (chosen(i) == m->enabled)
        return m->enabled ? "ON" : "OFF";
    return chosen(i) ? "ON*" : "OFF*";
}

static const char *mod_name(const bo3_mod *m)
{
    const char *f;
    if (m->manifest.name[0])
        return m->manifest.name;
    f = strrchr(m->folder, '/');
    if (!f || (strrchr(m->folder, '\\') && strrchr(m->folder, '\\') > f))
        f = strrchr(m->folder, '\\');
    return f ? f + 1 : m->folder;              /* a mod whose mod.toml was not read: its folder */
}

/* The MODS page's lines into the block; the count. */
static int build_mods(void)
{
    char buf[96], name[LABEL_CH];
    int n = 0, nm = nmods(), active = 0, per;

    read_saved();
    for (int i = 0; i < nm; i++)
        active += bo3_modfs_mod(i)->usable && bo3_modfs_mod(i)->enabled;
    per = nm <= PAGE_ALL ? PAGE_ALL : PAGE_MODS;
    if (nm <= PAGE_ALL || s_first < 0 || s_first >= nm)
        s_first = 0;
    s_prev_line = s_next_line = -1;
    if (s_first > 0) {
        s_prev_line = n;
        put_label(label_va(n), "PREVIOUS PAGE");
        s_line_mod[n++] = -1;
    }
    for (int i = s_first; i < nm && i < s_first + per; i++) {
        shown(name, sizeof name, mod_name(bo3_modfs_mod(i)));
        if (strlen(name) > NAME_CH) {
            size_t k = NAME_CH - 2;
            while (k && name[k - 1] == ' ')
                k--;
            snprintf(buf, sizeof buf, "%.*s..  %s", (int)k, name, state_word(i));
        } else {
            snprintf(buf, sizeof buf, "%s  %s", name, state_word(i));
        }
        put_label(label_va(n), buf);
        s_line_mod[n++] = i;
    }
    if (s_first + per < nm) {
        s_next_line = n;
        put_label(label_va(n), "NEXT PAGE");
        s_line_mod[n++] = -1;
    }
    s_apply_line = n;
    put_label(label_va(n), s_save_failed ? "NOT SAVED - SEE THE LOG"
                         : changes_unsaved() ? "APPLY - RESTART REQUIRED"
                         : restart_needed() ? "SAVED - RESTART REQUIRED" : "NO CHANGES");
    s_line_mod[n++] = -1;
    s_detail_line = n;                       /* about the line under the cursor (detail_refresh) */
    s_detail[0] = 0;
    put_label(label_va(n), "");
    s_line_mod[n++] = -1;
    s_info_line = n;
    if (!nm)
        snprintf(s_info, sizeof s_info, "NO MODS INSTALLED");
    else
        snprintf(s_info, sizeof s_info, "%d INSTALLED  %d ACTIVE", nm, active);
    put_label(label_va(n), s_info);
    s_line_mod[n++] = -1;
    for (int k = 0; k < n; k++) {
        MM32(s_blk + (uint32_t)k * 8) = label_va(k);
        MM32(s_blk + (uint32_t)k * 8 + 4) = 0;
    }
    s_nlines = n;                            /* at most 1 + 4 + 1 + 3 = 9 or 6 + 3 = 9: WIDGET_MAX */
    return n;
}

/* The details line, and the information line, for the line under the cursor. */
static void detail_text(int cur, char *det, size_t dn, char *info, size_t in)
{
    int nm = nmods(), active = 0;

    for (int i = 0; i < nm; i++)
        active += bo3_modfs_mod(i)->usable && bo3_modfs_mod(i)->enabled;
    if (nm)
        snprintf(info, in, "%d INSTALLED  %d ACTIVE", nm, active);
    else
        snprintf(info, in, "NO MODS INSTALLED");
    if (cur >= 0 && cur < s_nlines && s_line_mod[cur] >= 0) {
        const bo3_mod *m = bo3_modfs_mod(s_line_mod[cur]);
        if (shared_id(m)) {
            snprintf(det, dn, "ID ALSO USED BY %.60s", last_part(shared_id(m)->folder));   /* not the whole path */
        } else if (!m->usable) {
            snprintf(det, dn, "%s", m->problem);
        } else {
            char ver[BO3_MOD_VERSION_MAX + 1], by[BO3_MOD_AUTHOR_MAX + 1];
            shown(ver, sizeof ver, m->manifest.version);
            shown(by, sizeof by, m->manifest.author);
            if (m->wins + m->loses)
                snprintf(det, dn, "V%.8s  BY %.14s  %d CONFLICTS", ver[0] ? ver : "-", by[0] ? by : "UNKNOWN",
                         m->wins + m->loses);
            else
                snprintf(det, dn, "V%.10s  BY %.24s", ver[0] ? ver : "-", by[0] ? by : "UNKNOWN");
            if (m->manifest.description[0])
                snprintf(info, in, "%.100s", m->manifest.description);   /* a line shows 47 */
        }
    } else if (cur == s_apply_line) {
        snprintf(det, dn, "%s", s_save_failed ? "A: TRY AGAIN"
                                : changes_unsaved() ? "A: SAVE - USED AT THE NEXT START"
                                : restart_needed() ? "QUIT AND START THE GAME AGAIN" : "THE MODS IN USE ARE SAVED");
    } else if (cur == s_prev_line || cur == s_next_line) {
        snprintf(det, dn, "PAGE %d OF %d", s_first / PAGE_MODS + 1, (nm + PAGE_MODS - 1) / PAGE_MODS);
    } else if (cur == s_info_line) {
        snprintf(det, dn, "%s", nm ? "A: TURN A MOD ON OR OFF" : "ADD A MOD FOLDER WITH A MOD.TOML");
    } else {
        det[0] = 0;
    }
}

/* Every idle frame of the page: the two lines follow the cursor. A line is
 * written only when its text changes, and never the one under the cursor. */
static void detail_refresh(void)
{
    char det[LABEL_CH * 4], info[LABEL_CH * 4], a[LABEL_CH], b[LABEL_CH];
    int cur;
    if (!s_mode || s_detail_line < 0 || !s_blk)
        return;
    cur = MM16(SP_CURSOR);
    if (cur == s_detail_line || cur == s_info_line)
        return;                              /* on one of them: keep what they say */
    detail_text(cur, det, sizeof det, info, sizeof info);
    shown(a, sizeof a, det);
    shown(b, sizeof b, info);
    if (strcmp(a, s_detail)) {
        memcpy(s_detail, a, sizeof s_detail);
        put_label(label_va(s_detail_line), a);
    }
    if (strcmp(b, s_info)) {
        memcpy(s_info, b, sizeof s_info);
        put_label(label_va(s_info_line), b);
    }
}

/* ---- the title's flow ---- */

/* The current state's own hash, from the machine's table. */
static int own_hash(uint32_t *lo, uint32_t *hi)
{
    uint32_t cur = MM32(FLOW + 4), t = MM32(FLOW + 0xC);
    for (int k = 0; k < 512 && t; k++, t += 16) {
        uint32_t a = MM32(t), b = MM32(t + 4);
        if (!a && !b)
            break;
        if (FLOW + MM32(t + 8) == cur) {
            *lo = a;
            *hi = b;
            return 1;
        }
    }
    return 0;
}

/* The page built again by the title's set-up, in the mode asked. */
static void rebuild(int mode)
{
    uint32_t a = g_eax, b = g_ebx, c = g_ecx, d = g_edx, si = g_esi, di = g_edi, sp = g_esp, bp = g_ebp, sbp = g_seh_ebp;
    uint32_t slot = MM32(MM32(STRTAB) + 0xC) + TITLE_SLOT, old = MM32(slot);
    s_mode = mode;
    if (mode)
        MM32(slot) = title_va();
    s_rebuilding = 1;
    g_eax = SP_PAGE;
    PUSH32(g_esp, 0);
    sub_00085170();
    s_rebuilding = 0;
    MM32(slot) = old;
    g_eax = a; g_ebx = b; g_ecx = c; g_edx = d; g_esi = si; g_edi = di; g_esp = sp; g_ebp = bp; g_seh_ebp = sbp;
}

/* menuentry.c's si_items mark (0x85222), after its own: esi the page, eax
 * the items for the widget, the count in the page. */
static void mm_items(void)
{
    uint32_t n, i;
    if (g_esi != SP_PAGE)
        return;
    if (!s_rebuilding)
        s_mode = 0;                          /* the title built the page itself: the Settings list */
    s_mods_line = -1;
    if (!mm_on() || !mm_block())
        return;
    if (s_mode) {
        MM16(SP_COUNT) = (uint16_t)build_mods();
        if (MM16(SP_CURSOR) >= s_nlines)
            MM16(SP_CURSOR) = 0;
        if (s_page_built && s_prev_line == 0 && s_nlines > 1)
            MM16(SP_CURSOR) = 1;             /* a new page: on its first mod, not on PREVIOUS PAGE */
        s_page_built = 0;
        g_eax = s_blk;
        fprintf(stderr, "[MODS] page: %d lines, first mod %d\n", s_nlines, s_first);
        return;
    }
    n = MM16(SP_COUNT);
    if (n < 4 || n > 5 || (g_eax != SP_ITEMS && n != 5))
        return;                              /* not the list this file knows */
    for (i = 0; i < n; i++) {
        MM32(s_blk + i * 8) = MM32(g_eax + i * 8);
        MM32(s_blk + i * 8 + 4) = 0;
    }
    s_mods_line = (int)n;
    put_label(label_va((int)n), "MODS");
    MM32(s_blk + n * 8) = label_va((int)n);
    MM32(s_blk + n * 8 + 4) = 0;
    MM16(SP_COUNT) = (uint16_t)(n + 1);
    g_eax = s_blk;
    fprintf(stderr, "[MODS] Settings: MODS listed (line %d)\n", s_mods_line);
}

/* mark "mm_flow_sel": event 5, a line chosen, after `push 0x65857D40` (eax
 * the cursor). */
static void mm_flow_sel(void)
{
    uint32_t lo, hi;
    if (s_probing) {
        s_probed |= 1;
        return;
    }
    if (!mm_on())
        return;
    if (s_mode && s_page_pending >= 0) {     /* a page change on the MODS page */
        if (!own_hash(&lo, &hi)) {           /* mm_select checked it: cannot happen */
            s_page_pending = -1;
            return;
        }
        MM32(g_esp) = lo;
        MM32(g_esp + 4) = hi;
        s_first = s_page_pending;
        s_page_pending = -1;
        s_page_built = 1;
        rebuild(1);
        return;
    }
    if (s_mode || s_mods_line < 0 || g_eax != (uint32_t)s_mods_line || MM16(SP_CURSOR) != s_mods_line)
        return;
    if (!own_hash(&lo, &hi))
        return;                              /* mm_select let A through only with the hash found */
    MM32(g_esp) = lo;                        /* pushed: 0x65857D40 */
    MM32(g_esp + 4) = hi;                    /* pushed before it: 0x94413FC1 */
    s_first = 0;
    rebuild(1);
    fprintf(stderr, "[MODS] opened\n");
}

/* mark "mm_flow_back": event 5, back (result 3), after `push 0xEF6E02D9`. */
static void mm_flow_back(void)
{
    uint32_t lo, hi;
    if (s_probing) {
        s_probed |= 2;
        return;
    }
    if (!mm_on() || !s_mode)
        return;
    if (!own_hash(&lo, &hi)) {
        s_mode = 0;                          /* the title goes back to Driver Details, as it would */
        return;
    }
    MM32(g_esp) = lo;                        /* pushed: 0xEF6E02D9 */
    MM32(g_esp + 4) = hi;                    /* pushed before it: 0x94413FD6 */
    s_page_pending = -1;
    rebuild(0);
    if (s_mods_line >= 0)
        MM16(SP_CURSOR) = (uint16_t)s_mods_line;   /* MODS where it is now, never past the list */
    fprintf(stderr, "[MODS] closed\n");
}

/* The choices saved: each mod whose choice differs from the mod list. */
static void apply(void)
{
    int ok = 1, n = 0;
    for (int i = 0; i < nmods(); i++) {
        const bo3_mod *m = bo3_modfs_mod(i);
        if (!m->usable || chosen(i) == s_saved[i])
            continue;
        n++;
        if (bo3_modfs_set_enabled(m->manifest.id, chosen(i)))
            s_touched[i] = 0;                /* now what the mod list says */
        else
            ok = 0;                          /* this choice stays, to try again */
    }
    s_save_failed = !ok;
    if (ok)
        fprintf(stderr, "[MODS] APPLY: %d change(s) saved\n", n);
    else
        fprintf(stderr, "[MODS] APPLY: not every change saved: no mod list file (TAKEDAWN_MOD_LIST empty), or it "
                        "could not be read or written (lines above)\n");
}

/* menuentry.c's si_select mark (0x853B4), after its own: al = A taken. */
static void mm_select(void)
{
    int cur;
    uint32_t lo, hi;
    if (!mm_on())
        return;
    detail_refresh();                        /* every idle frame of the page reaches the A test */
    if (!(g_eax & 0xFFu))
        return;
    cur = MM16(SP_CURSOR);
    if (!s_mode) {
        /* From the fifth line on the title's next-page table has no entry:
         * A goes on only for MODS, and only when its no-op can be made.
         * (menuentry.c has taken A for its own line already.) */
        if (cur >= TITLE_LINES && (cur != s_mods_line || !own_hash(&lo, &hi))) {
            g_eax &= ~0xFFu;
            fprintf(stderr, "[MODS] A on Settings line %d kept from the title (%s)\n", cur,
                    cur == s_mods_line ? "the Settings state is not in the machine's table" : "not a line it has");
        }
        return;                              /* MODS: the title's own A (sound, exit), then mm_flow_sel */
    }
    g_eax &= ~0xFFu;                         /* the MODS page: never the title's exit, unless a page change */
    if (cur < 0 || cur >= s_nlines)
        return;
    if (s_line_mod[cur] >= 0) {
        int i = s_line_mod[cur];
        if (!bo3_modfs_mod(i)->usable)
            return;                          /* a mod that cannot be used: nothing to turn on */
        s_choice[i] = (unsigned char)!chosen(i);
        s_touched[i] = 1;
        s_save_failed = 0;
    } else if (cur == s_apply_line) {
        if (!changes_unsaved() && !s_save_failed)
            return;
        apply();
        read_saved();
    } else if (cur == s_next_line || cur == s_prev_line) {
        /* Another page has another number of lines, and the widget makes its
         * line frames once, at set-up: so a page change is the title's own A
         * (select sound, exit), and mm_flow_sel builds the next page. */
        if (!own_hash(&lo, &hi) || cur == TITLE_LINES - 1)
            return;                          /* event 5 for line 3 skips mm_flow_sel (never so laid out) */
        s_page_pending = cur == s_next_line ? s_first + PAGE_MODS : (s_first >= PAGE_MODS ? s_first - PAGE_MODS : 0);
        g_eax |= 1u;
        return;
    } else {
        return;                              /* the details and information lines */
    }
    MM16(SP_COUNT) = (uint16_t)build_mods(); /* the same number of lines: the frames stay */
    if (MM16(SP_CURSOR) >= s_nlines)
        MM16(SP_CURSOR) = (uint16_t)(s_nlines - 1);
    s_info[0] = s_detail[0] = 0;
    detail_refresh();
    play_select();
}

int bo3_modsmenu_mode(void) { return s_mode; }
void bo3_modsmenu_items(void) { mm_items(); }
void bo3_modsmenu_select(void) { mm_select(); }

void bo3_modsmenu_start(void)
{
    const char *e = getenv("TAKEDAWN_MODS_MENU");
    s_hidden = e && e[0] == '0' && !e[1];
    memset(s_touched, 0, sizeof s_touched);
    s_save_failed = 0;
    recomp_patch_on_mark("mm_flow_sel", mm_flow_sel);
    recomp_patch_on_mark("mm_flow_back", mm_flow_back);
    /* MODS is listed only with its marks in the generated code and the two
     * flow marks reaching mm_flow_*: the toolkit calls the first function
     * registered for a name and drops a registration past its table (and
     * menuentry.c registers si_items and si_select before these). Without
     * them A on MODS would reach the title's four-entry next-page table. */
    s_probing = 1;
    s_probed = 0;
    recomp_patch_mark("mm_flow_sel");
    recomp_patch_mark("mm_flow_back");
    s_probing = 0;
    {
        int gen = bo3_modsmenu_gen_marks();
        s_flow = gen && s_probed == 3;
        if (!s_flow)
            fprintf(stderr, "[MODS] the MODS page is not listed: %s\n",
                    !gen ? "the generated code lacks its marks (run regen.sh and build again)"
                         : "its marks reach other functions, or the toolkit's mark table is full");
    }
}
