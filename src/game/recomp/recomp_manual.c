/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

extern uint32_t g_eax;
extern ptrdiff_t g_xbox_mem_offset;

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
/* The wrapped functions below are the NTSC-U build's; the PAL build has
 * its own, at its own addresses, in recomp_manual_pal.c (regen.sh hands the
 * recompiler that file instead). */
#if !(defined(BO3_REGION_PAL) && BO3_REGION_PAL)
/* Custom music (BO-108, ../soundtrack.h): the functions of the title
 * through which a user soundtrack's song is opened and gets its decoder,
 * and its track start.
 * Wrapped, not replaced: the generated bodies are the _gen names, and are
 * all that runs unless audio.custom_music_dir names a folder. */
#include "../soundtrack.h"

extern void sub_001D0554_gen(void);
extern void sub_0030211F_gen(void);
extern void sub_001537C0_gen(void);
extern void sub_00155420_gen(void);

void sub_001D0554(void) { bo3_soundtrack_open_song(sub_001D0554_gen); }
void sub_0030211F(void) { bo3_soundtrack_create_decoder(sub_0030211F_gen); }
/* The title's track start: a custom list's index must not reach EA GAMES
 * TRAX's 44-entry table (BO-120). */
void sub_001537C0(void) { bo3_soundtrack_start_track(sub_001537C0_gen); }
/* The title's "start the music of the source chosen" (BO-230): the one place
 * the chosen soundtrack is read, and the one place a start switches it. */
void sub_00155420(void) { bo3_soundtrack_start_source(sub_00155420_gen); }

/* DirectSound's "voice done" (BO-173, ../dsound_notify.c): a voice whose
 * done notification still waits for the DPC is not queued twice. */
#include "../dsound_notify.h"

extern void sub_002FAAEF_gen(void);

void sub_002FAAEF(void) { bo3_dsound_voice_done(sub_002FAAEF_gen); }


/* Online play (BO-134, ../online.c): the XDK's XNet and XOnline API
 * stubs, answered from the toolkit's host network layer when
 * TAKEDAWN_ONLINE=1; otherwise the title's own libraries run as shipped
 * (no cable). 0x0020F9AE is XNetGetEthernetLinkStatus, linked into .text. */
#include "../online.h"

extern void sub_0020F9AE_gen(void);
extern void sub_00339722_gen(void);
extern void sub_00339739_gen(void);
extern void sub_00339827_gen(void);
extern void sub_0033932E_gen(void);
extern void sub_003392A9_gen(void);
extern void sub_003392B4_gen(void);
extern void sub_003392BF_gen(void);
extern void sub_003392D6_gen(void);
extern void sub_003392ED_gen(void);
extern void sub_003392F8_gen(void);
extern void sub_00339307_gen(void);
extern void sub_00339511_gen(void);
extern void sub_00339323_gen(void);
extern void sub_003394FB_gen(void);
extern void sub_00339506_gen(void);
extern void sub_00339339_gen(void);
extern void sub_00339344_gen(void);
extern void sub_0033934F_gen(void);
extern void sub_0033935A_gen(void);
extern void sub_00339365_gen(void);
extern void sub_00339374_gen(void);
extern void sub_0033937F_gen(void);
extern void sub_0033938A_gen(void);
extern void sub_00339395_gen(void);
extern void sub_003393A0_gen(void);
extern void sub_003393AB_gen(void);
extern void sub_003393D4_gen(void);
extern void sub_003393EE_gen(void);
extern void sub_003393FD_gen(void);
extern void sub_00339417_gen(void);
extern void sub_003393DF_gen(void);
extern void sub_00339408_gen(void);
extern void sub_003393BA_gen(void);
extern void sub_003393C9_gen(void);
extern void sub_00339426_gen(void);
extern void sub_0031AF83_gen(void);
extern void sub_0031B080_gen(void);
extern void sub_0031AA96_gen(void);
extern void sub_0031AAA1_gen(void);
extern void sub_0031AA80_gen(void);
extern void sub_0031AA8B_gen(void);
extern void sub_0031AAB0_gen(void);
extern void sub_0031AABB_gen(void);
extern void sub_0031AAC6_gen(void);
extern void sub_0031AB0B_gen(void);
extern void sub_0031AAD1_gen(void);
extern void sub_0031AADC_gen(void);
extern void sub_0031AAE7_gen(void);
extern void sub_0031AB16_gen(void);
extern void sub_0031AB43_gen(void);
extern void sub_0031AB4E_gen(void);
extern void sub_0031AB59_gen(void);
extern void sub_0031AB64_gen(void);
extern void sub_0031AB6F_gen(void);
extern void sub_0031AB7A_gen(void);
extern void sub_0031AB85_gen(void);
extern void sub_0031AB90_gen(void);
extern void sub_0031ABB1_gen(void);
extern void sub_0031ABD2_gen(void);
extern void sub_0031ABDD_gen(void);
extern void sub_0031ABE8_gen(void);
extern void sub_0031ABF3_gen(void);
extern void sub_0031ABFE_gen(void);
extern void sub_0031AC0D_gen(void);

void sub_0020F9AE(void) { bo3_online_stub(0x0020F9AEu, sub_0020F9AE_gen); }
void sub_00339722(void) { bo3_online_stub(0x00339722u, sub_00339722_gen); }
void sub_00339739(void) { bo3_online_stub(0x00339739u, sub_00339739_gen); }
void sub_00339827(void) { bo3_online_stub(0x00339827u, sub_00339827_gen); }
void sub_0033932E(void) { bo3_online_stub(0x0033932Eu, sub_0033932E_gen); }
void sub_003392A9(void) { bo3_online_stub(0x003392A9u, sub_003392A9_gen); }
void sub_003392B4(void) { bo3_online_stub(0x003392B4u, sub_003392B4_gen); }
void sub_003392BF(void) { bo3_online_stub(0x003392BFu, sub_003392BF_gen); }
void sub_003392D6(void) { bo3_online_stub(0x003392D6u, sub_003392D6_gen); }
void sub_003392ED(void) { bo3_online_stub(0x003392EDu, sub_003392ED_gen); }
void sub_003392F8(void) { bo3_online_stub(0x003392F8u, sub_003392F8_gen); }
void sub_00339307(void) { bo3_online_stub(0x00339307u, sub_00339307_gen); }
void sub_00339511(void) { bo3_online_stub(0x00339511u, sub_00339511_gen); }
void sub_00339323(void) { bo3_online_stub(0x00339323u, sub_00339323_gen); }
void sub_003394FB(void) { bo3_online_stub(0x003394FBu, sub_003394FB_gen); }
void sub_00339506(void) { bo3_online_stub(0x00339506u, sub_00339506_gen); }
void sub_00339339(void) { bo3_online_stub(0x00339339u, sub_00339339_gen); }
void sub_00339344(void) { bo3_online_stub(0x00339344u, sub_00339344_gen); }
void sub_0033934F(void) { bo3_online_stub(0x0033934Fu, sub_0033934F_gen); }
void sub_0033935A(void) { bo3_online_stub(0x0033935Au, sub_0033935A_gen); }
void sub_00339365(void) { bo3_online_stub(0x00339365u, sub_00339365_gen); }
void sub_00339374(void) { bo3_online_stub(0x00339374u, sub_00339374_gen); }
void sub_0033937F(void) { bo3_online_stub(0x0033937Fu, sub_0033937F_gen); }
void sub_0033938A(void) { bo3_online_stub(0x0033938Au, sub_0033938A_gen); }
void sub_00339395(void) { bo3_online_stub(0x00339395u, sub_00339395_gen); }
void sub_003393A0(void) { bo3_online_stub(0x003393A0u, sub_003393A0_gen); }
void sub_003393AB(void) { bo3_online_stub(0x003393ABu, sub_003393AB_gen); }
void sub_003393D4(void) { bo3_online_stub(0x003393D4u, sub_003393D4_gen); }
void sub_003393EE(void) { bo3_online_stub(0x003393EEu, sub_003393EE_gen); }
void sub_003393FD(void) { bo3_online_stub(0x003393FDu, sub_003393FD_gen); }
void sub_00339417(void) { bo3_online_stub(0x00339417u, sub_00339417_gen); }
void sub_003393DF(void) { bo3_online_stub(0x003393DFu, sub_003393DF_gen); }
void sub_00339408(void) { bo3_online_stub(0x00339408u, sub_00339408_gen); }
void sub_003393BA(void) { bo3_online_stub(0x003393BAu, sub_003393BA_gen); }
void sub_003393C9(void) { bo3_online_stub(0x003393C9u, sub_003393C9_gen); }
void sub_00339426(void) { bo3_online_stub(0x00339426u, sub_00339426_gen); }
void sub_0031AF83(void) { bo3_online_stub(0x0031AF83u, sub_0031AF83_gen); }
void sub_0031B080(void) { bo3_online_stub(0x0031B080u, sub_0031B080_gen); }
void sub_0031AA96(void) { bo3_online_stub(0x0031AA96u, sub_0031AA96_gen); }
void sub_0031AAA1(void) { bo3_online_stub(0x0031AAA1u, sub_0031AAA1_gen); }
void sub_0031AA80(void) { bo3_online_stub(0x0031AA80u, sub_0031AA80_gen); }
void sub_0031AA8B(void) { bo3_online_stub(0x0031AA8Bu, sub_0031AA8B_gen); }
void sub_0031AAB0(void) { bo3_online_stub(0x0031AAB0u, sub_0031AAB0_gen); }
void sub_0031AABB(void) { bo3_online_stub(0x0031AABBu, sub_0031AABB_gen); }
void sub_0031AAC6(void) { bo3_online_stub(0x0031AAC6u, sub_0031AAC6_gen); }
void sub_0031AB0B(void) { bo3_online_stub(0x0031AB0Bu, sub_0031AB0B_gen); }
void sub_0031AAD1(void) { bo3_online_stub(0x0031AAD1u, sub_0031AAD1_gen); }
void sub_0031AADC(void) { bo3_online_stub(0x0031AADCu, sub_0031AADC_gen); }
void sub_0031AAE7(void) { bo3_online_stub(0x0031AAE7u, sub_0031AAE7_gen); }
void sub_0031AB16(void) { bo3_online_stub(0x0031AB16u, sub_0031AB16_gen); }
void sub_0031AB43(void) { bo3_online_stub(0x0031AB43u, sub_0031AB43_gen); }
void sub_0031AB4E(void) { bo3_online_stub(0x0031AB4Eu, sub_0031AB4E_gen); }
void sub_0031AB59(void) { bo3_online_stub(0x0031AB59u, sub_0031AB59_gen); }
void sub_0031AB64(void) { bo3_online_stub(0x0031AB64u, sub_0031AB64_gen); }
void sub_0031AB6F(void) { bo3_online_stub(0x0031AB6Fu, sub_0031AB6F_gen); }
void sub_0031AB7A(void) { bo3_online_stub(0x0031AB7Au, sub_0031AB7A_gen); }
void sub_0031AB85(void) { bo3_online_stub(0x0031AB85u, sub_0031AB85_gen); }
void sub_0031AB90(void) { bo3_online_stub(0x0031AB90u, sub_0031AB90_gen); }
void sub_0031ABB1(void) { bo3_online_stub(0x0031ABB1u, sub_0031ABB1_gen); }
void sub_0031ABD2(void) { bo3_online_stub(0x0031ABD2u, sub_0031ABD2_gen); }
void sub_0031ABDD(void) { bo3_online_stub(0x0031ABDDu, sub_0031ABDD_gen); }
void sub_0031ABE8(void) { bo3_online_stub(0x0031ABE8u, sub_0031ABE8_gen); }
void sub_0031ABF3(void) { bo3_online_stub(0x0031ABF3u, sub_0031ABF3_gen); }
void sub_0031ABFE(void) { bo3_online_stub(0x0031ABFEu, sub_0031ABFE_gen); }
void sub_0031AC0D(void) { bo3_online_stub(0x0031AC0Du, sub_0031AC0D_gen); }


extern void sub_000FD660_gen(void);
/* The online lobby's start: its screen object (0xA05B0) and the leftover
 * start-lock check (0xFD660 from 0x9F121); see online.c. */
extern void sub_000A05B0_gen(void);
void sub_000A05B0(void) { bo3_online_lobby_screen(sub_000A05B0_gen); }
void sub_000FD660(void) { bo3_online_fd660(sub_000FD660_gen); }

/* A sound bank that did not load (BO-218, ../soundbank.c): the wave count
 * of a dictionary that is no list is 0, not an endless walk. */
#include "../soundbank.h"

extern void sub_001F65A0_gen(void);

void sub_001F65A0(void) { bo3_wave_dict_count(sub_001F65A0_gen); }

/* A traffic car out of the collision broadphase (BO-346, ../traffic_bp.c):
 * by its own slot, or not at all when it has none, never by a handle that
 * is not its slot (the axis lists' search has no end). */
#include "../traffic_bp.h"

extern void sub_00111680_gen(void);

void sub_00111680(void) { bo3_traffic_bp_remove(sub_00111680_gen); }

/* Pictures between the simulation's steps (../hirefresh.c):
 * the frame's steps, RenderWare's frame sync, the vehicles' draw, the
 * HUD's time, the players' cameras, the sparks' clock, the race audio and
 * the audio world, the effects clock, the car's reflection cube and the swap's
 * count. As shipped unless graphics.high_refresh is in force (the swap is only
 * counted). */
#include "../hirefresh.h"

extern void sub_000165F0_gen(void);
extern void sub_001E15E0_gen(void);
extern void sub_001DD280_gen(void);
extern void sub_000303D0_gen(void);
extern void sub_000535E0_gen(void);
extern void sub_00350DB0_gen(void);
extern void sub_00167940_gen(void);
extern void sub_00038200_gen(void);
extern void sub_000367C0_gen(void);
extern void sub_0013EF40_gen(void);
extern void sub_001CA530_gen(void);
extern void sub_00188750_gen(void);
extern void sub_0003FA20_gen(void);

void sub_000165F0(void) { bo3_update(sub_000165F0_gen); }
void sub_001E15E0(void) { bo3_frame_sync_dirty(sub_001E15E0_gen); }
void sub_001DD280(void) { bo3_frame_get_ltm(sub_001DD280_gen); }
void sub_000303D0(void) { bo3_draw_vehicle(sub_000303D0_gen); }
void sub_000535E0(void) { bo3_hud_update(sub_000535E0_gen); }
void sub_00350DB0(void) { bo3_swap(sub_00350DB0_gen); }
void sub_00167940(void) { bo3_player_frame(sub_00167940_gen); }
void sub_00038200(void) { bo3_spark_history(sub_00038200_gen); }
void sub_000367C0(void) { bo3_spark_draw(sub_000367C0_gen); }
void sub_0013EF40(void) { bo3_race_audio(sub_0013EF40_gen); }
void sub_001CA530(void) { bo3_audio_world(sub_001CA530_gen); }
void sub_00188750(void) { bo3_fx_clock(sub_00188750_gen); }
void sub_0003FA20(void) { bo3_reflection_cube(sub_0003FA20_gen); }

/* The front end's picture callbacks (BO-315, display.pictures in
 * ../main.c): the loading screen's and Crash Nav's pictures (0x1C1A60, which
 * draws most of the front end's sprites) and the films behind the menus
 * (0x1C59A0). As shipped unless the setting keeps the pictures 4:3. */
extern void sub_001C1A60_gen(void);
extern void sub_001C59A0_gen(void);
extern void bo3_fe_picture_draw(void (*fn)(void), uint32_t va);

void sub_001C1A60(void) { bo3_fe_picture_draw(sub_001C1A60_gen, 0x001C1A60u); }
void sub_001C59A0(void) { bo3_fe_picture_draw(sub_001C59A0_gen, 0x001C59A0u); }

/* QUICK PLAY (src/game/quickplay.c, Takedawn Online step 3): the create
 * state's name page and mode page builders. The generated bodies are the _gen
 * names; they are all that runs unless QUICK PLAY's create branch is on. */
#include "../quickplay.h"
#include "../vote.h"
extern void sub_00098050_gen(void);
extern void sub_000995F0_gen(void);
void sub_00098050(void) { bo3_quickplay_name_page(sub_00098050_gen); }   /* NTSC-U sub_00098050 */
void sub_000995F0(void) { bo3_quickplay_mode_page(sub_000995F0_gen); }   /* NTSC-U sub_000995F0 */
extern void sub_000CBE40_gen(void);
extern void sub_000CCF00_gen(void);
extern void sub_000A0BF0_gen(void);
void sub_000CBE40(void) { bo3_vote_box_open(sub_000CBE40_gen); }   /* NTSC-U sub_000CBE40 */
void sub_000CCF00(void) { bo3_vote_row(sub_000CCF00_gen); }   /* NTSC-U sub_000CCF00 */
void sub_000A0BF0(void) { bo3_vote_input(sub_000A0BF0_gen); }   /* NTSC-U sub_000A0BF0 */
extern void sub_000A1560_gen(void);
void sub_000A1560(void) { bo3_vote_start_check(sub_000A1560_gen); }   /* NTSC-U sub_000A1560 */

/* After a raced online game (src/game/stay.c, Takedawn Online step 4): the
 * room state's handler (its event 3 goes back to the game's lobby instead of
 * the room page) and the Quick Match page's per-record callback (a guest
 * looks for its host's game only). The generated bodies run otherwise. */
#include "../stay.h"
extern void sub_0006EEE0_gen(void);
extern void sub_0006DE50_gen(void);
extern void sub_000A9A90_gen(void);
void sub_0006EEE0(void) { bo3_stay_room_state(sub_0006EEE0_gen); }     /* NTSC-U sub_0006EEE0 */
void sub_0006DE50(void) { bo3_stay_car_page(sub_0006DE50_gen); }       /* NTSC-U sub_0006DE50 */
extern void sub_000C9240_gen(void);
extern void sub_000A4900_gen(void);
void sub_000C9240(void) { bo3_stay_heading(sub_000C9240_gen); }
void sub_000A4900(void) { bo3_stay_lobby_select(sub_000A4900_gen); }
extern void sub_000CAD00_gen(void);
void sub_000CAD00(void) { bo3_stay_line_text(sub_000CAD00_gen); }
extern void sub_000CA090_gen(void);
void sub_000CA090(void) { bo3_stay_line_first(sub_000CA090_gen); }
void sub_000A9A90(void) { bo3_quickplay_record(sub_000A9A90_gen); }    /* NTSC-U sub_000A9A90 */
#else
#include "../soundtrack.h"
#endif /* NTSC-U */

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    /* The methods of the custom-music decoder object (no title code is up
     * here: the kernel's thunks are the only other calls above 0xFE000000). */
    if (xbox_va >= BO3_SOUNDTRACK_VA_BASE)
        return bo3_soundtrack_lookup(xbox_va);
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}
