/*
 * controls.c - input.control_scheme: the PS2 version's driving controls,
 * only while driving (BO-106; docs/controls.md, controls.h).
 *
 * Where: the title reads its pads in one place, sub_001B57E0 (the pad
 * manager's poll, once a frame from the main loop's sub_00021A50): four
 * records of 0x20 at 0x4AE20C, { state, handle, XINPUT_STATE }, and its only
 * XInputGetState call (sub_00363C86) fills the record's XINPUT_STATE. The
 * "ctl_pad" mark (config/patches.json) is the instruction after that call,
 * so this code runs on the title's own thread between the read and the
 * title's first use of it (the device update, vtable 0x3A9B04 slot 1, which
 * turns the bytes into the 24 channels every menu and every car reads).
 * What is rewritten here is therefore what the whole title sees for that
 * frame, and whether the title is driving is read from its memory at the
 * same instant: the layout changes on the frame the title's state does, not
 * a report later.
 *
 * Which pads: a controller's state only. A pad a script drives
 * (RECOMP_PAD_SCRIPT / RECOMP_PAD_LIVE) keeps the Xbox layout, so "rt" in a
 * test or in the shader pre-build's race still accelerates whatever the
 * player's setting is; and while a keyboard key is held the keys keep their
 * documented Xbox buttons. BO3_CTL_SCRIPTED=1 lifts the first rule: a
 * script then stands in for a controller (tools/headless/controls_scene.sh),
 * face layout included: its buttons are then positions on the pad.
 */
#include "region.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "controls.h"
#include "input_map.h"
#include "keyboard.h"
#include "touch_pad.h"                 /* the on-screen controls (BO-189) */
#include "react.h"                    /* the quick reactions (BO-352) */
#include "onlinenav.h"                 /* the player's online navigator (BO-352) */
#include "onlinebrand.h"               /* replace the loaded service names before menus build */
#include "headfont.h"                  /* the heading font's W, the TAKEDAWN ONLINE heading */
#include "xboxlive.h"                  /* the Xbox Live menu's code picker (BO-352) */
#include "platform/recomp_env.h"
#include "platform/recomp_patch.h"
#include "recomp_types.h"
#include "race_module.h"

extern RECOMP_TLS uint32_t g_ebx, g_esi;
extern ptrdiff_t g_xbox_mem_offset;

#define M8(va)  (*(uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define M32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

/* The title's state (docs/controls.md has the evidence for each). */
#define CTL_PADS        BO3_VA(0x004AE20Cu, 0x004AE52Cu, 0x004AE5CCu)     /* the pad manager's four records */
#define CTL_MODULE      BO3_VA(0x004D5370u, 0x004D5690u, 0x004D5730u)     /* the current module ([app + 0x2E1D0], app 0x4A71A0) */
#define CTL_PAUSED_BY   BO3_VA(0x004D537Cu, 0x004D569Cu, 0x004D573Cu)     /* the player who paused, -1 while not paused */
#define CTL_MODULE_STEP BO3_VA(0x004D53B4u, 0x004D56D4u, 0x004D5774u)     /* the module switch: 4 and 5 coming up, 6 leaving */
#define CTL_P1          BO3_VA(0x004D0738u, 0x004D0A58u, 0x004D0AF8u)     /* player 1's race record; player 2's is CTL_P_SIZE on */
#define CTL_P_SIZE      0x0C50u
#define CTL_P_RESULTS   0x00u           /* byte: the results camera has reached its end; stale when that camera is not running */
#define CTL_P_STATE     0x08u           /* the camera: 4 chase, 0x40 crash, 0x400 fly-by, 0x2000 results */
#define CTL_P_INTRO     0x2Fu           /* byte: a crash event's fly-by or captions are up */
#define CTL_PHASE       BO3_VA(0x0073B51Cu, 0x0073B82Cu, 0x0073B8CCu)     /* player 1's event: 1 before the GO, 2 in play, 3 finished */
#define CTL_E_SIZE      0x27E0u         /* the event records of player 1 and 2 are this far apart */
#define CTL_COUNTDOWN   BO3_VA(0x0073B520u, 0x0073B830u, 0x0073B8D0u)     /* float: 0, then -1, until the countdown begins; then its length */
#define CTL_NO_PAUSE    BO3_VA(0x00752D33u, 0x00753043u, 0x007530E3u)     /* byte: the title takes no pause now */
#define CTL_ONLINE_MENU BO3_VA(0x004AED88u, 0x004AF0A8u, 0x004AF148u)     /* byte: an online race's pause menu is up */

static bo3_ctl_port s_port[4];
static int s_scripted_too;              /* BO3_CTL_SCRIPTED */
static FILE *s_log;                     /* BO3_CTL_LOG */
static uint32_t (*s_flips)(void);

/* The title is taking driving input now, for this player (bo3_ctl_driving
 * has the rule). */
static int ctl_driving_for(unsigned player)
{
    uint32_t mod = M32(CTL_MODULE), rec;
    bo3_ctl_title t;

    if (mod < 0x10000u || mod >= 0x04000000u || !bo3_race_module_vt(M32(mod)))
        return 0;
    /* One pad at a time plays the one-player stage (and Party Crash, which
     * passes it on): the first player's records whichever port it is on. Only
     * the two-player stage has a second player with a pad of their own. */
    if (M32(mod) != BO3_VT_GAME_2P)
        player = 0;
    rec = CTL_P1 + player * CTL_P_SIZE;
    /* Quitting from the pause menu clears the paused player a second before
     * the module changes: the module is on its way out then. */
    t.paused_by = M32(CTL_PAUSED_BY);
    t.module_step = M32(CTL_MODULE_STEP);
    t.no_pause = M8(CTL_NO_PAUSE);
    t.online_menu = M8(CTL_ONLINE_MENU);
    t.intro = M8(rec + CTL_P_INTRO);
    t.results = M8(rec + CTL_P_RESULTS);
    t.state = M32(rec + CTL_P_STATE);
    t.phase = M32(CTL_PHASE + player * CTL_E_SIZE);
    t.countdown = M32(CTL_COUNTDOWN + player * CTL_E_SIZE);
    return bo3_ctl_driving(&t);
}

/* Player 1's event: what the menus, the touch controls and Esc go by. */
static int ctl_driving(void)
{
    return ctl_driving_for(0);
}

/* BO3_CTL_LOG=<file>: what the gate saw and did, for the scene test
 * (tools/headless/controls_scene.sh) and for finding the title's state. "S"
 * a watched word changed, "G" a pad's layout changed, "P" a pad's bytes as
 * read and as given (when either changed). Off unless set. */
static void ctl_log_frame(void)
{
    static const struct { uint32_t va; const char *name; } w[] = {
        { CTL_MODULE, "module" }, { BO3_VA(0x004D536Cu, 0x004D568Cu, 0x004D572Cu), "time_stopped" }, { CTL_PAUSED_BY, "paused_by" },
        { BO3_VA(0x004D538Cu, 0x004D56ACu, 0x004D574Cu), "4D538C" }, { BO3_VA(0x004D5390u, 0x004D56B0u, 0x004D5750u), "4D5390" }, { CTL_MODULE_STEP, "module_step" },
        { CTL_P1 + CTL_P_RESULTS, "p1_results" }, { CTL_P1 + CTL_P_STATE, "p1_state" },
        { CTL_P1 + 0x2Cu, "p1_intro_word" }, { CTL_PHASE, "p1_phase" },
        { CTL_COUNTDOWN, "p1_countdown" }, { BO3_VA(0x00752D30u, 0x00753040u, 0x007530E0u), "752D30" },
        { CTL_PHASE + CTL_E_SIZE, "p2_phase" }, { CTL_COUNTDOWN + CTL_E_SIZE, "p2_countdown" },
        { CTL_P1 + CTL_P_SIZE + CTL_P_RESULTS, "p2_results" },
        { CTL_P1 + CTL_P_SIZE + 0x2Cu, "p2_intro_word" },
        { CTL_ONLINE_MENU, "online_menu" },
    };
    enum { N = sizeof w / sizeof w[0] };
    static uint32_t last[N + 1];
    static int primed, drv = -1, drv2 = -1;
    uint32_t flip = s_flips ? s_flips() : 0, mod = M32(CTL_MODULE), v;
    int i, d;

    for (i = 0; i <= N; i++) {
        if (i < N)
            v = M32(w[i].va);
        else
            v = (mod >= 0x10000u && mod < 0x04000000u) ? M32(mod) : 0;
        if (primed && v == last[i])
            continue;
        fprintf(s_log, "S %u %s %08X -> %08X\n", flip, i < N ? w[i].name : "module_vt", last[i], v);
        last[i] = v;
    }
    primed = 1;
    d = ctl_driving();
    if (d != drv)
        fprintf(s_log, "D %u driving %d\n", flip, d);
    drv = d;
    /* Player 2's gate, which the pad of the title's port 3 goes by (the same as
     * player 1's outside the two-player stage). */
    d = ctl_driving_for(1);
    if (d != drv2)
        fprintf(s_log, "D2 %u driving %d\n", flip, d);
    drv2 = d;
}

static void ctl_pad(void)
{
    uint32_t port = g_ebx - 1u, rec = g_esi - 4u;
    XBOX_GAMEPAD *g, raw;
    int scheme, driving, ps2, usb, scripted, nintendo;
    unsigned src;

    if (!g_xbox_mem_offset || port > 3u || rec != CTL_PADS + port * 0x20u)
        return;
    if (port == 0) {
        bo3_onlinebrand_apply();
        bo3_headfont_apply();
    }
    if (s_log && port == 0)
        ctl_log_frame();
    if (M32(rec) != 2 || !M32(rec + 4))         /* no pad open on this port */
        return;
    g = (XBOX_GAMEPAD *)((uintptr_t)g_xbox_mem_offset + rec + 0xC);
    /* Esc while driving is Start (bo3_ctl_esc). Keys are the host port 0's,
     * the title's port 2, and only when this read carries a Back: with the
     * overlay open the state is neutral, and its Esc must not pause. */
    usb = (int)port - 2;
    if (usb == 0) {
        int esc = (xbox_InputSources(0) & XBOX_INPUT_SRC_KEYS)
               && xbox_InputKeyActionHeld(XBOX_KEYACT_BACK)     /* Esc, or the key it was moved to */
               && (g->wButtons & XBOX_GAMEPAD_BACK);
        int was = s_port[port].esc_start;

        bo3_ctl_esc(&s_port[port], g, esc, esc ? ctl_driving() : 0);
        if (s_log && s_port[port].esc_start != was)
            fprintf(s_log, "E %u Esc %s\n", s_flips ? s_flips() : 0,
                    s_port[port].esc_start ? "is Start" : "released");
    }
    /* BO-352: a quick reaction on the menu pad (the title's port 2) */
    if (usb == 0)
        bo3_react_pad(g);
    scheme = xbox_InputControlScheme();
    /* BO-352: the online navigator writes its press into the menu player's
     * pad (the title's port 2) and clears every other button except B, so
     * the player's own presses do not mix in and B still cancels. Nothing
     * unless a navigator is running. */
    bo3_onlinenav_pad(port, g);
    /* BO-352: the Xbox Live code picker's left/right (xboxlive.c). Nothing
     * unless the picker is up. */
    bo3_xboxlive_pad(port, g);
    if (!scheme && !s_port[port].ps2 && !s_port[port].latch && !s_log && !s_scripted_too)
        return;                                 /* the Xbox layout: nothing to do */

    /* The emulated pads sit on the title's last two ports: the title's port
     * 2 is the first host pad. */
    src = usb >= 0 ? xbox_InputSources((DWORD)usb) : 0;
    /* The pad of the title's port 2 is player 1's, port 3 player 2's (split
     * screen): each goes by its own player's event. */
    driving = ctl_driving_for(usb >= 1 ? 1u : 0u);
    scripted = usb >= 0 && (xbox_InputScriptedPads() & (1u << usb));
    nintendo = xbox_InputFaceLayoutNintendo();
    /* The keys and the on-screen controls are the Xbox layout always. */
    ps2 = scheme == 1 && driving
       && !(src & (XBOX_INPUT_SRC_KEYS | XBOX_INPUT_SRC_TOUCH))
       && (s_scripted_too || !scripted);
    /* A script standing in for a controller (BO3_CTL_SCRIPTED) has to be one
     * all the way: the face layout is applied as a host pad is read, which a
     * script's buttons never pass, so it is applied to them here, in menus
     * and driving alike. The script's "a" is then the bottom button of the
     * pad, as SDL names it on any pad, and what follows sees what it would
     * see of a controller. Without this the PS2 layout, which reads the
     * positions back out of the mirrored state, took a script's "a" for the
     * right-hand button (BO-106, found on the RP5). */
    if (s_scripted_too && scripted && nintendo)
        bo3_ctl_mirror(g);
    raw = *g;
    if (s_log && ps2 != s_port[port].ps2)
        fprintf(s_log, "G %u port %u %s (scheme %d driving %d sources %u)\n",
                s_flips ? s_flips() : 0, port, ps2 ? "ps2" : "xbox", scheme, driving, src);
    bo3_ctl_apply(&s_port[port], g, ps2, nintendo);
    if (s_log) {
        static XBOX_GAMEPAD lraw[4], lout[4];
        if (memcmp(&raw, &lraw[port], sizeof raw) || memcmp(g, &lout[port], sizeof raw)) {
            const uint8_t *a = raw.bAnalogButtons, *o = g->bAnalogButtons;
            fprintf(s_log, "P %u port %u %s%s read %04X a%u b%u x%u y%u bk%u wh%u lt%u rt%u ry%d"
                           " -> a%u b%u x%u y%u bk%u wh%u lt%u rt%u ry%d\n",
                    s_flips ? s_flips() : 0, port, ps2 ? "ps2" : "xbox",
                    s_port[port].latch ? " latched" : "", raw.wButtons,
                    a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], raw.sThumbRY,
                    o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], g->sThumbRY);
            lraw[port] = raw;
            lout[port] = *g;
        }
        fflush(s_log);
    }
}

/* The on-screen controls' layout (touch_pad.h, BO-189): the race one while
 * the title takes driving input, the menu one otherwise. Asked on the
 * window's thread each frame; the words it reads are the title's, read
 * only. */
static int touch_driving(void)
{
    return g_xbox_mem_offset ? ctl_driving() : 0;
}

void bo3_controls_start(uint32_t (*flips)(void))
{
    const char *v = recomp_env(RENV_INPUT_CONTROL_SCHEME), *log = getenv("BO3_CTL_LOG");
    const char *sc = getenv("BO3_CTL_SCRIPTED");

    s_flips = flips;
    /* The setting, where the overlay (which also sets it, and at once when
     * it is changed there) is not built. */
    if (v && !strcmp(v, "ps2"))
        xbox_InputSetControlScheme(1);
    xbox_InputOfferControlScheme(1);
    s_scripted_too = sc && *sc && *sc != '0';
    if (log && *log) {
        s_log = fopen(log, "w");
        if (!s_log)
            fprintf(stderr, "BO3_CTL_LOG: cannot write %s\n", log);
    }
    recomp_patch_on_mark("ctl_pad", ctl_pad);
    xbox_InputTouchSetDrivingHook(touch_driving);
    if (xbox_InputControlScheme() == 1)
        fprintf(stderr, "[INPUT] driving controls: PS2 layout (input.control_scheme = ps2); "
                        "menus keep the Xbox layout\n");
}
