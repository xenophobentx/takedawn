/* The MODS page (src/game/modsmenu.c, with menuentry.c and modfs.c as they
 * are) against a stand-in for the title: guest memory is a host buffer, and
 * the title's functions the page calls are played here - the heap
 * (sub_001D2879), the Settings page's set-up (sub_00085170, which calls the
 * si_items mark and hands the list to the widget), the front-end sound
 * (sub_00141010) - together with the parts of its flow the marks sit in: A
 * and B on the page, the exit, event 5 and the state machine's change of
 * state. Mods are real folders in the temporary directory. No game data. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "platform/recomp_env.h"
#include "modfs.h"

#if defined(_WIN32)
#  include <direct.h>
#  include <windows.h>
#  define SETENV(k, v) _putenv_s(k, v)
#  define SEP "\\"
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  define SETENV(k, v) setenv(k, v, 1)
#  define SEP "/"
#endif

/* ---- the runtime's registers and memory ---- */
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
ptrdiff_t g_xbox_mem_offset;
const char *(*xbox_PathRedirectHook)(const char *xbox_path);
#define GMEM 0x01000000u
static uint8_t *s_mem;
#define M8(va)  (*(uint8_t *)(s_mem + (uint32_t)(va)))
#define M16(va) (*(uint16_t *)(s_mem + (uint32_t)(va)))
#define M32(va) (*(uint32_t *)(s_mem + (uint32_t)(va)))

/* ---- the toolkit's parts menuentry.c uses ---- */
const char *recomp_env_value_[RENV_COUNT];
volatile int recomp_env_ready_ = 1;
void recomp_env_init(void) {}
static int s_overlay, s_overlay_opens;
int xbox_OverlayAvailable(void) { return s_overlay; }
int xbox_OverlayRequestOpen(void) { s_overlay_opens++; return 1; }

/* The toolkit's marks (src/platform/recomp_patch.c): the first function
 * registered for a name is the one called; past its table a registration is
 * dropped (s_mark_room plays a table with that much room left). */
static struct { const char *name; void (*fn)(void); } s_marks[16];
static int s_nmarks, s_mark_room = 16;
void recomp_patch_on_mark(const char *name, void (*fn)(void))
{
    if (s_nmarks == s_mark_room)
        return;
    s_marks[s_nmarks].name = name;
    s_marks[s_nmarks++].fn = fn;
}
static void mark(const char *name)
{
    for (int i = 0; i < s_nmarks; i++)
        if (!strcmp(s_marks[i].name, name)) {
            s_marks[i].fn();
            return;
        }
}
void recomp_patch_mark(const char *name) { mark(name); }

/* The generated code: whether the build found the two flow marks in it
 * (cmake/modsmenu_marks.cmake makes this function for the game), and whether
 * the title played here calls them (generated code older than the marks:
 * event 5 changes state without them). */
static int s_gen_marks = 1, s_title_marks = 1;
int bo3_modsmenu_gen_marks(void) { return s_gen_marks; }
static int s_other_flow;
static void other_flow(void) { s_other_flow++; }   /* another file's function for mm_flow_sel */

/* ---- the title ---- */
#define PAGE     0x0055ABC4u
#define CURSOR   (PAGE + 4)
#define COUNT    (PAGE + 6)
#define ITEMS    (PAGE + 8)
#define FLOW     0x00567170u
#define STRTAB   0x004D532Cu
#define STRS     0x004E0000u            /* [[STRTAB] + 0xC]: the string pointers */
#define TITLE    (STRS + 0x6C4)
#define STRINGS     0x004F0000u            /* the title's own strings, UTF-16 */
#define TABLE    0x00580000u            /* the machine's states: {lo, hi, offset} */
#define SETTINGS_LO 0x5E771A01u
#define SETTINGS_HI 0x94413FC0u
#define NEXT_LO  0x65857D40u            /* what event 5 pushes for a chosen line, and for back */
#define NEXT_HI  0x94413FC1u
#define BACK_LO  0xEF6E02D9u
#define BACK_HI  0x94413FD6u
#define STACK    0x00700000u
#define HEAP     0x00800000u

static uint32_t s_heap, s_allocs, s_heap_fail;
static uint32_t s_widget_items, s_widget_count, s_widget_builds, s_widget_max;
static uint32_t s_title_seen;
static int s_sounds, s_out_of_range, s_left_settings;

/* Guard words: around every heap block, after the Settings page object (the
 * next state's object starts at 0x55ABEC), and after the list widget's two
 * frame arrays. A write past any of them is a failure. */
#define GUARD     0xCA11AB1Eu
#define WIDGET    0x005509F4u            /* the list widget: frames at +0x14 and +0x38, 9 each */
#define W_FRAMES  9
#define NEXT_OBJ  (PAGE + 0x28)
static uint32_t s_blocks[16][2];         /* heap blocks: address, size */
static int s_nblocks, s_guard_bad, s_widget_overflow;

static void guard_fill(uint32_t va, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        M32(va + i * 4) = GUARD;
}

static int guard_ok(uint32_t va, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        if (M32(va + i * 4) != GUARD)
            return 0;
    return 1;
}

static void guards_check(void)
{
    for (int b = 0; b < s_nblocks; b++)
        if (!guard_ok(s_blocks[b][0] - 16, 4) || !guard_ok(s_blocks[b][0] + s_blocks[b][1], 4))
            s_guard_bad = 1;
    if (!guard_ok(NEXT_OBJ, 4) || !guard_ok(WIDGET + 0x60, 16))
        s_guard_bad = 1;
}

void sub_001D2879(void)          /* (size, tag), stdcall; ret address pushed by the caller */
{
    uint32_t size = M32(g_esp + 4);
    g_esp += 12;
    if (s_heap_fail) {
        g_eax = 0;
        return;
    }
    guard_fill(s_heap, 4);
    g_eax = s_heap + 16;
    s_blocks[s_nblocks][0] = g_eax;
    s_blocks[s_nblocks++][1] = (size + 3) & ~3u;
    guard_fill(g_eax + ((size + 3) & ~3u), 4);
    s_heap += 32 + ((size + 15) & ~15u);
    s_allocs++;
}

/* The list widget's set-up loop (sub_000F0EA0, 0xF1048..0xF1101): for each
 * line, a frame pointer into [widget + 0x14 + 4 i] and [widget + 0x38 + 4 i],
 * then its own field at +0x5C. Played exactly, so a tenth line lands where
 * it would in the title: on the second array and past it. */
static void widget_setup(uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        M32(WIDGET + 0x14 + i * 4) = 0xF1000000u | (i << 4) | 1;
        M32(WIDGET + 0x38 + i * 4) = 0xF1000000u | (i << 4) | 2;
    }
    M32(WIDGET + 0x5C) = 0xF1FFFFF0u;
    for (uint32_t i = 0; i < count && i < W_FRAMES; i++)
        if (M32(WIDGET + 0x14 + i * 4) != (0xF1000000u | (i << 4) | 1)
                || M32(WIDGET + 0x38 + i * 4) != (0xF1000000u | (i << 4) | 2))
            s_widget_overflow = 1;
    if (count > W_FRAMES || !guard_ok(WIDGET + 0x60, 16))
        s_widget_overflow = 1;
}

void sub_00141010(void)
{
    s_sounds++;
    g_esp += 28;
}

static const char *const k_titles[4] = { "GAMEPLAY", "AUDIO", "CONTROLLERS", "EA GAMES TRAX" };

/* The Settings page's set-up (eax the page): four items, the cursor on the
 * first, the si_items mark, then the list widget made with what eax holds. */
void sub_00085170(void)
{
    uint32_t page = g_eax;
    g_esp += 4;
    if (page != PAGE)
        return;
    for (uint32_t i = 0; i < 4; i++) {
        M32(ITEMS + i * 8) = STRINGS + i * 64;
        M32(ITEMS + i * 8 + 4) = 0;
    }
    M16(COUNT) = 4;
    M16(CURSOR) = 0;
    s_title_seen = M32(TITLE);
    g_esi = PAGE;
    g_eax = ITEMS;
    mark("si_items");
    s_widget_items = g_eax;
    s_widget_count = M16(COUNT);
    s_widget_builds++;
    if (s_widget_count > s_widget_max)
        s_widget_max = s_widget_count;
    widget_setup(s_widget_count);
    guards_check();
}

/* The title's data as at a fresh start; the heap (blocks the host keeps for
 * the process) is left as it is. */
static void title_init(void)
{
    memset(s_mem, 0, HEAP);
    for (int i = 0; i < 4; i++)
        for (size_t k = 0; k <= strlen(k_titles[i]); k++)
            M16(STRINGS + (uint32_t)i * 64 + (uint32_t)k * 2) = (uint8_t)k_titles[i][k];
    M32(STRTAB) = STRS - 0xC + 0x100;     /* [[STRTAB] + 0xC] */
    M32(STRS + 0x100) = STRS;
    M32(TITLE) = STRINGS + 0x300;            /* "SETTINGS" */
    for (size_t k = 0; k <= 8; k++)
        M16(STRINGS + 0x300 + (uint32_t)k * 2) = (uint8_t)"SETTINGS"[k];
    M32(TABLE + 0) = 0x11111111;  M32(TABLE + 4) = 0x22222222;  M32(TABLE + 8) = 0x40;   /* Driver Details */
    M32(TABLE + 16) = SETTINGS_LO; M32(TABLE + 20) = SETTINGS_HI; M32(TABLE + 24) = 0x80; /* Settings */
    M32(FLOW + 0xC) = TABLE;
    M32(FLOW + 4) = FLOW + 0x80;          /* the current state: Settings */
    if (!s_heap)
        s_heap = HEAP;
    g_esp = STACK;
    guard_fill(NEXT_OBJ, 4);
    guard_fill(WIDGET + 0x60, 16);
}

/* The page shown: the widget's line i, in ASCII. */
static const char *line(int i)
{
    static char b[8][64];
    static int k;
    char *o = b[k++ & 7];
    uint32_t va;
    int n = 0;
    o[0] = 0;
    if (i < 0 || (uint32_t)i >= s_widget_count)
        return o;
    va = M32(s_widget_items + (uint32_t)i * 8);
    while (n < 63 && M16(va + (uint32_t)n * 2)) {
        o[n] = (char)M16(va + (uint32_t)n * 2);
        n++;
    }
    o[n] = 0;
    return o;
}

static const char *title_shown(void)
{
    static char b[64];
    int n = 0;
    while (n < 63 && M16(s_title_seen + (uint32_t)n * 2)) {
        b[n] = (char)M16(s_title_seen + (uint32_t)n * 2);
        n++;
    }
    b[n] = 0;
    return b;
}

static int find_line(const char *prefix)
{
    for (uint32_t i = 0; i < s_widget_count; i++)
        if (!strncmp(line((int)i), prefix, strlen(prefix)))
            return (int)i;
    return -1;
}

/* Entering Driver Details > Settings: the title builds its page. */
static void enter_settings(void)
{
    M32(FLOW + 4) = FLOW + 0x80;
    s_left_settings = 0;
    g_eax = PAGE;
    PUSH32(g_esp, 0);
    sub_00085170();
}

/* An idle frame of the page: the input handler reaches the A test, no A. */
static void idle(void)
{
    g_eax = 0;
    mark("si_select");
}

static void cursor_to(int i)
{
    M16(CURSOR) = (uint16_t)i;
    idle();
}

/* Event 5: what the flow does after the page's exit. */
static void event5(const char *markname, uint32_t lo, uint32_t hi, int line_chosen)
{
    uint32_t sp = g_esp;
    if (lo == NEXT_LO && line_chosen == 3) {
        s_left_settings = 1;              /* sub_000696D0 goes to EA GAMES TRAX without reaching the mark */
        return;
    }
    PUSH32(g_esp, hi);
    PUSH32(g_esp, lo);
    g_eax = (uint32_t)line_chosen;
    if (s_title_marks)
        mark(markname);
    if (M32(g_esp) == SETTINGS_LO && M32(g_esp + 4) == SETTINGS_HI) {
        /* the current state's own: sub_000146E0 does nothing */
    } else {
        s_left_settings = 1;
        if (lo == NEXT_LO && line_chosen >= 4)
            s_out_of_range = 1;           /* the title's next-page table has four entries */
    }
    g_esp = sp;
}

/* A on the line under the cursor: the si_select mark; if the title keeps its
 * A, its select sound and exit, then event 5. */
static void press_a(void)
{
    int cur = M16(CURSOR);
    g_eax = 1;
    mark("si_select");
    if (g_eax & 0xFF) {
        s_sounds++;                       /* the title's own select sound */
        event5("mm_flow_sel", NEXT_LO, NEXT_HI, cur);
    }
}

static void press_b(void)
{
    s_sounds++;                           /* the title's own back sound */
    event5("mm_flow_back", BACK_LO, BACK_HI, 0);
}

/* ---- checks and the mod folders ---- */
static int s_pass, s_fail;
#define CHECK(c, ...) do { if (c) s_pass++; else { s_fail++; fprintf(stderr, "FAIL line %d: ", __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static char s_root[600];
static char s_made[4096][700];
static int s_nmade;

static const char *at(const char *rel)
{
    static char b[8][700];
    static int k;
    char *o = b[k++ & 7];
    snprintf(o, 700, "%s" SEP "%s", s_root, rel);
#if defined(_WIN32)
    for (char *p = o; *p; p++)
        if (*p == '/')
            *p = '\\';
#endif
    return o;
}

static void made(const char *p)
{
    if (s_nmade == (int)(sizeof s_made / sizeof s_made[0])) {
        fprintf(stderr, "more fixtures than the test can remove: raise s_made\n");
        exit(2);
    }
    snprintf(s_made[s_nmade++], sizeof s_made[0], "%s", p);
}

static void mkdir_one(const char *p)
{
#if defined(_WIN32)
    if (_mkdir(p) == 0)
#else
    if (mkdir(p, 0755) == 0)
#endif
        made(p);
}

static int mk(const char *rel, const char *text)
{
    char p[700];
    FILE *f;
    snprintf(p, sizeof p, "%s", at(rel));
    for (char *s = p + strlen(s_root) + 1; *s; s++)
        if (*s == SEP[0]) {
            *s = 0;
            mkdir_one(p);
            *s = SEP[0];
        }
    if (!(f = fopen(p, "wb")))
        return 0;
    fputs(text, f);
    fclose(f);
    made(p);
    return 1;
}

static void remove_all(void)
{
    for (int i = s_nmade - 1; i >= 0; i--)
        if (remove(s_made[i]) != 0) {
#if defined(_WIN32)
            _rmdir(s_made[i]);
#else
            rmdir(s_made[i]);
#endif
        }
    s_nmade = 0;
}

static void mod(const char *folder, const char *id, const char *extra, const char *file, const char *text)
{
    char p[300], t[2048];
    snprintf(p, sizeof p, "%s/mod.toml", folder);
    snprintf(t, sizeof t, "id = \"%s\"\nname = \"%s\"\n%s", id, id, extra ? extra : "");
    mk(p, t);
    if (file) {
        snprintf(p, sizeof p, "%s/files/%s", folder, file);
        mk(p, text);
    }
}

extern void bo3_menu_entry_start(void);
extern void bo3_modsmenu_start(void);
extern int bo3_modsmenu_mode(void);

/* A start of the game (a new process there): the mod folders, and the
 * page's choices of the last session forgotten. The blocks on the title's
 * heap stay, as the test keeps its heap. */
static void start(const char *mods)
{
    SETENV("TAKEDAWN_MODS", mods ? mods : "");
    bo3_modfs_init();
    s_nmarks = 0;
    bo3_menu_entry_start();
    bo3_modsmenu_start();
}

static const char *served(const char *guest)
{
    static char b[64];
    const char *h = xbox_PathRedirectHook ? xbox_PathRedirectHook(guest) : NULL;
    FILE *f;
    size_t n;
    b[0] = 0;
    if (!h)
        return b;
#if defined(_WIN32)
    if (!strncmp(h, "\\\\?\\", 4))
        h += 4;
#endif
    if (!(f = fopen(h, "rb")))
        return "?";
    n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    b[n] = 0;
    return b;
}

static const char *slurp(const char *p)
{
    static char b[512];
    FILE *f = fopen(p, "rb");
    size_t n = 0;
    if (f) {
        n = fread(b, 1, sizeof b - 1, f);
        fclose(f);
    }
    b[n] = 0;
    return f ? b : NULL;
}

/* ---- the pagination gate ----
 * The crash an earlier version of this page had:
 * nine packages made an 11-line page; the list widget keeps its line frames
 * in two 9-entry arrays (sub_000F0EA0, played by widget_setup), so lines 10
 * and 11 overwrote the widget's next fields and the 2D scene crashed. The
 * fix: never more than 9 lines, pages of 4 with PREVIOUS / NEXT. */

static int s_seen[BO3_MODFS_MAX_MODS + 1];

/* n mods "p.000" .. in folder pg<n>; a fresh start on it, the page opened. */
static void open_with(int n, char *folder, size_t fn)
{
    char f[64], id[32];
    snprintf(f, sizeof f, "pg%d", n);
    for (int i = 0; i < n; i++) {
        char p[64];
        snprintf(p, sizeof p, "pg%d/m%03d", n, i);
        snprintf(id, sizeof id, "p.%03d", i);
        mod(p, id, NULL, NULL, NULL);
    }
    if (!n)
        mk("pg0/readme.txt", "loose");
    snprintf(folder, fn, "%s", at(f));
    start(folder);
    title_init();
    enter_settings();
    cursor_to(4);
    press_a();
}

/* The mods on the page shown, by row: their numbers, -1 for other lines. */
static int page_mods(int *rows, int *first, int *count)
{
    int k = 0;
    *first = -1;
    *count = 0;
    for (uint32_t r = 0; r < s_widget_count; r++) {
        int v;
        rows[r] = -1;
        if (sscanf(line((int)r), "P.%3d", &v) == 1) {
            rows[r] = v;
            if (*first < 0)
                *first = v;
            (*count)++;
            k++;
        }
    }
    return k;
}

static void pagination_case(int n)
{
    char folder[700];
    int rows[16], first, count, pages = 0, expect_pages = n <= 6 ? 1 : (n + 3) / 4, ok_rows = 1, ok_lines = 1;
    int sounds_ok = 1, builds_ok = 1, cursor_ok = 1, seen_once = 1;

    memset(s_seen, 0, sizeof s_seen);
    s_guard_bad = s_widget_overflow = 0;
    open_with(n, folder, sizeof folder);
    CHECK(bo3_modsmenu_mode(), "%d mods: the page opens", n);
    /* forward: every page, NEXT through the title's own A and exit */
    for (;;) {
        page_mods(rows, &first, &count);
        pages++;
        if (s_widget_count > W_FRAMES)
            ok_lines = 0;
        if (M16(CURSOR) >= s_widget_count)
            cursor_ok = 0;
        for (uint32_t r = 0; r < s_widget_count; r++)
            if (rows[r] >= 0) {
                if (rows[r] >= n || s_seen[rows[r]]++)
                    seen_once = 0;
                if (r > 0 && rows[r - 1] >= 0 && rows[r] != rows[r - 1] + 1)
                    ok_rows = 0;
            }
        if (n > 6 && count != (first + 4 <= n ? 4 : n - first))
            ok_rows = 0;
        if (find_line("NEXT PAGE") < 0)
            break;
        {
            int s0 = s_sounds;
            uint32_t b0 = s_widget_builds;
            cursor_to(find_line("NEXT PAGE"));
            press_a();
            if (s_sounds != s0 + 1)
                sounds_ok = 0;               /* the title's select sound only */
            if (s_widget_builds != b0 + 1 || s_left_settings)
                builds_ok = 0;               /* one exit + enter, still Settings */
        }
        if (pages > 40)
            break;
    }
    for (int i = 0; i < n; i++)
        if (s_seen[i] != 1)
            seen_once = 0;
    CHECK(pages == expect_pages, "%d mods: %d pages (want %d)", n, pages, expect_pages);
    CHECK(ok_lines && s_widget_max <= W_FRAMES && !s_widget_overflow && !s_guard_bad,
          "%d mods: every page at most %d lines, widget frames and guards intact", n, W_FRAMES);
    CHECK(seen_once && ok_rows, "%d mods: each mod on exactly one row, in id order", n);
    CHECK(sounds_ok && builds_ok && cursor_ok, "%d mods: page changes are the title's A (one sound, one rebuild), cursor in range", n);
    /* backward to the first page */
    {
        int back = 0, prev_first = 1 << 30, order_ok = 1;
        while (find_line("PREVIOUS PAGE") >= 0 && back < 40) {
            cursor_to(find_line("PREVIOUS PAGE"));
            press_a();
            page_mods(rows, &first, &count);
            if (first >= prev_first)
                order_ok = 0;
            prev_first = first;
            back++;
        }
        CHECK(back == expect_pages - 1 && order_ok && (n == 0 || !strncmp(line(0), "P.000", 5)),
              "%d mods: PREVIOUS back to page 1 (%d steps)", n, back);
    }
    CHECK(!s_widget_overflow && !s_guard_bad, "%d mods: guards after paging back", n);
    press_b();
    CHECK(!bo3_modsmenu_mode() && M16(CURSOR) == 4, "%d mods: B from page 1: Settings, cursor on MODS", n);
}

/* Toggle a mod on the first, a middle and the last page; APPLY; a restart. */
static void pagination_toggles(int n)
{
    char folder[700];
    int targets[3] = { 0, n / 2, n - 1 }, rows[16], first, count;
    char want[64];

    open_with(n, folder, sizeof folder);
    for (int t = 0; t < 3; t++) {
        int guard = 0;
        while (guard++ < 40) {
            int r = -1;
            page_mods(rows, &first, &count);
            for (uint32_t k = 0; k < s_widget_count; k++)
                if (rows[k] == targets[t])
                    r = (int)k;
            if (r >= 0) {
                cursor_to(r);
                press_a();
                snprintf(want, sizeof want, "P.%03d  OFF*", targets[t]);
                CHECK(!strcmp(line(r), want), "%d mods: row %d shows mod %d toggled (%s)", n, r, targets[t], line(r));
                break;
            }
            cursor_to(find_line("NEXT PAGE"));
            press_a();
        }
        while (find_line("PREVIOUS PAGE") >= 0) {   /* the choices survive page changes */
            cursor_to(find_line("PREVIOUS PAGE"));
            press_a();
        }
    }
    page_mods(rows, &first, &count);
    snprintf(want, sizeof want, "P.%03d  OFF*", 0);
    CHECK(!strcmp(line(0), want), "%d mods: back on page 1, the choice kept", n);
    cursor_to(find_line("APPLY"));
    press_a();
    CHECK(find_line("SAVED - RESTART REQUIRED") >= 0, "%d mods: APPLY from page 1 saves", n);
    {
        const char *st = slurp(at("state/mod-list.toml"));
        char a[16], b[16], c[16];
        snprintf(a, sizeof a, "\"p.%03d\"", targets[0]);
        snprintf(b, sizeof b, "\"p.%03d\"", targets[1]);
        snprintf(c, sizeof c, "\"p.%03d\"", targets[2]);
        CHECK(st && strstr(st, a) && strstr(st, b) && strstr(st, c), "%d mods: the three ids saved: %s", n, st ? st : "-");
    }
    press_b();
    start(folder);
    title_init();
    enter_settings();
    cursor_to(4);
    press_a();
    {
        int found = 0, guard = 0;
        for (;;) {
            page_mods(rows, &first, &count);
            for (uint32_t k = 0; k < s_widget_count; k++)
                for (int t = 0; t < 3; t++)
                    if (rows[k] == targets[t]) {
                        snprintf(want, sizeof want, "P.%03d  OFF", targets[t]);
                        found += !strcmp(line((int)k), want);
                    }
            if (find_line("NEXT PAGE") < 0 || guard++ > 40)
                break;
            cursor_to(find_line("NEXT PAGE"));
            press_a();
        }
        CHECK(found == 3, "%d mods: after a restart the three are OFF on their rows (%d)", n, found);
    }
    press_b();
    /* put them back on for the next case */
    for (int t = 0; t < 3; t++) {
        char id[16];
        snprintf(id, sizeof id, "p.%03d", targets[t]);
        bo3_modfs_set_enabled(id, 1);
    }
}

int main(void)
{
    char tmp[512], state[700];

    s_mem = calloc(1, GMEM);
    if (!s_mem)
        return 2;
    g_xbox_mem_offset = (ptrdiff_t)s_mem;
#if defined(_WIN32)
    GetTempPathA(sizeof tmp, tmp);
    snprintf(s_root, sizeof s_root, "%sbo3_modsmenu_%lu", tmp, (unsigned long)GetCurrentProcessId());
#else
    snprintf(tmp, sizeof tmp, "%s", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    snprintf(s_root, sizeof s_root, "%s/bo3_modsmenu_%ld", tmp, (long)getpid());
#endif
    mkdir_one(s_root);
    atexit(remove_all);
    title_init();
    bo3_menu_entry_start();
    bo3_modsmenu_start();
    CHECK(s_nmarks == 6, "marks registered: si_items, si_select, pe_items, pe_select, mm_flow_sel, mm_flow_back (%d)", s_nmarks);
    snprintf(state, sizeof state, "%s", at("state/mod-list.toml"));
    mkdir_one(at("state"));
    snprintf(s_made[s_nmade++], 700, "%s", state);
    snprintf(s_made[s_nmade++], 700, "%.690s.tmp", state);
    snprintf(s_made[s_nmade++], 700, "%.690s.bad", state);
    bo3_modfs_state_file(state);

    /* No mod folder: the title's page, untouched. */
    start(NULL);
    enter_settings();
    CHECK(s_widget_items == ITEMS && s_widget_count == 4 && s_allocs == 0, "no mod folder: the title's 4 lines, its own items");
    cursor_to(3);
    press_a();
    CHECK(s_left_settings && !s_out_of_range, "no mod folder: A on EA GAMES TRAX is the title's");

    /* The title's heap out of memory (the block is made once, at the first
     * Settings page with a mod folder): no MODS, the title's list; asked again
     * at the next page. */
    mk("empty/readme.txt", "loose");
    start(at("empty"));
    title_init();
    s_heap_fail = 1;
    enter_settings();
    CHECK(s_widget_count == 4 && s_widget_items == ITEMS && !bo3_modsmenu_mode(), "heap out of memory: the title's list");
    cursor_to(3);
    press_a();
    CHECK(s_left_settings && !s_out_of_range, "heap out of memory: A is the title's");
    s_heap_fail = 0;

    /* A mod folder without mods: MODS listed, the page says so. */
    title_init();
    enter_settings();
    CHECK(s_widget_count == 5 && !strcmp(line(4), "MODS") && !strcmp(line(0), "GAMEPLAY"),
          "a mod folder: MODS after the title's four (%s)", line(4));
    cursor_to(4);
    press_a();
    CHECK(!s_left_settings && bo3_modsmenu_mode() && !strcmp(title_shown(), "MODS"), "A on MODS: the page, titled MODS");
    CHECK(!strcmp(line(0), "NO CHANGES") && !strcmp(line(2), "NO MODS INSTALLED") && s_widget_count == 3,
          "no mods: %s / %s / %s", line(0), line(1), line(2));
    cursor_to(0);
    CHECK(!strcmp(line(1), "THE MODS IN USE ARE SAVED"), "details for the status line: %s", line(1));
    press_b();
    CHECK(!s_left_settings && !bo3_modsmenu_mode() && M16(CURSOR) == 4 && !strcmp(title_shown(), "SETTINGS")
          && s_widget_count == 5, "B: back to Settings, the cursor on MODS");
    press_b();
    CHECK(s_left_settings, "B on Settings: the title's, to Driver Details");

    /* Mods: enabled, invalid, a shared id, long and Unicode text. */
    mod("lib/a", "a.first", "version = \"1.2\"\nauthor = \"Ann\"\ndescription = \"First mod, with a description.\"\n",
        "Data/x.bin", "A");
    mod("lib/b", "b.second", "author = \"\xc3\x89lodie\"\n", "Data/x.bin", "B");
    mk("lib/broken/mod.toml", "id = \"c.broken\"\n");
    mod("lib/d1", "d.same", NULL, NULL, NULL);
    mod("lib/d2", "d.same", NULL, NULL, NULL);
    mk("lib/e/mod.toml", "id = \"e.long\"\nname = \"A very long name that does not fit on one line at all\"\n"
       "description = \"Line \xe2\x80\x94 with a dash and \xf0\x9f\x8f\x81 a flag, and quite a lot more text than fits\"\n");
    start(at("lib"));
    title_init();
    enter_settings();
    cursor_to(4);
    press_a();
    CHECK(bo3_modsmenu_mode(), "opened");
    {
        int la = find_line("A.FIRST"), lb = find_line("B.SECOND"), le = find_line("A VERY LONG NAME");
        CHECK(la == 0 && lb == 1 && !strcmp(line(la), "A.FIRST  ON") && !strcmp(line(lb), "B.SECOND  ON"),
              "usable mods by id, ON: %s / %s", line(0), line(1));
        CHECK(le >= 0 && !strcmp(line(le), "A VERY LONG NAME THAT..  ON"), "a long name cut: %s", line(le));
        CHECK(find_line("D.SAME  SAME ID") >= 0 && find_line("BROKEN  INVALID") >= 0,
              "a shared id and an invalid mod (its folder's name) shown as such");
        CHECK(s_widget_count == 9 && !strcmp(line(6), "NO CHANGES") && !strcmp(line(8), "6 INSTALLED  3 ACTIVE"),
              "6 mods on one page, 9 lines: %u, %s, %s", s_widget_count, line(6), line(8));
        cursor_to(la);
        CHECK(!strcmp(line(7), "V1.2  BY ANN  1 CONFLICTS") && !strcmp(line(8), "FIRST MOD, WITH A DESCRIPTION."),
              "details and description of the mod under the cursor: %s / %s", line(7), line(8));
        cursor_to(lb);
        CHECK(!strcmp(line(7), "V-  BY ?LODIE  1 CONFLICTS") && !strcmp(line(8), "6 INSTALLED  3 ACTIVE"),
              "Unicode shown as ?, no description: counts: %s / %s", line(7), line(8));
        cursor_to(le);
        CHECK(!strcmp(line(8), "LINE ? WITH A DASH AND ? A FLAG, AND QUITE A LO"), "a long Unicode description cut: %s",
              line(8));
        cursor_to(find_line("BROKEN"));
        CHECK(!strcmp(line(7), "MOD.TOML: NAME IS MISSING"), "an invalid mod: the reason: %s", line(7));
        {
            int s0 = s_sounds; uint32_t b0 = s_widget_builds;
            press_a();
            CHECK(s_sounds == s0 && s_widget_builds == b0 && !strcmp(line(find_line("BROKEN")), "BROKEN  INVALID"),
                  "A on an invalid mod: nothing");
        }
        /* toggle, apply, restart, the original file back */
        CHECK(!strcmp(served("D:\\Data\\x.bin"), "A"), "before: a.first's file");
        cursor_to(la);
        {
            int s0 = s_sounds, allocs = (int)s_allocs; uint32_t b0 = s_widget_builds;
            press_a();
            CHECK(!strcmp(line(la), "A.FIRST  OFF*") && !strcmp(line(6), "APPLY - RESTART REQUIRED") && s_sounds == s0 + 1
                  && s_widget_builds == b0 && (int)s_allocs == allocs && !s_left_settings,
                  "A on a mod: OFF*, the select sound, no exit, no new frames: %s / %s", line(la), line(6));
        }
        CHECK(!slurp(state), "nothing saved before APPLY");
        cursor_to(6);
        CHECK(!strcmp(line(7), "A: SAVE - USED AT THE NEXT START"), "APPLY's details: %s", line(7));
        press_a();
        CHECK(slurp(state) && strstr(slurp(state), "\"a.first\"") && !strcmp(line(6), "SAVED - RESTART REQUIRED")
              && !strcmp(line(la), "A.FIRST  OFF*"), "APPLY: saved through the mod list: %s", line(6));
        CHECK(!strcmp(served("D:\\Data\\x.bin"), "A"), "this session unchanged");
        press_b();
        cursor_to(4);
        press_a();
        CHECK(!strcmp(line(la), "A.FIRST  OFF*") && !strcmp(line(6), "SAVED - RESTART REQUIRED"), "reopened: as left");
        press_b();
    }
    start(at("lib"));                     /* the next start */
    CHECK(!strcmp(served("D:\\Data\\x.bin"), "B"), "after a restart: the next mod's file (b.second)");
    title_init();
    enter_settings();
    cursor_to(4);
    press_a();
    CHECK(!strcmp(line(0), "A.FIRST  OFF") && !strcmp(line(6), "NO CHANGES") && !strcmp(line(8), "6 INSTALLED  2 ACTIVE"),
          "after a restart: OFF, no changes: %s / %s", line(0), line(6));
    press_a();                            /* the cursor on line 0 */
    cursor_to(6);
    press_a();
    press_b();
    start(at("lib"));
    CHECK(!strcmp(served("D:\\Data\\x.bin"), "A"), "on again after a restart: the modded file back");

    /* A save that fails: said so, the choice kept, the file as it was. */
    title_init();
    enter_settings();
    cursor_to(4);
    press_a();
    {
        char p[720];
        snprintf(p, sizeof p, "%s.tmp", state);
        mkdir_one(p);
        cursor_to(1);
        press_a();
        cursor_to(6);
        press_a();
        CHECK(!strcmp(line(6), "NOT SAVED - SEE THE LOG") && !strcmp(line(1), "B.SECOND  OFF*")
              && !strstr(slurp(state), "b.second"), "a failed save: %s", line(6));
#if defined(_WIN32)
        _rmdir(p);
#else
        rmdir(p);
#endif
        s_nmade--;
        press_a();
        CHECK(!strcmp(line(6), "SAVED - RESTART REQUIRED") && strstr(slurp(state), "b.second"), "tried again: saved");
        cursor_to(1);
        press_a();
        cursor_to(6);
        press_a();
        CHECK(!strcmp(line(6), "NO CHANGES"), "back as this start: no changes (%s)", line(6));
        press_b();
    }

    /* Many mods: pages of four, PREVIOUS / NEXT through the title's exit. */
    for (int i = 0; i < 13; i++) {
        char f[32], id[32];
        snprintf(f, sizeof f, "many/m%02d", i);
        snprintf(id, sizeof id, "m.%02d", i);
        mod(f, id, NULL, NULL, NULL);
    }
    start(at("many"));
    title_init();
    enter_settings();
    cursor_to(4);
    press_a();
    CHECK(!strcmp(line(0), "M.00  ON") && !strcmp(line(4), "NEXT PAGE") && s_widget_count == 8, "page 1: %s .. %s",
          line(0), line(4));
    cursor_to(4);
    CHECK(!strcmp(line(6), "PAGE 1 OF 4"), "details: %s", line(6));
    press_a();
    CHECK(!s_left_settings && !strcmp(line(0), "PREVIOUS PAGE") && !strcmp(line(1), "M.04  ON") && s_widget_count == 9,
          "NEXT: page 2 built by the flow (%s, %s)", line(0), line(1));
    CHECK(M16(CURSOR) == 1, "a new page: the cursor on its first mod, not on PREVIOUS (%u)", M16(CURSOR));
    for (int p = 0; p < 2; p++) {
        cursor_to(find_line("NEXT PAGE"));
        press_a();
    }
    CHECK(!strcmp(line(1), "M.12  ON") && find_line("NEXT PAGE") < 0 && s_widget_max <= 9, "the last page, no NEXT");
    cursor_to(0);
    press_a();
    CHECK(!strcmp(line(1), "M.08  ON"), "PREVIOUS: %s", line(1));
    press_b();
    CHECK(!bo3_modsmenu_mode() && M16(CURSOR) == 4, "B from page 3: Settings, the cursor on MODS");
    cursor_to(4);
    press_a();
    CHECK(!strcmp(line(0), "M.00  ON"), "reopened: page 1");
    press_b();

    /* Repeated open and close: one block, never more than 9 lines. */
    {
        uint32_t allocs = s_allocs;
        for (int i = 0; i < 200; i++) {
            cursor_to(4);
            press_a();
            cursor_to(find_line("NEXT PAGE"));
            press_a();
            press_b();
        }
        CHECK(s_allocs == allocs && s_widget_max <= 9 && !s_out_of_range && !bo3_modsmenu_mode(),
              "200 opens, page changes and closes: no new block, <= 9 lines, no exit out of the table");
    }

    /* The title builds Settings itself while the mode was left on: the list. */
    cursor_to(4);
    press_a();
    enter_settings();
    CHECK(!bo3_modsmenu_mode() && s_widget_count == 5 && !strcmp(line(4), "MODS"), "a fresh Settings: the list");

    /* The state not in the machine's table: A on MODS taken, nothing opens. */
    M32(TABLE + 24) = 0x90;
    cursor_to(4);
    {
        int s0 = s_sounds;
        press_a();
        CHECK(!s_left_settings && !bo3_modsmenu_mode() && !s_out_of_range && s_sounds == s0,
              "hash not found: A on MODS does nothing");
    }
    M32(TABLE + 24) = 0x80;

    /* TAKEDAWN SETTINGS (an overlay): MODS after it; each line its own. */
    s_overlay = 1;
    enter_settings();
    CHECK(s_widget_count == 6 && !strcmp(line(4), "TAKEDAWN SETTINGS") && !strcmp(line(5), "MODS"),
          "with the overlay: %s, %s", line(4), line(5));
    cursor_to(4);
    press_a();
    CHECK(s_overlay_opens == 1 && !s_left_settings && !bo3_modsmenu_mode(), "TAKEDAWN SETTINGS opens the overlay");
    cursor_to(5);
    press_a();
    CHECK(bo3_modsmenu_mode() && s_overlay_opens == 1 && !strcmp(line(0), "M.00  ON"), "MODS opens the page");
    cursor_to(4);
    press_a();
    CHECK(s_overlay_opens == 1 && bo3_modsmenu_mode(), "line 4 of the MODS page is a mod, not the overlay");
    press_b();
    CHECK(M16(CURSOR) == 5 && s_widget_count == 6, "back: the cursor on MODS (line 5)");
    /* The overlay turns its own line off while the MODS page is open (it can:
     * interface.menu_entry is applied live): back on the list of five, the
     * cursor must be on MODS where it is now, and A never past the table. */
    cursor_to(5);
    press_a();
    s_overlay = 0;
    press_b();
    CHECK(s_widget_count == 5 && M16(CURSOR) == 4 && !strcmp(line(4), "MODS"),
          "the entry gone while the page was open: back on MODS at line 4 (cursor %u of %u)", M16(CURSOR), s_widget_count);
    M16(CURSOR) = 5;                      /* a line the list does not have (a title could not get there) */
    press_a();
    CHECK(!s_left_settings && !s_out_of_range, "A on a Settings line past the list: kept from the title");

    /* Registers: a mark on the MODS page changes only what it hands back. */
    cursor_to(4);
    press_a();
    {
        uint32_t b = 0x0B0B0B0Bu, si = 0x51515151u, di = 0xD1D1D1D1u, bp = 0xB9B9B9B9u, sp = g_esp;
        g_ebx = b; g_esi = si; g_edi = di; g_ebp = bp;
        cursor_to(0);
        g_eax = 1;
        mark("si_select");                /* a toggle: label rewrite and the select sound */
        CHECK(g_ebx == b && g_esi == si && g_edi == di && g_ebp == bp && g_esp == sp && !(g_eax & 0xFF),
              "a toggle keeps the title's registers (A taken)");
        g_eax = 1;
        mark("si_select");                /* turned back */
    }
    press_b();

    /* TAKEDAWN_MODS_MENU=0: hidden (read at the start). */
    SETENV("TAKEDAWN_MODS_MENU", "0");
    start(at("many"));
    enter_settings();
    CHECK(s_widget_count == 4 && s_widget_items == ITEMS, "TAKEDAWN_MODS_MENU=0: the title's list");
    SETENV("TAKEDAWN_MODS_MENU", "1");
    start(at("many"));

    /* The mod list cannot be read at the start (another program has it):
     * every mod on; once readable, the page shows what it says, and APPLY
     * keeps its ids; a choice made meanwhile stays as chosen. (POSIX: chmod
     * makes it unreadable, except to root, which skips this.) */
#if !defined(_WIN32)
    if (geteuid() != 0)
#endif
    {
        CHECK(mk("state/mod-list.toml", "disabled = []\n"), "setup the list");
#if defined(_WIN32)
        HANDLE h = CreateFileA(state, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);   /* no sharing: unreadable */
#else
        chmod(state, 0);
#endif
        start(at("many"));
        title_init();
        enter_settings();
        cursor_to(4);
        press_a();
        CHECK(!strcmp(line(1), "M.01  ON"), "unreadable at the start: on (%s)", line(1));
#if defined(_WIN32)
        CloseHandle(h);
#else
        chmod(state, 0644);
#endif
        {
            FILE *f = fopen(state, "wb");
            if (f) {
                fputs("disabled = [\"m.01\"]\n", f);
                fclose(f);
            }
        }
        press_b();
        cursor_to(4);
        press_a();
        CHECK(!strcmp(line(1), "M.01  OFF*") && find_line("SAVED - RESTART REQUIRED") == 5,
              "readable now: OFF from the next start, nothing to apply (%s / %s)", line(1), line(5));
        cursor_to(2);
        press_a();
        cursor_to(find_line("APPLY"));
        press_a();
        CHECK(strstr(slurp(state), "\"m.01\"") && strstr(slurp(state), "\"m.02\""),
              "APPLY of another change keeps m.01 off: %s", slurp(state));
        press_b();
        bo3_modfs_set_enabled("m.01", 1);
        bo3_modfs_set_enabled("m.02", 1);

        /* The list says off but cannot be read at the start; the player
         * turns the mod off too; the list becomes readable: still OFF as
         * chosen, after any rebuild, and APPLY keeps it off. */
        CHECK(mk("state/mod-list.toml", "disabled = [\"m.01\"]\n"), "setup the list");
#if defined(_WIN32)
        h = CreateFileA(state, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
#else
        chmod(state, 0);
#endif
        start(at("many"));
        title_init();
        enter_settings();
        cursor_to(4);
        press_a();
        cursor_to(1);
        press_a();
        CHECK(!strcmp(line(1), "M.01  OFF*"), "unreadable: turned off (%s)", line(1));
#if defined(_WIN32)
        CloseHandle(h);
#else
        chmod(state, 0644);
#endif
        cursor_to(4);
        press_a();                        /* NEXT and PREVIOUS: two rebuilds */
        cursor_to(0);
        press_a();
        CHECK(!strcmp(line(1), "M.01  OFF*") && find_line("SAVED - RESTART REQUIRED") == 5,
              "readable again, after rebuilds: still OFF as chosen (%s / %s)", line(1), line(5));
        cursor_to(2);
        press_a();
        cursor_to(find_line("APPLY"));
        press_a();
        CHECK(strstr(slurp(state), "\"m.01\"") && strstr(slurp(state), "\"m.02\""),
              "APPLY keeps it off: %s", slurp(state));
        press_b();
        bo3_modfs_set_enabled("m.01", 1);
        bo3_modfs_set_enabled("m.02", 1);
    }

    /* A name of exactly 24 characters is not cut; 25 is. A shared id names
     * the other mod's folder, never its whole host path. */
    mk("n24/a/mod.toml", "id = \"n.a\"\nname = \"ABCDEFGHIJKLMNOPQRSTUVWX\"\n");
    mk("n24/b/mod.toml", "id = \"n.b\"\nname = \"ABCDEFGHIJKLMNOPQRSTUVWXY\"\n");
    mod("n24/dup-one", "n.same", NULL, NULL, NULL);
    mod("n24/dup-two", "n.same", NULL, NULL, NULL);
    start(at("n24"));
    title_init();
    enter_settings();
    cursor_to(4);
    press_a();
    CHECK(!strcmp(line(0), "ABCDEFGHIJKLMNOPQRSTUVWX  ON") && !strcmp(line(1), "ABCDEFGHIJKLMNOPQRSTUV..  ON"),
          "24 characters whole, 25 cut: %s / %s", line(0), line(1));
    cursor_to(find_line("N.SAME"));
    CHECK(!strcmp(line(find_line("N.SAME")), "N.SAME  SAME ID") && !strcmp(line(s_widget_count - 2), "ID ALSO USED BY DUP-TWO"),
          "a shared id: the other folder's name only: %s", line(s_widget_count - 2));
    press_b();

    /* ---- the pagination gate ---- */
    /* Negative control: the harness sees the historical failure. An 11-line
     * page (that version, nine mods) on the widget's 9 frames. */
    title_init();
    s_widget_overflow = 0;
    widget_setup(11);
    CHECK(s_widget_overflow, "negative control: an 11-line page overruns the widget's 9 frames (detected)");
    title_init();
    s_widget_overflow = 0;
    widget_setup(9);
    CHECK(!s_widget_overflow, "control: exactly 9 lines fit");
    /* 0, 1, below, at and above the one-page limit (6), the historical nine,
     * full pages of four, the most mods there can be (64) */
    {
        static const int counts[] = { 0, 1, 5, 6, 7, 8, 9, 12, 13, 16, 17, BO3_MODFS_MAX_MODS };
        for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++)
            pagination_case(counts[i]);
    }
    {
        char folder[700];
        int rows[16], first, count;
        open_with(9, folder, sizeof folder);
        page_mods(rows, &first, &count);
        CHECK(s_widget_count == 8 && count == 4 && find_line("NEXT PAGE") == 4,
              "the historical nine: page 1 is 4 mods + NEXT + 3 lines = 8 (was 11): %u", s_widget_count);
        press_b();
    }
    pagination_toggles(9);
    pagination_toggles(13);
    pagination_toggles(BO3_MODFS_MAX_MODS);
    /* repeated paging and open / close on the largest list */
    {
        char folder[700];
        uint32_t allocs;
        s_guard_bad = s_widget_overflow = 0;
        open_with(BO3_MODFS_MAX_MODS, folder, sizeof folder);
        allocs = s_allocs;
        for (int c = 0; c < 30; c++) {
            for (int p = 0; p < 16 && find_line("NEXT PAGE") >= 0; p++) {
                cursor_to(find_line("NEXT PAGE"));
                press_a();
            }
            for (int p = 0; p < 16 && find_line("PREVIOUS PAGE") >= 0; p++) {
                cursor_to(find_line("PREVIOUS PAGE"));
                press_a();
            }
            press_b();
            cursor_to(4);
            press_a();
        }
        CHECK(s_allocs == allocs && !s_guard_bad && !s_widget_overflow && s_widget_max <= W_FRAMES && !s_out_of_range,
              "30 x (16 pages forward and back, close, open) with 64 mods: no new block, guards and frames intact");
        press_b();
    }
    CHECK(!s_guard_bad, "every heap block, the page object and the widget guards intact at the end");

    /* ---- the generated code without the flow marks ---- */
    /* A build from generated code made before config/patches.json had
     * mm_flow_sel / mm_flow_back (no regen.sh): the build says so, and the
     * title's event 5 changes state without them. MODS is never listed, and
     * every line is the title's or menuentry.c's, as before the page. */
    s_gen_marks = 0;
    s_title_marks = 0;
    start(at("empty"));
    title_init();
    enter_settings();
    CHECK(s_widget_count == 4 && s_widget_items == ITEMS && !bo3_modsmenu_mode() && find_line("MODS") < 0,
          "stale generated code: no MODS, the title's 4 lines and its own items (%u)", s_widget_count);
    for (int i = 0; i < 3; i++) {
        title_init();
        enter_settings();
        cursor_to(i);
        press_a();
        CHECK(s_left_settings && !s_out_of_range, "stale generated code: A on line %d is the title's, in its table", i);
    }
    s_overlay = 1;
    title_init();
    enter_settings();
    CHECK(s_widget_count == 5 && !strcmp(line(4), "TAKEDAWN SETTINGS") && find_line("MODS") < 0,
          "stale generated code, with the overlay: TAKEDAWN SETTINGS only (%u lines)", s_widget_count);
    cursor_to(4);
    {
        int opens = s_overlay_opens;
        press_a();
        CHECK(s_overlay_opens == opens + 1 && !s_left_settings && !s_out_of_range,
              "stale generated code: TAKEDAWN SETTINGS still opens the overlay");
    }
    s_overlay = 0;
    /* The marks generated, but not reaching this file: registered for the
     * name before it (the toolkit calls the first), or the table full. */
    s_gen_marks = 1;
    s_title_marks = 1;
    {
        int other;
        /* room: -1 none taken; else the slots left after menuentry.c's */
        struct { int room; int shadow; const char *what; } k[] = {
            { -1, 1, "mm_flow_sel registered before the page" },
            { 0, 0, "the mark table full before mm_flow_sel" },
            { 1, 0, "the mark table full before mm_flow_back" },
        };
        for (size_t c = 0; c < sizeof k / sizeof k[0]; c++) {
            SETENV("TAKEDAWN_MODS", at("empty"));
            bo3_modfs_init();
            s_nmarks = 0;
            s_other_flow = 0;
            if (k[c].shadow)
                recomp_patch_on_mark("mm_flow_sel", other_flow);
            bo3_menu_entry_start();
            other = s_nmarks;
            if (k[c].room >= 0)
                s_mark_room = other + k[c].room;
            bo3_modsmenu_start();
            CHECK(s_nmarks == (k[c].room >= 0 ? other + k[c].room : other + 2),
                  "%s: %d of the page's 2 marks registered", k[c].what, s_nmarks - other);
            title_init();
            enter_settings();
            CHECK(find_line("MODS") < 0 && s_widget_count == 4 && !bo3_modsmenu_mode(),
                  "%s: no MODS (%d marks before the page)", k[c].what, other);
            if (k[c].shadow)
                CHECK(s_other_flow == 1, "the other function was called once, at the page's start (%d)", s_other_flow);
            s_mark_room = 16;
        }
    }
    /* Negative control: the page as it was before this guard (it listed MODS
     * whatever the generated code was; here, a build claiming the marks)
     * with a title whose generated code lacks them: A on MODS reaches the
     * title's table past its four entries. The guard is what stops it. */
    s_gen_marks = 1;
    s_title_marks = 0;
    start(at("empty"));
    title_init();
    enter_settings();
    CHECK(s_widget_count == 5 && !strcmp(line(4), "MODS"), "negative control: an unguarded build lists MODS");
    cursor_to(4);
    press_a();
    CHECK(s_out_of_range && s_left_settings && !bo3_modsmenu_mode(),
          "negative control: without the marks A on MODS reads past the title's table (detected)");
    /* Generated again: as before. */
    s_title_marks = 1;
    s_out_of_range = 0;
    start(at("empty"));
    title_init();
    enter_settings();
    cursor_to(4);
    press_a();
    CHECK(bo3_modsmenu_mode() && !s_left_settings && !s_out_of_range, "the marks generated again: MODS opens the page");
    press_b();
    CHECK(!bo3_modsmenu_mode() && M16(CURSOR) == 4, "and B returns to MODS");

    start(NULL);
    printf("modsmenu: %d passed, %d failed\n", s_pass, s_fail);
    free(s_mem);
    return s_fail ? 1 : 0;
}
