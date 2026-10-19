/*
 * scoreboard.c - display.score_board (RECOMP_GFX_SCORE_BOARD, BO-165): the
 * board at the top in the middle of a race's HUD ("SCORE 1250": a dark
 * plate and two texts).
 *
 *   original  as shipped (the default): nothing here touches the guest.
 *   smooth    the plate fades with its texts, and the board fades in when
 *             it comes back after a crash or takedown camera.
 *
 * What the title does (BO-155): a showing of the board lasts 238 frames.
 * Its texts fade in and out over six frames each (alpha 1/6 a frame), the
 * plate stays at alpha 1.0 from the first frame to the last, so it pops in
 * and is cut out. A crash or takedown camera hides the HUD at once (its
 * frames' alpha 0), and when the camera ends the board is back with the
 * plate at full alpha. xemu does the same.
 *
 * Where: the HUD object (0x54F900) has a 2D scene per player at + 0x14 +
 * 0x70 p (0x54F914, 0x54F984; players at + 0x170), drawn by sub_000536C0
 * when the byte at 0x4D61D3 is set. A scene's layers (+ 0x2C, count
 * + 0x30, 16 bytes each, bit 0 of the first word: hidden) list their
 * objects from + 8, linked through their first word. sub_001C6500 draws an
 * object with its colour (+ 0x18, alpha at + 0x24) times its frame's
 * (+ 0x30, alpha at + 0x3C), through its callback (+ 0x44). The board is
 * the component the HUD's build sets up with sub_00056DB0 (esi 0x3FDC00
 * for player 1, 0x3FED50 for player 2); its objects (the plate, callback
 * 0x48430 as behind POS and LAP, and the texts) are drawn in its frame
 * (+ 8), in whatever pool slots they took for this showing: they are made
 * for each showing and given back after. The board is told apart from the
 * rest of the HUD by its frame, in the HUD scenes only. Road Rage and
 * split screen have no score board, Crash has its own score HUD (left
 * alone). The award messages under the board are left as they are.
 *
 * How: at the "uw_stage_render" mark (a stage's render, before it draws the
 * HUD scenes; main.c) the alpha of those objects is changed in guest
 * memory, and at the "uw_flip" mark (the frame ends) or the next render put
 * back, unless the title wrote it in between. The title's update runs
 * before the render and after the flip, so it only ever sees its own
 * values. In Original no object is written (nor read, without BO3_SB_LOG).
 * Any other value, an old "off" included, is Original.
 *
 * BO3_SB_LOG=<file>: a line per stage frame for each player's board (B:
 * plate and text alpha as the title has them, and the board as drawn) and
 * the HUD scene's objects whenever they change (S), for the curves in
 * BO-165; off unless set.
 */
#include "region.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controls.h"
#include "platform/recomp_env.h"
#include "platform/recomp_patch.h"

extern ptrdiff_t g_xbox_mem_offset;

#define SB_HUD          BO3_VA(0x0054F900u, 0x0054FC20u)   /* the HUD object */
#define SB_HUD_ON       BO3_VA(0x004D61D3u, 0x004D64F3u)   /* sub_000536C0 draws the HUD scenes */
#define SB_PLATE_CB     BO3_VA(0x00048430u, 0x000485F0u)   /* the plate's callback (POS, LAP too) */
#define SB_TEXT_CB      BO3_VA(0x000F6DC0u, 0x000F7120u)   /* the board's texts (POS, LAP too) */
#define SB_STEP         (1.0f / 6.0f + 0.001f)  /* the title's own fade step */

/* Per player: the score board component (sub_00056DB0's esi), its frame
 * at + 8. */
static const uint32_t k_board[2] = { BO3_VA(0x003FDC00u, 0x003FDF30u), BO3_VA(0x003FED50u, 0x003FF080u) };

enum { SB_ORIGINAL, SB_SMOOTH };

static void *sb_p(uint32_t va) { return (void *)((uintptr_t)g_xbox_mem_offset + va); }
static uint32_t sb_m32(uint32_t va) { uint32_t v; memcpy(&v, sb_p(va), 4); return v; }
static float sb_mf(uint32_t va) { float v; memcpy(&v, sb_p(va), 4); return v; }
static int sb_ok(uint32_t p) { return p >= 0x10000u && p < 0x03FFFF00u && !(p & 3u); }

/* The values written this frame, to put back. */
static struct { uint32_t va, put, was; } s_w[128];
static int s_nw;
static float s_vis[2];                /* smooth: the board's alpha as drawn */
static FILE *s_log;
static uint32_t (*s_flips)(void);

static void sb_write(uint32_t va, float v)
{
    uint32_t now = sb_m32(va), put;

    memcpy(&put, &v, 4);
    if (now == put || s_nw == (int)(sizeof s_w / sizeof s_w[0]))
        return;
    s_w[s_nw].va = va;
    s_w[s_nw].put = put;
    s_w[s_nw].was = now;
    s_nw++;
    memcpy(sb_p(va), &put, 4);
}

static void sb_restore(void)
{
    int i;

    for (i = s_nw - 1; i >= 0; i--)
        if (sb_m32(s_w[i].va) == s_w[i].put)
            memcpy(sb_p(s_w[i].va), &s_w[i].was, 4);
    s_nw = 0;
}

static int sb_mode(void)
{
    const char *v = recomp_env(RENV_GFX_SCORE_BOARD);

    if (!v || !*v)
        return SB_ORIGINAL;
    if (!strcmp(v, "smooth"))
        return SB_SMOOTH;
    return SB_ORIGINAL;
}

/* The board's frame as the component holds it is only its board while the
 * stage has one: in a mode without it (Road Rage, Burning Lap, split
 * screen, Crash) the component keeps a frame from before, which the pool
 * may have given to something else (Burning Lap's boost bar had the frame
 * a race's board had). So it counts only while what is in it looks like
 * the board: plates and texts only, a plate centred at the top. */
static int sb_is_board(uint32_t la, uint32_t nl, uint32_t fr)
{
    float ox = sb_mf(fr + 0x20), oy = sb_mf(fr + 0x24), sx = sb_mf(fr + 0x28), sy = sb_mf(fr + 0x2C);
    uint32_t l;
    int plate = 0;

    for (l = 0; l < nl; l++) {
        uint32_t n;
        int guard = 0;

        for (n = sb_m32(la + l * 16 + 8); sb_ok(n) && guard < 500; n = sb_m32(n), guard++) {
            uint32_t cb = sb_m32(n + 0x44);
            float cx, y;

            if (sb_m32(n + 0x40) != fr)
                continue;
            if (cb != SB_PLATE_CB && cb != SB_TEXT_CB)
                return 0;
            if (cb != SB_PLATE_CB)
                continue;
            cx = ox + sx * (sb_mf(n + 8) + sb_mf(n + 16) * 0.5f);
            y = oy + sy * sb_mf(n + 12);
            if (cx > 256.0f && cx < 384.0f && y > -40.0f && y < 120.0f)
                plate = 1;
        }
    }
    return plate;
}

/* BO3_SB_LOG: a scene's objects, when the set of them changes. */
static uint32_t s_sig[2];
static void sb_dump(int p, uint32_t la, uint32_t nl, uint32_t bfr)
{
    uint32_t l, sig = 2166136261u;

    for (l = 0; l < nl; l++) {
        uint32_t n;
        int guard = 0;

        for (n = sb_m32(la + l * 16 + 8); sb_ok(n) && guard < 500; n = sb_m32(n), guard++)
            sig = (sig ^ (l * 131u + sb_m32(n + 0x40) * 7u + sb_m32(n + 0x44))) * 16777619u;
    }
    if (sig == s_sig[p])
        return;
    s_sig[p] = sig;
    fprintf(s_log, "S %u p%d board_frame %08X\n", s_flips ? s_flips() : 0, p, bfr);
    for (l = 0; l < nl; l++) {
        uint32_t n;
        int guard = 0;

        for (n = sb_m32(la + l * 16 + 8); sb_ok(n) && guard < 500; n = sb_m32(n), guard++)
            fprintf(s_log, "  L%02u%s obj %08X fr %08X cb %08X x %.1f y %.1f a %.2f\n", l,
                    (sb_m32(la + l * 16) & 1u) ? "h" : " ", n, sb_m32(n + 0x40),
                    sb_m32(n + 0x44), sb_mf(n + 8), sb_mf(n + 12), sb_mf(n + 0x24));
    }
}

/* One player's HUD scene (smooth): fade the board as one. */
static void sb_scene(int p, int mode, int hud_on)
{
    uint32_t sc = SB_HUD + 0x14u + 0x70u * (uint32_t)p;
    uint32_t la = sb_m32(sc + 0x2C), nl = sb_m32(sc + 0x30), l;
    uint32_t bfr = sb_m32(k_board[p] + 8);
    uint32_t plate[4], text[8];
    int np = 0, nt = 0, shown = 0, i;
    float fa = 0.0f, ap = 0.0f, at = 0.0f, t, vis;

    if (!sb_ok(la) || !nl || nl > 256)
        return;
    if (!sb_ok(bfr) || !sb_is_board(la, nl, bfr))
        bfr = 0;
    if (s_log)
        sb_dump(p, la, nl, bfr);
    for (l = 0; l < nl; l++) {
        int hidden = (sb_m32(la + l * 16) & 1u) != 0, guard = 0;
        uint32_t n;

        for (n = sb_m32(la + l * 16 + 8); sb_ok(n) && guard < 500; n = sb_m32(n), guard++) {
            uint32_t fr = sb_m32(n + 0x40), cb = sb_m32(n + 0x44);

            if (bfr && fr == bfr) {
                if (cb == SB_PLATE_CB) {
                    if (np < 4)
                        plate[np++] = n;
                    if (!hidden && sb_mf(n + 0x24) > ap)
                        ap = sb_mf(n + 0x24);
                } else {
                    if (nt < 8)
                        text[nt++] = n;
                    if (!hidden && sb_mf(n + 0x24) > at)
                        at = sb_mf(n + 0x24);
                }
                if (!hidden)
                    shown = 1;
            }
        }
    }
    /* Smooth: the board as drawn is its frame's alpha times the lower of
     * the plate's and the brightest text's; from nothing it rises by the
     * title's own step a frame at most, so a board that is back at full
     * alpha after a camera fades in like a new one. */
    if (bfr)
        fa = sb_mf(bfr + 0x3C);
    t = (hud_on && shown && np && nt) ? fa * (ap < at ? ap : at) : 0.0f;
    if (!(t > 0.004f))
        t = 0.0f;
    vis = s_vis[p] + SB_STEP;
    if (vis > t)
        vis = t;
    if (s_log && (np || nt))
        fprintf(s_log, "B %u p%d hud %d shown %d fa %.3f plate %.3f text %.3f mode %d drawn %.3f\n",
                s_flips ? s_flips() : 0, p, hud_on, shown, fa, ap, at, mode,
                mode == SB_SMOOTH ? vis : fa * ap);
    if (mode != SB_SMOOTH)
        return;
    s_vis[p] = vis;
    if (t == 0.0f || !(fa > 0.0f))
        return;
    for (i = 0; i < np; i++)
        sb_write(plate[i] + 0x24, vis / fa);
    for (i = 0; i < nt; i++)
        if (sb_mf(text[i] + 0x24) > vis / fa)
            sb_write(text[i] + 0x24, vis / fa);
}

void bo3_score_board_render(void)
{
    uint32_t players;
    int mode, p, hud_on;

    sb_restore();
    mode = sb_mode();
    if (mode != SB_SMOOTH)
        s_vis[0] = s_vis[1] = 0.0f;
    if ((mode == SB_ORIGINAL && !s_log) || !g_xbox_mem_offset)
        return;
    players = sb_m32(SB_HUD + 0x170);
    if (players > 2)
        players = 2;
    hud_on = *(const uint8_t *)sb_p(SB_HUD_ON) != 0;
    for (p = 0; p < (int)players; p++)
        sb_scene(p, mode, hud_on);
    if (s_log)
        fflush(s_log);
}

void bo3_score_board_flip(void)
{
    if (s_nw)
        sb_restore();
}

void bo3_score_board_start(uint32_t (*flips)(void))
{
    const char *path = getenv("BO3_SB_LOG");

    s_flips = flips;
    /* The overlay's row (Display) shows once the setting is enabled. */
    recomp_patch_enable("display.score_board", RENV_GFX_SCORE_BOARD);
    if (path && *path) {
        s_log = fopen(path, "w");
        if (!s_log)
            fprintf(stderr, "BO3_SB_LOG: cannot write %s\n", path);
    }
}
