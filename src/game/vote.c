/*
 * vote.c - the track vote of a QUICK PLAY game's lobby (vote.h).
 */
#include "vote.h"

#include <stdio.h>
#include <string.h>

/* ---- the pure rules (tests/vote) ---- */

uint32_t bo3_vote_rand(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

int bo3_vote_host_begin(vote_host *h, uint8_t round, const uint8_t *valid, int nvalid,
                        unsigned long long now, uint32_t *rng)
{
    uint8_t pool[64];
    int n = nvalid > (int)sizeof pool ? (int)sizeof pool : nvalid, i, k;
    memset(h, 0, sizeof *h);
    if (n < 1)
        return 0;
    memcpy(pool, valid, (size_t)n);
    k = n < VOTE_TRACKS ? n : VOTE_TRACKS;
    for (i = 0; i < VOTE_TRACKS; i++)
        h->track[i] = VOTE_NO_TRACK;
    for (i = 0; i < k; i++) {
        int j = i + (int)(bo3_vote_rand(rng) % (uint32_t)(n - i));
        uint8_t t = pool[i];
        pool[i] = pool[j];
        pool[j] = t;
        h->track[i] = pool[i];
    }
    h->phase = VP_VOTE;
    h->round = round;
    h->rows = k + 1;
    h->end_ms = now + VOTE_MS;
    h->np = 1;
    h->p[0].id = 0;
    h->p[0].choice = -1;
    h->win_row = -1;
    h->win_track = VOTE_NO_TRACK;
    return 1;
}

void bo3_vote_host_players(vote_host *h, const uint32_t *ids, int n)
{
    vote_player keep[VOTE_PLAYERS];
    int i, k, np = 1;
    keep[0] = h->p[0];
    for (i = 0; i < n && np < VOTE_PLAYERS; i++) {
        vote_player v;
        memset(&v, 0, sizeof v);
        v.id = ids[i];
        v.choice = -1;
        for (k = 1; k < h->np; k++)
            if (h->p[k].id == ids[i])
                v = h->p[k];
        keep[np++] = v;
    }
    memcpy(h->p, keep, sizeof(keep[0]) * (size_t)np);
    h->np = np;
    bo3_vote_host_tally(h);
}

int bo3_vote_host_choice(vote_host *h, uint32_t id, uint8_t round, int row, uint8_t seq)
{
    int i;
    if (h->phase != VP_VOTE || round != h->round || row < 0 || row >= h->rows)
        return 0;
    for (i = 0; i < h->np; i++) {
        vote_player *p = &h->p[i];
        if (p->id != id)
            continue;
        if (p->seen && (uint8_t)(seq - p->seq) >= 128u)
            return 0;               /* an old one, overtaken */
        p->seq = seq;
        p->seen = 1;
        p->choice = row;
        bo3_vote_host_tally(h);
        return 1;
    }
    return 0;
}

void bo3_vote_host_tally(vote_host *h)
{
    int i;
    memset(h->tally, 0, sizeof h->tally);
    for (i = 0; i < h->np; i++)
        if (h->p[i].choice >= 0 && h->p[i].choice < h->rows)
            h->tally[h->p[i].choice]++;
}

int bo3_vote_winner(const int *tally, int rows, uint32_t *rng)
{
    int i, top = -1, n = 0, pick;
    for (i = 0; i < rows; i++)
        if (tally[i] > top)
            top = tally[i];
    for (i = 0; i < rows; i++)
        n += tally[i] == top;
    pick = n > 1 ? (int)(bo3_vote_rand(rng) % (uint32_t)n) : 0;
    for (i = 0; i < rows; i++)
        if (tally[i] == top && pick-- == 0)
            return i;
    return 0;
}

int bo3_vote_host_tick(vote_host *h, unsigned long long now, const uint8_t *valid, int nvalid, uint32_t *rng)
{
    int i, voted = 0;
    if (h->phase != VP_VOTE)
        return VH_NONE;
    if (h->np < 2) {
        h->phase = VP_OVER;
        return VH_OFF;
    }
    for (i = 0; i < h->np; i++)
        voted += h->p[i].choice >= 0;
    if (now < h->end_ms && voted < h->np)
        return VH_NONE;
    bo3_vote_host_tally(h);
    h->win_row = bo3_vote_winner(h->tally, h->rows, rng);
    if (h->win_row == h->rows - 1)
        h->win_track = nvalid > 0 ? valid[bo3_vote_rand(rng) % (uint32_t)nvalid] : h->track[0];
    else
        h->win_track = h->track[h->win_row];
    h->phase = VP_WIN;
    h->won_ms = now;
    return VH_WON;
}

int bo3_vote_host_secs(const vote_host *h, unsigned long long now)
{
    if (h->phase != VP_VOTE || now >= h->end_ms)
        return 0;
    return (int)((h->end_ms - now + 999) / 1000);
}

/*  0 kind, 1 round, 2 phase, 3 seconds, 4..6 tracks, 7 rows, 8..11 the tally,
 *  12 the winner row (0xFF none), 13 the winner track, 14 your row + 1,
 *  15 your choice's number */
void bo3_vote_host_pack(const vote_host *h, uint32_t id, unsigned long long now, uint8_t *d)
{
    int i;
    memset(d, 0, VOTE_DATA);
    d[0] = VK_STATE;
    d[1] = h->round;
    d[2] = (uint8_t)h->phase;
    d[3] = (uint8_t)bo3_vote_host_secs(h, now);
    for (i = 0; i < VOTE_TRACKS; i++)
        d[4 + i] = h->track[i];
    d[7] = (uint8_t)h->rows;
    for (i = 0; i < VOTE_ROWS; i++)
        d[8 + i] = (uint8_t)h->tally[i];
    d[12] = h->win_row >= 0 ? (uint8_t)h->win_row : 0xFFu;
    d[13] = h->win_track;
    for (i = 0; i < h->np; i++)
        if (h->p[i].id == id) {
            d[14] = (uint8_t)(h->p[i].choice + 1);
            d[15] = h->p[i].seq;
        }
}

void bo3_vote_client_reset(vote_client *c)
{
    memset(c, 0, sizeof *c);
    c->choice = -1;
    c->win_row = -1;
}

int bo3_vote_client_state(vote_client *c, uint32_t from, const uint8_t *d, unsigned long long now)
{
    int i, changed = 0;
    if (d[0] != VK_STATE || d[2] < VP_VOTE || d[2] > VP_OVER || d[7] < 2 || d[7] > VOTE_ROWS)
        return 0;
    c->state_ms = now;
    c->host = from;
    if (d[1] != c->round || c->phase == VP_NONE) {
        uint8_t keep_seq = c->seq;  /* a choice number never goes back, whatever the round */
        bo3_vote_client_reset(c);
        c->state_ms = now;
        c->host = from;
        c->seq = keep_seq;
        c->round = d[1];
        changed = 1;
    }
    if (c->phase != d[2] || c->secs != d[3] || c->rows != d[7])
        changed = 1;
    c->phase = d[2];
    c->secs = d[3];
    c->rows = d[7];
    for (i = 0; i < VOTE_TRACKS; i++) {
        changed |= c->track[i] != d[4 + i];
        c->track[i] = d[4 + i];
    }
    for (i = 0; i < VOTE_ROWS; i++) {
        changed |= c->tally[i] != d[8 + i];
        c->tally[i] = d[8 + i];
    }
    c->win_row = d[12] == 0xFFu ? -1 : d[12];
    c->win_track = d[13];
    if (c->choice >= 0 && d[14] == (uint8_t)(c->choice + 1) && d[15] == c->seq)
        c->acked = 1;
    return changed;
}

void bo3_vote_client_choose(vote_client *c, int row)
{
    if (c->phase != VP_VOTE || row < 0 || row >= c->rows)
        return;
    c->choice = row;
    c->seq++;
    c->acked = 0;
    c->send_ms = 0;
}

int bo3_vote_client_tick(vote_client *c, unsigned long long now, int *lost)
{
    if (lost)
        *lost = 0;
    if (c->phase == VP_NONE)
        return 0;
    if (now - c->state_ms > VOTE_LOST_MS) {
        if (lost)
            *lost = 1;
        return 0;
    }
    if (c->phase != VP_VOTE || c->choice < 0 || c->acked)
        return 0;
    if (c->send_ms && now - c->send_ms < VOTE_RESEND_MS)
        return 0;
    c->send_ms = now ? now : 1;
    return 1;
}

void bo3_vote_client_pack(const vote_client *c, uint8_t *d)
{
    memset(d, 0, VOTE_DATA);
    d[0] = VK_CHOICE;
    d[1] = c->round;
    d[2] = (uint8_t)c->choice;
    d[3] = c->seq;
}

int bo3_vote_choice_read(const uint8_t *d, uint8_t *round, int *row, uint8_t *seq)
{
    if (d[0] != VK_CHOICE)
        return 0;
    *round = d[1];
    *row = d[2];
    *seq = d[3];
    return 1;
}

int bo3_vote_note_step(int *ours, int ev, int box_open, int box_kind)
{
    int close = *ours && box_open && box_kind == VOTE_NOTE_KIND;
    switch (ev) {
    case VN_WE_OPEN:
        *ours = 1;
        return 0;
    case VN_FRAME:
        /* the title closed ours (its "if open, close" guards) */
        if (!box_open || box_kind != VOTE_NOTE_KIND)
            *ours = 0;
        return 0;
    default:                        /* the title opens, the time is up, gone */
        *ours = 0;
        return close;
    }
}

#ifndef VOTE_PURE_ONLY

/* ---- the glue ---- */

#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "online.h"
#include "recomp_types.h"
#include "region.h"
#include "statustext.h"
#include "languages.h"
#include "xnet_hle.h"

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
extern ptrdiff_t g_xbox_mem_offset;
extern const char *xbox_SettingsText(const char *key, int *locked);

extern void BO3_SUB(sub_000CBE40, sub_000CC140, sub_000CC1E0)(void);    /* the box opens (al flag; ecx box; kind, text, row texts, the row asked) */
extern void BO3_SUB(sub_000CCC90, sub_000CCF90, sub_000CD030)(void);    /* the box closes and gives every node back (ecx box) */
extern void BO3_SUB(sub_000CCF00, sub_000CD200, sub_000CD2F0)(void);    /* a row's text (ecx box, edx row; centre, highlighted) */
extern void BO3_SUB(sub_000CD0A0, sub_000CD3A0, sub_000CD490)(void);    /* the bar behind the row (eax box; a scale) */
extern void BO3_SUB(sub_000CD2A0, sub_000CD5A0, sub_000CD690)(void);    /* the box's up and down (edi box; pad, which A) */
extern void BO3_SUB(sub_001C6850, sub_001C6B70, sub_001C6C10)(void);    /* a node from the pool (ecx pool, eax layer; the scene) */
extern void BO3_SUB(sub_00017790, sub_000178B0, sub_000178A0)(void);    /* "A taken" (al pad, esi pads) */
extern void BO3_SUB(sub_00017810, sub_00017930, sub_00017920)(void);    /* "B pressed" (al pad, edx pads) */
extern void BO3_SUB(sub_00141440, sub_001416E0, sub_00141780)(void);    /* the select sound (eax the sound block) */
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);    /* the title's heap: (size, tag), stdcall */

#define VT_M8(va)   (*(uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define VT_M16(va)  (*(uint16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define VT_M32(va)  (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define VT_MF(va)   (*(float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define VT_BOX      BO3_VA(0x00550DA0u, 0x005510C0u, 0x00551160u)
#define VT_POOL     BO3_VA(0x00557A20u, 0x00557D40u, 0x00557DE0u)
#define VT_SCENE    BO3_VA(0x00557A34u, 0x00557D54u, 0x00557DF4u)
#define VT_PADS     BO3_VA(0x004AE20Cu, 0x004AE52Cu, 0x004AE5CCu)
#define VT_SOUNDS   BO3_VA(0x00411560u, 0x00411890u, 0x00411930u)
#define VT_PHASE    BO3_VA(0x00557A64u, 0x00557D84u, 0x00557E24u)
#define VT_STRTAB   BO3_VA(0x004D532Cu, 0x004D564Cu, 0x004D56ECu)
#define VT_NKEYS    BO3_VA(0x004D3004u, 0x004D3324u, 0x004D33C4u)
#define VT_KEYS     BO3_VA(0x004D3408u, 0x004D3728u, 0x004D37C8u)
#define VT_TRACKKEY BO3_VA(0x0039EBC0u, 0x0039EEE0u, 0x0039EF80u)   /* the 36 tracks' keys */
#define VT_NAMES    BO3_VA(0x0039ECE0u, 0x0039F000u, 0x0039F0A0u)   /* their upper-case names, as string numbers */
#define VT_OPEN     BO3_VA(0x0044D0CCu, 0x0044D3ECu, 0x0044D48Cu)   /* the tracks this profile has */
#define VT_RACE_LO  BO3_VA(0x00463BD8u, 0x00463EF8u, 0x00463F98u)
#define VT_RACE_HI  BO3_VA(0x00463BDCu, 0x00463EFCu, 0x00463F9Cu)

#define VT_TRACKS   36
#define VT_STR_RANDOM 686           /* region: not an address (a string number) */
#define VT_STR_WINS   752           /* "%1 WINS!", region: not an address (a string number) */

/* the box object (0x550DA0): rows count +0xE, cursor +0xC, row nodes +0x20..,
 * text node +0x84, the rows' top +0x50 */
#define BX_OPEN     0x0Au
#define BX_CURSOR   0x0Cu
#define BX_COUNT    0x0Eu
#define BX_NODES    0x20u
#define BX_TOP      0x50u
#define BX_PULSE    0x5Cu
#define BX_KIND     0x94u
#define BX_TEXT     0x84u
#define BX_YESNO    7               /* the kind with two rows */
#define BX_NOTICE   0               /* the kind with none */
#define BX_ROW_STEP 34.0f           /* the box's own pitch: 32 + 2 */
#define BX_ROW_LIFT 32.0f           /* what each row more takes off the rows' top */
#define NODE_Y      4u
#define ROW_FIT     30              /* characters of a row at the highlighted size */

/* the lobby screen */
#define SC_STATE    0x13ACu
#define SC_LINKED   0x13A4u
#define SC_ALIVE    0x13A5u
#define SC_APPLY    0x08u           /* the flag the options page's return sets (0x55CBA8) */
#define SC_BLOCK    0x12E0u         /* the request block (0x55DE80) */
#define BLOCK_LO    0x0Cu
#define BLOCK_HI    0x20u
#define BLOCK_MODE  0x14u
#define ST_PLAIN    1

#define V_LOG(...) do { fprintf(stderr, "[VOTE] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

/* our text and the row object, on the title's heap: once, for good */
#define MEM_HEAD    0x000u
#define MEM_ROW0    0x080u
#define MEM_ROW_N   0x060u
#define MEM_WIN     0x200u
#define MEM_SHADOW  0x280u
#define MEM_SIZE    0x340u
#define SHADOW_N    0x98u
#define HEAP_TAG    0x54454E58u     /* "XNET" */

static vote_host s_h;
static vote_client s_c;
static int s_host;                  /* this copy runs the vote */
static uint32_t s_rng;
static uint8_t s_round;
static uint8_t s_valid[VT_TRACKS];
static int s_nvalid;
static unsigned long long s_host_send, s_win_until, s_lobby_ms, s_apply_at;
static int s_applied, s_apply_seen;
static int s_open, s_note, s_suspend, s_shown_win, s_in_input;
static int s_win_box;               /* the box holds our winner notice (bo3_vote_note_step) */
static int s_novote;                /* this game cannot have a vote */
static uint32_t s_mem;
static int s_rows_shown[VOTE_ROWS], s_secs_shown, s_choice_shown;
static uint32_t s_scr;
static uint32_t s_free_low = 0xFFFFFFFFu;
static uint8_t s_round_seen;
static int s_cycles;                /* test: TAKEDAWN_VOTE_TEST=cycles, votes without races */

int bo3_vote_holds_start(void);

static unsigned long long vt_ms(void)
{
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
#endif
}

/* a guest function with its stack arguments already pushed (quickplay.c qp_call) */
static void vt_call(void (*fn)(void), uint32_t argbytes, uint32_t *lo)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, b = g_ebx, si = g_esi, di = g_edi, bp = g_ebp, seh = g_seh_ebp;
    PUSH32(g_esp, 0);
    fn();
    if (lo) *lo = g_eax;
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp + argbytes; g_ebx = b; g_esi = si; g_edi = di; g_ebp = bp; g_seh_ebp = seh;
}

static uint32_t vt_mem(void)
{
    if (!s_mem) {
        uint32_t p = 0;
        PUSH32(g_esp, HEAP_TAG);
        PUSH32(g_esp, MEM_SIZE);
        vt_call(BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39), 8u, &p);
        s_mem = p;
        if (p)
            memset((void *)((uintptr_t)g_xbox_mem_offset + p), 0, MEM_SIZE);
    }
    return s_mem;
}

static uint32_t vt_string(uint32_t n)
{
    return VT_M32(VT_M32(VT_M32(VT_STRTAB) + 0xCu) + n * 4u);
}

/* UTF-16 helpers on guest memory */
static int put_u16(uint32_t dst, int at, int cap, uint32_t src)
{
    while (src && at < cap - 1) {
        uint16_t c = VT_M16(src);
        if (!c)
            break;
        VT_M16(dst + 2u * (uint32_t)at++) = c;
        src += 2u;
    }
    VT_M16(dst + 2u * (uint32_t)at) = 0;
    return at;
}

static int put_asc(uint32_t dst, int at, int cap, const char *s)
{
    while (*s && at < cap - 1)
        VT_M16(dst + 2u * (uint32_t)at++) = (uint8_t)*s++;
    VT_M16(dst + 2u * (uint32_t)at) = 0;
    return at;
}

static uint32_t track_name(uint8_t t)
{
    return t < VT_TRACKS ? vt_string(VT_M32(VT_NAMES + 4u * t)) : 0u;
}

static void name_ascii(uint32_t p, char *out, size_t n)
{
    size_t i = 0;
    while (p && VT_M16(p) && i + 1 < n) {
        out[i++] = (char)VT_M16(p);
        p += 2u;
    }
    out[i] = 0;
}

static int vt_lang(void)
{
    int lang = bo3_language_index(xbox_SettingsText("game.language", NULL));
    return lang >= 0 ? lang : BO3_LANG_ENGLISH;
}

/* the box's heading: the question and the seconds */
static void text_head(int secs)
{
    char n[16];
    uint32_t p = s_mem + MEM_HEAD;
    int at = put_asc(p, 0, 60, bo3_statustext(vt_lang(), BO3_TXT_VOTE));
    snprintf(n, sizeof n, "   %d", secs);
    put_asc(p, at, 60, n);
}

/* a row: > TRACK NAME   2 (the mark is the player's own vote; the cursor is
 * the box's own bar) */
static void text_row(int row)
{
    char n[12];
    uint32_t p = s_mem + MEM_ROW0 + MEM_ROW_N * (uint32_t)row;
    int at = 0, cap = (int)(MEM_ROW_N / 2u);
    if (s_c.choice == row)
        at = put_asc(p, at, cap, "> ");
    if (row == s_c.rows - 1)
        at = put_u16(p, at, cap, vt_string(VT_STR_RANDOM));
    else
        at = put_u16(p, at, cap, track_name(s_c.track[row]));
    snprintf(n, sizeof n, "  %d", s_c.tally[row]);
    put_asc(p, at, cap, n);
}

/* "%1 WINS!" with the winning track */
static void text_win(void)
{
    uint32_t p = s_mem + MEM_WIN, src = vt_string(VT_STR_WINS), name = track_name(s_c.win_track);
    int at = 0, cap = 60;
    while (src && VT_M16(src) && at < cap - 1) {
        if (VT_M16(src) == '%' && VT_M16(src + 2u) == '1') {
            at = put_u16(p, at, cap, name);
            src += 4u;
            continue;
        }
        VT_M16(p + 2u * (uint32_t)at++) = VT_M16(src);
        src += 2u;
    }
    VT_M16(p + 2u * (uint32_t)at) = 0;
}

/* a free node in the box's pool layer, counted */
static uint32_t pool_free(void)
{
    int layer = (int16_t)VT_M16(VT_BOX + 8u) + 1;
    uint32_t tab = VT_M32(VT_POOL + 0x2Cu), head = VT_M32((uint32_t)layer * 16u + tab + 4u), n = 0;
    uint32_t e = head ? VT_M32(head + 0x10u) : 0;
    while (e && n < 4096u) {
        n++;
        e = VT_M32(e);
    }
    return n;
}

static uint32_t node_new(void)
{
    uint32_t a = g_eax, c = g_ecx, node = 0;
    g_eax = (uint32_t)((int16_t)VT_M16(VT_BOX + 8u) + 1);
    g_ecx = VT_POOL;
    PUSH32(g_esp, VT_M32(VT_SCENE));
    vt_call(BO3_SUB(sub_001C6850, sub_001C6B70, sub_001C6C10), 4u, &node);
    g_eax = a;
    g_ecx = c;
    return node;
}

static void row_set(int row, int highlighted)
{
    uint32_t c = g_ecx, d = g_edx;
    g_ecx = VT_BOX;
    g_edx = (uint32_t)row;
    PUSH32(g_esp, (uint32_t)highlighted);
    PUSH32(g_esp, 0x43A00000u);     /* the centre, 320.0 */
    vt_call(BO3_SUB(sub_000CCF00, sub_000CD200, sub_000CD2F0), 8u, NULL);
    g_ecx = c;
    g_edx = d;
}

static void box_close(void)
{
    uint32_t c = g_ecx;
    g_ecx = VT_BOX;
    vt_call(BO3_SUB(sub_000CCC90, sub_000CCF90, sub_000CD030), 0u, NULL);
    g_ecx = c;
}

static void box_call_open(int kind, uint32_t text, uint32_t row0, uint32_t row1)
{
    uint32_t a = g_eax, c = g_ecx;
    PUSH32(g_esp, 0xFu);            /* no row asked for: the first */
    PUSH32(g_esp, row1);
    PUSH32(g_esp, row0);
    PUSH32(g_esp, text);
    PUSH32(g_esp, (uint32_t)kind);
    g_eax &= ~0xFFu;
    g_ecx = VT_BOX;
    s_in_input = 1;
    vt_call(BO3_SUB(sub_000CBE40, sub_000CC140, sub_000CC1E0), 20u, NULL);
    s_in_input = 0;
    g_eax = a;
    g_ecx = c;
}

/* the winner notice's box: one close for each open of ours (vote.h) */
static void note_event(int ev)
{
    if (bo3_vote_note_step(&s_win_box, ev, VT_M8(VT_BOX + BX_OPEN), VT_M8(VT_BOX + BX_KIND))) {
        V_LOG("the winner notice closes (%d)", ev);
        box_close();
    }
}

static void note_open(void)
{
    box_call_open(BX_NOTICE, s_mem + MEM_WIN, 0u, 0u);
    if (VT_M8(VT_BOX + BX_OPEN))
        bo3_vote_note_step(&s_win_box, VN_WE_OPEN, 1, BX_NOTICE);
}

/* The title's question box with the rows it has room for in its objects: two
 * by its table, two more in the slots its three-picture kind uses, which the
 * box's close gives back as well (sub_000CCC90 walks +0x20 .. +0x30). */
static void popup_open(void)
{
    uint32_t box = VT_BOX, mem = vt_mem();
    float top;
    int r;
    if (!mem)
        return;
    if (pool_free() < 12u) {
        V_LOG("too few UI nodes free for the box (%u): no popup", pool_free());
        s_suspend = 1;
        return;
    }
    text_head(s_c.secs);
    for (r = 0; r < s_c.rows; r++)
        text_row(r);
    box_call_open(BX_YESNO, mem + MEM_HEAD, mem + MEM_ROW0, mem + MEM_ROW0 + MEM_ROW_N);
    VT_M16(box + BX_COUNT) = (uint16_t)s_c.rows;
    for (r = 2; r < s_c.rows; r++)
        VT_M32(box + BX_NODES + 4u * (uint32_t)r) = node_new();
    /* the rows' top and the heading move up as the title's layout would have
     * moved them for this many rows */
    top = VT_MF(box + BX_TOP);
    VT_MF(box + BX_TOP) = top - (float)(s_c.rows - 2) * BX_ROW_LIFT;
    VT_MF(VT_M32(box + BX_TEXT) + NODE_Y) -= (float)(s_c.rows - 2) * BX_ROW_LIFT * 0.5f;
    s_open = 1;
    for (r = 0; r < s_c.rows; r++) {
        row_set(r, 0);
        s_rows_shown[r] = s_c.tally[r];
    }
    s_secs_shown = s_c.secs;
    s_choice_shown = s_c.choice;
    /* back on the row of this player's vote */
    if (s_c.choice >= 0)
        VT_M16(box + BX_CURSOR) = (uint16_t)s_c.choice;
    {
        uint32_t f = pool_free();
        if (f < s_free_low)
            s_free_low = f;
        V_LOG("popup open, %d rows, %u nodes free", s_c.rows, f);
    }
}

static void popup_close(void)
{
    if (s_open && VT_M8(VT_BOX + BX_OPEN))
        box_close();
    s_open = 0;
}

static void popup_update(void)
{
    uint32_t box = VT_BOX;
    int r, cur = VT_M16(box + BX_CURSOR), any = 0;
    if (s_secs_shown != s_c.secs) {
        text_head(s_c.secs);
        s_secs_shown = s_c.secs;
    }
    for (r = 0; r < s_c.rows; r++)
        if (s_rows_shown[r] != s_c.tally[r] || (s_choice_shown != s_c.choice && (r == s_c.choice || r == s_choice_shown))) {
            text_row(r);
            row_set(r, r == cur);
            s_rows_shown[r] = s_c.tally[r];
            any |= r == cur;
        }
    s_choice_shown = s_c.choice;
    if (any) {
        uint32_t a = g_eax;
        g_eax = box;
        PUSH32(g_esp, 0x3F800000u);
        vt_call(BO3_SUB(sub_000CD0A0, sub_000CD3A0, sub_000CD490), 4u, NULL);
        g_eax = a;
    }
}

static int pad_a(void)
{
    uint32_t r = 0, a = g_eax, si = g_esi;
    g_eax = 0;
    g_esi = VT_PADS;
    vt_call(BO3_SUB(sub_00017790, sub_000178B0, sub_000178A0), 0u, &r);
    g_eax = a;
    g_esi = si;
    return (r & 0xFFu) != 0;
}

static int pad_b(void)
{
    uint32_t r = 0, a = g_eax, d = g_edx;
    g_eax = 0;
    g_edx = VT_PADS;
    vt_call(BO3_SUB(sub_00017810, sub_00017930, sub_00017920), 0u, &r);
    g_eax = a;
    g_edx = d;
    return (r & 0xFFu) != 0;
}

/* sub_000A0BF0, the lobby's plain state's input (edi the screen). While the
 * box is up the title's lobby takes no press. B closes the box and goes on to
 * the title, whose own "leave the game?" comes up with NO first; the vote runs
 * on behind it and the box is back on NO. */
void bo3_vote_input(void (*title_fn)(void))
{
    uint32_t di = g_edi, a = g_eax;
    if (!s_open || !g_xbox_mem_offset) {
        title_fn();
        return;
    }
    if (pad_b()) {
        V_LOG("B: the title's leave question, the vote goes on");
        popup_close();
        s_suspend = 1;
        title_fn();
        return;
    }
    g_edi = VT_BOX;
    PUSH32(g_esp, 0u);
    PUSH32(g_esp, 0u);
    vt_call(BO3_SUB(sub_000CD2A0, sub_000CD5A0, sub_000CD690), 8u, NULL);
    g_edi = di;
    if (pad_a()) {
        int row = VT_M16(VT_BOX + BX_CURSOR);
        uint32_t ea = g_eax;
        g_eax = VT_SOUNDS;
        vt_call(BO3_SUB(sub_00141440, sub_001416E0, sub_00141780), 0u, NULL);
        g_eax = ea;
        if (s_host) {
            s_c.choice = row;
            bo3_vote_host_choice(&s_h, 0u, s_h.round, row, (uint8_t)(s_h.p[0].seq + 1u));
        } else
            bo3_vote_client_choose(&s_c, row);
        V_LOG("voted row %d", row);
    }
    g_eax = a;
    g_esp += 4u;                    /* its ret: the title's input does not run */
}

/* sub_000A1560 (eax the screen): the host's START. A vote runs, so this test
 * of the lobby's own ("the session is alive") says no for its one look; the
 * rest of the function, the race starting, reads other fields. */
void bo3_vote_start_check(void (*title_fn)(void))
{
    uint32_t scr = g_eax;
    uint8_t alive;
    if (!g_xbox_mem_offset || !bo3_vote_holds_start()) {
        title_fn();
        return;
    }
    alive = VT_M8(scr + SC_ALIVE);
    VT_M8(scr + SC_ALIVE) = 0;
    title_fn();
    VT_M8(scr + SC_ALIVE) = alive;
}

/* sub_000CCF00: the title keeps two rows' texts, the rest of its row objects
 * has no text slot, so each row goes through a copy of the box that holds that
 * row as its first: the title's function does the measuring and the look. */
void bo3_vote_row(void (*title_fn)(void))
{
    uint32_t box = VT_BOX, sh, row = g_edx;
    if (!s_open || g_ecx != box || !s_mem || row >= VOTE_ROWS) {
        title_fn();
        return;
    }
    sh = s_mem + MEM_SHADOW;
    memcpy((void *)((uintptr_t)g_xbox_mem_offset + sh), (void *)((uintptr_t)g_xbox_mem_offset + box), SHADOW_N);
    VT_M32(sh + 0x10u) = s_mem + MEM_ROW0 + MEM_ROW_N * row;
    /* the highlighted size of the longest names does not fit the box: those
     * keep the plain size, the cursor's arrows say which row it is */
    {
        uint32_t p = s_mem + MEM_ROW0 + MEM_ROW_N * row;
        int n = 0;
        while (VT_M16(p + 2u * (uint32_t)n) && n < 60)
            n++;
        if (n > ROW_FIT)
            VT_M32(g_esp + 8u) = 0;
    }
    VT_M32(sh + BX_NODES) = VT_M32(box + BX_NODES + 4u * row);
    VT_MF(sh + BX_TOP) = VT_MF(box + BX_TOP) + (float)row * BX_ROW_STEP;
    g_ecx = sh;
    g_edx = 0;
    title_fn();
    VT_MF(box + BX_PULSE) = VT_MF(sh + BX_PULSE);
}

/* sub_000CBE40: the title wants the box. Ours goes first: its close gives the
 * nodes back. While the winner shows, the title's notes on the update carry
 * the winner, so the box does not change under the player. */
void bo3_vote_box_open(void (*title_fn)(void))
{
    if (g_ecx == VT_BOX && !s_in_input && g_xbox_mem_offset) {
        if (s_open) {
            V_LOG("the title opens the box: the vote's goes");
            popup_close();
        }
        note_event(VN_TITLE_OPEN);  /* the title's open takes nodes, gives none back */
        if (s_note && s_win_until && vt_ms() < s_win_until && s_scr && MEM32(g_esp + 4u) == BX_NOTICE && s_mem) {
            uint8_t st = VT_M8(s_scr + SC_STATE);
            if (st >= 0x0Cu && st <= 0x0Eu)
                MEM32(g_esp + 8u) = s_mem + MEM_WIN;
        }
    }
    title_fn();
}

/* the valid tracks: what the title's own random pick takes (sub_0009AC40) */
static int valid_tracks(uint8_t *out)
{
    int n = (int)VT_M32(VT_NKEYS), k, t, o = 0;
    for (k = 0; k < n && k < 128; k++) {
        uint32_t lo = VT_M32(VT_KEYS + 8u * (uint32_t)k), hi = VT_M32(VT_KEYS + 8u * (uint32_t)k + 4u);
        for (t = 0; t < VT_TRACKS; t++) {
            if (VT_M32(VT_TRACKKEY + 8u * (uint32_t)t) != lo || VT_M32(VT_TRACKKEY + 8u * (uint32_t)t + 4u) != hi)
                continue;
            if (VT_M8(VT_OPEN + (uint32_t)t) && o < VT_TRACKS)
                out[o++] = (uint8_t)t;
            break;
        }
    }
    return o;
}

static void host_send(unsigned long long now)
{
    uint8_t d[VOTE_DATA];
    int i;
    for (i = 1; i < s_h.np; i++) {
        bo3_vote_host_pack(&s_h, s_h.p[i].id, now, d);
        xnet_hle_vote_send(s_h.p[i].id, d);
    }
    /* our own view */
    bo3_vote_host_pack(&s_h, 0u, now, d);
    bo3_vote_client_state(&s_c, 0u, d, now);
}

/* the winner into the request block, and the flag the options page's return
 * sets (sub_0006DD70): the lobby's next frame sends the update through the
 * title's own code, states 0xC and 0xD */
static void host_apply(uint32_t scr)
{
    uint32_t b = scr + SC_BLOCK, lo = VT_M32(VT_TRACKKEY + 8u * s_h.win_track), hi = VT_M32(VT_TRACKKEY + 8u * s_h.win_track + 4u);
    VT_M32(b + BLOCK_LO) = lo;
    VT_M32(b + BLOCK_HI) = hi;
    VT_M32(VT_RACE_LO) = lo;
    VT_M32(VT_RACE_HI) = hi;
    VT_M8(scr + SC_APPLY) = 1;
    V_LOG("host: track %u goes out by the title's update, key %08X:%08X", s_h.win_track, lo, hi);
}

/* test runs: the rows asked for (TAKEDAWN_VOTE_TRACKS=a,b,c), for the frames
 * of the longest names */
static void vt_test_rows(void)
{
    const char *e = getenv("TAKEDAWN_VOTE_TRACKS");
    int i;
    if (getenv("TAKEDAWN_VOTE_TEST") && !strcmp(getenv("TAKEDAWN_VOTE_TEST"), "cycles"))
        s_cycles = 1;
    if (e)
        for (i = 0; i < VT_TRACKS; i++) {
            char a[40];
            name_ascii(track_name((uint8_t)i), a, sizeof a);
            V_LOG("track %d: %s", i, a);
        }
    for (i = 0; e && *e && i < VOTE_TRACKS; i++) {
        int t = atoi(e);
        if (t >= 0 && t < VT_TRACKS)
            s_h.track[i] = (uint8_t)t;
        e = strchr(e, ',');
        e = e ? e + 1 : NULL;
    }
}

int bo3_vote_host_frame(uint32_t scr, int players, int idle, int guests_ready)
{
    unsigned long long now = vt_ms();
    uint32_t ids[VOTE_PLAYERS * 2];
    int n, ev;
    if (s_novote)
        return -1;
    s_scr = scr;
    if (s_applied && (players < 2 || s_cycles)) {
        /* everyone left once the track was out: the next ones get a vote of their own */
        V_LOG("host: nobody left to race with, the vote starts over");
        s_applied = s_apply_seen = 0;
        s_apply_at = 0;
        s_win_until = 0;
        note_event(VN_NEW_ROUND);
        s_note = s_shown_win = 0;
        memset(&s_h, 0, sizeof s_h);
        bo3_vote_client_reset(&s_c);
    }
    if (s_h.phase == VP_NONE || s_h.phase == VP_OVER) {
        if (players >= 2 && idle && guests_ready && VT_M8(scr + SC_LINKED) && !s_applied) {
            uint32_t mode = VT_M32(scr + SC_BLOCK + BLOCK_MODE);
            /* the modes the title gives a race track; the crash modes have junctions */
            s_nvalid = mode <= 1u ? valid_tracks(s_valid) : 0;
            if (!s_rng)
                s_rng = (uint32_t)time(NULL) ^ (uint32_t)now ^ ((uint32_t)xnet_hle_port_offset() << 16);
            if (!s_nvalid) {
                V_LOG("no vote for this game: mode %u, %d tracks", mode, s_nvalid);
                s_novote = 1;
                return -1;
            }
            s_round = (uint8_t)(s_round + 1u) ? (uint8_t)(s_round + 1u) : 1u;
            bo3_vote_host_begin(&s_h, s_round, s_valid, s_nvalid, now, &s_rng);
            s_host = 1;
            s_host_send = 0;
            vt_test_rows();
            {
                char a[40], b[40], c[40];
                name_ascii(track_name(s_h.track[0]), a, sizeof a);
                name_ascii(track_name(s_h.track[1]), b, sizeof b);
                name_ascii(track_name(s_h.track[2]), c, sizeof c);
                V_LOG("host: round %u, %d rows: %s / %s / %s", s_round, s_h.rows, a, b, c);
            }
        } else
            return 0;
    }
    n = xnet_hle_vote_members(ids, (int)(sizeof ids / sizeof ids[0]));
    if (s_h.phase == VP_VOTE || s_h.phase == VP_WIN)
        bo3_vote_host_players(&s_h, ids, n);
    ev = bo3_vote_host_tick(&s_h, now, s_valid, s_nvalid, &s_rng);
    if (ev == VH_WON) {
        V_LOG("host: winner row %d, track %u, tally %d %d %d %d", s_h.win_row, s_h.win_track, s_h.tally[0], s_h.tally[1],
              s_h.tally[2], s_h.tally[3]);
        s_apply_at = now + VOTE_APPLY_MS;
        s_apply_seen = 0;
        s_host_send = 0;
    } else if (ev == VH_OFF) {
        V_LOG("host: below two players, the vote is off");
        s_host_send = 0;
    }
    if (!s_host_send || now - s_host_send >= VOTE_SEND_MS) {
        s_host_send = now;
        host_send(now);
    }
    if (s_h.phase == VP_WIN) {
        if (s_apply_at && now >= s_apply_at && idle) {
            s_apply_at = 0;
            host_apply(scr);
        } else if (!s_apply_at && !s_applied) {
            if (!idle)
                s_apply_seen = 1;   /* the title's update runs */
            else if (s_apply_seen && now >= s_h.won_ms + VOTE_WIN_MS) {
                s_applied = 1;
                V_LOG("host: the update is through");
            }
        }
        return s_applied && !s_cycles;
    }
    return 0;
}

int bo3_vote_holds_start(void)
{
    return s_host && (s_h.phase == VP_VOTE || s_h.phase == VP_WIN);
}

int bo3_vote_busy(void)
{
    return (s_c.phase == VP_VOTE && s_scr) || (s_c.phase == VP_WIN && s_win_until && vt_ms() < s_win_until);
}

int bo3_vote_bot_view(int *rows, int *cursor, int *mine, unsigned *round)
{
    if (!s_open || s_c.phase != VP_VOTE)
        return 0;
    *rows = s_c.rows;
    *cursor = VT_M16(VT_BOX + BX_CURSOR);
    *mine = s_c.choice;
    *round = s_c.round;
    return 1;
}

/* the copy's own frame: packets in, the choice out, the box */
void bo3_vote_lobby_frame(uint32_t scr)
{
    unsigned long long now = vt_ms();
    uint8_t d[VOTE_DATA];
    uint32_t from;
    int lost = 0, plain;
    if (!g_xbox_mem_offset)
        return;
    s_scr = scr;
    s_lobby_ms = now;
    while (xnet_hle_vote_get(&from, d)) {
        uint8_t round, seq;
        int row;
        if (s_host) {
            if (bo3_vote_choice_read(d, &round, &row, &seq) && bo3_vote_host_choice(&s_h, from, round, row, seq))
                V_LOG("host: a vote for row %d", row);
        } else
            bo3_vote_client_state(&s_c, from, d, now);
    }
    if (!s_host) {
        if (bo3_vote_client_tick(&s_c, now, &lost)) {
            bo3_vote_client_pack(&s_c, d);
            xnet_hle_vote_send(s_c.host, d);
        }
        if (lost) {
            V_LOG("no word from the host: the vote is gone");
            popup_close();
            note_event(VN_LOST);
            s_note = 0;
            bo3_vote_client_reset(&s_c);
            s_suspend = s_shown_win = 0;
            s_win_until = 0;
        }
    }
    if (s_c.phase != VP_NONE && s_c.round != s_round_seen) {
        /* a new round in the same lobby */
        s_round_seen = s_c.round;
        note_event(VN_NEW_ROUND);   /* the last winner, still showing */
        s_suspend = s_shown_win = 0;
        s_note = 0;
        s_win_until = 0;
    }
    plain = VT_M8(scr + SC_STATE) == ST_PLAIN && VT_M32(VT_PHASE) == 5u;
    note_event(VN_FRAME);
    /* the box went without us (the title closed it) */
    if (s_open && (!VT_M8(VT_BOX + BX_OPEN) || VT_M8(VT_BOX + BX_KIND) != BX_YESNO)) {
        s_open = 0;
        s_suspend = 1;
    }
    if (s_c.phase != VP_VOTE && s_open)
        popup_close();
    if (s_c.phase == VP_VOTE) {
        if (s_suspend && plain && !VT_M8(VT_BOX + BX_OPEN))
            s_suspend = 0;
        if (s_open)
            popup_update();
        else if (!s_suspend && plain && !VT_M8(VT_BOX + BX_OPEN))
            popup_open();
    } else if (s_c.phase == VP_WIN && !s_shown_win) {
        s_shown_win = 1;
        s_win_until = now + VOTE_WIN_MS;
        V_LOG("the winner shows: track %u", s_c.win_track);
        if (vt_mem()) {
            text_win();
            s_note = 1;             /* a box of the title's up: its next one carries the winner */
        }
    } else if (s_c.phase == VP_OVER) {
        bo3_vote_client_reset(&s_c);
        s_suspend = s_shown_win = 0;
    }
    if (s_note && s_win_until) {
        if (now >= s_win_until) {
            s_note = 0;
            note_event(VN_TIMEOUT); /* in any state: a title's box of its own is not ours */
        } else if (plain && !VT_M8(VT_BOX + BX_OPEN))
            note_open();
    }
}

void bo3_vote_flip(void)
{
    uint8_t d[VOTE_DATA];
    uint32_t from;
    if (!g_xbox_mem_offset)
        return;
    if (s_lobby_ms && vt_ms() - s_lobby_ms < 1500ull)
        return;
    /* nobody in a lobby reads these: never left for the next lobby to find */
    while (xnet_hle_vote_get(&from, d))
        ;
    if (!s_lobby_ms)
        return;
    /* the lobby has gone: nothing of the vote stays */
    if (s_open || s_note)
        V_LOG("the lobby left with the box up");
    if (s_open && VT_M8(VT_BOX + BX_OPEN))
        popup_close();
    note_event(VN_LEFT);
    V_LOG("lobby left: race track %08X:%08X, lowest free nodes %u", VT_M32(VT_RACE_LO), VT_M32(VT_RACE_HI), s_free_low);
    s_open = s_note = s_suspend = s_shown_win = 0;
    s_win_until = s_apply_at = 0;
    s_round_seen = 0;
    s_applied = s_apply_seen = 0;
    s_novote = 0;
    s_host = 0;
    s_lobby_ms = 0;
    s_scr = 0;
    memset(&s_h, 0, sizeof s_h);
    bo3_vote_client_reset(&s_c);
}

#endif /* VOTE_PURE_ONLY */
