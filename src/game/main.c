/**
 * Burnout 3: Takedown (Xbox) - Recompiled Game Entry Point
 *
 * Hosts the recompiled default.xbe on the shared xboxrecomp runtime
 * (../xboxrecomp), the same way hl2/src/game/main.c does. Nothing here draws,
 * drives a menu or decides game state: the title's own boot sequence runs from
 * its real entry point, and anything the host does is limited to bringing up
 * the hardware the XBE expects to find.
 *
 *   1. load the XBE                     the recompiled code reads its own
 *                                        .rdata/.data out of the mapped image
 *   2. map the Xbox memory layout       at the original virtual addresses
 *   3. bring up the MCPX APU            DirectSound spins on the audio DSP
 *   4. kernel replacement + thunk table
 *   5. set the guest stack pointer
 *   6. jump to the recompiled entry point
 *
 * Usage: burnout3 [--headless] [--record out.mp4] [--seconds N] [default.xbe]
 *
 * A window by default. --headless opens none; --record dumps every presented
 * frame and hands the sequence to ffmpeg when the run ends (after --seconds,
 * or on Ctrl+C). RECOMP_FB_DUMP=<prefix> writes frames to BMPs directly.
 * See docs/running.md.
 *
 * XBE details (retail, XDK 5849):
 *   Base addr:   0x00010000
 *   Entry point: 0x001D2807
 *   Kernel imports: 147
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE   /* dladdr's Dl_info, the ucontext REG_* names */
#endif
#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <direct.h>   /* _getcwd */
#if defined(XBOXRECOMP_WIN_VULKAN)
#include "video/video_player.h"   /* xbox_HostWindowMain, xbox_HostWindowSetTitle */
#endif
#else
/* <signal.h> already brings ucontext_t and the Darwin mcontext; <ucontext.h>
 * itself is an #error on macOS without _XOPEN_SOURCE. BOOL and friends come
 * from the toolkit's kernel.h. */
#include <signal.h>
#if defined(__linux__)
#include <ucontext.h>
#endif
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include "video/video_player.h"   /* xbox_HostWindowMain, xbox_HostWindowSetTitle */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <stdarg.h>

/* Every pad motor off, from a crash handler (toolkit, src/input/input_core.c). */
extern void xbox_InputPanicStop(int wait_ms);

#include "recomp_icall_feedback.h"
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "usb/ohci.h"
#include "controls.h"                 /* input.control_scheme (BO-106) */
#include "install.h"                  /* the data root, --install, --version (BO-103) */
#include "fatal.h"                    /* a start-up failure the player sees */
#include "glyphs.h"                   /* the button prompts for the pad in use (BO-107) */
#include "soundtrack.h"               /* custom music (BO-108) */
#include "mixer.h"                    /* the host mixer (BO-114) */
#include "dsound_notify.h"            /* the DirectSound done-list guard (BO-173) */
#include "gp_image.h"                 /* the GP effects image (BO-119) */
#include "nv2a_texrep.h"              /* the LED sign decals' depth offset (BO-176) */
#include "nv2a_texup.h"               /* graphics.texture_upscale's road skip list (BO-245) */
#if defined(__has_include)
#  if __has_include("platform/recomp_restart.h")
#    include "platform/recomp_restart.h"    /* the overlay's "Restart game" (BO-124) */
#    define BO3_HAVE_RESTART 1
#  endif
#endif
#if defined(__has_include)
#  if __has_include("platform/recomp_utf8_args.h")
#    include "platform/recomp_utf8_args.h"  /* Windows: the arguments as UTF-8 (BO-103) */
#    define BO3_HAVE_UTF8_ARGS 1
#  endif
#endif
#ifdef XBOXRECOMP_HAVE_OVERLAY         /* the toolkit's in-game settings overlay */
#include "xbox_settings.h"
#endif
#ifdef RECOMP_ENV_HAVE_ENHANCE_KEYS   /* the toolkit's opt-in enhancements layer */
#include "enhance.h"
#include "enhance_cfg.h"
#include "platform/recomp_exe_dir.h"
#endif
/* The shader pre-build (BO-93): on the backends that keep a key list, Metal
 * and Vulkan, and so not in the old Direct3D 11 Windows host. */
#if defined(RECOMP_ENV_HAVE_ENHANCE_KEYS) && (defined(__APPLE__) || defined(XBOXRECOMP_HAVE_VULKAN)) \
    && (!defined(_WIN32) || defined(XBOXRECOMP_WIN_VULKAN)) && !defined(__ANDROID__)
#define BO3_HAVE_SHADERBUILD 1
#include "shaderbuild.h"
#endif

/* RECOMP_TLS is required: the runtime defines these thread-local, and a plain
 * extern resolves to the image's TLS template rather than this thread's copy,
 * which starts the guest with every register at zero. */
extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_seh_ebp;
extern RECOMP_TLS uint32_t g_ebx, g_esi, g_edi;
extern ptrdiff_t g_xbox_mem_offset;

extern void xbe_entry_point(void);
int recomp_dispatch_init(void);

#if defined(_WIN32)
#define BO3_XBE_DEFAULT  "Burnout 3 Takedown\\default.xbe"
#else
#define BO3_XBE_DEFAULT  "Burnout 3 Takedown/default.xbe"
#endif
#define BO3_GAME_DIR     "Burnout 3 Takedown"
#define BO3_SAVE_DIR     "saves"

/* The runtime this links against. The apexJCL fork (the macOS/Metal one)
 * has recomp_env.h; upstream xboxrecomp does not. Two defaults differ:
 *
 * - The DirectSound doorbell (docs/boot.md #5) is a contiguous allocation,
 *   so its address moves with the allocator. The fork finds it from GPSADDR
 *   (RECOMP_APU_DSP_ACK=auto, apexJCL/xboxrecomp f81723d); on its allocator
 *   it is not at 0x825F8810, and that fixed address leaves the boot spinning
 *   in sub_002F7EC2.
 *
 * - The fork gives title-built KEVENTs a real host event, so D3D's wait for
 *   the vblank event at device+0x1DBC (sub_00350EC0, the display-mode set)
 *   blocks until a vblank sets it; upstream fails that wait at once and the
 *   loop moves on (xboxrecomp #160). The vblank interrupt is opt-in in both,
 *   so on the fork it has to be asked for, as blinx2-recomp does. */
#if defined(__has_include)
#if __has_include("platform/recomp_env.h")
#define BO3_FORK_RUNTIME 1
#endif
/* Crash and hang reports in the save folder, and the per-user save folder
 * itself (the toolkit's xbox_crash.h and platform/recomp_save_dir.h). */
#if __has_include("xbox_crash.h") && __has_include("platform/recomp_save_dir.h")
#define BO3_HAVE_REPORTS 1
#include "xbox_crash.h"
#include "platform/recomp_env.h"
#include "platform/recomp_patch.h"
#include "platform/recomp_save_dir.h"
#include "platform/recomp_instance.h"     /* one copy per save root (BO-129) */
#include "kernel_saveguard.h"             /* saves replaced whole (BO-129) */
#include "kernel_save_import.h"           /* --import-save: an original Xbox save (BO-209) */
#if !defined(_WIN32) || defined(XBOXRECOMP_WIN_VULKAN)
#define SDL_MAIN_HANDLED                  /* our own main(): no SDL_main rename, no SDL2main */
#include <SDL.h>                          /* the "already running" box */
#endif
static void save_guard_init(void);
#endif
#endif

/* The bug report archive (BO-99; the toolkit's xbox_report.h): the overlay's
 * "Create bug report", --bug-report on the command line, the Android start
 * page. */
#if defined(BO3_HAVE_REPORTS) && defined(__has_include)
#if __has_include("xbox_report.h")
#define BO3_HAVE_BUG_REPORT 1
#include "xbox_report.h"
#if defined(BO3_HAVE_BUILD_ID)
#include "bo3_build_id.h"       /* BO3_BUILD_COMMIT, made at build time */
#endif
#endif
#endif
#ifndef BO3_BUILD_COMMIT
#define BO3_BUILD_COMMIT "unknown"
#endif

#if !defined(BO3_HAVE_REPORTS)
/* A runtime without them: the report goes to stderr only. */
#define xbox_crash_printf(...) fprintf(stderr, __VA_ARGS__)
static int xbox_crash_begin(const char *kind, const char *headline)
{
    (void)kind;
    XR_ERROR( "%s\n", headline);
    return 0;
}
static const char *xbox_crash_end(void) { return ""; }
static void xbox_crash_log_flush(unsigned ms) { (void)ms; }
#endif

/* A Steam Deck (BO-98: its rumble default). The machine, not the pad: the
 * defaults are set before any controller is open, and under Steam Input the
 * pad is Steam's virtual one, not "Steam Deck Controller" 28de:1205.
 *   - SteamDeck=1: Steam sets it for what it launches on a Deck, Gaming Mode
 *     and Desktop Mode, and Proton hands it to a Windows build.
 *   - Linux, started outside Steam: the DMI board, vendor "Valve" and
 *     product "Jupiter" (LCD) or "Galileo" (OLED). */
#if !defined(__ANDROID__)
static int host_is_steam_deck(void)
{
    const char *e = getenv("SteamDeck");

    if (e && !strcmp(e, "1"))
        return 1;
#if defined(__linux__)
    {
        char vendor[32] = "", product[32] = "";
        FILE *f = fopen("/sys/devices/virtual/dmi/id/sys_vendor", "r");
        if (f) {
            if (!fgets(vendor, sizeof vendor, f))
                vendor[0] = 0;
            fclose(f);
        }
        f = fopen("/sys/devices/virtual/dmi/id/product_name", "r");
        if (f) {
            if (!fgets(product, sizeof product, f))
                product[0] = 0;
            fclose(f);
        }
        if (!strncmp(vendor, "Valve", 5)
                && (!strncmp(product, "Jupiter", 7) || !strncmp(product, "Galileo", 7)))
            return 1;
    }
#endif
    return 0;
}
#endif

/* Under gamescope (BO-103): the Steam Deck's Game Mode, and the game session
 * of Bazzite and SteamOS on other handhelds and living-room PCs. The window
 * is the screen there and there may be no mouse or keyboard. */
#if !defined(__ANDROID__)
static int host_is_gamescope(void)
{
#if defined(__linux__)
    const char *e = getenv("GAMESCOPE_WAYLAND_DISPLAY");

    if (e && *e)
        return 1;
    e = getenv("XDG_CURRENT_DESKTOP");
    if (e && !strcmp(e, "gamescope"))
        return 1;
#endif
    return 0;
}
#endif

/* BO-176: the LED accident signs over the US tunnels (Silver Lake's
 * REDUCE / SPEED and the like) are a quad with the words, "Chgo_AccidentSign_
 * Words" or "_Words2", drawn about 4 cm in front of the sign's dark dot board
 * (9977adbbcd40a47d), depth test LEQUAL. The title draws with w-buffering
 * on (SET_CONTROL0 bit 16), where that lead is plenty; the host's z-buffer
 * takes the title's screen-space z, whose float32 noise at a distance is
 * about as large as the lead, so on some frames parts of the words or all of
 * them fell behind the board. Their draws take a constant depth offset
 * toward the eye: 512 units of the 24-bit range, a few hundred more than
 * that noise and, at 60 m, about 20 cm. The same two textures (same bytes)
 * are in US C3, M1 and P1; C1 has its own copies (BO-204, found by name in
 * the tracks' static.dat and checked in drive-bys).
 * BO-253: with culling off the toolkit applies the offset on the front faces
 * only; from behind, the words would otherwise come through the board and
 * read mirrored (GitHub #53). */
static void bo3_led_signs_init(void)
{
    static const struct nv2a_texrep_zbias k_signs[] = {
        { "Chgo_AccidentSign_Words",  0xb993baaf1f3b5af5ull, 0x0C, 128, 128, -512.0f },
        { "Chgo_AccidentSign_Words2", 0xdf22c82ff01643bcull, 0x0C, 128, 128, -512.0f },
        /* BO-204: the same signs on Waterfront (US C1_V1/_V2) are other bytes */
        { "Chgo_AccidentSign_Words (C1)",  0xf7857dd943dd87efull, 0x0C, 128, 128, -512.0f },
        { "Chgo_AccidentSign_Words2 (C1)", 0x789dc11b2c86ba3bull, 0x0C, 128, 128, -512.0f },
        /* BO-204b: the Chicago LED word atlases of US M1 and P2 (Chgo_words/
         * _words2; US C2 has its own copies, which do not flicker) and the
         * Riviera chevron markers of EU C3: drive-bys with the textures
         * tinted and the offsets toggled every flip */
        { "Chgo_words",  0x690d64f43e926f3bull, 0x0C, 128, 256, -512.0f },
        { "Chgo_words2", 0x71dfa2dae587dc99ull, 0x0C, 128, 256, -512.0f },
        { "R_signals1 (EU C3)",       0x3b17abe4902f4996ull, 0x0C, 128, 128, -512.0f },
        { "R_signals2 (EU C3)",       0x492b48f19fcad69aull, 0x0F, 128, 128, -512.0f },
        { "R_lighthouse01 (EU C3)",   0x4c63134fa968c814ull, 0x0C, 128, 128, -512.0f },
        { "R_Warning_Marker (EU C3)", 0x8004a89eada4b714ull, 0x0C, 128, 128, -512.0f },
    };
    nv2a_texrep_register_zbias(k_signs, sizeof k_signs / sizeof k_signs[0]);
}

/* BO-245: the road surfaces graphics.texture_upscale leaves as they are
 * while graphics.texup_roads is off (the default): the upscalers make their
 * fine grain noisy (FSRCNNX most). texrep hashes of level 0, picked by look
 * from the BO-240 dumps of six tracks (US C1, C2, C3, EU M1, P2, AS C1): the
 * grey asphalt noise sheets, the worn lanes with and without their markings,
 * the concrete carriageways. The tracks not dumped yet are phase 3's. */
static void bo3_texup_roads_init(void)
{
    static const uint64_t k_roads[] = {
        0x08fcb275cac943c1ull,
        0x127ecbbbc68d1663ull,
        0x18a810a0ff3e3e02ull,
        0x1e93b224b6f7221eull,
        0x2f16591fdebcf5f7ull,
        0x332adea031b67a31ull,
        0x451f38fe7d1e2b8cull,
        0x5e1eb6c68c085b14ull,
        0x639dbb75e9632325ull,
        0x7e9a4671936e5ca6ull,
        0x8719b45b0e994450ull,
        0xa58356aef15329b6ull,
        0xa9b429efebbad73bull,
        0xb58e3302c94354b6ull,
        0xccf50e366e7defbdull,
        0xdd0055c160c56c73ull,
        0xecc6cbfebb1a27ceull,
        0xf42c45d4ce360871ull,
    };
    nv2a_texup_set_skip_list(k_roads, sizeof k_roads / sizeof k_roads[0]);
}

/* BO-110: the title's light sprites drawn in a view's 3D, brighter with HDR
 * on (nv2a_texrep_register_hdr_gain; SDR never reads it). Found with
 * RECOMP_DEBUG=hdr,hdr_texlog in the pre-build's race scene (boost, a
 * crash): additive (SRC_ALPHA, ONE) scene draws. The gain is in linear
 * light, before the roll-off to the display's peak. */
static void bo3_hdr_gains_init(void)
{
    static const struct nv2a_texrep_hdr_gain k_light[] = {
        /* the boost's blue exhaust glow, and the orange exhaust flame strip */
        { "boost exhaust glow",  0x4a67ab55aa789ec2ull, 0x0C, 64, 64, 2.5f, NV2A_HDR_GAIN_MUL },
        { "exhaust flame",       0x3bda224676d2c0acull, 0x0F, 32, 8,  2.5f, NV2A_HDR_GAIN_MUL },
        /* sunblob, ONE/ONE into the car's reflections: the paint's sun glints. Also drawn over
         * walls, hedges and road reflections, so a small gain (BO-195: 1.5 to 1.2; HI mode's
         * fixed knee of 0.6 would still lift most of this soft blob, median texel 0.68) */
        { "sunblob",             0x547198130b4c6690ull, 0x0F, 128, 128, 1.2f, NV2A_HDR_GAIN_MUL },
    };
    nv2a_texrep_register_hdr_gain(k_light, sizeof k_light / sizeof k_light[0]);
}

/* BO-203: the sun's (and the moon's) 32x32 disc sprite, the one global
 * 7e99c3527a07c692: white texels, alpha 1 out to r = 0.84 of its half width,
 * one texel of ramp, then 0. It is drawn alpha-tested under the 13 additive
 * corona quads, and the title's frame pass doubles the view: in HDR it is a
 * flat plateau of ~600 nits with a hard edge, that takes the shape of any gap
 * in the scenery (a square between houses); SDR clips it into the corona's
 * white. With HDR on its colours are replaced by a radial profile and its
 * alpha stays the title's: the title counts the fragments that pass its alpha
 * test, probably an occlusion count, to size the corona's glare: with another
 * alpha, or no test, the glare changed by a third or more (EU M1_V2 grid).
 *
 * out = a * C + (1 - a) * D, with the sprite's colour C = tint * rgb and D the
 * sky under it. rgb = p + (1 - p) * rho gives out = D + p * (the old disc's
 * out - D) when rho = D / C, the sky's share of the tint (about a fifth in
 * daylight): the disc's centre as it was, falling smoothly to the sky at the
 * rim, p = sqrt(1 - (r / 0.95)^2) (r in half widths; the title's alpha is full
 * to 0.84 and ramps to 0 at 0.91). Tried at the EU grid, centre / ring in nits
 * (the flat disc: 590 / 197): exponent 1.5, edge 0.84: 490 / 153; 0.5, 0.95:
 * 557 / 175. The ring is down because the title's own bloom of the flat disc
 * is.
 * Another sky and tint only move the edge by a(rho' - rho) C, a few nits.
 * Built here at start from the formula: nothing of the game is shipped; SDR
 * never sees it.
 *
 * Stage 0 alone (BO-203, 2026-10-17): the draw's combiner also blends in
 * stage 1, the track's sky sheet (EU M1_V2: dace40c6da57205f, 1024x256),
 * lerping by the sheet's alpha, colour and alpha alike:
 *   rgb = t1.rgb * t1.a + t0.rgb * t0.a * (1 - t1.a)
 *   a   = t1.b   * t1.a + t0.b   * t0.a * (1 - t1.a)
 * The quad's stage-1 coordinates hit one point (or one line) of the sheet,
 * so where that point is bright sky (t1.b * t1.a above the test's 53/255,
 * 42 % of the Vienna sheet) every fragment of the quad passes the alpha test
 * and the quad paints a flat square, or vertical streaks, of the sheet's
 * colour over the sky. SDR clips it into the corona's white; HDR shows it:
 * the square the owner saw with the pause menu open, and the barred block
 * behind the hill. With HDR on the disc's draw reads (0, 0, 0, 0) at stage
 * 1: rgb and alpha are the disc's own, the quad's corners fail the test. */
static uint32_t s_sun_disc[32 * 32];
static void bo3_hdr_sun_init(void)
{
    static const struct nv2a_texrep_rect k_all = { 0, 0, 32, 32 };
    static const struct nv2a_texrep_variant k_var = { s_sun_disc, &k_all, 1, 1 /* colours only */,
                                                      1 /* stage 0 alone */ };
    static const struct nv2a_texrep_tex k_tex[] = {
        { "sun disc", 0x7e99c3527a07c692ull, 0x0F, 32, 32, &k_var, 1 },
    };
    const double rho = 0.2, edge = 0.95;
    int x, y;
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++) {
            double dx = x + 0.5 - 16.0, dy = y + 0.5 - 16.0;
            double r = sqrt(dx * dx + dy * dy) / 16.0 / edge, p, c;
            p = r >= 1.0 ? 0.0 : pow(1.0 - r * r, 0.5);
            c = p + (1.0 - p) * rho;
            s_sun_disc[y * 32 + x] = ((uint32_t)(c * 255.0 + 0.5) * 0x010101u);
        }
    nv2a_texrep_register_hdr_image(k_tex, sizeof k_tex / sizeof k_tex[0]);
}

/* Set a default for a runtime variable, leaving one the user set alone. The
 * settings overlay's file (settings.toml) is applied to the environment
 * before the defaults, so it outranks them; it is told each default so a
 * "reset" in the overlay goes back to this one. */
static void env_default(const char *name, const char *value)
{
#ifdef XBOXRECOMP_HAVE_OVERLAY
    xbox_SettingsGameDefault(name, value);
#endif
    if (getenv(name))
        return;
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 0);
#endif
}

static BOOL load_xbe(const char *path, void **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *data;

    if (!f) {
        XR_ERROR( "Cannot open XBE: %s\n", path);
        return FALSE;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || !(data = malloc((size_t)size))) {
        fclose(f);
        return FALSE;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return FALSE;
    }
    fclose(f);
    *out_data = data;
    *out_size = (size_t)size;
    return TRUE;
}

#if defined(_WIN32)
/* Every generated function is a real symbol in the image, so dbghelp turns a
 * host fault address into the guest function (sub_XXXXXXXX) that faulted. */
static void print_host_symbol(void *addr)
{
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line;
    DWORD ldisp = 0;

    memset(buf, 0, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    if (SymFromAddr(GetCurrentProcess(), (DWORD64)(uintptr_t)addr, &disp, sym))
        xbox_crash_printf("  in %s+0x%llX", sym->Name, (unsigned long long)disp);
    else {
        /* No symbols (an llvm-mingw build has no PDB): the image offset,
         * for llvm-symbolizer --obj=burnout3.exe --adjust-vma=<ImageBase>. */
        HMODULE mod = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)addr, &mod) && mod == GetModuleHandleA(NULL))
            xbox_crash_printf("  at exe+0x%llX", (unsigned long long)
                    ((uintptr_t)addr - (uintptr_t)mod));
        else
            xbox_crash_printf("  at 0x%llX", (unsigned long long)(uintptr_t)addr);
    }
    memset(&line, 0, sizeof(line));
    line.SizeOfStruct = sizeof(line);
    if (SymGetLineFromAddr64(GetCurrentProcess(), (DWORD64)(uintptr_t)addr,
                             &ldisp, &line))
        xbox_crash_printf(" (%s:%lu)", line.FileName, line.LineNumber);
    xbox_crash_printf("\n");
}
#endif

/* BO-114, the host mixer and the mix study (the toolkit's apu_vp.c): who is
 * turning a voice on. The words on the calling guest thread's stack that
 * point into the title's own code (.text, below the XDK sections, so not
 * DirectSound's) just after a call instruction, innermost first. Called
 * only with the mixer or RECOMP_MIX_TRACE on. */
static int bo3_voice_tag(uint32_t *out, int max)
{
    const uint8_t *mem = (const uint8_t *)(uintptr_t)g_xbox_mem_offset;
    uint32_t sp = g_esp;
    int n = 0;

    if (!mem || sp < 0x10000u || sp >= 0x10000000u)
        return 0;
    sp &= ~3u;
    for (int i = 0; i < 1024 && n < max; i++) {
        uint32_t a = sp + 4u * (uint32_t)i, v;
        if ((a & 0xFFFu) == 0 && i && a >= 0x04000000u)
            break;
        v = *(const uint32_t *)(mem + a);
        if (v < 0x00011010u || v >= 0x002CC200u)
            continue;
        /* call rel32, or an indirect call (FF /2) of 2, 3 or 6 bytes. */
        if (mem[v - 5] == 0xE8 ||
            (mem[v - 2] == 0xFF && (mem[v - 1] & 0x38) == 0x10) ||
            (mem[v - 3] == 0xFF && (mem[v - 2] & 0x38) == 0x10) ||
            (mem[v - 6] == 0xFF && (mem[v - 5] & 0x38) == 0x10))
            out[n++] = v;
    }
    return n;
}

/* Return addresses on the guest stack that land in the image: a cheap guest
 * backtrace, since the host stack only shows generated C frames. */
static void print_guest_stack(void)
{
    const uint32_t *sp;
    int i, shown = 0;

    if (!g_xbox_mem_offset || !g_esp || g_esp >= 0x10000000u)
        return;
    sp = (const uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp);
    fprintf(stderr, "  guest stack @%08X:", g_esp);
    for (i = 0; i < 16; i++)
        fprintf(stderr, "%s%08X", (i % 8) ? " " : "\n    ", sp[i]);
    fprintf(stderr, "\n  guest stack return candidates:");
    for (i = 0; i < 256 && shown < 12; i++) {
        uint32_t v = sp[i];
        if (v >= 0x00011000u && v < 0x00400000u) {
            fprintf(stderr, " %08X", v);
            shown++;
        }
    }
    fprintf(stderr, "\n");
}

/* BO3_PEEK=va[,va...]: dump 64 bytes of guest memory at each address, from
 * the fault report and from each hang sample. */
static void peek_guest_memory(void)
{
    const char *p = getenv("BO3_PEEK");
    while (p && *p && g_xbox_mem_offset) {
        char *end;
        uint32_t va = (uint32_t)strtoul(p, &end, 16);
        const uint32_t *m = (const uint32_t *)((uintptr_t)g_xbox_mem_offset + va);
        int k;
        fprintf(stderr, "  peek %08X:", va);
        for (k = 0; k < 16; k++)
            fprintf(stderr, "%s%08X", (k % 8) ? " " : "\n    ", m[k]);
        fprintf(stderr, "\n");
        p = (*end == ',') ? end + 1 : NULL;
    }
}


#define HD_M32(va) (*(const uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define HD_MF(va)  (*(const float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

/* BO3_HUD_MON=<file>: the HUD extent monitor (BO-89), off unless set. At
 * every flip, on the guest's thread (nv2a_pb_set_flip_hook), it walks the
 * title's four 2D scenes as the title's own render does (sub_001C67D0: the
 * layers without the hidden bit; sub_001C6500: an object is drawn at frame
 * world origin + world scale x its position, with its colour times the
 * frame's) and writes a line for every drawn box that an edge of the visible
 * picture cuts: the columns the window shows of the 640-wide frame
 * (nv2a_present_visible_x, 21..619 with the overscan crop), the same ones the
 * display.hud_fix patches lay the HUD out by.
 *
 *   H <flip> vis <x0> <x1> aspect <a> fix <0|1>      at the start, and when one of them changes
 *   V <flip> s<scene> L<layer> cb <callback> fr <frame> obj <node>
 *       x <x0> <x1> y <y0> <y1> w <object w> h <object h> a <alpha> side <L|R|LR>
 *   T <flip> ... the same fields, side - / L / R / LR / OUT (wholly outside)
 *
 * T lines: BO3_HUD_MON_TRACE=<callback>[,<callback>...] (hex) follows the
 * objects of those render callbacks at every flip, cut or not: the path of a
 * sliding message. BO3_HUD_MON_TRACE=all follows every drawn object.
 *
 * BO3_HUD_MON_SHOTS=<prefix>: frames to go with the lines, written at the
 * flip itself as <prefix><flip>_s<scene>_<callback>_<side>.bmp: a HUD or EA
 * Trax box (not the front end's) when its kind (scene, callback, side) is
 * cut for the first time and every 20 flips while it stays cut, 6 a kind;
 * and a traced callback's objects 8, 45 and 120 flips after they appear
 * (side "t"), 60 a callback. A shot reads the frame back and stops it for
 * 11-30 ms (Windows, RX 9060 XT: 47 of them in a 5-minute race, 10 racing
 * frames over 20 ms), so for a session someone plays
 * BO3_HUD_MON_SHOTS_CB=<callback>[,<callback>...] keeps only the cut boxes of
 * those callbacks and no traced object's. Cut them to the visible picture
 * (hud_extents.py present) before judging one: a frame dump is the whole
 * 640 columns, the window shows 21..619.
 *
 * tools/headless/hud_extents.py makes the per-run table and the 4:3 against
 * 16:9 difference: a box cut only in 16:9 with the fix on is the fix's defect,
 * one cut in both is the title's own use of the overscan.
 *
 * What it cannot see: a scene's objects stay in memory while the title does
 * not render the scene (the HUD's during a menu), so a box can be listed for
 * flips it was not drawn in; glyphs that leave their object's box; and the 2D
 * drawn outside the scenes (countdown, rival markers). The scene layout it
 * walks is the one config/patches.json's notes describe. */
void nv2a_pb_set_flip_hook(void (*fn)(uint32_t flip));
void xbox_PresentCadenceSteps(uint32_t flip, int steps);   /* toolkit: present_cadence.h */
void nv2a_present_visible_x(uint32_t fw, uint32_t fh, double *x0, double *x1);

int nv2a_pb_dump_frame(const char *path);

static FILE *s_hm_f;
static const char *s_hm_shots;
static struct { uint32_t cb, last; uint8_t scene, side, n; } s_hm_kind[128];
static int s_hm_nkind;
static uint32_t s_hm_only[16];      /* BO3_HUD_MON_SHOTS_CB */
static int s_hm_nonly;

static void hud_mon_bmp(uint32_t flip, int scene, uint32_t cb, const char *side)
{
    char path[1024];

    snprintf(path, sizeof path, "%s%05u_s%d_%08X_%s.bmp", s_hm_shots, flip, scene, cb, side);
    nv2a_pb_dump_frame(path);
}

/* BO3_HUD_MON_SHOTS: this flip's frame for a cut box, sparingly per kind. */
static void hud_mon_shot(uint32_t flip, int scene, uint32_t cb, const char *side)
{
    int i;

    if (!s_hm_shots || scene == 2)
        return;
    if (s_hm_nonly) {
        for (i = 0; i < s_hm_nonly && s_hm_only[i] != cb; i++)
            ;
        if (i == s_hm_nonly)
            return;
    }
    for (i = 0; i < s_hm_nkind; i++)
        if (s_hm_kind[i].cb == cb && s_hm_kind[i].scene == scene
                && s_hm_kind[i].side == (uint8_t)(side[0] + side[1]))
            break;
    if (i == s_hm_nkind) {
        if (s_hm_nkind == 128)
            return;
        s_hm_kind[i].cb = cb;
        s_hm_kind[i].scene = (uint8_t)scene;
        s_hm_kind[i].side = (uint8_t)(side[0] + side[1]);
        s_hm_kind[i].n = 0;
        s_hm_nkind++;
    } else if (s_hm_kind[i].n >= 6 || flip - s_hm_kind[i].last < 20) {
        return;
    }
    s_hm_kind[i].n++;
    s_hm_kind[i].last = flip;
    hud_mon_bmp(flip, scene, cb, side);
}

static uint32_t s_hm_trace[16];
static struct { uint32_t seen, since; int n; } s_hm_tr[16];
static int s_hm_ntrace, s_hm_all;

/* BO3_HUD_MON_SHOTS: a traced callback's object is drawn at this flip. */
static void hud_mon_trace_shot(uint32_t flip, int scene, int k)
{
    uint32_t age;

    if (s_hm_tr[k].seen == flip)        /* one a flip, however many objects */
        return;
    if (flip - s_hm_tr[k].seen > 30)
        s_hm_tr[k].since = flip;        /* it was away: it appears now */
    s_hm_tr[k].seen = flip;
    age = flip - s_hm_tr[k].since;
    if (s_hm_shots && !s_hm_nonly && s_hm_tr[k].n < 60
            && (age == 8 || age == 45 || age == 120)) {
        s_hm_tr[k].n++;
        hud_mon_bmp(flip, scene, s_hm_trace[k], "t");
    }
}

/* A guest pointer the walk may follow: inside the retail 64 MB. */
static int hm_ok(uint32_t p)
{
    return p >= 0x10000u && p < 0x03FFFF00u && !(p & 3u);
}

static void hud_mon_flip(uint32_t flip)
{
    static const uint32_t scenes[4] = { 0x54F914, 0x54F984, 0x557A20, 0x45BAD0 };
    static double last_x0 = -1.0, last_x1 = -1.0, last_aspect = -1.0;
    static int last_fix = -1;
    const double eps = 0.5;
    double vx0, vx1, aspect;
    int s, wrote = 0, fix;

    if (!s_hm_f || !g_xbox_mem_offset)
        return;
    nv2a_present_visible_x(640, 480, &vx0, &vx1);
    fix = recomp_patch_on("display.hud_fix", "1", RECOMP_PATCH_LATCH_USE);
    aspect = recomp_patch_var("aspect");
    if (vx0 != last_x0 || vx1 != last_x1 || fix != last_fix || aspect != last_aspect) {
        fprintf(s_hm_f, "H %u vis %.2f %.2f aspect %.4f fix %d\n", flip, vx0, vx1, aspect, fix);
        last_aspect = aspect;
        last_x0 = vx0;
        last_x1 = vx1;
        last_fix = fix;
        wrote = 1;
    }
    for (s = 0; s < 4; s++) {
        uint32_t sc = scenes[s], root = HD_M32(sc + 0x14), nl = HD_M32(sc + 0x30);
        uint32_t la = HD_M32(sc + 0x2C), l;

        if (!hm_ok(root) || !hm_ok(la) || !nl || nl > 256)
            continue;
        for (l = 0; l < nl; l++) {
            uint32_t n;
            int guard = 0;

            if (HD_M32(la + l * 16) & 1u)
                continue;
            for (n = HD_M32(la + l * 16 + 8); hm_ok(n) && guard < 500; n = HD_M32(n), guard++) {
                uint32_t fr = HD_M32(n + 0x40), cb = HD_M32(n + 0x44);
                float ox, oy, sx, sy, fa, x, y, w, h, a;
                double x0, x1, y0, y1, t;
                const char *side;
                int traced = s_hm_all, k;

                if (!hm_ok(fr))
                    continue;
                ox = HD_MF(fr + 0x20), oy = HD_MF(fr + 0x24);
                sx = HD_MF(fr + 0x28), sy = HD_MF(fr + 0x2C), fa = HD_MF(fr + 0x3C);
                x = HD_MF(n + 8), y = HD_MF(n + 12), w = HD_MF(n + 16), h = HD_MF(n + 20);
                a = HD_MF(n + 0x24) * fa;
                x0 = ox + (double)sx * x, x1 = ox + (double)sx * ((double)x + w);
                y0 = oy + (double)sy * y, y1 = oy + (double)sy * ((double)y + h);
                if (x1 < x0)
                    t = x0, x0 = x1, x1 = t;
                if (y1 < y0)
                    t = y0, y0 = y1, y1 = t;
                /* Drawn at all: some alpha, some area, on the frame's rows.
                 * NaN fails every test. */
                if (!(a > 0.004f) || !(x1 - x0 > 0.01) || !(y1 - y0 > 0.01)
                        || !(y1 > 0.0) || !(y0 < 480.0))
                    continue;
                for (k = 0; k < s_hm_ntrace && !traced; k++)
                    if (s_hm_trace[k] == cb) {
                        traced = 1;
                        hud_mon_trace_shot(flip, s, k);
                    }
                if (x1 <= vx0 || x0 >= vx1)
                    side = "OUT";
                else if (x0 < vx0 - eps && x1 > vx1 + eps)
                    side = "LR";
                else if (x0 < vx0 - eps)
                    side = "L";
                else if (x1 > vx1 + eps)
                    side = "R";
                else
                    side = "-";
                if (!traced && (side[0] == '-' || side[0] == 'O'))
                    continue;
                if (side[0] != '-' && side[0] != 'O')
                    hud_mon_shot(flip, s, cb, side);
                if (side[0] != '-' && side[0] != 'O')
                    fprintf(s_hm_f, "V %u s%d L%02u cb %08X fr %08X obj %08X x %.2f %.2f"
                                    " y %.2f %.2f w %.2f h %.2f a %.2f side %s\n",
                            flip, s, l, cb, fr, n, x0, x1, y0, y1, w, h, a, side);
                if (traced)
                    fprintf(s_hm_f, "T %u s%d L%02u cb %08X fr %08X obj %08X x %.2f %.2f"
                                    " y %.2f %.2f w %.2f h %.2f a %.2f side %s\n",
                            flip, s, l, cb, fr, n, x0, x1, y0, y1, w, h, a, side);
                wrote = 1;
            }
        }
    }
    if (wrote)
        fflush(s_hm_f);
}

/* BO3_DRIVE_LOG=<file>: one line per frame of a race, from the HUD
 * speedometer's update (the "drive_log" mark, config/patches.json, BO-96):
 * the flip, the speed the HUD shows (m/s; a magnitude, it rises again when
 * the car reverses), and what the title read that frame from the first
 * connected pad: the port, wButtons and the eight analog bytes (A B X Y
 * Black White, LT, RT). tools/headless/reverse_scene.sh asserts on it. Off
 * unless set. */
#define DL_PADS   0x004AE20Cu           /* the pad manager (sub_00021A50's edi) */
static FILE *s_dl_f;
static volatile uint32_t s_uw_flips;    /* the title's flips (uw_flip, below) */

static void drive_log(void)
{
    uint32_t hud = g_ebx, car, k;
    const uint8_t *st;

    if (!s_dl_f || !g_xbox_mem_offset || hud < 0x10000u || hud >= 0x08000000u)
        return;
    car = HD_M32(hud + 0x2C);
    if (car < 0x10000u || car >= 0x08000000u)
        return;
    /* The first open port's XINPUT_STATE as XInputGetState left it
     * (sub_001B57E0: four records of 0x20, state 2 = open, the XINPUT_STATE
     * at +8; the emulated pads sit on ports 3 and 4). */
    for (k = 0; k < 3 && HD_M32(DL_PADS + k * 0x20) != 2; k++)
        ;
    st = (const uint8_t *)((uintptr_t)g_xbox_mem_offset + DL_PADS + k * 0x20 + 8);
    fprintf(s_dl_f, "D %u v %.4f port %u btn %02X%02X an %u %u %u %u %u %u lt %u rt %u\n",
            s_uw_flips, HD_MF(car + 0xBC), k, st[5], st[4],
            st[6], st[7], st[8], st[9], st[10], st[11], st[12], st[13]);
    fflush(s_dl_f);
}

static uint32_t bo3_flips(void) { return s_uw_flips; }

static void drive_log_start(void)
{
    const char *path = getenv("BO3_DRIVE_LOG");

    if (!path || !*path)
        return;
    s_dl_f = fopen(path, "w");
    if (!s_dl_f) {
        fprintf(stderr, "BO3_DRIVE_LOG: cannot write %s\n", path);
        return;
    }
    recomp_patch_on_mark("drive_log", drive_log);
}

/* BO3_PACE_LOG=<file>: one line per flip the title emits (the "uw_flip"
 * mark), for the 60 fps crash camera switch (graphics.crash_60fps, BO-111,
 * docs/crash60.md): host time and the title's own pacing state. Off unless
 * set; tools/headless/pace_report.py reads it.
 *
 *   P <flip> t <ms> req <r> cur <c> mode <m>/<m2> steps <n> gs <steps>
 *     wt <ticks> slow <div> run <0|1> crash <b0><b1> st <state> obj <va>
 *     gt <ticks> ft <seconds> pl <players> mu <state>/<state>
 *
 * req / cur: the game object's (0x4A71A0) "half rate" bytes, +0x2E1ED asked
 * for by the stage's update and +0x2E1EE in force (sub_00016EF0 copies one
 * to the other and sets D3DRS_PRESENTATIONINTERVAL to 2 or 1; the stepper
 * sub_001B58E0 takes 2..8 steps a frame with it and 1..4 without). mode:
 * the stepper's mode, +0x2E1F0 (used while +0x2E1F4 is 3): 0 steps = the
 * minimum every frame, 1 by the clock, 2 the menus'. steps: the steps this
 * frame ran (+0x2E20C). gs: the global step counter 0x4A1EB4. wt / slow /
 * run: the world timer 0x60EA00's ticks (+0xC: steps / divisor), slow-motion
 * divisor (+0x18) and running byte (+0x28). crash: the two players' "crash
 * camera" bytes (0x73BACA, the next 0x27E0 on). st: the game's state (+0x2E214). obj:
 * the stage's mode object ([stage + 0x1B8]). gt: the game timer 0x4AE1E0's
 * ticks (never paused). ft: the frame's simulated time, steps x the world
 * timer's step (+0x2E210), which the cameras are moved by. pl: the player
 * count 0x73A1C0. mu: the music manager's (0x4115E8) in-game state (+0x5F4;
 * 0xD = 13 is "no music", what split screen has as shipped) and its outer
 * state (+0x5F8); BO-116, docs/split-screen.md. */
static FILE *s_pl_f;

static double pace_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

static void pace_log(uint32_t flip)
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)g_xbox_mem_offset;
    uint32_t stage;

    if (!s_pl_f || !m)
        return;
    stage = HD_M32(0x4D5370);
    fprintf(s_pl_f, "P %u t %.3f req %u cur %u mode %u/%u steps %d gs %u"
            " wt %u slow %u run %u crash %u%u st %u obj %08X gt %u ft %.5f"
            " pl %u mu %u/%u\n",
            flip, pace_ms(), m[0x4D538D], m[0x4D538E], HD_M32(0x4D5390),
            HD_M32(0x4D5394), (int)HD_M32(0x4D53AC), HD_M32(0x4A1EB4),
            HD_M32(0x60EA0C), HD_M32(0x60EA18), m[0x60EA28],
            m[0x73BACA], m[0x73BACA + 0x27E0], HD_M32(0x4D53B4),
            (stage >= 0x10000u && stage < 0x08000000u) ? HD_M32(stage + 0x1B8) : 0,
            HD_M32(0x4AE1EC), HD_MF(0x4D53B0),
            HD_M32(0x73A1C0), HD_M32(0x4115E8 + 0x5F4), HD_M32(0x4115E8 + 0x5F8));
}

static void pace_log_start(void)
{
    const char *path = getenv("BO3_PACE_LOG");

    if (!path || !*path)
        return;
    s_pl_f = fopen(path, "w");
    if (!s_pl_f)
        fprintf(stderr, "BO3_PACE_LOG: cannot write %s\n", path);
    else
        setvbuf(s_pl_f, NULL, _IOLBF, 0);
}

static void hud_mon_start(void)
{
    const char *path = getenv("BO3_HUD_MON"), *t = getenv("BO3_HUD_MON_TRACE");

    if (!path || !path[0])
        return;
    s_hm_f = fopen(path, "w");
    if (!s_hm_f) {
        fprintf(stderr, "BO3_HUD_MON: cannot write %s\n", path);
        return;
    }
    if (t && !strcmp(t, "all"))
        s_hm_all = 1;
    while (t && *t && !s_hm_all && s_hm_ntrace < 16) {
        char *end;
        uint32_t cb = (uint32_t)strtoul(t, &end, 16);

        if (end == t)
            break;
        s_hm_trace[s_hm_ntrace++] = cb;
        t = (*end == ',') ? end + 1 : end;
    }
    t = getenv("BO3_HUD_MON_SHOTS");
    if (t && t[0])
        s_hm_shots = t;
    t = getenv("BO3_HUD_MON_SHOTS_CB");
    while (t && *t && s_hm_nonly < 16) {
        char *end;
        uint32_t cb = (uint32_t)strtoul(t, &end, 16);

        if (end == t)
            break;
        s_hm_only[s_hm_nonly++] = cb;
        t = (*end == ',') ? end + 1 : end;
    }
    nv2a_pb_set_flip_hook(hud_mon_flip);
    fprintf(stderr, "BO3_HUD_MON: writing %s (%d traced callbacks%s)\n", path,
            s_hm_ntrace, s_hm_all ? ", all objects" : "");
}

/* graphics.bloom / graphics.motion_blur (RECOMP_GFX_BLOOM,
 * RECOMP_GFX_MOTION_BLUR, BO-212): the strength 0..1 the patches' expressions
 * ("loaded * bloom_strength", "loaded * blur_strength", config/patches.json)
 * multiply the title's bloom weight / blur opacity by while the setting reads
 * anything but 1. Unset or unreadable is 1: the title as it ships; the old
 * spellings false / off read 0. docs/effects.md. */
static double fx_strength(recomp_env_id id)
{
    const char *v = recomp_env(id);
    char *end;
    double d;

    if (!v || !*v)
        return 1.0;
    d = strtod(v, &end);
    if (end == v || *end)
        return (!strcmp(v, "false") || !strcmp(v, "off")) ? 0.0 : 1.0;
    return d < 0.0 ? 0.0 : d > 1.0 ? 1.0 : d;
}

static double bloom_strength(void) { return fx_strength(RENV_GFX_BLOOM); }
static double blur_strength(void) { return fx_strength(RENV_GFX_MOTION_BLUR); }

/* graphics.vehicle_detail (RECOMP_GFX_VEHICLE_DETAIL, BO-112): the factor on
 * the title's vehicle level-of-detail distances, read by the patches'
 * expression ("loaded * vehicle_lod_scale", config/patches.json) while the
 * setting is not "original". docs/draw-distance.md. */
static double vehicle_lod_scale(void)
{
    const char *v = recomp_env(RENV_GFX_VEHICLE_DETAIL);

    if (v && !strcmp(v, "far"))
        return 2.0;
    if (v && !strcmp(v, "farthest"))
        return 4.0;
    return 1.0;
}

/* graphics.scenery_detail (RECOMP_GFX_SCENERY_DETAIL, BO-177): the factor on
 * the switch distance of the track's props with two models (pine trees above
 * all), read by the patches' expression ("loaded * scenery_lod_scale",
 * config/patches.json) while the setting is "far". docs/draw-distance.md. */
#define SCENERY_FAR_SCALE 4.0
static double scenery_lod_scale(void)
{
    const char *v = recomp_env(RENV_GFX_SCENERY_DETAIL);

    return (v && !strcmp(v, "far")) ? SCENERY_FAR_SCALE : 1.0;
}

/* display.rival_marker (RECOMP_GFX_RIVAL_MARKER, BO-241): where the marker
 * over a rival (sub_0018F060) changes from its arrow to its race position
 * and back, as view depths (world units, metres), read by the patches'
 * expressions while the setting is "farther" (config/patches.json). As
 * shipped one distance, 35, checked every 30 steps; "farther" the position
 * below RIVAL_NEAR, the arrow again past RIVAL_FAR, checked every step.
 * BO3_RIVAL_SWITCH=<near>,<far>: other distances, for a test.
 * docs/rival-marker.md. */
#define RIVAL_NEAR 60.0
#define RIVAL_FAR  75.0
static double rival_switch(int which)
{
    static double v[2] = { -1.0, -1.0 };

    if (v[0] < 0.0) {
        const char *e = getenv("BO3_RIVAL_SWITCH");
        double n = RIVAL_NEAR, f = RIVAL_FAR;
        if (e && sscanf(e, "%lf,%lf", &n, &f) == 2 && n > 0.0 && f >= n) {
            fprintf(stderr, "[RIVAL] switch at %.1f / %.1f (BO3_RIVAL_SWITCH); the marker"
                    " hidden past %.1f (%.1f with 0x5A3759 = %u)\n", n, f,
                    HD_MF(0x60E560), HD_MF(0x60E540), (unsigned)(HD_M32(0x5A3758) >> 8 & 0xFFu));
        } else {
            n = RIVAL_NEAR;
            f = RIVAL_FAR;
        }
        v[0] = n;
        v[1] = f;
    }
    return v[which];
}
static double rival_near(void) { return rival_switch(0); }
static double rival_far(void)  { return rival_switch(1); }

/* The prop types of the track in play (MEM32(0x7397B4): + 0x34 the count,
 * + 0x38 the 0x70-byte records), one line each: the models (+0x5C detailed,
 * +0x5E simple, +0x60), the flags (+0x62; bit 0: two models), the switch
 * and draw distances (+0x64, +0x68). For BO3_FX_AT "sdetail". */
static void scenery_props_dump(void)
{
    uint32_t trk, tab;
    int n, i;

    if (!g_xbox_mem_offset || !(trk = HD_M32(0x7397B4)))
        return;
    n = *(const int16_t *)((uintptr_t)g_xbox_mem_offset + trk + 0x34);
    tab = HD_M32(trk + 0x38);
    for (i = 0; i < n && tab; i++) {
        uint32_t r = tab + (uint32_t)i * 0x70;
        const uint8_t *b = (const uint8_t *)((uintptr_t)g_xbox_mem_offset + r);
        char name[33];
        int k;

        for (k = 0; k < 32 && b[k] >= 0x20 && b[k] < 0x7F; k++)
            name[k] = (char)b[k];
        name[k] = 0;
        fprintf(stderr, "BO3_FX_AT: prop %3d models %d/%d/%d flags 0x%02X"
                " switch %.1f draw %.1f +6C %.1f '%s'\n", i,
                *(const int16_t *)(b + 0x5C), *(const int16_t *)(b + 0x5E),
                *(const int16_t *)(b + 0x60), b[0x62],
                HD_MF(r + 0x64), HD_MF(r + 0x68), HD_MF(r + 0x6C), name);
    }
}

/* BO3_FX_AT=<file>: a test aid for the two effect switches (BO-101,
 * docs/effects.md; "bloom" and "blur" take a strength, 0..1, BO-212) and the
 * crash camera rate (BO-111, docs/crash60.md).
 * Lines appended to the file, "<flip> bloom|blur|crash60 0|1" in
 * rising flip order, are taken at that flip (the "[GPU] flip N" number; a
 * flip already past: at once): graphics.bloom / graphics.motion_blur /
 * graphics.crash_60fps is set
 * as the overlay's row sets it. One headless run then has the same scene
 * with an effect and without: the frame of flip N was drawn before the
 * change, that of N + 1 after it (tools/headless/fx_scene.sh). Uses the one
 * flip hook: not together with BO3_HUD_MON. */
static const char *s_fx_path;
static long s_fx_pos;

static void fx_at_flip(uint32_t flip)
{
    char line[64], what[16];
    unsigned at;
    int v;
    double dv;
    FILE *f = fopen(s_fx_path, "r");

    if (!f)
        return;
    while (!fseek(f, s_fx_pos, SEEK_SET) && fgets(line, sizeof line, f)
           && strchr(line, '\n')
           && sscanf(line, "%u %15s %lf", &at, what, &dv) == 3 && at <= flip) {
        int blur = !strcmp(what, "blur");

        v = (int)dv;

        s_fx_pos = ftell(f);
        if (!strcmp(what, "crash60")) {     /* BO-111, docs/crash60.md */
#ifdef XBOXRECOMP_HAVE_OVERLAY
            xbox_SettingSet(XS_GRAPHICS_CRASH_60FPS, v != 0);
#else
            recomp_env_set(RENV_GFX_CRASH_60FPS, v ? "1" : "0");
#endif
            fprintf(stderr, "BO3_FX_AT: flip %u, graphics.crash_60fps = %d\n",
                    flip, v != 0);
            continue;
        }
        if (!strcmp(what, "vdetail")) {
            /* "<flip> vdetail 0|1|2": graphics.vehicle_detail (BO-112,
             * docs/draw-distance.md), with the five distances the title
             * holds (0x4D61D8..0x4D61E8: they never change). */
            static const char *const k[] = { "original", "far", "farthest" };
            if (v < 0 || v > 2)
                continue;
#ifdef XBOXRECOMP_HAVE_OVERLAY
            xbox_SettingSet(XS_GRAPHICS_VEHICLE_DETAIL, v);
#else
            recomp_env_set(RENV_GFX_VEHICLE_DETAIL, k[v]);
#endif
            fprintf(stderr, "BO3_FX_AT: flip %u, graphics.vehicle_detail = %s"
                    " (x%.0f; title's distances %.0f %.0f %.0f / %.0f %.0f)\n",
                    flip, k[v], vehicle_lod_scale(),
                    g_xbox_mem_offset ? HD_MF(0x4D61D8) : 0.0f,
                    g_xbox_mem_offset ? HD_MF(0x4D61DC) : 0.0f,
                    g_xbox_mem_offset ? HD_MF(0x4D61E0) : 0.0f,
                    g_xbox_mem_offset ? HD_MF(0x4D61E4) : 0.0f,
                    g_xbox_mem_offset ? HD_MF(0x4D61E8) : 0.0f);
            continue;
        }
        if (!strcmp(what, "sdetail")) {
            /* "<flip> sdetail 0|1": graphics.scenery_detail (BO-177,
             * docs/draw-distance.md); the first one lists the track's
             * prop types. */
            static int dumped;
            static const char *const k[] = { "original", "far" };
            if (v < 0 || v > 1)
                continue;
#ifdef XBOXRECOMP_HAVE_OVERLAY
            xbox_SettingSet(XS_GRAPHICS_SCENERY_DETAIL, v);
#else
            recomp_env_set(RENV_GFX_SCENERY_DETAIL, k[v]);
#endif
            fprintf(stderr, "BO3_FX_AT: flip %u, graphics.scenery_detail = %s"
                    " (x%.0f)\n", flip, k[v], scenery_lod_scale());
            if (!dumped++)
                scenery_props_dump();
            continue;
        }
        if (!strncmp(what, "pdraw", 5) && what[5] >= '0' && what[5] <= '9') {
            /* "<flip> pdraw<type> <metres>": the draw distance (+0x68) of
             * that prop type of the track in play, 0 puts the title's own
             * back (BO-253: which props are drawn only up to a short
             * distance, in a paused scene). */
            static float saved[64];
            static unsigned char have[64];
            uint32_t trk = g_xbox_mem_offset ? HD_M32(0x7397B4) : 0;
            int t = atoi(what + 5);

            if (trk && t >= 0 && t < 64
                && t < *(const int16_t *)((uintptr_t)g_xbox_mem_offset + trk + 0x34)) {
                float *p = (float *)((uintptr_t)g_xbox_mem_offset + HD_M32(trk + 0x38)
                                     + (uint32_t)t * 0x70 + 0x68);

                if (!have[t]) {
                    saved[t] = *p;
                    have[t] = 1;
                }
                *p = dv > 0.0 ? (float)dv : saved[t];
                fprintf(stderr, "BO3_FX_AT: flip %u, prop %d draw distance %.0f"
                        " (the title's %.0f)\n", flip, t, (double)*p, (double)saved[t]);
            }
            continue;
        }
        if (!blur && strcmp(what, "bloom"))
            continue;
#ifdef XBOXRECOMP_HAVE_OVERLAY
        xbox_SettingSet(blur ? XS_GRAPHICS_MOTION_BLUR : XS_GRAPHICS_BLOOM, dv);
#else
        {
            char sv[32];
            snprintf(sv, sizeof sv, "%g", dv);   /* a strength 0..1 (BO-212) */
            recomp_env_set(blur ? RENV_GFX_MOTION_BLUR : RENV_GFX_BLOOM, sv);
        }
#endif
        /* With the values the title would draw the effects by: the track's
         * bloom Threshold and Intensity (0x7397C0, ValueDB/TrackBloom) and
         * the first player's blur strength (the effect block at 0x4D6340,
         * +0x58: sub_00179FD0). */
        fprintf(stderr, "BO3_FX_AT: flip %u, %s = %g (bloom threshold %.3f"
                " intensity %.3f, blur strength %.3f)\n", flip,
                blur ? "graphics.motion_blur" : "graphics.bloom", dv,
                g_xbox_mem_offset ? HD_MF(0x7397C0) : 0.0f,
                g_xbox_mem_offset ? HD_MF(0x7397C4) : 0.0f,
                g_xbox_mem_offset ? HD_MF(0x4D6340 + 0x58) : 0.0f);
    }
    fclose(f);
}

static void fx_at_start(void)
{
    s_fx_path = getenv("BO3_FX_AT");
    if (s_fx_path && *s_fx_path)
        nv2a_pb_set_flip_hook(fx_at_flip);
}

#ifndef _WIN32
/* BO3_HUD_DUMP=<file>: a debug aid for HUD layout work (BO-89), off unless
 * set. A thread watches for two trigger files:
 *
 *   <file>.req   append to <file> a dump of the title's 2D scenes: the two HUD
 *                scenes (CB3HUDManager 0x54F900: scenes 0x54F914, 0x54F984,
 *                layout rectangles at +0x48 / +0xB8), the front-end scene
 *                0x557A20 and the EA Trax scene 0x45BAD0. Per scene the frame
 *                tree (local and world origin x scale) and per layer (f: bit 0
 *                hidden) every object: render callback, frame, position, size
 *                and where the frame puts it on screen. The first line of
 *                .req is the dump's tag.
 *   <file>.set   "1" / "0": set display.hud_fix through the settings API, as
 *                the overlay's row does (a RECOMP_GFX_HUD_FIX in the
 *                environment locks the setting: leave it unset).
 *
 * It reads guest memory from its own thread, unsynchronised: good for looking
 * at a layout, not for anything exact. The scene layout it walks (frame +0x44
 * first child, layer 16 bytes with the object list at +8, object +0x38 frame,
 * +0x3C callback) is the one config/patches.json's notes describe. */
#include <pthread.h>
#include <unistd.h>
static void hud_dump_frames(FILE *f, uint32_t fr, int depth)
{
    uint32_t n;
    int guard = 0;

    fprintf(f, "  %*sframe %08X local (%.2f,%.2f)x(%.3f,%.3f) world (%.2f,%.2f)x(%.3f,%.3f)\n",
            depth * 2, "", fr, HD_MF(fr), HD_MF(fr + 4), HD_MF(fr + 8), HD_MF(fr + 12),
            HD_MF(fr + 0x20), HD_MF(fr + 0x24), HD_MF(fr + 0x28), HD_MF(fr + 0x2C));
    for (n = HD_M32(fr + 0x44); n && guard < 200; n = HD_M32(n), guard++)
        hud_dump_frames(f, n + 8, depth + 1);
}

static void hud_dump_scenes(FILE *f)
{
    static const uint32_t scenes[4] = { 0x54F914, 0x54F984, 0x557A20, 0x45BAD0 };
    int s;

    fprintf(f, "rect0 %.2f %.2f %.2f %.2f rect1 %.2f %.2f %.2f %.2f nplayers %u\n",
            HD_MF(0x54F948), HD_MF(0x54F94C), HD_MF(0x54F950), HD_MF(0x54F954),
            HD_MF(0x54F9B8), HD_MF(0x54F9BC), HD_MF(0x54F9C0), HD_MF(0x54F9C4),
            HD_M32(0x54FA70));
    for (s = 0; s < 4; s++) {
        uint32_t sc = scenes[s], root = HD_M32(sc + 0x14), nl = HD_M32(sc + 0x30);
        uint32_t la = HD_M32(sc + 0x2C), l;

        fprintf(f, "scene %08X root %08X layers %u\n", sc, root, nl);
        if (!root || nl > 256)
            continue;
        hud_dump_frames(f, root, 0);
        for (l = 0; l < nl; l++) {
            uint32_t n = HD_M32(la + l * 16 + 8);
            int guard = 0;

            for (; n && guard < 500; n = HD_M32(n), guard++) {
                uint32_t fr = HD_M32(n + 0x40);
                float ox = fr ? HD_MF(fr + 0x20) : 0, sx = fr ? HD_MF(fr + 0x28) : 1;
                float oy = fr ? HD_MF(fr + 0x24) : 0, sy = fr ? HD_MF(fr + 0x2C) : 1;
                float x = HD_MF(n + 8), y = HD_MF(n + 12), w = HD_MF(n + 16), h = HD_MF(n + 20);

                fprintf(f, "  L%02u f=%u obj %08X cb %08X fr %08X pos (%.1f,%.1f) size (%.1f,%.1f)"
                           " a=%.2f -> x %.1f..%.1f y %.1f..%.1f\n",
                        l, HD_M32(la + l * 16), n, HD_M32(n + 0x44), fr, x, y, w, h,
                        HD_MF(n + 0x24), ox + sx * x, ox + sx * (x + w), oy + sy * y,
                        oy + sy * (y + h));
            }
        }
    }
}

static void *hud_dump_thread(void *arg)
{
    const char *path = (const char *)arg;
    char req[1024], set[1024];

    snprintf(req, sizeof req, "%s.req", path);
    snprintf(set, sizeof set, "%s.set", path);
    for (;;) {
        FILE *f;

        usleep(20000);
        if ((f = fopen(set, "r")) != NULL) {
            int v = fgetc(f) == '1';

            fclose(f);
            unlink(set);
#ifdef XBOXRECOMP_HAVE_OVERLAY
            xbox_SettingSet(XS_DISPLAY_HUD_FIX, v);
#endif
            fprintf(stderr, "BO3_HUD_DUMP: display.hud_fix set to %d\n", v);
        }
        if (access(req, F_OK) || !g_xbox_mem_offset)
            continue;
        if ((f = fopen(path, "a")) != NULL) {
            char tag[128] = "";
            FILE *r = fopen(req, "r");

            if (r) {
                if (!fgets(tag, sizeof tag, r))
                    tag[0] = 0;
                fclose(r);
            }
            fprintf(f, "=== dump %s", tag[0] ? tag : "\n");
            hud_dump_scenes(f);
            fclose(f);
        }
        unlink(req);
    }
    return NULL;
}

static void hud_dump_start(void)
{
    pthread_t t;

    if (getenv("BO3_HUD_DUMP"))
        pthread_create(&t, NULL, hud_dump_thread, (void *)getenv("BO3_HUD_DUMP"));
}
#else
static void hud_dump_start(void) { }
#endif

#if defined(_WIN32)
/* The emulated MCPX APU answers its own registers. */
bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr,
                          uint32_t fault_xbox_va, int is_write);

static LONG CALLBACK veh_handler(PEXCEPTION_POINTERS ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    static unsigned reports;

    /* USB host controller registers are trapped MMIO, not faults. */
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        g_xbox_mem_offset && er->NumberParameters >= 2) {
        uintptr_t fault = (uintptr_t)er->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)g_xbox_mem_offset;
        if (fault >= base) {
            uint32_t va = (uint32_t)(fault - base);
            if (xbox_OhciOwnsAddress(va)
             && xbox_OhciHandleMmio(ep->ContextRecord, va))
                return EXCEPTION_CONTINUE_EXECUTION;
            /* RECOMP_AC97_READY makes the APU aperture PAGE_NOACCESS so the
             * APU model can answer it. Burnout 3 needs a working
             * DirectSoundCreate: RenderWare's audio device is created during
             * engine start (sub_0020A540), and when it fails the NULL is
             * handed on until the pipeline builder faults on it. */
            if (va >= 0xFE800000u && va < 0xFE880000u
             && apu_hook_handle_mmio(ep->ContextRecord, fault, va,
                                     (int)er->ExceptionInformation[0]))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    /* BO3_TRAP_NAN: an unmasked SSE invalid-operation -- the first op that
     * made a NaN. Log the site once, re-mask for this thread, and let the op
     * run again to produce its NaN as before. */
    if (er->ExceptionCode == STATUS_FLOAT_INVALID_OPERATION
     || er->ExceptionCode == STATUS_FLOAT_MULTIPLE_TRAPS) {
        static void *seen[40];
        static int nseen;
        int i, dup = 0;
        for (i = 0; i < nseen; i++)
            dup |= seen[i] == er->ExceptionAddress;
        if (!dup && nseen < 40) {
            STACKFRAME64 sf;
            CONTEXT walk = *ep->ContextRecord;
            int f;
            seen[nseen++] = er->ExceptionAddress;
            fprintf(stderr, "\n[NAN] invalid op #%d, thread %lu, at ", nseen,
                    GetCurrentThreadId());
            print_host_symbol(er->ExceptionAddress);
            memset(&sf, 0, sizeof sf);
            sf.AddrPC.Offset = walk.Rip;    sf.AddrPC.Mode = AddrModeFlat;
            sf.AddrFrame.Offset = walk.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
            sf.AddrStack.Offset = walk.Rsp; sf.AddrStack.Mode = AddrModeFlat;
            for (f = 0; f < 8 && StackWalk64(IMAGE_FILE_MACHINE_AMD64,
                     GetCurrentProcess(), GetCurrentThread(), &sf, &walk, NULL,
                     SymFunctionTableAccess64, SymGetModuleBase64, NULL)
                     && sf.AddrPC.Offset; f++) {
                if (f) {
                    fprintf(stderr, "      <- ");
                    print_host_symbol((void *)(uintptr_t)sf.AddrPC.Offset);
                }
            }
            fprintf(stderr, "  guest regs: eax=%08X ecx=%08X esi=%08X edi=%08X"
                            " esp=%08X\n", g_eax, g_ecx, g_esi, g_edi, g_esp);
            fflush(stderr);
        }
        ep->ContextRecord->MxCsr |= 0x80;           /* IM back on */
        ep->ContextRecord->MxCsr &= ~0x3Fu;          /* clear the flags */
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    /* Report anything fatal, not only access violations: a stack overflow or
     * illegal instruction otherwise ends the process looking like a clean
     * exit. Capped so a systematic fault does not bury the first ones. */
    if (er->ExceptionCode < 0x80000000u || ++reports > 6)
        return EXCEPTION_CONTINUE_SEARCH;

    g_xr_log_diag = 1;      /* a fault: everything from here on is said (xr_log.h) */
    fprintf(stderr, "\n[FAULT] code 0x%08lX at host %p (thread %lu)\n",
            (unsigned long)er->ExceptionCode, er->ExceptionAddress,
            GetCurrentThreadId());
    print_host_symbol(er->ExceptionAddress);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        er->NumberParameters >= 2) {
        uintptr_t addr = (uintptr_t)er->ExceptionInformation[1];
        fprintf(stderr, "  %s host 0x%p",
                er->ExceptionInformation[0] ? "write to" : "read from",
                (void *)addr);
        if (g_xbox_mem_offset && addr >= (uintptr_t)g_xbox_mem_offset &&
            addr < (uintptr_t)g_xbox_mem_offset + 0xF0000000u)
            fprintf(stderr, " = guest 0x%08X",
                    (uint32_t)(addr - (uintptr_t)g_xbox_mem_offset));
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "  guest regs: eax=%08X ecx=%08X edx=%08X ebx=%08X\n"
                    "              esp=%08X seh_ebp=%08X esi=%08X edi=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx, g_esp, g_seh_ebp, g_esi, g_edi);
    print_guest_stack();
    peek_guest_memory();
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Every thread's host RIP and, for a busy one, its stack, naming the
 * generated function each is in. A spin shows up as the same symbol with the
 * indirect-call count flat, which is what separates a hang from something
 * merely slow. Ported from hl2/src/game/main.c. The toolkit's hang watchdog
 * (RECOMP_HANG_WATCHDOG, xbox_crash.h) calls it into its report; without
 * that, RECOMP_HANG_WATCHDOG=<seconds> prints it every N seconds. */
extern volatile uint64_t g_icall_count;

static void sample_all_threads(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();

    if (snap == INVALID_HANDLE_VALUE)
        return;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            HANDLE th;
            CONTEXT ctx;
            FILETIME c, e, k, u;
            ULONGLONG busy = 0;

            if (te.th32OwnerProcessID != pid || te.th32ThreadID == me)
                continue;
            th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                            THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!th)
                continue;
            if (GetThreadTimes(th, &c, &e, &k, &u))
                busy = (((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) +
                        ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime)) / 10000;
            memset(&ctx, 0, sizeof(ctx));
            ctx.ContextFlags = CONTEXT_FULL;
            /* Resume before printing: a thread suspended while holding the
             * CRT's stderr lock deadlocks the sampler's own fprintf, which is
             * exactly what heavy kernel logging makes likely. The stack of a
             * busy thread is walked while it is suspended and printed after. */
            if (SuspendThread(th) != (DWORD)-1) {
                BOOL got = GetThreadContext(th, &ctx);
                DWORD64 frames[10];
                int nframes = 0;
                if (got && busy > 10000) {
                    STACKFRAME64 sf;
                    CONTEXT walk = ctx;
                    memset(&sf, 0, sizeof sf);
                    sf.AddrPC.Offset = ctx.Rip;    sf.AddrPC.Mode = AddrModeFlat;
                    sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
                    sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
                    while (nframes < 10
                        && StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(),
                                       th, &sf, &walk, NULL,
                                       SymFunctionTableAccess64,
                                       SymGetModuleBase64, NULL)
                        && sf.AddrPC.Offset)
                        frames[nframes++] = sf.AddrPC.Offset;
                }
                ResumeThread(th);
                if (got) {
                    int f;
                    xbox_crash_printf("  tid %-6lu cpu %6llums ",
                            (unsigned long)te.th32ThreadID,
                            (unsigned long long)busy);
                    print_host_symbol((void *)(uintptr_t)ctx.Rip);
                    for (f = 1; f < nframes; f++) {
                        xbox_crash_printf("      <- ");
                        print_host_symbol((void *)(uintptr_t)frames[f]);
                    }
                }
            }
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

#if !defined(BO3_HAVE_REPORTS)
static DWORD WINAPI hang_watchdog(LPVOID arg)
{
    unsigned seconds = (unsigned)(uintptr_t)arg;
    unsigned long long prev = 0;
    int sample;

    for (sample = 1; ; sample++) {
        Sleep(seconds * 1000);
        g_xr_log_diag = 1;  /* as a fault */
        fprintf(stderr, "\n[HANG] sample %d after %us, indirect calls %llu (+%llu)\n",
                sample, sample * seconds,
                (unsigned long long)g_icall_count,
                (unsigned long long)(g_icall_count - prev));
        prev = g_icall_count;
        sample_all_threads();
        peek_guest_memory();
        fflush(stderr);
    }
    return 0;
}
#endif

/* BO3_TRAP_NAN=<flag file>: once that file exists, unmask the SSE invalid
 * exception on every thread so veh_handler names the op that first makes a
 * NaN. Armed by a file, not a timer, because what matters is a moment in
 * play (the race start) that a driver script reaches at no fixed time. */
static DWORD WINAPI nan_trap_arm(LPVOID arg)
{
    const char *flag = (const char *)arg;
    HANDLE snap;
    THREADENTRY32 te;
    DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();

    while (GetFileAttributesA(flag) == INVALID_FILE_ATTRIBUTES)
        Sleep(500);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    te.dwSize = sizeof te;
    if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te)) {
        do {
            HANDLE th;
            CONTEXT ctx;
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == me)
                continue;
            th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                            THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!th)
                continue;
            if (SuspendThread(th) != (DWORD)-1) {
                memset(&ctx, 0, sizeof ctx);
                ctx.ContextFlags = CONTEXT_FLOATING_POINT;
                if (GetThreadContext(th, &ctx)) {
                    ctx.MxCsr &= ~0x80u;
                    ctx.MxCsr &= ~0x3Fu;
                    SetThreadContext(th, &ctx);
                }
                ResumeThread(th);
            }
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    if (snap != INVALID_HANDLE_VALUE)
        CloseHandle(snap);
    fprintf(stderr, "[NAN] trap armed\n");
    return 0;
}

/* --record: frames are dumped as BMPs while the game runs, and encoded when
 * it stops. ponytail: not a live pipe -- the software GPU runs at a few fps,
 * so buffering to disk costs nothing and survives an ffmpeg that is missing
 * or fails; pipe to ffmpeg's stdin if long recordings fill the disk. */
static char s_rec_out[MAX_PATH];
static char s_rec_dir[MAX_PATH];
static volatile LONG s_rec_done;

static void record_finish(void)
{
    char cmd[3 * MAX_PATH];

    if (!s_rec_out[0] || InterlockedExchange(&s_rec_done, 1))
        return;
    snprintf(cmd, sizeof cmd,
             "ffmpeg -hide_banner -loglevel error -y -framerate 30 "
             "-i \"%s\\f%%05d.bmp\" -pix_fmt yuv420p \"%s\"",
             s_rec_dir, s_rec_out);
    fprintf(stderr, "[REC] encoding %s\n", s_rec_out);
    if (system(cmd) != 0) {
        XR_ERROR( "[REC] ffmpeg failed; the frames are in %s\n",
                s_rec_dir);
        return;
    }
    snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\"", s_rec_dir);
    system(cmd);
}

static BOOL WINAPI record_ctrl(DWORD type)
{
    (void)type;
    record_finish();
    ExitProcess(0);
    return TRUE;
}

static DWORD WINAPI record_timer(LPVOID arg)
{
    Sleep((DWORD)(uintptr_t)arg * 1000u);
    record_finish();
    ExitProcess(0);
    return 0;
}

static void record_start(const char *out, unsigned seconds)
{
    char tmp[MAX_PATH];

    GetFullPathNameA(out, MAX_PATH, s_rec_out, NULL);
    GetTempPathA(MAX_PATH, tmp);
    snprintf(s_rec_dir, sizeof s_rec_dir, "%sbo3rec_%lu", tmp,
             GetCurrentProcessId());
    CreateDirectoryA(s_rec_dir, NULL);
    {
        char prefix[MAX_PATH + 4];
        snprintf(prefix, sizeof prefix, "%s\\f", s_rec_dir);
        _putenv_s("RECOMP_FB_DUMP", prefix);
    }
    _putenv_s("RECOMP_FB_DUMP_FLIPS", "1");
    SetConsoleCtrlHandler(record_ctrl, TRUE);
    if (seconds)
        CloseHandle(CreateThread(NULL, 0, record_timer,
                                 (LPVOID)(uintptr_t)seconds, 0, NULL));
    printf("Recording to %s (frames in %s)\n", s_rec_out, s_rec_dir);
}

#if defined(BO3_HAVE_REPORTS)
/* An exception nothing handled: the report folder, with a minidump beside
 * it for WinDbg or Visual Studio, and then the process ends as it would
 * have. veh_handler has already printed the first-chance report. */
static LONG WINAPI unhandled_filter(PEXCEPTION_POINTERS ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    static volatile LONG once;
    char head[160];

    if (InterlockedExchange(&once, 1))
        return EXCEPTION_CONTINUE_SEARCH;
    xbox_InputPanicStop(0);             /* a pad must not go on shaking */
    snprintf(head, sizeof head, "[CRASH] exception 0x%08lX at %p (thread %lu)",
             (unsigned long)er->ExceptionCode, er->ExceptionAddress,
             GetCurrentThreadId());
    if (xbox_crash_begin("crash", head)) {
        STACKFRAME64 sf;
        CONTEXT walk = *ep->ContextRecord;
        char path[1200];
        HANDLE f;
        int n;

        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
            uintptr_t addr = (uintptr_t)er->ExceptionInformation[1];
            xbox_crash_printf("  %s host 0x%p", er->ExceptionInformation[0] ? "write to"
                                                                          : "read from",
                              (void *)addr);
            if (g_xbox_mem_offset && addr >= (uintptr_t)g_xbox_mem_offset
                && addr < (uintptr_t)g_xbox_mem_offset + 0xF0000000u)
                xbox_crash_printf(" = guest 0x%08X",
                                  (uint32_t)(addr - (uintptr_t)g_xbox_mem_offset));
            xbox_crash_printf("\n");
        }
        xbox_crash_printf("  host backtrace:\n");
        memset(&sf, 0, sizeof sf);
        sf.AddrPC.Offset = walk.Rip;    sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrFrame.Offset = walk.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
        sf.AddrStack.Offset = walk.Rsp; sf.AddrStack.Mode = AddrModeFlat;
        for (n = 0; n < 24 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(),
                                          GetCurrentThread(), &sf, &walk, NULL,
                                          SymFunctionTableAccess64, SymGetModuleBase64,
                                          NULL) && sf.AddrPC.Offset; n++) {
            xbox_crash_printf("    %s", n ? "<- " : "pc ");
            print_host_symbol((void *)(uintptr_t)sf.AddrPC.Offset);
        }
        xbox_crash_guest_state();
        snprintf(path, sizeof path, "%s/minidump.dmp", xbox_crash_end());
        f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                              MiniDumpWithThreadInfo, &mei, NULL, NULL);
            CloseHandle(f);
        }
    }
    xbox_crash_log_flush(500);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static void hang_watchdog_start(void)
{
#if defined(BO3_HAVE_REPORTS)
    SetUnhandledExceptionFilter(unhandled_filter);
    xbox_crash_set_sampler(sample_all_threads);
#else
    const char *spec = getenv("RECOMP_HANG_WATCHDOG");
    unsigned seconds = spec ? (unsigned)atoi(spec) : 0;

    if (seconds)
        CloseHandle(CreateThread(NULL, 0, hang_watchdog,
                                 (LPVOID)(uintptr_t)seconds, 0, NULL));
#endif
    if (getenv("BO3_TRAP_NAN"))
        CloseHandle(CreateThread(NULL, 0, nan_trap_arm,
                                 (LPVOID)getenv("BO3_TRAP_NAN"), 0, NULL));
}

#else /* !_WIN32 */

/* POSIX: SIGBUS/SIGSEGV, and SIGILL, SIGFPE, SIGABRT. Adapted from
 * apexJCL/blinx2-recomp src/main.c (MIT), which runs BLiNX 2 on this
 * runtime on macOS arm64.
 *
 * Guest memory is reserved PROT_NONE, and macOS raises SIGBUS for a mapped
 * but inaccessible page, so the trapped hardware registers (APU, AC'97,
 * OHCI) arrive here as SIGBUS. xbox_PosixMmioFault decodes the A64 access,
 * emulates it and steps the PC; anything else is a real fault, reported
 * once, into <save dir>/crash/ (xbox_crash.h), and then left to kill the
 * process by its own signal.
 *
 * The host backtrace is the toolkit's: frame records from the signal
 * context on macOS (arm64 and x86-64), the unwinder on Linux, whose
 * x86-64 Release code keeps no frame pointer to walk.
 *
 * Not formally async-signal-safe (snprintf, dladdr), which is accepted for
 * a crash report, as in BLiNX 2. */
static volatile sig_atomic_t s_in_crash;

static void restore_default(int sig)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGILL:  return "SIGILL";
    case SIGFPE:  return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default:      return "signal";
    }
}

static void posix_crash_handler(int sig, siginfo_t *si, void *ucv)
{
    uintptr_t addr = (uintptr_t)si->si_addr;
    char head[160];

    /* arm64, and x86-64 Linux: the runtime decodes the trapped access (it
     * returns 0 on a host without a decoder). */
    if ((sig == SIGBUS || sig == SIGSEGV) && xbox_PosixMmioFault(ucv, addr))
        return;
    if (s_in_crash) {
        restore_default(sig);
        return;
    }
    s_in_crash = 1;
    /* A pad must not go on shaking for a process that has crashed: the
     * toolkit's stop thread is woken and given a moment (signal-safe). */
    xbox_InputPanicStop(100);

    snprintf(head, sizeof head, "\n[FAULT] %s (%d), address 0x%llX", signal_name(sig),
             sig, (unsigned long long)addr);
    xbox_crash_begin("crash", head);
    if (g_xbox_mem_offset && addr >= (uintptr_t)g_xbox_mem_offset &&
        addr < (uintptr_t)g_xbox_mem_offset + 0xF0000000u)
        xbox_crash_printf("  = guest 0x%08X\n", (uint32_t)(addr - (uintptr_t)g_xbox_mem_offset));
#if defined(BO3_HAVE_REPORTS)
    xbox_crash_host_backtrace(ucv);
    xbox_crash_guest_state();
#else
    (void)ucv;
    fprintf(stderr, "  guest regs: eax=%08X ecx=%08X edx=%08X ebx=%08X\n"
                    "              esp=%08X seh_ebp=%08X esi=%08X edi=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx, g_esp, g_seh_ebp, g_esi, g_edi);
    print_guest_stack();
#endif
    peek_guest_memory();
    xbox_crash_end();
    xbox_crash_log_flush(500);
    /* Returning re-runs the instruction under SIG_DFL (abort raises again):
     * the process dies by the original signal, which is what the shell then
     * reports. */
    restore_default(sig);
}

/* SIGSTKSZ is 128 KB on Darwin; the report needs a few KB of it. */
static char s_crash_altstack[1 << 17];

static void host_install_crash_handler(void)
{
    static const int sigs[] = { SIGBUS, SIGSEGV, SIGILL, SIGFPE, SIGABRT };
    stack_t ss;
    struct sigaction sa;
    unsigned i;

    memset(&ss, 0, sizeof ss);
    ss.ss_sp = s_crash_altstack;
    ss.ss_size = sizeof s_crash_altstack;
    sigaltstack(&ss, NULL);

    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = posix_crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        sigaction(sigs[i], &sa, NULL);
}

#endif /* _WIN32 */

void bo3_pcache_manifest_register(const char *xbe_path);   /* pcmanifest.c */
static const char *s_xbe_path = BO3_XBE_DEFAULT;
static const char *s_save_dir = BO3_SAVE_DIR;

/* A start that cannot go on: what failed, what to do about it, and where the
 * log is (a release build has no terminal; fatal.h shows a message box
 * unless the run is headless). */
static void host_fatal(const char *what, const char *todo)
{
#if defined(__ANDROID__)
    const char *log = "takedawn.log in the app's files folder"
                      " (Android/data/io.github.xenophobentx.takedawn/files)";
#else
    char log[1200];

    snprintf(log, sizeof log, "run.log in the logs folder of the save folder (%s/logs)",
             s_save_dir);
#endif
    bo3_fatal(BO3_APP_NAME " cannot start", "%s\n\n%s\n\nThe log of this run is %s.", what, todo,
              log);
}

/* The boot sequence, the same on every host. On Windows it runs on the
 * process main thread; on macOS xbox_HostWindowMain keeps that thread for the
 * SDL window (AppKit runs its event loop nowhere else) and runs this on a
 * thread of its own. */
#if defined(__ANDROID__) && defined(XBOXRECOMP_HAVE_OVERLAY)
/* The overlay's driver rows on the Graphics page (xbox_OverlaySetHostRows):
 * the choice is the activity's (GameActivity.driverMenu, the app's GpuDriver);
 * this asks it for each row's text, once a second at most, and passes a row's
 * activation on. Called on the overlay's thread. */
#include <jni.h>
#include <SDL.h>
#include <SDL_system.h>
extern void xbox_SettingsFlush(void);
extern void xbox_OverlaySetHostRows(int n, const char *(*fn)(int row, int activate));
static const char *android_driver_row(int row, int activate)
{
    static char text[3][320];
    static uint32_t at[3];
    uint32_t now = SDL_GetTicks();
    JNIEnv *env;
    jobject act;
    jclass cls;
    jmethodID m;
    jstring js;

    if (row < 0 || row > 2)
        return NULL;
    if (!activate && at[row] && now - at[row] < 1000)
        return text[row];
    env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    act = env ? (jobject)SDL_AndroidGetActivity() : NULL;
    if (!act)
        return text[row];
    if (activate)
        xbox_SettingsFlush();           /* a restart may follow */
    cls = (*env)->GetObjectClass(env, act);
    m = (*env)->GetMethodID(env, cls, "driverMenu", "(II)Ljava/lang/String;");
    js = m ? (jstring)(*env)->CallObjectMethod(env, act, m, (jint)row, (jint)activate) : NULL;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        js = NULL;
    }
    text[row][0] = 0;
    if (js) {
        const char *c = (*env)->GetStringUTFChars(env, js, NULL);
        if (c) {
            snprintf(text[row], sizeof text[row], "%s", c);
            (*env)->ReleaseStringUTFChars(env, js, c);
        }
        (*env)->DeleteLocalRef(env, js);
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    at[row] = now ? now : 1;
    if (activate)
        at[0] = at[1] = at[2] = 0;      /* every row may read differently now */
    return text[row];
}
#endif

static int host_main(void)
{
    void *xbe_data = NULL;
    size_t xbe_size = 0;

#if !defined(_WIN32)
    /* Here, not in main: the alternate signal stack belongs to the thread
     * that runs the guest. */
    host_install_crash_handler();
#endif

    if (!recomp_dispatch_init())
        XR_ERROR( "[BOOT] flat dispatch unavailable; "
                        "indirect calls will use the binary search\n");

    printf("=== Burnout 3: Takedown (Xbox) - Static Recompilation ===\n");

    if (!load_xbe(s_xbe_path, &xbe_data, &xbe_size)) {
        char what[1200];

        snprintf(what, sizeof what, "The game's program file could not be read:\n%s", s_xbe_path);
        host_fatal(what, "The game's data is missing or damaged. Install it again from your own"
                         " Burnout 3: Takedown disc (the setup program, or --install), and"
                         " check that the folder can be read.");
        return 1;
    }
    printf("XBE loaded: %zu bytes\n", xbe_size);


    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        XR_ERROR( "Xbox memory layout init failed "
                        "(required VA range unavailable?)\n");
        free(xbe_data);
        host_fatal("The memory the game needs could not be reserved. It runs as a copy of the"
                   " Xbox and needs a fixed range of address space to itself.",
                   "Close other programs, and any other copy of the game, and start it again."
                   " Restarting the computer usually frees the range. If it keeps happening,"
                   " send the log with a bug report.");
        return 1;
    }
    g_xbox_mem_offset = xbox_GetMemoryOffset();
    printf("Xbox memory mapped at host offset 0x%llX\n",
           (unsigned long long)g_xbox_mem_offset);

    /* The APU reads physical RAM, which is mapped at g_xbox_mem_offset. */
    {
        typedef struct MCPXAPUState MCPXAPUState;
        extern MCPXAPUState *mcpx_apu_init_standalone(uint8_t *ram_ptr);
        extern MCPXAPUState *g_apu_state;
        /* The title's GP effects image, sound/DSP/B3XboxFx.bin, as the
         * APU models it: which mixbin reaches which speaker with what
         * gain and sign, the distortion on the player's engine and
         * exhaust, and the copy of the bins the title's return voices
         * play (gp_image.c). Stereo and 5.1. */
        bo3_gp_wire();

        {
            /* Who turns a voice on: asked only with the mixer or the mix
             * study on (apu_vp.c). */
            extern void mcpx_apu_set_voice_tag_hook(int (*fn)(uint32_t *, int));
            mcpx_apu_set_voice_tag_hook(bo3_voice_tag);
        }
        g_apu_state = mcpx_apu_init_standalone(
            (uint8_t *)(uintptr_t)xbox_GetMemoryOffset());
        printf("MCPX APU: %s\n", g_apu_state ? "initialised" : "FAILED");
    }

    xbox_OhciInit();
    xbox_WatchdogStart();

#if defined(_WIN32)
    /* BO-282.10: on Windows the guest threads run unpinned (the toolkit pins
     * them to one core unless the title opts out). The pin cost this title
     * pictures at 120 Hz (its thread waited for the idle pinned core to wake
     * after each hand-off), and its race class is empty: the Mac has always
     * run it unpinned, and a lockstep race gives the same step hashes pinned
     * and unpinned. RECOMP_DEBUG=guest_cpus=<n> pins again. */
    xbox_GuestCpusDefaultUnpinned();
#endif
    printf("Initializing kernel replacement...\n");
    xbox_kernel_init();
    /* The HDD partitions live in the save folder; without it Partition1.img
     * cannot be created and XAPI quick-reboots at once (docs/boot.md #2).
     * host_saves_init has made it. */
#if defined(_WIN32)
    CreateDirectoryA(s_save_dir, NULL);
#else
    mkdir(s_save_dir, 0755);
#endif
    xbox_path_init(BO3_GAME_DIR, s_save_dir);
#if defined(BO3_HAVE_REPORTS)
    /* BO-129: the title's saves are replaced whole, with a history. */
    save_guard_init();
#endif
    /* BO-108: the user's music folder as the title's custom soundtracks.
     * Nothing unless audio.custom_music_dir (--music-dir) names one. */
    bo3_soundtrack_init();
    /* Mod folders: disc files replaced by files in the folders TAKEDAWN_MODS
     * names (modfs.c, docs/mod-folders.md). Nothing unless it is set (on
     * Android: or the app's mods folder exists). After the soundtrack, whose
     * path hook it chains to. */
    {
        extern void bo3_modfs_init(void);
        bo3_modfs_init();
    }
    /* BO-114: the host mixer's buses, wave table and preset file. Names
     * only unless audio.mixer (RECOMP_AUDIO_MIXER) is on. */
    {
        extern const char *xbox_SettingsPath(void);
        bo3_mixer_init(BO3_GAME_DIR, xbox_SettingsPath(), s_save_dir);
    }
    bo3_dsound_notify_init();           /* BO-173: the done-list guard */
    xbox_kernel_bridge_init();

    g_esp = XBOX_STACK_TOP;

    /* The GPU fence D3D8LTCG waits on. sub_00351770 reserves pushbuffer
     * space and spins until the GPU has consumed enough of it:
     *
     *     mov esi, [0x35FB48]   ; the D3D device
     *     mov eax, [esi+0x30]   ; -> what the GPU has consumed
     *     mov ecx, [eax]
     *     mov eax, [esi+0x2C]   ;    what the title has submitted
     *
     * Same layout as HL2 (same XDK 5849 D3D8LTCG). The runtime advances the
     * fence as an honest acknowledgement of everything submitted; the chain
     * is re-read and bounds-checked on every poll, so registering before the
     * device exists is harmless. */
    xbox_Nv2aMirrorFence(0x0035FB48u, 0x2Cu, 0x30u);

    /* The vblank count the XMV player paces on. sub_0034E0A0 hands back
     * [device+0x1DE8], which only D3D's vblank handler (sub_00356DA0, inc at
     * 0x00356DC6) increments; the intro movie (ovid\englis30.xmv) presents a
     * frame only once that count has passed the frame's timestamp, so without
     * a vblank it holds on the first frame forever and the boot never gets
     * past it. Same field the HL2 loader drives (same XDK 5849 D3D8LTCG).
     *
     * With RECOMP_VBLANK (the fork's default here) D3D's handler runs and
     * moves the count itself; the runtime's per-flip increment on top made
     * it run at 60 + flips per second, so it is registered only where no
     * vblank is delivered. */
#if !defined(BO3_FORK_RUNTIME)
    xbox_Nv2aFrameCounter(0x0035FB48u, 0x1DE8u);
#endif

#if defined(BO3_HAVE_REPORTS)
    /* This thread runs the entry point; the hang watchdog starts with it. */
    xbox_crash_thread_register("host-main");
    xbox_crash_watchdog_start();
#endif

#if defined(BO3_HAVE_BUG_REPORT)
    xbox_BugReportArm();        /* RECOMP_DEBUG=bug_report=<seconds>[:shot] */
#endif

    printf("\nStarting game at entry point 0x001D2807...\n");
    RECOMP_ICALL_FEEDBACK_INIT();
    xbe_entry_point();

    printf("\nGame returned. Cleaning up...\n");
    xbox_OhciReport();
    RECOMP_ICALL_FEEDBACK_DUMP();
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);
    return 0;
}

/* What the window shows of the title's 640x480 frame, for the display.hud_fix
 * patches (config/patches.json, BO-89): the presenter's own source rectangle
 * (nv2a_present_layout), so another overscan or fit setting stays right.
 * vis_x0 / vis_x1: the columns the picture shows as it is presented;
 * ref_x0 / ref_x1: the columns a 4:3 picture shows in the same window, the
 * reference for "as far from the visible edge as in 4:3" (the 16:10 fill
 * crops a 16:9 picture's sides and not a 4:3 one's). In the title's 4:3 the
 * two are the same call, so the patched values are the title's. */
void nv2a_present_visible_x(uint32_t fw, uint32_t fh, double *x0, double *x1);
void nv2a_present_visible_x_at(uint32_t fw, uint32_t fh, int aspect, double *x0, double *x1);
void nv2a_present_note_output(uint32_t dw, uint32_t dh);
static int s_hud_wide;      /* the title is in its 16:9 mode */
static void hud_ref_x(double *x0, double *x1)
{
    if (s_hud_wide)
        nv2a_present_visible_x_at(640, 480, 1 /* NV2A_ASPECT_4_3 */, x0, x1);
    else
        nv2a_present_visible_x(640, 480, x0, x1);
}
/* The picture as the stage's frames will be presented: at its wide aspect
 * when display.ultrawide set one up (below), else as configured. */
void nv2a_present_visible_x_ratio(uint32_t fw, uint32_t fh, double ratio, double *x0, double *x1);
static volatile double s_uw_stage;
static double hud_vis_x0(void)
{
    double x0, x1;
    nv2a_present_visible_x_ratio(640, 480, s_uw_stage, &x0, &x1);
    return x0;
}
static double hud_vis_x1(void)
{
    double x0, x1;
    nv2a_present_visible_x_ratio(640, 480, s_uw_stage, &x0, &x1);
    return x1;
}
static double hud_ref_x0(void)
{
    double x0, x1;
    hud_ref_x(&x0, &x1);
    return x0;
}
static double hud_ref_x1(void)
{
    double x0, x1;
    hud_ref_x(&x0, &x1);
    return x1;
}

/* ---- display.ultrawide (BO-94) -------------------------------------------
 *
 * The title's 3D drawn for a picture wider than 16:9, its front end left as
 * it is. Only the title's own values change, through the guest patches of
 * config/patches.json; this is the state they read.
 *
 *   uw_stage_setup   a stage (race, crash, ...) is being set up: its camera
 *                    (sub_0002F380 / sub_0002F480) and the HUD's layout take
 *                    the wide aspect if the setting is on now. The choice
 *                    holds until the next set-up: a change shows from the
 *                    next race.
 *   uw_fe_setup      the front end sets its camera up: the title's 16:9.
 *   uw_stage_render  the stage drew the 3D world in this frame.
 *   uw_flip          the title emits a flip (NV097_FLIP_STALL): the frame
 *                    that ends here is wide if the stage drew in it. The
 *                    presenter asks by flip number (uw_frame_aspect) and shows
 *                    that frame at the wide aspect, any other at 16:9 in the
 *                    middle: a loading screen, a menu, a video are never
 *                    stretched, and the switch is on the frame, not near it.
 *
 * Leaving a stage, the title draws one more stage frame with the loading
 * screen already up (uw_fe_owns_picture: a 16:9 frame), then flips once
 * without drawing; a flip without a stage frame is shown as the frame before
 * it, once.
 *
 * The flips are counted here as the title emits them and by the GPU executor
 * as it reaches them; the same sequence, so the same numbers. A flip the
 * count has not reached (it cannot, unless a flip came from elsewhere) is
 * shown at 16:9. */
void nv2a_present_frame_aspect_fn(double (*fn)(uint32_t flip));
void nv2a_present_window_aspect(double aspect);
double nv2a_present_output_aspect(void);

#define UW_RING 64                      /* flips the presenter may lag by */
static double s_base_aspect = 4.0 / 3.0;    /* the title's: 4:3 or 16:9 */
static volatile double s_uw_stage;      /* the set-up stage's wide aspect, or 0 */
static volatile double s_uw_frame;      /* of the frame being drawn, or 0 */
static volatile uint32_t s_uw_flips;    /* flips the title emitted */
static float s_uw_ring[UW_RING];        /* wide aspect of flip n, or 0 */
static int s_uw_repeat;                 /* the repeated last frame was taken */
static int s_uw_log;

/* The setting's aspect now; 0: off (or the title is not in 16:9). */
static double uw_setting(void)
{
    const char *v = recomp_env(RENV_DISPLAY_ULTRAWIDE);
    double a = 0.0;

    if (!v || s_base_aspect < 1.5)
        return 0.0;
    if (!strcmp(v, "21:9"))
        a = 64.0 / 27.0;                /* 2560x1080, 3440x1440 */
    else if (!strcmp(v, "32:9"))
        a = 32.0 / 9.0;
    else if (!strcmp(v, "window"))
        a = nv2a_present_output_aspect();
    if (a > 4.0)
        a = 4.0;
    return a > s_base_aspect + 0.01 ? a : 0.0;
}

static void uw_stage_setup(void)
{
    double a = uw_setting();
    if (s_uw_log && a != s_uw_stage)
        fprintf(stderr, "UW: stage set-up, aspect %.4f (flip %u)\n", a, s_uw_flips);
    s_uw_stage = a;
}

static void uw_fe_setup(void)
{
    if (s_uw_log && s_uw_stage != 0.0)
        fprintf(stderr, "UW: front-end set-up (flip %u)\n", s_uw_flips);
    s_uw_stage = 0.0;
}

static volatile int s_stage_drew;       /* a stage drew in this frame (BO-222) */
static void uw_stage_render(void)
{
    s_uw_frame = s_uw_stage;
    s_stage_drew = 1;
    /* display.score_board (scoreboard.c, BO-165): before the HUD is drawn. */
    bo3_score_board_render();
}

/* BO-110 (HDR prototype, RECOMP_DEBUG=hdr): the start and the end of a
 * view's 3D (config/patches.json hdr_scene / hdr_ui), handed to the
 * toolkit as places in the push buffer: where the title's D3D will write
 * its next command, the device's m_pPut (D3D__pDevice at 0x35FB48, m_pPut
 * at +0; sub_0034CBF0 writes FLIP_STALL there). The executor runs behind
 * the guest, so the backend learns the change when its walk reaches that
 * word, in order with the draws around it. */
void nv2a_pb_stream_mark(uint32_t va, uint32_t tag);   /* toolkit nv2a_pb_state.h */
int  nv2a_host_hdr(void);                               /* toolkit nv2a_backend_common.h */
static void hdr_mark(uint32_t tag)
{
    uint32_t dev, put;
    /* graphics.hdr (live) or RECOMP_DEBUG=hdr: before, only the debug
     * switch marked, so the setting alone never gave the scene its light. */
    if (!nv2a_host_hdr())
        return;
    dev = HD_M32(0x35FB48);
    if (!dev)
        return;
    put = HD_M32(dev);
    if (put)
        nv2a_pb_stream_mark(put, tag);
}
static void hdr_scene(void) { hdr_mark(1); }
static void hdr_ui(void) { hdr_mark(0); }

/* ---- graphics.texture_upscale before the GO (BO-272) ----------------------
 *
 * The toolkit's upscaler runs its jobs with a larger budget while nobody
 * drives (nv2a_texup_set_loading): the track's textures are first drawn in
 * the countdown, so a first visit's jobs fit there instead of the race's
 * first minute. Told at every flip the title emits, from its own state:
 *   LOADING   the game object's state (0x4D53B4) is not 5, its running
 *             state: a load, a stage's set-up or hand-over (the loading
 *             screen); 0 before the game object exists.
 *   PRESTART  state 5 in a stage (its mode object, 0x4D5370 + 0x1B8, set;
 *             0 in the front end) with player 1's car (0x73A1D0) before its
 *             event phase 2 (+ 0x134C: 0 in the countdown of a Single Event
 *             race, seen 2026-10-21; 1 on a rolling start; 2 from the GO,
 *             3 finished; autopilot.c), for at most 30 s of it.
 *   PLAY      everything else.
 * Only an atomic in the toolkit changes; with texture upscale off nothing
 * reads it. */
void nv2a_texup_set_loading(int how);   /* toolkit nv2a_texup.h: 0 play, 1 prestart, 2 loading */

static double s_tl_pre_ms;              /* when PRESTART began, 0: not in it */

static void texup_loading_flip(void)
{
    uint32_t state, stage;
    int how = 0, pre;

    if (!g_xbox_mem_offset)
        return;
    state = HD_M32(0x4D53B4);
    stage = HD_M32(0x4D5370);
    pre = state == 5 && stage >= 0x10000u && stage < 0x08000000u && HD_M32(stage + 0x1B8u)
       && HD_M32(0x73A1D0u + 0x134Cu) < 2;
    if (state != 5 && state != 0) {
        how = 2;
    } else if (pre) {
        double now = pace_ms();
        if (!s_tl_pre_ms)
            s_tl_pre_ms = now;
        how = now - s_tl_pre_ms < 30000.0 ? 1 : 0;
    }
    if (!pre)
        s_tl_pre_ms = 0;
    {   /* RECOMP_DEBUG=texup_log: what it read, at each change */
        static uint32_t last = 0xFFFFFFFFu;
        uint32_t mode = stage >= 0x10000u && stage < 0x08000000u ? HD_M32(stage + 0x1B8u) : 0;
        uint32_t key = (state & 0xFF) | (mode ? 0x100u : 0) | (HD_M32(0x73A1D0u + 0x134Cu) & 0xFF) << 12
                     | (uint32_t)how << 24;
        if (key != last && recomp_env(RENV_TEXUP_LOG))
            fprintf(stderr, "[TEXUP] title state %u, stage %s, car phase %u: %s (flip %u)\n",
                    state, mode ? "on" : "none", HD_M32(0x73A1D0u + 0x134Cu),
                    how == 2 ? "loading" : how == 1 ? "prestart" : "play", s_uw_flips);
        last = key;
    }
    nv2a_texup_set_loading(how);
}

static void uw_flip(void)
{
    uint32_t n = s_uw_flips + 1;
    float a = (float)s_uw_frame, prev = s_uw_ring[(n - 1) % UW_RING];

    /* The first flip after a stage's last frame shows that frame again (the
     * title flips without drawing while it leaves the stage): wide still.
     * Once: the frame after is the loading screen's. */
    if (a == 0.0f && prev > 0.0f && !s_uw_repeat) {
        a = prev;
        s_uw_repeat = 1;
    } else if (a > 0.0f) {
        s_uw_repeat = 0;
    }
    if (s_uw_log && a != prev)
        fprintf(stderr, "UW: flip %u %s\n", n, a > 0.0f ? "wide" : "16:9");
    s_uw_ring[n % UW_RING] = a;
    s_uw_flips = n;
    texup_loading_flip();
    bo3_score_board_flip();
    pace_log(n);
    /* The simulation steps this frame ran (the stepper's count, +0x2E20C:
     * see BO3_PACE_LOG), for the overlay's frame-time graph: a frame of two
     * steps moves the world twice as far (present_cadence.h, BO-115). */
    if (g_xbox_mem_offset)
        xbox_PresentCadenceSteps(n, (int)HD_M32(0x4D53AC));
    s_uw_frame = 0.0;
    s_stage_drew = 0;
}

static double uw_frame_aspect(uint32_t flip)
{
    uint32_t n = s_uw_flips;
    if (flip == 0 || flip > n || n - flip >= UW_RING)
        return 0.0;
    return (double)s_uw_ring[flip % UW_RING];
}

/* "aspect": the picture a stage is laid out for (the HUD's rectangle, the
 * camera); "frame_aspect": the picture this frame is shown at (what is
 * drawn outside a stage too: the EA Trax pop-up); "fe_squeeze": the factor
 * that keeps the front-end scene's 16:9 shape when it is drawn over a wide
 * frame (the pause menu, the race intro, results), 1 otherwise. */
static double hud_aspect(void)
{
    double a = s_uw_stage;
    return a > 0.0 ? a : s_base_aspect;
}

static double hud_frame_aspect(void)
{
    double a = s_uw_frame;
    return a > 0.0 ? a : s_base_aspect;
}

/* The front-end scene (0x557A20) puts a picture that fills the screen (a
 * loading screen's, a menu's background) on its layer 0; what it draws over
 * the 3D world (pause menu, intro banner, results) never uses that layer.
 * Leaving a stage, the title draws one more stage frame with the loading
 * screen already up: the front end owns that picture, so the frame is not a
 * wide one (the 3D is behind the picture anyway) and is shown, like the
 * loading screens after it, at 16:9. Read from the scene as the title built
 * it (the layer array at +0x2C, a layer's first object at +8); asked when the
 * scene's root is synced, which is before anything of it is drawn. */
static int uw_fe_owns_picture(void)
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)g_xbox_mem_offset;
    uint32_t layers = *(const uint32_t *)(m + 0x557A20u + 0x2Cu);
    return layers && *(const uint32_t *)(m + layers + 8u) != 0;
}

static double uw_fe_squeeze(void)
{
    double a = s_uw_frame;
    if (a > 0.0 && uw_fe_owns_picture()) {
        s_uw_frame = 0.0;
        s_uw_repeat = 1;        /* and the flip after it repeats a 16:9 frame */
        return 1.0;
    }
    return a > 0.0 ? s_base_aspect / a : 1.0;
}

/* ---- display.hud_fix in the front end (BO-222) ---------------------------
 *
 * The front-end scene (0x557A20: menus, loading screens, race intro, pause
 * menu, results) is laid out in 640x480 like the HUD, and the anamorphic
 * 16:9 picture stretches it 4/3 wide. With display.hud_fix its root is
 * squeezed by (4/3)/base_aspect about x = 320 (config/patches.json,
 * sub_001C17A0), so the content keeps its 4:3 shape in the middle; over a
 * wide frame (display.ultrawide) that composes with fe_squeeze. The
 * decoration that the title spans across the whole frame (the borders, the
 * checkered strips, the header, the menu video, the loading picture, the
 * highlight line, full-screen fades) is drawn where the title puts it
 * instead, which is the whole picture again: one rule in the object draw
 * (sub_001C6500), by geometry, not by element.
 *
 *   fe_scene      mark at sub_001C17A0 (the root sync, the first thing
 *                 sub_001C67D0 does for a scene): edi is the scene drawn next.
 *   fe_dx, fe_dw  read at the object draw's stores of the drawn x and width
 *                 (ebx the object, its frame at +0x40): what to add. An
 *                 object of the front-end scene whose box, taken back to the
 *                 title's own columns (through the root's world and local
 *                 x), reaches from the left edge to the right one (<= 0.5,
 *                 >= 639.5) is drawn at those columns. One at least 400
 *                 wide that reaches one edge only (the loading screen's tip
 *                 bars, 140..640) gets that edge at the title's column and
 *                 keeps the other where the squeeze puts it. Everything else
 *                 (text, icons, the logo, panels) is left to the squeeze, and
 *                 so is the one box across the frame that is not decoration:
 *                 the selected line's brackets (render callback 0xFB900).
 *   base_aspect   the title's picture (4/3 or 16/9): the root's factor.
 *   stage_frame   1 while a stage drew the frame being built: the EA Trax
 *                 pop-up keeps its place beside the race HUD then, and sits
 *                 with the menu's squeezed content otherwise. */
static volatile uint32_t s_fe_scene;    /* the 2D scene being drawn */

static volatile uint32_t s_map_walk;    /* the map screen is to be looked for */

static void fe_scene_mark(void)
{
    s_fe_scene = g_edi;
    s_map_walk = 1;
}

static int fe_ok(uint32_t p)
{
    return p >= 0x10000u && p < 0x03FFFF00u && !(p & 3u);
}

/* ---- display.crashnav_map: the map screen with hud_fix (BO-222) ----------
 *
 * Crash Nav's Select Location (a region's map) is a picture of six tiles
 * (render callback 0x1C1A60, 512x512 each, 1536x1024 in all) on a frame of
 * its own, scaled to the 640 columns of the screen, with on that frame the
 * route line (0x1C0790, a box from the map's corner to the route's far
 * end), the crosshair lines through the chosen place (0x1C1930), the
 * places' shields (0xD77A0, the chosen one drawn once more over the info
 * box) and their NEW labels (0x5AC20); the info box beside the chosen place
 * (plates 0xF9CE0, text 0x1C51A0, the medal 0x1C1A60, the count 0x1C5330)
 * is on the root frame, placed in the title's columns from the place. The
 * squeeze takes the frame with the root, so every part stays on the map;
 * but a route box reaching 400 columns from the map's left edge met the
 * decoration's one-edge rule, and the route was drawn from the screen's
 * edge, over the frame beside the map (the owner's Alpine Expressway on
 * the Deck). The map screen is told by its route object, and its parts
 * never take that rule. The setting chooses its shape:
 *
 *   stretched  (default) the map is drawn at the title's own columns, like
 *              the frame around it: tiles, route and crosshair across the
 *              picture; the shields and labels at their places there but
 *              at their 4:3 shape (a label moves with its nearest shield),
 *              and the info box moved with the chosen place.
 *   native     the map 4:3 with the rest of the menu: the squeeze alone.
 *
 * In both, what the title keeps beyond the screen's edges on this screen
 * (a NEW label of the location list at column 740) stays there instead of
 * showing in the columns the squeeze opens beside the 4:3 content. */
#define MAP_ICONS 48
static uint32_t s_map_frame;            /* the map's frame; 0: not the map screen */
static int s_map_n;                     /* the shields: drawn centre, title-column shift */
static double s_map_cx[MAP_ICONS], s_map_cy[MAP_ICONS], s_map_d[MAP_ICONS];
static double s_map_box_d;              /* the info box's shift */

/* An object of the root frame that is part of the info box: one of its
 * callbacks, above the bottom panel (y < 416 in the root's 480 rows),
 * inside the 4:3 columns and narrower than the decoration. */
static int map_info_obj(uint32_t obj, double lo, double hi)
{
    uint32_t cb = HD_M32(obj + 0x44);
    if (cb != 0x000F9CE0u && cb != 0x001C51A0u && cb != 0x001C1A60u && cb != 0x001C5330u)
        return 0;
    return HD_MF(obj + 12) < 416.0f && lo > 0.5 && hi < 639.5 && hi - lo < 400.0;
}

/* Find the map screen in the front-end scene (once a frame, at its first
 * drawn object, when its frames are synced) and where its shields go. */
static void map_walk(uint32_t root, double rwx, double rsx, double rlx, double rlsx)
{
    uint32_t sc = 0x557A20u, la = HD_M32(sc + 0x2C), nl = HD_M32(sc + 0x30), l, n, fr;
    int guard, i, j, sel = -1, nbox = 0;
    double bx0 = 1e9, bx1 = -1e9, by0 = 1e9, by1 = -1e9;
    double px[MAP_ICONS], py[MAP_ICONS];

    s_map_frame = 0, s_map_n = 0, s_map_box_d = 0.0;
    if (!fe_ok(la) || !nl || nl > 256)
        return;
    for (l = 0; l < nl && !s_map_frame; l++) {
        if (HD_M32(la + l * 16) & 1u)           /* a hidden layer */
            continue;
        for (n = HD_M32(la + l * 16 + 8), guard = 0; fe_ok(n) && guard < 500; n = HD_M32(n), guard++)
            if (HD_M32(n + 0x44) == 0x001C0790u && fe_ok(HD_M32(n + 0x40))) {
                s_map_frame = HD_M32(n + 0x40);
                break;
            }
    }
    if (!s_map_frame)
        return;
    for (l = 0; l < nl; l++) {
        if (HD_M32(la + l * 16) & 1u)
            continue;
        for (n = HD_M32(la + l * 16 + 8), guard = 0; fe_ok(n) && guard < 500; n = HD_M32(n), guard++) {
            uint32_t cb = HD_M32(n + 0x44);
            float sx, sy, x, y, w, h;
            double a, b, ta, tb;

            fr = HD_M32(n + 0x40);
            if (!fe_ok(fr))
                continue;
            sx = HD_MF(fr + 0x28), sy = HD_MF(fr + 0x2C);
            x = HD_MF(n + 8), y = HD_MF(n + 12), w = HD_MF(n + 0x10), h = HD_MF(n + 0x14);
            a = HD_MF(fr + 0x20) + (double)sx * x, b = a + (double)sx * w;
            if (fr == s_map_frame && cb == 0x000D77A0u && s_map_n < MAP_ICONS) {
                double c = (a + b) / 2.0;
                s_map_cx[s_map_n] = c;
                s_map_cy[s_map_n] = HD_MF(fr + 0x24) + (double)sy * (y + h / 2.0f);
                s_map_d[s_map_n] = (c - rwx) / rsx * rlsx + rlx - c;
                px[s_map_n] = x, py[s_map_n] = y;
                s_map_n++;
            } else if (fr == root) {
                ta = (a - rwx) / rsx * rlsx + rlx, tb = (b - rwx) / rsx * rlsx + rlx;
                if (map_info_obj(n, ta < tb ? ta : tb, ta < tb ? tb : ta)) {
                    double ya = HD_MF(fr + 0x24) + (double)sy * y, yb = ya + (double)sy * h;
                    bx0 = fmin(bx0, fmin(a, b)), bx1 = fmax(bx1, fmax(a, b));
                    by0 = fmin(by0, fmin(ya, yb)), by1 = fmax(by1, fmax(ya, yb));
                    nbox++;
                }
            }
        }
    }
    /* The info box goes with the chosen place: the shield drawn twice;
     * else the shield nearest the box. */
    for (i = 0; i < s_map_n && sel < 0; i++)
        for (j = i + 1; j < s_map_n; j++)
            if (px[i] == px[j] && py[i] == py[j]) {
                sel = i;
                break;
            }
    if (sel < 0 && nbox) {
        double best = 1e18, cx = (bx0 + bx1) / 2.0, cy = (by0 + by1) / 2.0;
        for (i = 0; i < s_map_n; i++) {
            double ddx = s_map_cx[i] - cx, ddy = s_map_cy[i] - cy;
            if (ddx * ddx + ddy * ddy < best)
                best = ddx * ddx + ddy * ddy, sel = i;
        }
    }
    if (sel >= 0)
        s_map_box_d = s_map_d[sel];
}

/* The shift of the shield nearest (cx, cy), drawn columns and rows. */
static double map_near_d(double cx, double cy)
{
    double best = 1e18, d = 0.0;
    int i;
    for (i = 0; i < s_map_n; i++) {
        double ddx = s_map_cx[i] - cx, ddy = s_map_cy[i] - cy;
        if (ddx * ddx + ddy * ddy < best)
            best = ddx * ddx + ddy * ddy, d = s_map_d[i];
    }
    return d;
}

/* What to add to the drawn x (*dx) and width (*dw) of the object being
 * drawn; 0 and 0 for one the squeeze is right for. */
static void fe_decor(double *dx, double *dw)
{
    uint32_t obj = g_ebx, fr, root, cb;
    float sx, x, w;
    double rwx, rsx, rlx, rlsx, xa, xb, ta, tb, lo, hi, na, nb;
    int left, right, a_lo;

    *dx = *dw = 0.0;
    if (s_fe_scene != 0x557A20u || !g_xbox_mem_offset || !fe_ok(obj))
        return;
    cb = HD_M32(obj + 0x44);
    /* A box across the frame that is not decoration: the selected menu
     * line's brackets (sub_000FB900) sit beside its text, inside the box. */
    if (cb == 0x000FB900u)
        return;
    fr = HD_M32(obj + 0x40);
    root = HD_M32(0x557A20u + 0x14u);
    if (!fe_ok(fr) || !fe_ok(root))
        return;
    rwx = HD_MF(root + 0x20), rsx = HD_MF(root + 0x28);
    rlx = HD_MF(root), rlsx = HD_MF(root + 8);
    if (!(fabs(rsx) > 1e-6) || rsx == rlsx)     /* not squeezed (4:3) */
        return;
    if (s_map_walk) {
        s_map_walk = 0;
        map_walk(root, rwx, rsx, rlx, rlsx);
    }
    /* As sub_001C6500 computes them (in float): x = frame x + scale * x. */
    sx = HD_MF(fr + 0x28);
    x = HD_MF(obj + 8) * sx;
    x = x + HD_MF(fr + 0x20);
    w = HD_MF(obj + 0x10) * sx;
    xa = x, xb = (double)x + w;
    /* In the title's own columns: the root's squeeze undone. */
    ta = (xa - rwx) / rsx * rlsx + rlx;
    tb = (xb - rwx) / rsx * rlsx + rlx;
    a_lo = ta <= tb;
    lo = a_lo ? ta : tb, hi = a_lo ? tb : ta;
    if (!(lo == lo) || !(hi == hi))             /* NaN */
        return;
    if (s_map_frame) {
        int stretched = !recomp_patch_on("display.crashnav_map", "native", 0);

        if (hi <= 0.0 || lo >= 640.0) {         /* off the screen in 4:3: kept off */
            *dx = ta - xa;
            *dw = (tb - ta) - (xb - xa);
            return;
        }
        if (fr == s_map_frame) {
            if (!stretched)
                return;
            if (cb == 0x000D77A0u || cb == 0x0005AC20u) {
                /* A shield, or a label beside one: its place, its shape. */
                float sy = HD_MF(fr + 0x2C);
                double cy = HD_MF(fr + 0x24) + (double)sy * (HD_MF(obj + 12) + HD_MF(obj + 0x14) / 2.0f);
                *dx = map_near_d((xa + xb) / 2.0, cy);
                return;
            }
            *dx = ta - xa;                      /* the map itself */
            *dw = (tb - ta) - (xb - xa);
            return;
        }
        if (fr == root && map_info_obj(obj, lo, hi)) {
            if (stretched)
                *dx = s_map_box_d;
            return;
        }
    }
    left = lo <= 0.5, right = hi >= 639.5;
    if (!(left && right) && !((left || right) && hi - lo >= 400.0))
        return;
    na = xa, nb = xb;
    if (a_lo ? left : right) na = ta;
    if (a_lo ? right : left) nb = tb;
    *dx = na - xa;
    *dw = (nb - na) - (xb - xa);
}

static double fe_dx(void) { double dx, dw; fe_decor(&dx, &dw); return dx; }
static double fe_dw(void) { double dx, dw; fe_decor(&dx, &dw); return dw; }
static double fe_base_aspect(void) { return s_base_aspect; }
static double fe_stage_frame(void) { return s_stage_drew ? 1.0 : 0.0; }

static void uw_init(void)
{
    const char *v = recomp_env(RENV_DISPLAY_ULTRAWIDE);

    s_uw_log = getenv("BO3_UW_LOG") != NULL;
    recomp_patch_set_var_fn("aspect", hud_aspect);
    recomp_patch_set_var_fn("frame_aspect", hud_frame_aspect);
    recomp_patch_set_var_fn("fe_squeeze", uw_fe_squeeze);
    recomp_patch_set_var_fn("base_aspect", fe_base_aspect);
    recomp_patch_set_var_fn("stage_frame", fe_stage_frame);
    recomp_patch_set_var_fn("fe_dx", fe_dx);
    recomp_patch_set_var_fn("fe_dw", fe_dw);
    nv2a_present_frame_aspect_fn(uw_frame_aspect);
    /* A new window opens at the chosen shape (the title's 16:9 otherwise). */
    if (v && s_base_aspect > 1.5) {
        if (!strcmp(v, "21:9"))
            nv2a_present_window_aspect(64.0 / 27.0);
        else if (!strcmp(v, "32:9"))
            nv2a_present_window_aspect(32.0 / 9.0);
    }
}

/* The enhancements (src/enhance in the apexJCL runtime): enhance.toml in the
 * per-user settings folder, else beside the executable (bin/; install.h),
 * RECOMP_* over it, every key stock by default;
 * config/enhance.toml.default lists them. Called once the environment
 * defaults are in and before the guest starts: the backends, the window and
 * the kernel read settled values, and the kernel bridge copies the AV pack
 * into guest memory.
 *
 * display.widescreen and display.hdtv are the dashboard's video settings,
 * which the kernel reports and the title reads itself: XGetVideoFlags
 * (sub_001CF913) at device setup, sub_0003C8A0, bit 0 -> widescreen, which
 * sub_0003D690 turns into D3DPRESENTFLAG_WIDESCREEN and sub_0002F380 into a
 * 16:9 projection; bit 3 (480p, HDTV pack only) -> D3DPRESENTFLAG_PROGRESSIVE.
 * The back buffer is 640x480 in every case, so 16:9 is anamorphic and
 * present.aspect stretches it. Without the layer this is nothing. */
static void host_enhance_init(void)
{
#ifdef RECOMP_ENV_HAVE_ENHANCE_KEYS
    xbox_enhance_init(bo3_enhance_root(), NULL);
    /* The picture's shape, as the title gets it at device setup, for the
     * float patches of config/patches.json (display.hud_fix, BO-89). */
    s_hud_wide = enhance_cfg_bool("display.widescreen", 0);
    s_base_aspect = s_hud_wide ? 16.0 / 9.0 : 4.0 / 3.0;
    uw_init();
    enhance_cfg_report_unused();
    fflush(stderr);
#endif
}

/* ── the bug report (BO-99) ────────────────────────────────────────── */
#if defined(BO3_HAVE_BUG_REPORT)
#if !defined(_WIN32) || defined(XBOXRECOMP_WIN_VULKAN)
#define BO3_REPORT_PROBE 1      /* xbox_HostReportProbe: the SDL hosts' video_player.h */
#endif

static const char *s_report_out;    /* --out <dir>; NULL: <save dir>/reports */
static int s_report_standalone;     /* no title in this process (--bug-report) */
static char s_report_note[1024];    /* where it was made from, for README.txt */
static char s_report_path[1200];    /* the last archive, or why none */

/* What every way to a report calls (the toolkit's xbox_BugReportMake): this
 * title's name, folders and variables, the rest is the toolkit's. */
static int host_bug_report(int screenshot, char *out, unsigned n)
{
    static const char *const prefixes[] = { "RECOMP_", "BO3_", NULL };
    xbox_bug_report_opts o;

    memset(&o, 0, sizeof o);
    o.name = "takedawn";
    o.title = BO3_APP_NAME " (Burnout 3: Takedown, static recompilation), game " BO3_BUILD_COMMIT
              ", built " __DATE__ " " __TIME__;
    o.save_dir = s_save_dir;
    o.out_dir = s_report_out;
    o.env_prefixes = prefixes;
    o.screenshot = screenshot && !s_report_standalone;
    o.no_current_run = s_report_standalone;
    o.note = s_report_note[0] ? s_report_note : NULL;
    return xbox_BugReport(&o, out, n);
}

static const char *report_kernel_log(void) { return "xbox_kernel.log"; }
#if defined(__ANDROID__)
static const char *report_env_txt(void) { return "env.txt"; }
#endif

/* Is the game data there, and whole? Names and sizes of the disc's top
 * folder only; nothing of the data goes into a report. */
static void report_game_data(xbox_report_buf *b)
{
    FILE *f = fopen(s_xbe_path, "rb");
    long size = -1;

    if (f) {
        if (fseek(f, 0, SEEK_END) == 0)
            size = ftell(f);
        fclose(f);
    }
    if (size >= 0)
        xbox_report_printf(b, "default.xbe: %s, %ld bytes\n", s_xbe_path, size);
    else
        xbox_report_printf(b, "default.xbe: %s: NOT FOUND (the game cannot start without"
                           " it)\n", s_xbe_path);
    xbox_report_dir_summary(b, "game data", BO3_GAME_DIR);
}

/* standalone: nothing else runs in this process, so the listing may start
 * SDL's video and controllers itself and ask the graphics API. */
static void host_report_init(int standalone)
{
    s_report_standalone = standalone;
    xbox_BugReportSetMaker(host_bug_report);
    if (!standalone)
        xbox_BugReportRuntime();
#ifdef XBOXRECOMP_HAVE_OVERLAY
    xbox_SettingsReportRegister();
#endif
#if defined(BO3_REPORT_PROBE)
#if defined(__ANDROID__)
    /* An app has SDL only while its activity runs the title. */
    if (!standalone)
        xbox_HostReportProbe(0);
#else
    xbox_HostReportProbe(standalone);
#endif
#endif
    xbox_report_section("system.txt", "game data (names and sizes only)", report_game_data);
    xbox_report_attach("logs/xbox_kernel.log", report_kernel_log, 8192, 24576);
#if defined(__ANDROID__)
    xbox_report_attach("env.txt", report_env_txt, 16384, 0);
#endif
}

/* --bug-report [--out <dir>]: the archive without starting the title. The
 * settings and the defaults are in place (main got that far), so the report
 * shows what a start would use; no log is rotated, no backup made, no window
 * opened. */
static int host_bug_report_cli(void)
{
    s_save_dir = recomp_save_dir(BO3_USER_DIR);
    host_report_init(1);
    if (!s_report_note[0])
        snprintf(s_report_note, sizeof s_report_note,
                 "Made from the command line (--bug-report): no title was running.");
    if (!xbox_BugReportMake(0, s_report_path, sizeof s_report_path)) {
        XR_ERROR( "bug report: %s\n", s_report_path);
        return 1;
    }
    XR_OUT("%s\n", s_report_path);     /* the answer of --bug-report: not logging */
    return 0;
}
#endif /* BO3_HAVE_BUG_REPORT */

/* ── the save out of the profile and back (BO-123) ─────────────────── */

/* The title's folder under UDATA and TDATA: its title id, as its XAPI
 * spells it. */
#define BO3_TITLE_ID      "4541005b"
/* In the player's home folder. */
#define BO3_EXPORT_FOLDER "Takedawn-saves"

static char s_import_report[2600];

/* The import the overlay staged (recomp_save_dir.h), first thing in a start:
 * before the old-name folder is looked for (bo3_user_dirs_init: a save root
 * that holds the note is already the new-named one), before
 * recomp_save_dir makes or names the root for anyone, before the settings
 * are read, and before host_saves_init's backup rotation, which then backs
 * up the imported save. The root is found the way recomp_save_dir finds
 * it, without making it: no folder is created by a start that has nothing
 * to import. */
static void save_import_at_start(int argc, char **argv)
{
#if defined(BO3_HAVE_REPORTS)
    char root[1024];
    const char *ov = getenv("RECOMP_SAVE_DIR");
    int i;

    /* The shader pre-build and its scenes play on saves of their own: an
     * import waits for the game. */
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--build-shaders") || !strcmp(argv[i], "--build-shaders-scene"))
            return;

    if (ov && *ov)
        snprintf(root, sizeof root, "%s", ov);
    else if (!recomp_user_dir(RECOMP_DIR_DATA, BO3_USER_DIR, root, sizeof root))
        return;
    recomp_save_import_apply(root, BO3_TITLE_ID, s_import_report, sizeof s_import_report);
    xbox_SaveImportCleanup(root);       /* the Xbox import's converted copy (BO-209) */
#endif
}

/* BO-209: --import-save <folder or .zip>: a Burnout 3 save from an original
 * Xbox or xemu is signed again for the port and put in place, with the save in
 * use backed up first. A program of its own, like --version: no game starts.
 * 0 imported, 1 not. */
static int import_save_cli(const char *path)
{
#if defined(BO3_HAVE_REPORTS)
    char root[1024], out[1024], info[400], err[1400], report[2600], backup[1024];
    const char *ov = getenv("RECOMP_SAVE_DIR");
    int r;

    if (!path || !*path) {
        fprintf(stderr, "usage: --import-save <save folder, a folder that holds it, or a .zip>\n");
        return 1;
    }
    if (!(ov && *ov))
        bo3_user_dirs_init();           /* the old-name folder moves first, as in a start */
    if (ov && *ov)
        snprintf(root, sizeof root, "%s", ov);
    else if (!recomp_user_dir(RECOMP_DIR_DATA, BO3_USER_DIR, root, sizeof root)) {
        fprintf(stderr, "Import failed: the save folder could not be found.\n");
        return 1;
    }
    recomp_fs_mkdir_p(root);
    if (recomp_instance_lock(root, 0) == 0) {
        fprintf(stderr, "Import failed: Takedawn is running with this save folder. Close it first.\n");
        return 1;
    }
    if (!xbox_SaveImportPrepare(path, BO3_TITLE_ID, root, root, out, sizeof out, info, sizeof info,
                                err, sizeof err)) {
        fprintf(stderr, "Import failed: %s\n", err);
        return 1;
    }
    if (!recomp_save_import_stage(root, out, BO3_TITLE_ID, err, sizeof err)) {
        fprintf(stderr, "Import failed: %s\n", err);
        xbox_SaveImportCleanup(root);
        return 1;
    }
    /* The usual backup first (the start-up one: UDATA and TDATA to backup/<date>), then the
     * import, which also sets the save in use aside in save-before-import/. */
    if (recomp_save_backup(root, (int)recomp_env_int(RENV_SAVE_BACKUPS, 5), backup, sizeof backup) && backup[0])
        printf("[SAVE] backup: %s\n", backup);
    r = recomp_save_import_apply(root, BO3_TITLE_ID, report, sizeof report);
    xbox_SaveImportCleanup(root);
    if (r != 1) {
        fprintf(stderr, "Import failed: %s\n", report[0] ? report : "nothing was imported.");
        return 1;
    }
    printf("%s\n%s\nStart Takedawn to play it.\n", info, report);
    return 0;
#else
    (void)path;
    fprintf(stderr, "--import-save is not in this build.\n");
    return 1;
#endif
}

/* BO-129: what the title writes under UDATA/TDATA goes to a new file beside
 * the old one and takes its place whole at the last close (the toolkit's
 * kernel_saveguard.h). Before the first file of a save takes its place, the
 * save as it still is goes to <save root>/save-history (recomp_save_dir.h),
 * once for the files one save writes together: a save writes its meta file
 * and the profile within a moment, so a copy is not taken again within a
 * few seconds of the last. On the guard's own thread: nothing the game
 * draws or plays waits for it. */
static double s_history_last;

static double save_now_s(void)
{
#if defined(_WIN32)
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

static void save_before_replace(void)
{
    char made[1024];
    double t0 = save_now_s();

    if (s_history_last > 0 && t0 - s_history_last < 3.0)
        return;
    s_history_last = t0;
    if (recomp_save_history_take(s_save_dir, BO3_TITLE_ID, made, sizeof made))
        fprintf(stderr, "[SAVE] the save before this one is kept: %s (%.1f ms)\n", made,
                (save_now_s() - t0) * 1000.0);
}

static void save_guard_init(void)
{
    xbox_SaveGuardEnable(BO3_TITLE_ID);
    xbox_SaveGuardSetHook(save_before_replace);
}

/* The save folder, by platform (recomp_save_dir.h): macOS
 * ~/Library/Application Support/Takedawn, Linux ~/.local/share/takedawn,
 * Windows %APPDATA%\Takedawn; RECOMP_SAVE_DIR replaces it. The folder of the
 * port's earlier name, Burnout3, was moved there (bo3_user_dirs_init). Saves from
 * before it (./saves, where the working directory kept them) are copied
 * over once. Then a backup of the title's data, before it can write, and the
 * crash reports' home, <save dir>/crash. After host_enhance_init: this reads
 * the settled environment. */
static void host_saves_init(int argc, char **argv)
{
#if defined(BO3_HAVE_REPORTS)
    char backup[1024], settings[8192], cwd[1024] = "?";
    int n = 0, i, keep;

    s_save_dir = recomp_save_dir(BO3_USER_DIR);
    /* BO-129: one running copy per save root. A second one on the same save
     * would hold the profile in memory too and write over the first one's
     * progress when it saves. */
    if (recomp_instance_lock(s_save_dir, 5000) == 0) {
        static const char msg[] = "Takedawn is already running (with the same save folder).\n\n"
            "Close it first: two copies of the game on one save would each write over the "
            "other's progress.";
        fprintf(stderr, "[SAVE] %s\n[SAVE] save folder: %s\n", msg, s_save_dir);
        if (!recomp_env(RENV_HEADLESS)) {
#if !defined(_WIN32) || defined(XBOXRECOMP_WIN_VULKAN)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Takedawn", msg, NULL);
#else
            MessageBoxA(NULL, msg, "Takedawn", MB_OK | MB_ICONWARNING);
#endif
        }
        exit(3);
    }
    if (!recomp_env(RENV_SAVE_DIR) && recomp_save_dir_migrate(BO3_SAVE_DIR, s_save_dir))
        XR_NOTICE( "[SAVE] copied ./%s to %s (the old folder is no longer "
                        "read)\n", BO3_SAVE_DIR, s_save_dir);
    xbox_crash_init(s_save_dir);
    keep = (int)recomp_env_int(RENV_SAVE_BACKUPS, 5);
    if (keep > 0 && recomp_save_backup(s_save_dir, keep, backup, sizeof backup))
        XR_NOTICE("[SAVE] backup: %s (keeping %d)\n", backup, keep);
    XR_NOTICE("[SAVE] dir: %s\n", s_save_dir);
    if (s_import_report[0])
        XR_NOTICE("[SAVE] %s\n", s_import_report);
#ifdef XBOXRECOMP_HAVE_OVERLAY
    {
        /* The overlay's Export / Import rows: to and from a folder in the
         * player's home (on Android the app's own external files folder). */
        static char dest[1024], note[200];
        char home[900];
        if (recomp_home_dir(home, sizeof home)) {
            xbox_save_io io;
            snprintf(dest, sizeof dest, "%s/%s", home, BO3_EXPORT_FOLDER);
            snprintf(note, sizeof note, "from %.150s", bo3_version_long());
            io.save_dir = s_save_dir;
            io.title_id = BO3_TITLE_ID;
            io.export_dir = dest;
            io.note = note;
            xbox_OverlaySetSaveIO(&io);
        }
    }
#endif
    /* BO-131: Unlock everything (unlockall.c); after the save root is known. */
    bo3_unlock_all_start();
#if defined(BO3_HAVE_BUG_REPORT)
    host_report_init(0);
#endif

#if defined(_WIN32)
    if (!_getcwd(cwd, sizeof cwd))
        snprintf(cwd, sizeof cwd, "?");
#else
    if (!getcwd(cwd, sizeof cwd))
        snprintf(cwd, sizeof cwd, "?");
#endif
    n += snprintf(settings + n, sizeof settings - (size_t)n,
                  "%s\nBurnout 3: Takedown, built %s %s\ncommand line:", bo3_version_long(),
                  __DATE__, __TIME__);
    for (i = 0; i < argc && n < (int)sizeof settings - 256; i++)
        n += snprintf(settings + n, sizeof settings - (size_t)n, " %s", argv[i]);
    n += snprintf(settings + n, sizeof settings - (size_t)n,
                  "\nworking directory: %s\nxbe: %s\nsave dir: %s\n",
                  cwd, s_xbe_path, s_save_dir);
#ifdef RECOMP_ENV_HAVE_ENHANCE_KEYS
    {
        int f;
        for (f = 0; f < 2; f++) {
            const recomp_cfg *c = enhance_cfg_file(f);
            size_t k;
            if (!c)
                continue;
            n += snprintf(settings + n, sizeof settings - (size_t)n, "\n[%s]\n",
                          recomp_cfg_name(c));
            for (k = 0; k < recomp_cfg_count(c) && n < (int)sizeof settings - 256; k++) {
                const char *key = recomp_cfg_key_at(c, k);
                const char *env = enhance_cfg_source_of(key) == ENHANCE_SRC_ENV
                                ? "   (the environment overrides it)" : "";
                switch (recomp_cfg_type_of(c, key)) {
                case RCFG_STRING:
                    n += snprintf(settings + n, sizeof settings - (size_t)n, "%s = \"%s\"%s\n",
                                  key, recomp_cfg_string(c, key, ""), env);
                    break;
                case RCFG_INT:
                    n += snprintf(settings + n, sizeof settings - (size_t)n, "%s = %lld%s\n",
                                  key, recomp_cfg_int(c, key, 0), env);
                    break;
                case RCFG_FLOAT:
                    n += snprintf(settings + n, sizeof settings - (size_t)n, "%s = %g%s\n",
                                  key, recomp_cfg_float(c, key, 0), env);
                    break;
                case RCFG_BOOL:
                    n += snprintf(settings + n, sizeof settings - (size_t)n, "%s = %s%s\n",
                                  key, recomp_cfg_bool(c, key, 0) ? "true" : "false", env);
                    break;
                default:
                    n += snprintf(settings + n, sizeof settings - (size_t)n,
                                  "%s = [...]%s\n", key, env);
                    break;
                }
            }
        }
    }
#endif
    xbox_crash_set_settings(settings);
#else
    (void)argc; (void)argv;
#endif
}

#if defined(__ANDROID__)
/* Android (docs/android.md): SDL's activity loads this library and calls
 * SDL_main on a thread of its own. */
#include "platform/android_host.h"
__attribute__((visibility("default"))) int SDL_main(int argc, char **argv);
#define main SDL_main
#endif

#if defined(_WIN32) && defined(BO3_HAVE_UTF8_ARGS)
static int s_utf8_args;
#endif

/* --seconds N: the window layer posts the close request after N seconds
 * (its RECOMP_WINDOW_QUIT_AFTER), the event the window's close button gives.
 * The variables are read once, so the cached value is set too. */
#if !defined(_WIN32) || defined(XBOXRECOMP_WIN_VULKAN)
static void set_quit_after(const char *seconds)
{
#if defined(BO3_HAVE_REPORTS)
    recomp_env_set(RENV_WINDOW_QUIT_AFTER, seconds);
#elif defined(_WIN32)
    _putenv_s("RECOMP_WINDOW_QUIT_AFTER", seconds);
#else
    setenv("RECOMP_WINDOW_QUIT_AFTER", seconds, 1);
#endif
}
#endif

int main(int argc, char **argv)
{
    const char *record = NULL, *sb_scene = NULL, *data_arg = NULL;
    unsigned seconds = 0;
    char seconds_text[16] = "0";
    int headless = 0, i, sb_build = 0, bug_report = 0, xbe_named = 0, no_setup = 0;

#if defined(_WIN32)
    /* A release build is a window program (no console of its own): started
     * from a terminal without redirection, write to that terminal. */
    {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if ((out == NULL || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
    }
#endif
#if defined(_WIN32) && defined(BO3_HAVE_UTF8_ARGS)
    /* Every argument as UTF-8, from the wide command line (the executable's
     * manifest makes the process's code page UTF-8, so the narrow file
     * calls and the environment are UTF-8 too): --data, --install and
     * --dest, --music-dir, --cache-dir, --record, --out and the xbe may name
     * a folder in any script. On a Windows too old for the manifest the
     * arguments stay in its ANSI code page, as before. After the console is
     * attached: its code page is set with it. */
    s_utf8_args = recomp_win_utf8_args(&argc, &argv);
#endif
#if defined(BO3_HAVE_RESTART)
    /* BO-124: the arguments, directory and environment as given, for the
     * overlay's "Restart game"; its exit handler is registered here, before
     * any other, so it runs after all of them. */
    recomp_restart_init(argc, argv);
#endif
    /* BO-123: a save import asked for in the overlay, before anything else
     * of this start looks at the save root. */
    save_import_at_start(argc, argv);
    /* What needs none of the runtime and none of the game's data: the
     * version, and putting the data in place (install.h). */
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version"))
            return bo3_version_main();
        if (!strcmp(argv[i], "--import-save"))
            return import_save_cli(i + 1 < argc ? argv[i + 1] : NULL);
#if !defined(__ANDROID__)
        /* --install and --data-status take the whole line but their own
         * word, so that --data (or --dest) may stand before or after them:
         * "--data <dir> --data-status" and "--data-status --data <dir>" are
         * the same question. */
        if (!strcmp(argv[i], "--install") || !strcmp(argv[i], "--data-status")) {
            static char *rest[64];
            int n = 0, k, install = argv[i][2] == 'i';
            for (k = 1; k < argc && n < 63; k++)
                if (k != i)
                    rest[n++] = argv[k];
            rest[n] = NULL;
            return install ? bo3_install_main(n, rest) : bo3_data_status_main(n, rest);
        }
#endif
        if (!strcmp(argv[i], "--build-shaders"))
            break;                      /* the rest of the line is the pre-build's */
    }
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--bug-report"))
            bug_report = 1;
    /* The print gate (the toolkit's xr_log.h, BO-125): a release build says
     * only errors and the few notices a player or a report needs, unless
     * diagnostic logging is on (Advanced > Diagnostic logging,
     * RECOMP_LOG_DIAG=1) or a developer switch asks for the log. Here: after
     * the stand-alone programs above (--version, --install, --data-status,
     * whose output is theirs), before the first line of the game's own and
     * before any RECOMP_* default is set, so only what the user gave counts. */
    {
        int hl = 0;
        for (i = 1; i < argc; i++)
            if (!strcmp(argv[i], "--headless"))
                hl = 1;
#if defined(BO3_RELEASE_BUILD)
        xr_log_init(1, hl);
#else
        xr_log_init(0, hl);
#endif
    }
#if defined(__ANDROID__)
    /* The log to logcat and <files>/takedawn.log, and the working directory
     * to the app's files directory, where "Burnout 3 Takedown" is. A report
     * from the start page keeps the game's log file as the last run left it. */
    recomp_android_host_init("takedawn", bug_report ? NULL : "takedawn.log");
#endif
#if defined(BO3_HAVE_BUG_REPORT)
    /* Before any default is set: which variables the user's environment
     * had, for the report's "source" column. */
    xbox_report_env_snapshot();
#endif

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless"))
            headless = 1;
        else if (!strcmp(argv[i], "--bug-report"))
            ;                           /* seen above */
#if defined(BO3_HAVE_BUG_REPORT)
        else if (bug_report && !strcmp(argv[i], "--out") && i + 1 < argc)
            s_report_out = argv[++i];
#endif
#ifdef BO3_HAVE_SHADERBUILD
        /* --build-shaders [options]: the shader pre-build, which takes the
         * rest of the line (shaderbuild.h). --build-shaders-scene <name> is
         * how it starts one scene of it. */
        else if (!strcmp(argv[i], "--build-shaders")) {
            int k;
            sb_build = i + 1;
            for (k = i + 1; k + 1 < argc; k++)
                if (!strcmp(argv[k], "--data")) {
                    data_arg = argv[k + 1];     /* after the word as well as before it */
                } else if (!strcmp(argv[k], "--cache-dir")) {
#if defined(_WIN32)
                    _putenv_s("RECOMP_RENDER_SHADER_CACHE", argv[k + 1]);
#else
                    setenv("RECOMP_RENDER_SHADER_CACHE", argv[k + 1], 1);
#endif
                }
            break;
        } else if (!strcmp(argv[i], "--build-shaders-scene") && i + 1 < argc) {
            sb_scene = argv[++i];
            headless = 1;
        }
#endif
        else if (!strcmp(argv[i], "--data") && i + 1 < argc)
            data_arg = argv[++i];
        else if (!strcmp(argv[i], "--no-setup"))
            no_setup = 1;
        /* --music-dir <folder>: audio.custom_music_dir for this run (BO-108). */
        else if (!strcmp(argv[i], "--music-dir") && i + 1 < argc) {
#if defined(_WIN32)
            _putenv_s("RECOMP_AUDIO_MUSIC_DIR", argv[++i]);
#else
            setenv("RECOMP_AUDIO_MUSIC_DIR", argv[++i], 1);
#endif
        }
        else if (!strcmp(argv[i], "--record") && i + 1 < argc)
            record = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            seconds = (unsigned)strtoul(argv[++i], NULL, 10);
            snprintf(seconds_text, sizeof seconds_text, "%u", seconds);
        }
        else if (argv[i][0] == '-') {
            const char *self = getenv("APPIMAGE");
            if (!self || !*self)
                self = argv[0];
            XR_ERROR( "usage: %s [--headless] [--data <dir>] [--no-setup] "
#if defined(_WIN32) && !defined(XBOXRECOMP_WIN_VULKAN)
                            "[--record out.mp4] "    /* the old GDI window's */
#endif
                            "[--seconds N] [--music-dir <folder>] [default.xbe]\n"
                            "       %s --install <image-or-folder> [--dest <dir>] [--check]\n"
                            "       %s --data-status [--data <dir>]\n"
                            "       %s --build-shaders [--cache-dir <dir>] [--scenes a,b]"
                            " [--force] [--status]\n"
                            "       %s --bug-report [--out <dir>]\n"
                            "       %s --import-save <Xbox save folder or .zip>\n"
                            "       %s --version\n",
                    self, self, self, self, self, self, self);
            return 2;
        } else {
            s_xbe_path = argv[i];
            xbe_named = 1;
        }
    }

    /* The game's data, which becomes the working directory. Without it
     * there is no title to start: the setup window for a person, exit
     * status 3 for an unattended start (install.h). With it, and before
     * anything opens a per-user folder, the folders of the port's earlier
     * name are moved (in bo3_data_enter): a start that runs no title, and
     * the bug report, move nothing. */
    bo3_user_dirs_names();
    {
        int rc, status_only = 0;
#ifdef BO3_HAVE_SHADERBUILD
        for (i = sb_build; sb_build && i < argc; i++)
            status_only = status_only || !strcmp(argv[i], "--status");
#endif
        /* The report reads the saves and settings where they are now: under
         * the old name while their move is still to come. */
        if (bug_report && bo3_user_dirs_pending(NULL, 0) && bo3_save_dir_now()[0])
            env_default("RECOMP_SAVE_DIR", bo3_save_dir_now());
        rc = bo3_data_enter(data_arg, xbe_named,
                                (bug_report ? BO3_DATA_QUIET : 0)
                                | (status_only ? BO3_DATA_NO_MOVE : 0)
                                | (no_setup || headless || sb_build || sb_scene
                                   ? BO3_DATA_NO_SETUP : 0));
        if (rc)
            return rc;
    }

#ifdef BO3_HAVE_SHADERBUILD
    /* The pre-build itself runs no title: it needs the enhancements layer
     * (which names the shader cache) and nothing else of the runtime. */
    if (sb_build) {
        host_enhance_init();
        return bo3_shaderbuild_main(argc - sb_build, argv + sb_build, s_xbe_path);
    }
    /* One scene of it: the variables of an unattended run, before anything
     * reads the environment. */
    if (sb_scene && !bo3_shaderbuild_scene_env(sb_scene))
        return 2;
#else
    (void)sb_build; (void)sb_scene;
#endif

    /* RECOMP_STDIO_LOG=<path>: stdout and stderr to a file, truncated, before
     * anything prints. As blinx2-recomp's host_stdio_setup (MIT): a
     * GUI-subsystem Windows exe has no stderr descriptor to dup onto, so
     * both streams are reopened in append mode there. */
    {
        const char *log = getenv("RECOMP_STDIO_LOG");
        if (log && *log) {
#if defined(_WIN32)
            FILE *trunc = fopen(log, "w");
            if (trunc)
                fclose(trunc);
            if (freopen(log, "a", stdout))
                freopen(log, "a", stderr);
#else
            if (freopen(log, "w", stdout))
                dup2(fileno(stdout), fileno(stderr));
#endif
        }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#if defined(BO3_HAVE_BUG_REPORT)
    /* The log tee from here on, so the run log has the settings' and the
     * enhancements' lines too (host_saves_init opens the file). */
    if (!bug_report)
        xbox_crash_log_start();
#endif
    if (!bug_report)
        XR_NOTICE( "[BOOT] %s\n%s", bo3_version_long(), bo3_user_dirs_notes());
#if defined(BO3_HAVE_RESTART)
    if (!bug_report)
        XR_NOTICE("%s", recomp_restart_note());
#endif
#if defined(_WIN32) && defined(BO3_HAVE_UTF8_ARGS)
    if (!bug_report)
        fprintf(stderr, s_utf8_args
                ? "[BOOT] text is UTF-8 (arguments, environment, paths)\n"
                : "[BOOT] this Windows has no UTF-8 code page for programs (needs Windows 10 1903):"
                  " a path with letters outside its regional code page may not open\n");
#endif

#ifdef XBOXRECOMP_HAVE_OVERLAY
    /* The overlay's settings.toml, in the save folder, into the environment:
     * a RECOMP_* variable the user set wins over it, it wins over the
     * defaults below (xbox_settings.h). */
    xbox_SettingsInit(BO3_USER_DIR);
    /* The saved diagnostic_log is in force from here (BO-193); the one line
     * says the state and what decided it. */
    if (!bug_report)
        xr_log_announce();
    xbox_OverlaySetAbout(bo3_version_long());
    /* The menu's own fonts for the overlay (BO-80): GlobalFont and HeadFont,
     * the two DXT5 atlases and glyph tables inside the US retail
     * default.xbe, read from the player's copy at start. Another file (by
     * size and SHA-256, the install manifest's) or none: the overlay keeps
     * its own font. */
    {
        static const xbox_overlay_bitmap_font global_font = { 0x3b9198, 182, 0x3ba958, 256, 256 };
        static const xbox_overlay_bitmap_font head_font = { 0x3ca978, 47, 0x3cb058, 128, 256 };
        xbox_OverlaySetGameFonts(s_xbe_path, 4308992ull,
                                 "9f497acd82adb8edfb72cc31aeeb35010b45d32b13a3f1fc6540717ff24d13f9",
                                 &global_font, &head_font);
    }
    /* The menu's own pictures, for the overlay's sprites (BO-80): the arrows,
     * the value boxes' corners, the tick bar, the d-pad, the grid and the arc,
     * and the button prompts, small DXT images inside Data/Global.txd of the
     * player's install (offsets of the NTSC-U file, its size and SHA-256 from
     * the install manifest), with the port's own drawings of the prompts for
     * the other pads. Another file, or none: the overlay keeps its drawn look. */
    {
        static const xbox_overlay_sprite art[XBOX_OVERLAY_ART_COUNT] = {
            { 0x2480, 64, 32, 5 },      /* Arrow */
            { 0x5fe00, 128, 64, 5 },    /* Buttons */
            { 0x79700, 32, 16, 5 },     /* dpad */
            { 0x9d200, 16, 16, 1 },     /* grid (DXT1) */
            { 0x8b80, 128, 128, 5 },    /* big_curve */
            { 0x5f500, 32, 32, 5 },     /* box_curve */
            { 0x5f980, 32, 32, 5 },     /* box_curveb */
            { 0xc80, 16, 16, 5 },       /* 16_curve */
            { 0xe00, 16, 16, 5 },       /* 16_curveb */
        };
        char txd[1100];
        const char *slash = strrchr(s_xbe_path, '/');
#if defined(_WIN32)
        const char *bs = strrchr(s_xbe_path, '\\');
        if (bs && (!slash || bs > slash))
            slash = bs;
#endif
        snprintf(txd, sizeof txd, "%.*sData/Global.txd", slash ? (int)(slash - s_xbe_path + 1) : 0, s_xbe_path);
        {
            unsigned n_sheets = 0;
            const struct nv2a_texrep_variant *sheets = bo3_glyphs_sheets(&n_sheets);
            xbox_OverlaySetPadSheets(sheets, n_sheets);
        }
        /* More of them (BO-190): the AUDIO page's volume bar, the tick and
         * the ring for the switches (Global.txd), the padlock for a setting
         * the environment fixes (Data/Frontend.txd, its own size and
         * SHA-256 from the manifest). */
        {
            static const char global_sha[] = "fbee55d041d16b71b59ec3f460f0c580e8299bb303b9fe1dd92f77a90d0744a9";
            static const char front_sha[] = "b5d4dd9b5e2249ab4b135343a0eb031f9e444d17a65c8bccaafdaa4bd0c9c1cd";
            char front[1100];
            snprintf(front, sizeof front, "%.*sData/Frontend.txd", slash ? (int)(slash - s_xbe_path + 1) : 0,
                     s_xbe_path);
            xbox_overlay_sprite_file more[XBOX_OVERLAY_ART2_COUNT];
            more[XBOX_OVERLAY_ART2_VOLUMEBAR] = (xbox_overlay_sprite_file){ txd, 1470080ull, global_sha, 0x155d80, 128, 32, 8 };
            more[XBOX_OVERLAY_ART2_TICK] = (xbox_overlay_sprite_file){ txd, 1470080ull, global_sha, 0x144b80, 64, 64, 5 };
            more[XBOX_OVERLAY_ART2_CIRCLE] = (xbox_overlay_sprite_file){ txd, 1470080ull, global_sha, 0x69f00, 32, 32, 5 };
            more[XBOX_OVERLAY_ART2_PADLOCK] = (xbox_overlay_sprite_file){ front, 7067008ull, front_sha, 0x355c80, 32, 32, 8 };
            xbox_OverlaySetGameArtMore(more);
        }
        xbox_OverlaySetGameArt(txd, 1470080ull,
                               "fbee55d041d16b71b59ec3f460f0c580e8299bb303b9fe1dd92f77a90d0744a9", art);
    }
    /* The menu's own sounds for the overlay (BO-190): raw mono 16-bit PCM
     * waves in sound/Fe.awd and sound/Generic.awd of the player's install
     * (offsets, lengths and rates from the files' wave tables; sizes and
     * SHA-256 from the install manifest). Which the title plays for what,
     * from a trace of its menus (RECOMP_MIX_TRACE): Vertical for a cursor
     * move, a value change and a page that comes in (the pause menu opening),
     * Select for A, Back for B and for the pause menu closing. The levels are
     * the ones it plays them at (front end, effects volume as shipped).
     * Another file, or none: the overlay stays silent. */
    {
        static const char fe_sha[] = "7032da847168891e2873cd07a198840f86e744fbb278fef9eaac2e32922baac6";
        static const char gen_sha[] = "94ef859b45ccd54ebd9266ce94259d50d2af628582cfa5758b4c66b8a87f0c1c";
        char fe[1100], gen[1100];
        const char *slash = strrchr(s_xbe_path, '/');
#if defined(_WIN32)
        const char *bs = strrchr(s_xbe_path, '\\');
        if (bs && (!slash || bs > slash))
            slash = bs;
#endif
        const int dl = slash ? (int)(slash - s_xbe_path + 1) : 0;
        snprintf(fe, sizeof fe, "%.*ssound/Fe.awd", dl, s_xbe_path);
        snprintf(gen, sizeof gen, "%.*ssound/Generic.awd", dl, s_xbe_path);
        const xbox_overlay_sound vertical = { fe, 3180544ull, fe_sha, 0x430a0, 15912, 32000, -15.5f };
        const xbox_overlay_sound select = { gen, 1681408ull, gen_sha, 0x175d00, 29376, 22050, -16.8f };
        const xbox_overlay_sound back = { fe, 3180544ull, fe_sha, 0x7d030, 35584, 32000, -22.8f };
        xbox_overlay_sound cues[XBOX_OVERLAY_SND_COUNT];
        cues[XBOX_OVERLAY_SND_MOVE] = vertical;
        cues[XBOX_OVERLAY_SND_CHANGE] = vertical;
        cues[XBOX_OVERLAY_SND_SELECT] = select;
        cues[XBOX_OVERLAY_SND_BACK] = back;
        cues[XBOX_OVERLAY_SND_OPEN] = vertical;
        cues[XBOX_OVERLAY_SND_CLOSE] = back;
        cues[XBOX_OVERLAY_SND_PAGE] = vertical;
        xbox_OverlaySetMenuSounds(cues, "UI/HUD");      /* the mixer's bus (mixer.c) */
    }
    /* The Advanced page's "Check for updates" (BO-100): the one request to
     * GitHub's latest-release record is made only when that row is pressed.
     * TAKEDAWN_UPDATE_REPO points it at another repository, for the tests of
     * the "a newer version is out" answer. Not on Android: no row there. */
    {
        const char *repo = getenv("TAKEDAWN_UPDATE_REPO");
        xbox_OverlaySetUpdateCheck(repo && repo[0] ? repo : "xenophobentx/takedawn", bo3_version_number());
    }
#if defined(__linux__) && !defined(__ANDROID__)
    /* "Add Steam library art" (BO-143): for a player who updated and never sees
     * the setup window's Add to Steam step. Only where the setup program is. */
    if (bo3_steam_art_available())
        xbox_OverlaySetSteamArt(bo3_steam_art_run);
#endif
#if defined(__ANDROID__)
    xbox_OverlaySetHostRows(3, android_driver_row);
#endif
#else
    if (!bug_report)
        xr_log_announce();   /* no settings file in this build: the build default */
#endif
    /* The opt-in guest patches of config/patches.json (BO-79): the US disc's
     * own metric display, behind the language index. Off (mph and miles, as
     * shipped) until game.speed_units / RECOMP_SPEED_UNITS says kmh. */
    recomp_patch_enable("game.speed_units", RENV_SPEED_UNITS);
    /* BO-89: the 2D HUD in 16:9. The title lays its HUD out in 640x480 and
     * the anamorphic picture stretches it 4/3 wide; the patches squeeze the
     * HUD's own 2D scene about the screen centre and widen its layout
     * rectangle to match, so elements keep their shape at the true screen
     * edges. Off (as shipped) until display.hud_fix / RECOMP_GFX_HUD_FIX=1;
     * in 4:3 the patched values equal the title's. "aspect" is 4:3 until
     * host_enhance_init knows the picture's shape. */
    recomp_patch_set_var("aspect", 4.0 / 3.0);
    /* The columns of the 640-wide frame the window shows (the overscan crop
     * takes 21 a side, the 16:10 fill more): the HUD's margins are kept to
     * the visible edges, as far from them as in 4:3. */
    recomp_patch_set_var_fn("vis_x0", hud_vis_x0);
    recomp_patch_set_var_fn("vis_x1", hud_vis_x1);
    recomp_patch_set_var_fn("ref_x0", hud_ref_x0);
    recomp_patch_set_var_fn("ref_x1", hud_ref_x1);
    /* BO3_HUD_OUTPUT=<w>x<h>: a headless run lays the HUD out as for a
     * window of that size (the 16:10 fill on a 1280x800 panel), for frame
     * dumps; a window's own size wins. */
    {
        const char *o = getenv("BO3_HUD_OUTPUT");
        unsigned ow = 0, oh = 0;
        if (o && sscanf(o, "%ux%u", &ow, &oh) == 2 && ow && oh)
            nv2a_present_note_output(ow, oh);
    }
    recomp_patch_enable("display.hud_fix", RENV_GFX_HUD_FIX);
    /* BO-222: the Crash Nav map's shape under display.hud_fix (fe_decor):
     * stretched (default) or native, display.crashnav_map. */
    recomp_patch_enable("display.crashnav_map", RENV_DISPLAY_CRASHNAV_MAP);
    recomp_patch_enable("display.ultrawide", RENV_DISPLAY_ULTRAWIDE);
    /* BO-101: the title's own bloom and blur (radial while boosting,
     * directional in the crash and takedown cameras), on as shipped. At
     * graphics.bloom / graphics.motion_blur = false (RECOMP_GFX_BLOOM=0,
     * RECOMP_GFX_MOTION_BLUR=0) the title's own conditions for drawing them
     * read false; docs/effects.md. */
    recomp_patch_enable("graphics.bloom", RENV_GFX_BLOOM);
    recomp_patch_enable("graphics.motion_blur", RENV_GFX_MOTION_BLUR);
    /* BO-212: both are strengths 0..1; between, the bloom's weight in the
     * frame pass and the blur's opacity are times these. */
    recomp_patch_set_var_fn("bloom_strength", bloom_strength);
    recomp_patch_set_var_fn("blur_strength", blur_strength);
    /* BO-111: the title halves its frame rate (presentation interval 2, two
     * simulation steps a frame) while a crash or takedown camera is on. At
     * graphics.crash_60fps = true (RECOMP_GFX_CRASH_60FPS=1) the stage's
     * update reads "no crash camera" where it chooses the rate and takes
     * its own full-rate path; off as shipped. docs/crash60.md. */
    recomp_patch_enable("graphics.crash_60fps", RENV_GFX_CRASH_60FPS);
    /* BO-116: two-player split screen, which the title runs at the same half
     * rate from the stage's set-up on and without its music. At
     * graphics.split_60fps = true (RECOMP_GFX_SPLIT_60FPS=1) the set-up's
     * request for the half rate is stored as "no"; at audio.split_music =
     * true (RECOMP_AUDIO_SPLIT_MUSIC=1) the music manager reads one player
     * where it decides to stay silent. Both off as shipped.
     * docs/split-screen.md. */
    recomp_patch_enable("graphics.split_60fps", RENV_GFX_SPLIT_60FPS);
    recomp_patch_enable("audio.split_music", RENV_AUDIO_SPLIT_MUSIC);
    /* Frames at the display's refresh rate, the simulation at its own 60
     * steps a second and the pictures between steps blended. At
     * graphics.high_refresh = true (RECOMP_GFX_HIGH_REFRESH=1) with both
     * switches above on; off as shipped. Not a guest patch: wraps of the
     * stepper's frame, RenderWare's frame sync, the vehicles' draw and the
     * HUD (hirefresh.c); registered here so the overlay shows its row. */
    recomp_patch_enable("graphics.high_refresh", RENV_GFX_HIGH_REFRESH);
    /* BO-159: a one-player stage starts the title's simulation stepper in its
     * one-step-a-frame mode, so a host below 60 plays the race in slow
     * motion until the first crash camera ends (the title then switches to
     * its wall-clock mode itself). At
     * game.race_clock = true (RECOMP_GAME_RACE_CLOCK=1) the start stores the
     * wall-clock mode itself; off as shipped. docs/race-clock.md. */
    recomp_patch_enable("game.race_clock", RENV_GAME_RACE_CLOCK);
    /* Soft-body vehicle damage, drawn only: vehicles crumple, tear and bend
     * where they are hit. At graphics.softbody = true
     * (RECOMP_GFX_SOFTBODY=1), read at start (softbody.c); off as shipped.
     * Not a guest patch: marks the damage model hooks; registered here so
     * the overlay shows its row. docs/softbody.md. */
    recomp_patch_enable("graphics.softbody", RENV_GFX_SOFTBODY);
    /* BO-112: the distances at which the title swaps a race car's or a
     * traffic vehicle's model for a simpler one (15/30/60 m and 20/80 m),
     * times 2 or 4 at graphics.vehicle_detail = far / farthest
     * (RECOMP_GFX_VEHICLE_DETAIL); as shipped at "original".
     * docs/draw-distance.md. */
    recomp_patch_set_var_fn("vehicle_lod_scale", vehicle_lod_scale);
    recomp_patch_enable("graphics.vehicle_detail", RENV_GFX_VEHICLE_DETAIL);
    /* BO-177: the distance at which a track prop with two models (the pine
     * trees above all) swaps its detailed model for its simple one, times
     * SCENERY_FAR_SCALE at graphics.scenery_detail = far
     * (RECOMP_GFX_SCENERY_DETAIL); as shipped at "original".
     * docs/draw-distance.md. */
    recomp_patch_set_var_fn("scenery_lod_scale", scenery_lod_scale);
    recomp_patch_enable("graphics.scenery_detail", RENV_GFX_SCENERY_DETAIL);
    /* BO-241: where the marker over a rival changes from its arrow to its
     * race position: farther and with a band at display.rival_marker =
     * farther (RECOMP_GFX_RIVAL_MARKER); as shipped at "original".
     * docs/rival-marker.md. */
    recomp_patch_set_var_fn("rival_near", rival_near);
    recomp_patch_set_var_fn("rival_far", rival_far);
    recomp_patch_enable("display.rival_marker", RENV_GFX_RIVAL_MARKER);
    recomp_patch_on_mark("uw_stage_setup", uw_stage_setup);
    recomp_patch_on_mark("uw_fe_setup", uw_fe_setup);
    recomp_patch_on_mark("uw_stage_render", uw_stage_render);
    recomp_patch_on_mark("uw_flip", uw_flip);
    recomp_patch_on_mark("fe_scene", fe_scene_mark);
    recomp_patch_on_mark("hdr_scene", hdr_scene);
    recomp_patch_on_mark("hdr_ui", hdr_ui);
    hud_dump_start();
    hud_mon_start();
    fx_at_start();
    drive_log_start();
    pace_log_start();
    /* BO-106: input.control_scheme (controls.c). */
    bo3_controls_start(bo3_flips);
    /* BO-130: RECOMP_DEBUG=autopilot=1 (autopilot.c). */
    bo3_autopilot_start(bo3_flips);
    /* graphics.softbody (softbody.c). */
    bo3_softbody_start(bo3_flips);
    /* BO-133: interface.menu_entry (menuentry.c). */
    bo3_menu_entry_start();
    /* A or START leaves a training film (tutorial.c). */
    bo3_tutorial_start();
    /* BO-165: display.score_board (scoreboard.c). */
    bo3_score_board_start(bo3_flips);
    /* The defaults go in before anything reads the environment: the runtime
     * reads it once, at the first lookup.
     *
     * Audio has to come up for the engine to start. RenderWare creates its
     * audio device during engine start (sub_0020A540 -> DirectSoundCreate),
     * and when that fails the NULL device is handed on until the pipeline
     * builder faults on it. DirectSound needs the AC'97 codec to report ready
     * (RECOMP_AC97_READY) and then waits for the DSP to clear a command
     * doorbell it writes 3 to; with no DSP56300 here the runtime acknowledges
     * it. The doorbell is a contiguous allocation, so its address is per-title
     * data, observed as ebx+0x810 in the DirectSound spin at 0x002F8029.
     * Set either variable yourself to override. */
    env_default("RECOMP_AC97_READY", "1");
#if defined(BO3_FORK_RUNTIME)
    env_default("RECOMP_APU_DSP_ACK", "auto");
    env_default("RECOMP_VBLANK", "1");
    /* The title steps its simulation a fixed amount per frame
     * (sub_001B58E0 at 0x00016C31: at least one step a frame), so it must
     * not flip faster than the console's vblank allows: 60 Hz, 30 in the
     * modes that ask for interval two. */
    env_default("RECOMP_FLIP_PACING", "vblank");
    /* The title draws 448 lines (clip y=16..464) into its 640x480 buffer and
     * leaves the rest black, for a TV's overscan to hide; show the picture
     * the way that TV did (Metal and Vulkan presenters). */
    env_default("RECOMP_PRESENT_OVERSCAN", "crop");
#if !defined(__ANDROID__)
    /* The picture of a first start (BO-103). These are defaults like the
     * others: a settings.toml that names a value keeps it, a RECOMP_*
     * variable wins over both, no file is written or rewritten, and 4:3, a
     * window and 1x stay in the overlay's Display and Graphics pages. A
     * --headless run (the scene scripts, the shader pre-build) keeps the
     * title's own picture, 640x480 in 4:3.
     *
     * Every platform class gets the same picture, said out loud for each
     * (the owner's decision of 2026-10-12): 16:9 with the HUD fix (the 2D
     * kept in shape, BO-89), the linear filter, SMAA, sharpening 0.30 (CAS),
     * and Menu/HUD upscaling 2x (graphics.ui_scale, docs/ui-scale.md). The
     * classes differ in the render scale and in how the frame meets the
     * screen.
     *
     * A Steam Deck, and any machine under gamescope: full screen; two times
     * the internal resolution (960 rows; it was 3x, 1440 rows, until
     * 2026-10-12); the 16:9 frame shown whole, letterboxed on the 16:10
     * panel (display.fit "fill16x10" crops its sides by a twentieth each and
     * stays in the overlay). Measured on the Deck in Game Mode, the Balanced
     * default, a 90 s race, at 3x with the nearest filter and without the
     * post passes: flips p50 16.67 / p95 16.78 / p99 16.84 ms, 0.17 % over
     * 20 ms, APU 9.2 W (at 1x: p99 16.70, 7.5 W). Not re-measured with SMAA,
     * sharpening and the 2D upscaling at 2x.
     *
     * A desktop (macOS, Windows, Linux outside gamescope), packaged program
     * only: borderless full screen at the desktop's resolution, three times
     * the internal resolution (1440 rows; it was 2x until 2026-10-12; 1, 2
     * and 4 are in the overlay). A development build opens the title's own 4:3 window
     * as it always did.
     *
     * Both also get texture sharpness (graphics.lod_bias "auto": the mip
     * bias follows the render scale, -0.5 at 2x, -1.0 at 4x) and vehicle
     * detail "far" (graphics.vehicle_detail, 2x the title's model-swap
     * distances, docs/draw-distance.md); anisotropy stays 8x (below), dither
     * off. The owner played several races on the Deck with them.
     *
     * Android has its own entries below (anisotropy 4x, texture sharpness
     * and vehicle detail as the title ships them). */
    if (!headless) {
        if (host_is_steam_deck() || host_is_gamescope()) {
            env_default("RECOMP_PRESENT_FULLSCREEN", "1");
            env_default("RECOMP_DISPLAY_WIDESCREEN", "1");
            env_default("RECOMP_GFX_HUD_FIX", "1");
            env_default("RECOMP_PRESENT_FIT", "letterbox");
            env_default("RECOMP_PRESENT_FILTER", "linear");
            env_default("RECOMP_RENDER_SCALE", "2");
            env_default("RECOMP_GFX_AA", "smaa");
            env_default("RECOMP_GFX_SHARPEN", "0.30");
            env_default("RECOMP_GFX_UI_SCALE", "2x");
            env_default("RECOMP_GFX_LOD_BIAS", "auto");
            env_default("RECOMP_GFX_VEHICLE_DETAIL", "far");
            env_default("RECOMP_GFX_SCENERY_DETAIL", "far");
        }
#if defined(BO3_RELEASE_BUILD)
        else {
            env_default("RECOMP_PRESENT_FULLSCREEN", "1");
            env_default("RECOMP_DISPLAY_WIDESCREEN", "1");
            env_default("RECOMP_GFX_HUD_FIX", "1");
            env_default("RECOMP_PRESENT_FILTER", "linear");
            env_default("RECOMP_RENDER_SCALE", "3");
            env_default("RECOMP_GFX_AA", "smaa");
            env_default("RECOMP_GFX_SHARPEN", "0.30");
            env_default("RECOMP_GFX_UI_SCALE", "2x");
            env_default("RECOMP_GFX_LOD_BIAS", "auto");
            env_default("RECOMP_GFX_VEHICLE_DETAIL", "far");
            env_default("RECOMP_GFX_SCENERY_DETAIL", "far");
        }
#endif
    }
#endif
    /* Anisotropic filtering of the mipmapped textures (BO-82): 8x until the
     * Deck's watt measurement says otherwise. */
#if defined(__ANDROID__)
    /* A phone GPU: 4x (docs/android.md); the overlay's Graphics page has the
     * rest. */
    env_default("RECOMP_GFX_ANISO", "4");
    /* An app's window is the screen: the Fullscreen setting starts on, so
     * the overlay does not take the window out of it. */
    env_default("RECOMP_PRESENT_FULLSCREEN", "1");
    /* 960x720 inside: the 640x480 picture is too coarse on a 1080-row
     * handheld panel (the user's verdict on the Retroid Pocket 5); 2 and 1
     * are in the overlay. */
    env_default("RECOMP_RENDER_SCALE", "1.5");
    /* The same picture as the other platforms (the owner's decision of
     * 2026-10-12): 16:9 with the HUD fix, the linear filter, sharpening
     * 0.30, and Menu/HUD upscaling 2x (nv2a_texscale_held() is 0 here since
     * the toolkit's f3c436e: no hold on Android by default). FXAA, the
     * light anti-aliasing pass, by the owner's choice for a phone GPU. */
    env_default("RECOMP_DISPLAY_WIDESCREEN", "1");
    env_default("RECOMP_GFX_AA", "fxaa");
    env_default("RECOMP_GFX_HUD_FIX", "1");
    env_default("RECOMP_PRESENT_FILTER", "linear");
    env_default("RECOMP_GFX_SHARPEN", "0.30");
    env_default("RECOMP_GFX_UI_SCALE", "2x");
    /* MAILBOX, not FIFO: the title is paced by the display-locked vblank
     * (the Choreographer's refresh times), one frame a refresh, so nothing
     * tears; with FIFO the presenter sat in dequeueBuffer most of every
     * frame and a 2x race had 16 % slow frames against 1.5 % (BO-85). */
    env_default("RECOMP_PRESENT_VSYNC", "0");
#else
    env_default("RECOMP_GFX_ANISO", "8");
#endif
    /* Rumble (BO-98; the toolkit's rumble_curve.h). The title's strongest
     * rumble is 52054 of 65535 (a crash, then 18349, 5734, 1791 as it dies
     * away): that is full on the host motor. The strength is the curve's
     * exponent, motor = (speed / 52054) ^ (1 / strength): 1.0 is the Xbox
     * pad's own, and each host gets the lift its motors want. Full is full
     * at any strength, so a crash and a scrape stay apart. */
    env_default("RECOMP_INPUT_RUMBLE_PEAK", "52054");
#if defined(__ANDROID__)
    /* The device's own vibrator (the toolkit's android_vibration.c), weak
     * beside a pad's motors: the Retroid Pocket 5. */
    env_default("RECOMP_INPUT_RUMBLE_GAIN", "2.0");
#else
    /* A desktop pad 1.5; the Steam Deck's own haptics, between a pad's
     * motors and a phone's vibrator, 1.75. */
    env_default("RECOMP_INPUT_RUMBLE_GAIN", host_is_steam_deck() ? "1.75" : "1.5");
#endif
#if defined(__APPLE__)
    /* The runtime's executor defaults to the CPU rasteriser, which runs
     * the 3D on the host CPU; Metal is the backend this port is built for. */
    env_default("RECOMP_PB_BACKEND", "metal");
    /* Balanced (power.mode = performance), BO-75: sleep in the title's waits
     * and spin the last 8 ms before each frame. Measured on the Mac (two
     * 180 s races each): p95 16.72-16.73, p99 16.76-16.77, CPU 66 % against
     * Stock's 122-127 % at p95 16.95-16.97; no added latency. Maximum stays
     * in the overlay, and a stored power.mode still wins. */
    env_default("RECOMP_PRESENT_PACING", "sleep");
    env_default("RECOMP_PRESENT_SPIN_LEAD", "8");
#elif defined(XBOXRECOMP_HAVE_VULKAN)
    /* Native Linux (Steam Deck): the toolkit's Vulkan backend, the Metal
     * backend's port (BO-19). */
    env_default("RECOMP_PB_BACKEND", "vulkan");
    /* Measured on the Steam Deck (BO-19/BO-83): the emulated vblank locked
     * to the panel's refresh; the title's waits sleep, and the last 8 ms
     * before each vblank spin, which keeps the shared CPU clock up for the
     * executor (the firmware drops every core to ~2.3 GHz when they idle;
     * a util clamp does nothing on amd-pstate-epp). A steady 60 in races
     * at about 10 W, against 14 W spinning throughout. */
    env_default("RECOMP_VBLANK_SYNC", "display");
    env_default("RECOMP_PRESENT_PACING", "sleep");
    env_default("RECOMP_PRESENT_SPIN_LEAD", "8");
#endif
#else
    env_default("RECOMP_APU_DSP_ACK", "0x825F8810");
#endif
    /* The pad. Without the USB controller model there is no way past
     * "Press START"; with it the host's XInput pad (or RECOMP_PAD_SCRIPT /
     * RECOMP_PAD_LIVE) drives the title's own XPP driver. */
    env_default("RECOMP_USB", "1");
    /* Two controllers: Burnout 3's split-screen modes are two-player and
     * are greyed out unless a second pad is connected. Pad 2 follows host
     * XInput pad 2, or "p2-" steps in RECOMP_PAD_LIVE / RECOMP_PAD_SCRIPT. */
    env_default("RECOMP_USB_PADS", "2");
    /* The NV2A executor: without it nothing is drawn. RECOMP_PB_EXEC=0 turns
     * it off (the runtime tests only for presence), which runs the boot ~50x
     * faster when following it by file accesses. */
    if (!getenv("RECOMP_PB_EXEC"))
        env_default("RECOMP_PB_EXEC", "1");
    else if (!strcmp(getenv("RECOMP_PB_EXEC"), "0")) {
#if defined(_WIN32)
        _putenv_s("RECOMP_PB_EXEC", "");
#else
        unsetenv("RECOMP_PB_EXEC");
#endif
    }

    /* Before host_enhance_init(): it snapshots the environment, so a
     * default set after it is never seen (--headless opened a window). */
#if defined(_WIN32) && !defined(XBOXRECOMP_WIN_VULKAN)
    if (!headless)
        env_default("RECOMP_FB_WINDOW", "1");
#else
    if (headless)
        env_default("RECOMP_HEADLESS", "1");
#endif
#ifdef XBOXRECOMP_HAVE_OVERLAY
    xbox_SettingsFinish();              /* the last default is in */
#endif
    bo3_pcache_manifest_register(s_xbe_path);  /* BO-208: the shipped shader coverage list */
    host_enhance_init();
    bo3_glyphs_init();                  /* input.glyphs: the prompts for the pad in use */
    bo3_led_signs_init();               /* the tunnel LED signs stay lit (BO-176) */
    bo3_hdr_sun_init();                 /* the sun's disc, a soft profile with HDR (BO-203) */
    bo3_hdr_gains_init();               /* the boost's light brighter with HDR (BO-110) */
    bo3_texup_roads_init();             /* the road surfaces texture upscale leaves alone (BO-245) */
    if (bug_report) {
#if defined(BO3_HAVE_BUG_REPORT)
        return host_bug_report_cli();
#else
        XR_ERROR( "this build has no bug report (the toolkit has no xbox_report.h)\n");
        return 2;
#endif
    }
#if defined(BO3_HAVE_BUG_REPORT)
    /* A pre-build scene is not a play session: it leaves the run logs alone. */
    if (sb_scene)
        xbox_crash_set_run_log(0);
#endif
    host_saves_init(argc, argv);
#ifdef BO3_HAVE_SHADERBUILD
    if (sb_scene)
        bo3_shaderbuild_scene_start(s_xbe_path);
    else
        bo3_shaderbuild_host_init(headless);
#endif

#if defined(_WIN32)
    /* Symbols up front: at fault time SymInitialize would have to allocate
     * inside a process that is already in a bad way. */
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    AddVectoredExceptionHandler(1, veh_handler);
    hang_watchdog_start();
#if defined(XBOXRECOMP_WIN_VULKAN)
    /* Windows + Vulkan: the SDL window owns the process main thread, as on
     * every other host (--record is the old GDI window's). */
    if (record)
        XR_ERROR( "[BOOT] --record is not implemented on "
                        "this host; use RECOMP_FB_DUMP\n");
    if (seconds)    /* the window layer posts the close request, as its button does */
        set_quit_after(seconds_text);
    xbox_HostWindowSetTitle(BO3_APP_NAME);
    return xbox_HostWindowMain(host_main);
#else
    if (record)
        record_start(record, seconds);
    return host_main();
#endif
#else
    /* --record is the Windows host's; RECOMP_FB_DUMP works everywhere. */
    if (record)
        XR_ERROR( "[BOOT] --record is not implemented on "
                        "this host; use RECOMP_FB_DUMP\n");
    if (seconds)    /* the window layer posts the close request, as its button does */
        set_quit_after(seconds_text);
    xbox_HostWindowSetTitle(BO3_APP_NAME);
    return xbox_HostWindowMain(host_main);
#endif
}

#if defined(__ANDROID__) && defined(BO3_HAVE_BUG_REPORT)
/* ReportActivity.nativeBugReport (android/java): the report from the start
 * page, where no title runs. The activity lives in a process of its own
 * (android:process=":report" in the manifest), so nothing this leaves in the
 * environment or in the runtime's tables reaches a game started later. It is
 * --bug-report: main, as far as the settings and the defaults. `note` says
 * what the start page found (missing data, the device's Vulkan version).
 * Returns the archive's path, or "!" and the reason. */
#include <jni.h>
JNIEXPORT jstring JNICALL
Java_io_github_xenophobentx_takedawn_ReportActivity_nativeBugReport(JNIEnv *env, jclass cls,
                                                                    jstring note)
{
    static char arg0[] = "burnout3", arg1[] = "--bug-report";
    char *argv[] = { arg0, arg1, NULL };
    char result[1300];
    const char *t = note ? (*env)->GetStringUTFChars(env, note, NULL) : NULL;

    (void)cls;
    snprintf(s_report_note, sizeof s_report_note, "%s", t ? t : "Made from the start page.");
    if (t)
        (*env)->ReleaseStringUTFChars(env, note, t);
    s_report_path[0] = 0;
    if (SDL_main(2, argv) == 0)
        snprintf(result, sizeof result, "%s", s_report_path);
    else
        snprintf(result, sizeof result, "!%s", s_report_path[0] ? s_report_path
                                                                : "the report could not be made");
    return (*env)->NewStringUTF(env, result);
}
#endif
