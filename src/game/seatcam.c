/*
 * seatcam.c - BO3_SEATCAM=<right>,<up>,<ahead>: the race is drawn from a fixed seat in the
 * player's car instead of the title's camera (a test for a VR driving view; docs/vr-openxr-proposal.md,
 * "Camera views"). Off unless set. "1" takes the default seat: on the centre line, 0.66 m above the
 * car's origin (where the title's own low front camera sits), 0.15 m ahead of it.
 *
 * How: the stage's render (sub_0001A6A0) begins by building the camera's frame and the view matrix from the
 * game's camera object (sub_001C8A60; the race's view is at 0x4D6200, the camera object at its + 0x68: + 0x20 the
 * orientation as a quaternion (x, y, z, w), + 0x30 the position; the first attempt wrote matrix rows there and
 * the world was gone; hirefresh.c). The "uw_stage_render"
 * mark is that function's entry, so bo3_seatcam_apply() writes the seat there, and bo3_seatcam_restore() puts
 * the camera object back at the next flip (the title's camera follows its own state from step to step; our
 * values must not leak into it). A mark runs only its first handler, so main.c calls both.
 *
 * The seat is the car's own axes: the camera turns, pitches and rolls with the body. The car's matrix has the
 * LEFT axis first (row 0), then up, then forward (docs/wheel-force-feedback-proposal.md); right = -row 0.
 *
 * Applied only to a race in play (phase 2) with the car driving (+0x3D0 is 4) and the title's camera within
 * 8 m of the car (a chase or front view, not a crash camera or a replay). BO3_SEATCAM_LOG=1 prints the camera
 * object and the car's axes a few times, to check the layout above.
 *
 * Looking around (a stand-in for the headset's tracking, to see what the stage looks like from the seat
 * sideways): the pad's RIGHT stick, which the title ignores in a race, or on Windows the keys J and L (left and
 * right) and I and K (up and down). It turns the camera about the seat point, up to 150 degrees sideways and
 * 60 up or down, easing back to the front when let go.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <stdarg.h>

#include "seatcam.h"
#include "seatcam_addr.h"            /* the guest addresses, for each disc */
#include "platform/recomp_patch.h"
#include "recomp_types.h"

extern RECOMP_TLS uint32_t g_esi;

extern ptrdiff_t g_xbox_mem_offset;
int xbox_VrInit(void);                  /* toolkit src/video/oxr.h */
typedef struct xbox_vr_pose {           /* the same layout as oxr.h's */
    float    q[4], p[3], ipd, vwx, vwy;
    unsigned id;
} xbox_vr_pose;
int xbox_VrPose(xbox_vr_pose *out);
void xbox_VrRecentre(void);
void xbox_VrFrame(unsigned id, float vwx, float vwy);

#define SC_PTR(va)  ((float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define SC_M32(va)  (*(const uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define SC_MAX_D  8.0f
#define SC_FRONT_D 2.5f                /* the title camera nearer than this to the car: its front view (no car drawn) */
#define SC_LOOK_YAW   (150.0f * 3.14159265f / 180.0f)
#define SC_LOOK_PITCH (60.0f * 3.14159265f / 180.0f)

static int s_on, s_log;
static FILE *s_lf;                      /* BO3_SEATCAM_LOG=<file>: the diagnostics go here (the game's own stderr is not in the log file) */

static void slog(const char *fmt, ...)
{
    va_list a;

    if (!s_log)
        return;
    va_start(a, fmt);
    vfprintf(s_lf ? s_lf : stderr, fmt, a);
    va_end(a);
    if (s_lf)
        fflush(s_lf);
}
static float s_seat[3] = { 0.0f, 0.66f, 0.15f };      /* right, up, ahead */
static struct {
    int applied;
    uint32_t va;
    float orig[16], wrote[16];
} s_cam;

static xbox_vr_pose s_vr_pose;          /* the headset pose this render is drawn from (id 0: none) */
static float s_vr_ref[4];               /* the recentre: the yaw (radians) and the head position then */
static int s_vr_ref_set;
static int s_np_dirty;                  /* the seat point was nudged and not yet saved */
static unsigned s_np_idle;
static float s_vr_lean[3];              /* the same in seat terms: right, up, ahead (metres) */
static int s_boost_fx = 1;               /* BO3_VR_BOOSTFX=0: the boost's field of view widening is not shown */
static int s_hide_driver = 0;           /* BO3_VR_DRIVER=1 drops the triangles in the driver's box: off by default, it did not remove the driver and cut holes in the interior */
static int s_seat_slot = -1;            /* the seat slot the camera is at now (0 seat, 1 bonnet), -1 in a chase view */
static float s_vr_off[3];               /* the head's move from the recentre, along the world axes */
static float s_vw_keep[4], *s_vw_at;    /* the title's view window, put back at the flip */
static float s_yaw, s_pitch;            /* the look now, radians: right and up positive */
static int s_vr;                        /* --vr: a headset system was found (the toolkit's xbox_VrInit) */
static float s_ipd;                     /* the eye separation, metres; 0 = no stereo (BO3_SEATCAM_STEREO, --stereo[=ipd]) */

/* The per-car seat (below): declared here for the start-up code. */
static float s_cur[2][3];               /* slot 0 the seat, slot 1 over the bonnet: right, up, ahead */
static char s_file[600] = "seatcam.txt";
static void tab_load(void);

/* The near clip plane. The title's is 0.5 m (docs/draw-distance.md), so with the seat in the cabin the roof and
 * the window pillars, closer than that, are cut away. BO3_SEATCAM_NEAR=<metres> (default 0.1) replaces it while the
 * seat is drawn. Where it lives is not documented: the RwCamera (view + 0x58) is searched, once, for the pair
 * 0.5 then 10000.0 (the near and far planes sit side by side in a RwCamera), and what was found is logged. */
static float s_near = 0.1f;
static int s_ipd_cli;                   /* --stereo=<ipd> was given: the settings' eye separation does not override it */

/* A VR option: its RECOMP_VR_* variable (the settings' VR page sets it) or the older BO3_ one. */
static const char *venv(const char *a, const char *b)
{
    const char *v = getenv(a);

    return v && *v ? v : getenv(b);
}
static int venv_is(const char *a, const char *b, char c)   /* the first character is c */
{
    const char *v = venv(a, b);

    return v && *v == c;
}
static int s_near_off = -2;             /* the float index of the near plane; -2 not looked for, -1 not found */
static uint32_t s_near_va;
static float s_near_orig;
static int s_near_applied;

static void near_apply(void)
{
    uint32_t rc = SC_M32(SC_VIEW + 0x58u);
    float *f;
    int k;

    if (rc < 0x10000u || rc >= 0x08000000u)
        return;
    f = SC_PTR(rc);
    if (s_near_off == -2) {
        s_near_off = -1;
        for (k = 2; k < 0x120 / 4; k++)
            if (f[k] == 0.5f && f[k + 1] == 10000.0f) {
                s_near_off = k;
                break;
            }
        if (s_near_off >= 0) {
            slog("[SEATCAM] the camera %08x has its near plane at +0x%X (0.5) and the far plane after it (10000)\n", rc,
                 s_near_off * 4);
        } else {
            slog("[SEATCAM] the camera %08x: no 0.5 / 10000.0 pair found in its first 0x120 bytes; floats:\n", rc);
            for (k = 0; k < 0x120 / 4; k += 4)
                slog("  +0x%03X %12.4f %12.4f %12.4f %12.4f\n", k * 4, f[k], f[k + 1], f[k + 2], f[k + 3]);
        }
    }
    if (s_near_off < 0 || !(s_near > 0.0f) || f[s_near_off] == s_near)
        return;
    s_near_va = rc + (uint32_t)s_near_off * 4u;
    s_near_orig = f[s_near_off];
    f[s_near_off] = s_near;
    s_near_applied = 1;
}

/* ---- stereo: side by side, each eye drawn by the renderer ---------------------------
 *
 * --stereo[=ipd] (BO3_SEATCAM_STEREO): the seat camera stays ONE camera, between the eyes; the toolkit's Vulkan backend
 * draws each scene draw twice, the left eye in the left half of the picture and the right in the right half, each
 * squeezed to half width, with a small sideways shift of the clip position (nv2a_pb_state.h's stereo channel). What
 * the guest does is tell it, in order with the draws, when to start (here, at the stage's render), where the shift is
 * zero (the HUD: main.c's hdr_ui mark calls bo3_seatcam_stereo_scene) and when to stop (the flip).
 *
 * The shift is K = P00 * ipd / 2, where P00 is how much one metre sideways in front of the camera moves the clip x:
 * 1 / (the view window's half width). The RwCamera (view + 0x58) has the view window at + 0x68 and its reciprocal at
 * + 0x70 (checked against each other; 0.75, a 16:9 picture's, when they do not agree). Bloom and motion blur
 * (render flags 12 and 14, 0x4D61C4 +) read the whole picture back and mix the eyes, so they are off while stereo is on. */
void nv2a_pb_stream_mark_stereo(uint32_t va, uint32_t value);       /* toolkit nv2a_pb_state.h */
void nv2a_pb_stream_hide_set(const float *box);
#define SC_STEREO_ON 0x40000000u
static int s_stereo_on;                 /* this picture is drawn as a pair */
static uint32_t s_stereo_scene, s_stereo_ui;    /* the stereo values (nv2a_pb_state.h): the scene's shift, and none for the HUD */
static uint8_t s_rflag_keep[2];
static int s_rflag_set;

static void stereo_mark(uint32_t value)
{
    uint32_t dev = SC_M32(SC_D3D_PUSH), put;

    if (!dev)
        return;
    put = SC_M32(dev);
    if (put)
        nv2a_pb_stream_mark_stereo(put, value);
}

static void stereo_begin(uint32_t rc)
{
    const float *f = SC_PTR(rc);
    float vw = f[0x68 / 4], rcp = f[0x70 / 4], p00 = 0.75f, k;
    static int logged;
    uint8_t *fl = (uint8_t *)((uintptr_t)g_xbox_mem_offset + SC_RFLAGS);

    if (!(s_ipd > 0.0f))
        return;
    if (vw > 0.2f && vw < 5.0f && fabsf(vw * rcp - 1.0f) < 0.02f)
        p00 = 1.0f / vw;
    k = p00 * (s_vr_pose.id ? s_vr_pose.ipd : s_ipd) * 0.5f;
    {
                float vwy = f[0x68 / 4 + 1];
        uint32_t a = (uint32_t)(k / 2e-5f + 0.5f), b = (uint32_t)(vw * 128.0f + 0.5f), c;

        if (!(vwy > 0.2f && vwy < 5.0f))
            vwy = 0.75f;
        c = (uint32_t)(vwy * 128.0f + 0.5f);
        if (a > 4095u) a = 4095u;
        if (b > 511u) b = 511u;
        if (c > 511u) c = 511u;
        (void)b;
        (void)c;
        /* nv2a_pb_state.h's nv2a_stereo_pack: K in the top 12 bits, then the pose id (8 bits) */
        s_stereo_scene = SC_STEREO_ON | a << 18 | (s_vr_pose.id & 0xFFu) << 10;
        s_stereo_ui = SC_STEREO_ON | (s_vr_pose.id & 0xFFu) << 10;
    }
    if (!logged) {
        logged = 1;
        slog("[SEATCAM] stereo: view window %.4f (reciprocal %.4f) -> P00 %.4f, ipd %.3f m, clip shift K = %.5f\n", vw, rcp, p00,
             s_ipd, k);
    }
    if (!s_rflag_set) {                  /* bloom and motion blur off for the picture */
        s_rflag_keep[0] = fl[12];
        s_rflag_keep[1] = fl[14];
        if (!(venv("RECOMP_VR_KEEP_FX", "BO3_VR_KEEP_FX") && *venv("RECOMP_VR_KEEP_FX", "BO3_VR_KEEP_FX") != '0'))   /* RECOMP_VR_KEEP_FX=1: bloom and motion blur stay on (the picture may be brighter) */
            fl[12] = fl[14] = 0;
        s_rflag_set = 1;
    }
    s_stereo_on = 1;
    stereo_mark(s_stereo_scene);
}

static void stereo_end(void)
{
    uint8_t *fl = (uint8_t *)((uintptr_t)g_xbox_mem_offset + SC_RFLAGS);

    if (s_rflag_set) {
        fl[12] = s_rflag_keep[0];
        fl[14] = s_rflag_keep[1];
        s_rflag_set = 0;
    }
    if (s_stereo_on) {
        s_stereo_on = 0;
        stereo_mark(0);
    }
}

/* The player's car body drawn with both faces (BO3_VR_CULL=0 leaves it): its shell is one-sided, so from the seat the rear and the
 * roof are not there. The title's own marks bracket the draw (config/patches.json: sb_draw at the start of sub_0018DE00, esi the car,
 * and sb_drawn after its call that draws the model); softbody.c calls these from its handlers, and bo3_seatcam_marks takes the
 * marks itself when softbody is off. */
void bo3_seatcam_car_draw(int begin)
{
    static int off = -1;

    if (off < 0)
        off = getenv("BO3_VR_CULL") && *getenv("BO3_VR_CULL") == '0';
    if (venv_is("RECOMP_VR_HIDE_DRIVER", "BO3_VR_DRIVER", '1'))
        s_hide_driver = 1;
    if (venv_is("RECOMP_VR_BOOSTFX", "BO3_VR_BOOSTFX", '0'))
        s_boost_fx = 0;
    if (off || !s_stereo_on || g_esi != SC_CAR1)
        return;
    if (begin && s_seat_slot < 0)
        return;                                 /* a chase view: the car is drawn as the title does */
    /* nv2a_pb_state.h's NV2A_STEREO_NOCULL (1) and NV2A_STEREO_HIDE (2: the driver's box) */
    stereo_mark(begin ? (s_stereo_scene | 1u | (s_hide_driver ? 2u : 0u)) : s_stereo_scene);
}
static void car_mark_begin(void) { bo3_seatcam_car_draw(1); }
static void car_mark_end(void)   { bo3_seatcam_car_draw(0); }
void bo3_seatcam_marks(void)
{
    recomp_patch_on_mark("sb_draw", car_mark_begin);
    recomp_patch_on_mark("sb_drawn", car_mark_end);
}

/* main.c's scene and UI marks (hdr_scene, hdr_ui): the shift for the scene, none for the HUD and menus. */
void bo3_seatcam_stereo_scene(int scene)
{
    if (s_stereo_on)
        stereo_mark(scene ? s_stereo_scene : s_stereo_ui);
}

static void near_restore(void)
{
    if (!s_near_applied)
        return;
    s_near_applied = 0;
    if (*SC_PTR(s_near_va) == s_near)
        *SC_PTR(s_near_va) = s_near_orig;
}

/* The look asked for, each -1..1: the right stick, or the keys. */
static void look_input(float *x, float *y)
{
    uint32_t k;
    const uint8_t *st;
    int rx, ry;

    *x = *y = 0.0f;
    for (k = 0; k < 3 && SC_M32(SC_PADS + k * 0x20) != 2; k++)
        ;
    st = (const uint8_t *)((uintptr_t)g_xbox_mem_offset + SC_PADS + k * 0x20 + 8);
    rx = (int)(int16_t)(st[18] | st[19] << 8);
    ry = (int)(int16_t)(st[20] | st[21] << 8);
    if (rx > 8000 || rx < -8000)
        *x = (float)rx / 32767.0f;
    if (ry > 8000 || ry < -8000)
        *y = (float)ry / 32767.0f;
#ifdef _WIN32
    if (GetAsyncKeyState('L') & 0x8000) *x = 1.0f;
    if (GetAsyncKeyState('J') & 0x8000) *x = -1.0f;
    if (GetAsyncKeyState('I') & 0x8000) *y = 1.0f;
    if (GetAsyncKeyState('K') & 0x8000) *y = -1.0f;
#endif
}

/* The pad's right stick pressed in (R3), for recentring the headset's view (the keyboard's C does too). The pad block is the one
 * look_input reads: its buttons word is 4 bytes in, the right stick's X at 14. */
static int pad_recentre(void)
{
    uint32_t k;
    const uint8_t *st;

    for (k = 0; k < 3 && SC_M32(SC_PADS + k * 0x20) != 2; k++)
        ;
    st = (const uint8_t *)((uintptr_t)g_xbox_mem_offset + SC_PADS + k * 0x20 + 8);
    return (st[4] | st[5] << 8) & 0x0080;
}

#define SC_MAXCARS 256
#define SC_NSLOT   2
static struct { uint32_t key; int slot; float v[3]; } s_tab[SC_MAXCARS];
static int s_ntab;
static uint32_t s_curkey;               /* the car the slots are for; 0 none yet */
static int s_alt;                       /* 0 the seat, 1 the close chase, 2 the far chase (V), while the car is drawn */
static const struct { const char *name; float v[3]; float pitch_deg; } k_alt[3] = {
    { "the driver's seat", { 0, 0, 0 }, 0.0f },
    { "close chase", { 0.0f, 1.7f, -4.3f }, -7.0f },
    { "far chase", { 0.0f, 2.9f, -9.5f }, -10.0f },
};
static float s_bonnet[3] = { 0.0f, 1.1f, 0.35f };    /* the default over-the-bonnet point */
static float s_pitch_bias;              /* the view's own tilt, radians (up positive) */

/* The command line: --seatcam[=<right>,<up>,<ahead>[,save]] and --bonnet=<right>,<up>,<ahead>[,save] (main.c hands over
 * argc and argv before anything runs). --seatcam turns the seat camera on, like BO3_SEATCAM; with three numbers it sets
 * the default point of the driver's seat, --bonnet the default over the bonnet. A trailing ",save" keeps the point as the
 * DEFAULT in the seat file (a line with the model key 0), so that later runs start from it without the parameter.
 * A car with a point of its own in the file (the numpad's 0) uses that instead. */
static int s_cli_on;
static int s_cli_has[2], s_cli_save[2];
static float s_cli_v[2][3];

static void cli_point(const char *a, int slot)
{
    float x, y, z;
    int n = sscanf(a, "%f,%f,%f", &x, &y, &z);

    if (n == 3) {
        s_cli_v[slot][0] = x;
        s_cli_v[slot][1] = y;
        s_cli_v[slot][2] = z;
        s_cli_has[slot] = 1;
        s_cli_save[slot] = strstr(a, ",save") != NULL;
    }
}

void bo3_seatcam_args(int argc, char **argv)
{
    int i;

    if (!SC_SUPPORTED) {
        /* A disc whose addresses are not all known (seatcam_addr.h): nothing of this runs. */
        for (i = 1; i < argc; i++)
            if (!strcmp(argv[i], "--vr") || !strncmp(argv[i], "--stereo", 8) || !strncmp(argv[i], "--seatcam", 9)
                    || !strncmp(argv[i], "--bonnet=", 9)) {
                fprintf(stderr, "[SEATCAM] %s: the seat camera's addresses for this disc are not known yet (docs/vr.md): VR is off\n",
                        argv[i]);
                break;
            }
        return;
    }
    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--seatcam", 9) && (argv[i][9] == 0 || argv[i][9] == '=')) {
            s_cli_on = 1;
            if (argv[i][9] == '=')
                cli_point(argv[i] + 10, 0);
        } else if (!strcmp(argv[i], "--vr")) {
            /* The headset (OpenXR, the toolkit's oxr.c): before anything makes the window or the Vulkan device. The
             * eye separation is the default unless --stereo=<ipd> says; stereo is on with it. */
            s_cli_on = 1;
            if (s_ipd == 0.0f)
                s_ipd = 0.064f;
            if (xbox_VrInit())
                s_vr = 1;
        } else if (!strncmp(argv[i], "--stereo", 8) && (argv[i][8] == 0 || argv[i][8] == '=')) {
            s_cli_on = 1;
            s_ipd = argv[i][8] == '=' ? (float)atof(argv[i] + 9) : 0.064f;
            s_ipd_cli = argv[i][8] == '=';
        } else if (!strncmp(argv[i], "--bonnet=", 9)) {
            s_cli_on = 1;
            cli_point(argv[i] + 9, 1);
        }
    }
}

static void tab_put(uint32_t key, int slot, const float *v);
static void tab_save(uint32_t key, int slot);
static int tab_find(uint32_t key, int slot);

void bo3_seatcam_start(void)
{
    const char *e = getenv("BO3_SEATCAM");

    if (!SC_SUPPORTED)
        return;
    /* Off when unset, empty or exactly "0": a seat such as "0,0.66,0.15" starts with a 0 (no sideways offset). */
    if ((!e || !*e || !strcmp(e, "0")) && !s_cli_on)
        return;
    if (!e)
        e = "";
    s_on = 1;
    {
        const char *l = getenv("BO3_SEATCAM_LOG");

        s_log = l != NULL && *l;
        if (s_log && strlen(l) > 1)
            s_lf = fopen(l, "w");
    }
    if (strchr(e, ',')) {
        float a, b, c;

        if (sscanf(e, "%f,%f,%f", &a, &b, &c) == 3) {
            s_seat[0] = a;
            s_seat[1] = b;
            s_seat[2] = c;
        }
    }
    {
        const char *st = getenv("BO3_SEATCAM_STEREO");

        const char *mm = getenv("RECOMP_VR_IPD");      /* the settings' VR page: millimetres; not over a --stereo=<ipd> */

        if (st && *st && s_ipd == 0.0f)
            s_ipd = (float)atof(st) > 0.0f ? (float)atof(st) : 0.064f;
        if (mm && *mm && s_ipd > 0.0f && !s_ipd_cli && atof(mm) >= 40.0 && atof(mm) <= 90.0)
            s_ipd = (float)atof(mm) / 1000.0f;
        if (s_ipd > 0.0f)
            slog("[SEATCAM] stereo: the eyes %.3f m apart, side by side in one picture (the renderer draws each eye)\n", s_ipd);
    }
    {
        const char *nn = getenv("BO3_SEATCAM_NEAR");

        const char *nm = getenv("RECOMP_VR_NEAR");     /* the settings' VR page: millimetres; BO3_SEATCAM_NEAR (metres) wins */

        if (nm && *nm && atof(nm) > 0.0)
            s_near = (float)atof(nm) / 1000.0f;
        if (nn && *nn)
            s_near = (float)atof(nn);
    }
    {
        const char *sf = getenv("BO3_SEATCAM_FILE");

        if (sf && *sf)
            snprintf(s_file, sizeof s_file, "%s", sf);
    }
    tab_load();
    {
        /* The defaults: the saved ones (key 0 in the file), then the environment's seat (above), then the command line's. */
        int t0 = tab_find(0, 0), t1 = tab_find(0, 1);

        if (t0 >= 0 && !strchr(e, ','))
            memcpy(s_seat, s_tab[t0].v, sizeof s_seat);
        if (t1 >= 0)
            memcpy(s_bonnet, s_tab[t1].v, sizeof s_bonnet);
        if (s_cli_has[0])
            memcpy(s_seat, s_cli_v[0], sizeof s_seat);
        if (s_cli_has[1])
            memcpy(s_bonnet, s_cli_v[1], sizeof s_bonnet);
        if (s_cli_has[0] && s_cli_save[0])
            tab_put(0, 0, s_seat);
        if (s_cli_has[1] && s_cli_save[1])
            tab_put(0, 1, s_bonnet);
    }
    slog("[SEATCAM] near plane while seated: %.3f m (0 = the title's)\n", s_near);
    fprintf(stderr, "[SEATCAM] on: the race is drawn from the car, right %.2f up %.2f ahead %.2f m (BO3_SEATCAM)\n",
            s_seat[0], s_seat[1], s_seat[2]);
    slog("[SEATCAM] on: right %.2f up %.2f ahead %.2f m\n", s_seat[0], s_seat[1], s_seat[2]);
}

void bo3_seatcam_restore(void)
{
    if (s_vr) {
        /* Recentre, in the race and in the menus: Tab or C on the keyboard, the pad's right stick in (R3). */
        static int was;
        int now = pad_recentre();

#ifdef _WIN32
        now |= (GetAsyncKeyState(VK_TAB) & 0x8000) != 0 || (GetAsyncKeyState('C') & 0x8000) != 0;
#endif
        static unsigned held;
        static float lean_at_press[3];

        if (now && !was) {
            memcpy(lean_at_press, s_vr_lean, sizeof lean_at_press);     /* where the head was, before the recentre */
            held = 0;
        }
        if (now && was && ++held == 90) {                               /* held for about 1.5 s: this is the seat */
            if (s_curkey && s_seat_slot >= 0) {
                float *c = s_cur[s_seat_slot];

                c[0] += lean_at_press[0];
                c[1] += lean_at_press[1];
                c[2] += lean_at_press[2];
                tab_save(s_curkey, s_seat_slot);
                slog("[SEATCAM] recentre held: the seat of car model 0x%08X is now right %.3f up %.3f ahead %.3f (saved)\n", s_curkey, c[0],
                     c[1], c[2]);
            }
        }
        if (now && !was) {
            s_vr_ref_set = 0;                       /* the race: the next pose is "ahead" and the origin */
            xbox_VrRecentre();                      /* the menus: the screen comes in front of the player again */
            slog("[SEATCAM] recentred\n");
        }
        was = now;
    }
    near_restore();
    if (s_vw_at) {
        memcpy(s_vw_at, s_vw_keep, sizeof s_vw_keep);
        s_vw_at = NULL;
    }
    s_vr_pose.id = 0;
    stereo_end();
    if (!s_cam.applied)
        return;
    s_cam.applied = 0;
    if (!memcmp(SC_PTR(s_cam.va), s_cam.wrote, sizeof s_cam.wrote))    /* still ours: nothing else wrote it */
        memcpy(SC_PTR(s_cam.va), s_cam.orig, sizeof s_cam.orig);
}

/* ---- the views, and the seat for each car -------------------------------
 *
 * What the title shows depends on its own view (change-view toggles it): in its chase view the player's car is
 * drawn, in its front view it is not. The seat camera follows that:
 *   car drawn (the title's chase view): slot 0, the DRIVER'S SEAT, behind the steering wheel: a US car's wheel is
 *     on the left, a UK one's on the right, so the seat is off the centre line by a different amount in each car.
 *     The key V cycles the seat with a CLOSE CHASE and a FAR CHASE view (the car seen from behind).
 *   car not drawn (the title's front view): slot 1, OVER THE BONNET: there is no body to sit in, so it is centred
 *     by default, above and a little ahead of the cabin.
 * Cars differ in size, so slots 0 and 1 are kept per car and set by hand in the game: the numpad moves the
 * active one (4 and 6 left and right, 8 and 2 ahead and back, 9 and 3 up and down, about 0.4 m a second), 5 puts
 * it back to the saved or default one, 0 saves it for this car. A car is known by the size field of its model, the
 * .bgv (car + 0x50, its + 8: unique to a model, the same every run). The table is a text file, one slot of one
 * car a line: "0x<key> <slot> <right> <up> <ahead>" (a line of four numbers is slot 0),
 * BO3_SEATCAM_FILE=<path> (default seatcam.txt in the game's folder). The chase views are fixed. */
static void tab_load(void)
{
    FILE *f = fopen(s_file, "r");
    char line[200];

    if (!f)
        return;
    while (fgets(line, sizeof line, f) && s_ntab < SC_MAXCARS) {
        unsigned k;
        int sl;
        float a, b, c;

        if (sscanf(line, "%x %d %f %f %f", &k, &sl, &a, &b, &c) == 5 && sl >= 0 && sl < SC_NSLOT) {
            s_tab[s_ntab].key = k;
            s_tab[s_ntab].slot = sl;
            s_tab[s_ntab].v[0] = a;
            s_tab[s_ntab].v[1] = b;
            s_tab[s_ntab].v[2] = c;
            s_ntab++;
        } else if (sscanf(line, "%x %f %f %f", &k, &a, &b, &c) == 4) {
            s_tab[s_ntab].key = k;
            s_tab[s_ntab].slot = 0;
            s_tab[s_ntab].v[0] = a;
            s_tab[s_ntab].v[1] = b;
            s_tab[s_ntab].v[2] = c;
            s_ntab++;
        }
    }
    fclose(f);
    slog("[SEATCAM] %d saved views read from %s\n", s_ntab, s_file);
}

static int tab_find(uint32_t key, int slot)
{
    int i;

    for (i = 0; i < s_ntab; i++)
        if (s_tab[i].key == key && s_tab[i].slot == slot)
            return i;
    return -1;
}

static void tab_put(uint32_t key, int slot, const float *v)
{
    FILE *f;
    int i = tab_find(key, slot);

    if (i < 0) {
        if (s_ntab >= SC_MAXCARS)
            return;
        i = s_ntab++;
    }
    s_tab[i].key = key;
    s_tab[i].slot = slot;
    memcpy(s_tab[i].v, v, sizeof s_tab[i].v);
    f = fopen(s_file, "w");
    if (!f) {
        slog("[SEATCAM] cannot write %s\n", s_file);
        return;
    }
    for (i = 0; i < s_ntab; i++)
        fprintf(f, "0x%08X %d %.3f %.3f %.3f\n", s_tab[i].key, s_tab[i].slot, s_tab[i].v[0], s_tab[i].v[1],
                s_tab[i].v[2]);
    fclose(f);
    slog("[SEATCAM] %s for %s saved to %s: right %.3f up %.3f ahead %.3f\n",
         slot ? "the over-bonnet point" : "the seat", key ? "this car" : "every car without its own", s_file, v[0], v[1], v[2]);
}

static void tab_save(uint32_t key, int slot)
{
    tab_put(key, slot, s_cur[slot]);
}

/* The key of the player's car: its model's size field, or 0. */
static uint32_t car_key(void)
{
    uint32_t bgv = SC_M32(SC_CAR1 + 0x50u);

    if (bgv < 0x80000000u || bgv >= 0x84000000u || SC_M32(bgv) != 0x17u)
        return 0;
    return SC_M32(bgv + 8u);
}

static void slot_default(int slot, float *out)
{
    memcpy(out, slot ? s_bonnet : s_seat, sizeof s_cur[0]);
}

/* Called once a render while the camera is ours. front: the title is in its front view (the car is not drawn).
 * Returns the point to use (right, up, ahead) and sets s_pitch_bias. */
static const float *seat_update(int front)
{
    uint32_t key = car_key();
    int slot = front ? 1 : 0, i;
    static int v_down;

    if (key != s_curkey || (s_curkey == 0 && s_cur[0][1] == 0.0f && s_cur[1][1] == 0.0f)) {
        s_curkey = key;
        for (i = 0; i < SC_NSLOT; i++) {
            int t = key ? tab_find(key, i) : -1;

            if (t >= 0)
                memcpy(s_cur[i], s_tab[t].v, sizeof s_cur[i]);
            else
                slot_default(i, s_cur[i]);
            slog("[SEATCAM] car model 0x%08X, %s: right %.3f up %.3f ahead %.3f (%s)\n", key,
                 i ? "over the bonnet" : "the seat", s_cur[i][0], s_cur[i][1], s_cur[i][2], t >= 0 ? "saved" : "the default");
        }
    }
#ifdef _WIN32
    if (GetAsyncKeyState('V') & 0x8000) {
        if (!v_down) {
            v_down = 1;
            s_alt = (s_alt + 1) % 3;
            slog("[SEATCAM] view: %s\n", k_alt[s_alt].name);
        }
    } else {
        v_down = 0;
    }
    if (front || !s_alt) {
        static int saved_down, reset_down;
        const float step = 0.4f / 60.0f;                    /* 0.4 m a second at 60 renders a second */
        float *c = s_cur[slot];
        int moved = 0;

        if (GetAsyncKeyState(VK_NUMPAD6) & 0x8000) { c[0] += step; moved = 1; }   /* right */
        if (GetAsyncKeyState(VK_NUMPAD4) & 0x8000) { c[0] -= step; moved = 1; }   /* left */
        if (GetAsyncKeyState(VK_NUMPAD8) & 0x8000) { c[2] += step; moved = 1; }   /* ahead */
        if (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) { c[2] -= step; moved = 1; }   /* back */
        if (GetAsyncKeyState(VK_NUMPAD9) & 0x8000) { c[1] += step; moved = 1; }   /* up */
        if (GetAsyncKeyState(VK_NUMPAD3) & 0x8000) { c[1] -= step; moved = 1; }   /* down */
        s_seat_slot = slot;
        if (moved) {
            static unsigned t;

            s_np_dirty = 1;
            s_np_idle = 0;

            if ((t++ % 30) == 0)
                slog("[SEATCAM] %s now right %.3f up %.3f ahead %.3f\n", slot ? "over the bonnet" : "the seat", c[0],
                     c[1], c[2]);
        }
        if (!moved && s_np_dirty && ++s_np_idle > 120 && s_curkey) {   /* two seconds after the last nudge: kept for this car */
            s_np_dirty = 0;
            tab_save(s_curkey, slot);
        }
        if (GetAsyncKeyState(VK_NUMPAD5) & 0x8000) {
            if (!reset_down) {
                int t = s_curkey ? tab_find(s_curkey, slot) : -1;

                reset_down = 1;
                if (t >= 0)
                    memcpy(c, s_tab[t].v, sizeof s_cur[0]);
                else
                    slot_default(slot, c);
                slog("[SEATCAM] reset to %s: right %.3f up %.3f ahead %.3f\n", t >= 0 ? "the saved one" : "the default",
                     c[0], c[1], c[2]);
            }
        } else {
            reset_down = 0;
        }
        if (GetAsyncKeyState(VK_NUMPAD0) & 0x8000) {
            if (!saved_down) {
                saved_down = 1;
                if (s_curkey)
                    tab_save(s_curkey, slot);
                else
                    slog("[SEATCAM] this car has no model key: not saved\n");
            }
        } else {
            saved_down = 0;
        }
    }
#endif
    if (!front && s_alt) {
        s_seat_slot = -1;
        s_pitch_bias = k_alt[s_alt].pitch_deg * 3.14159265f / 180.0f;
        return k_alt[s_alt].v;
    }
    s_pitch_bias = 0.0f;
    return s_cur[slot];
}

void bo3_seatcam_apply(void)
{
    static int shown, why_last = -1;
    static unsigned applied_n, calls_n;
    uint32_t cp;
    int why = 0;
    float *c, *car, d[3], dist, n[16];
    const float *sv;
    int i;

    if (!s_on || !g_xbox_mem_offset)
        return;
    bo3_seatcam_restore();                                  /* a flip we missed */
    car = SC_PTR(SC_CAR1 + 0x10u);
    calls_n++;
    cp = SC_M32(SC_VIEW + 0x68u);
    if (SC_M32(SC_CAR1 + 0x134Cu) != 2)
        why = 1;
    else if (SC_M32(SC_CAR1 + 0x3D0u) != 4)
        why = 2;
    else if (cp < 0x10000u || cp >= 0x08000000u)
        why = 3;
    if (why) {
        if (why != why_last) {
            why_last = why;
            slog("[SEATCAM] stage render #%u: not applied: %s\n", calls_n,
                 why == 1 ? "the race is not in play (phase)" : why == 2 ? "the car is not driving (+0x3D0)"
                                                                         : "no camera object at the view");
        }
        return;
    }
    c = SC_PTR(cp);
    for (i = 0; i < 3; i++)
        d[i] = c[12 + i] - car[12 + i];
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (s_log && shown < 6 && (dist < SC_MAX_D)) {
        shown++;
        slog("[SEATCAM] camera object %08x, dist %.2f:\n", cp, dist);
        for (i = 0; i < 4; i++)
            slog("  cam row %d  %8.4f %8.4f %8.4f %8.4f   car row %d  %8.4f %8.4f %8.4f %8.4f\n", i,
                    c[4 * i], c[4 * i + 1], c[4 * i + 2], c[4 * i + 3], i, car[4 * i], car[4 * i + 1],
                    car[4 * i + 2], car[4 * i + 3]);
    }
    if (dist >= SC_MAX_D) {
        if (why_last != 4) {
            why_last = 4;
            slog("[SEATCAM] stage render #%u: not applied: the title's camera is %.1f m from the car (a crash camera or a replay)\n",
                 calls_n, dist);
        }
        return;
    }
    if (why_last != 0) {
        why_last = 0;
        slog("[SEATCAM] stage render #%u: applied (the title's camera was %.2f m from the car)\n", calls_n, dist);
    }
    applied_n++;
    if (applied_n % 600 == 0)
        slog("[SEATCAM] %u renders drawn from the seat so far\n", applied_n);
    sv = seat_update(dist < SC_FRONT_D);
    if (s_seat_slot >= 0 && s_hide_driver) {
        /* The driver's box, in the model's space (x along the car's left, y up, z ahead), around the seat point: head and
         * shoulders, not the wheel (ahead of the eye) or the seat's base. Whole triangles inside it are not drawn. */
        float box[6], c3[3];
        static uint32_t told_key;

        /* The driver's helmet is known for some models (docs/vr-openxr-proposal.md: found in the model's vertices; right, up,
         * ahead of the car origin, so x is the negative of "right"); the others use the seat point, which is only the eye. */
        if (s_curkey == 0x00090000u || s_curkey == 0x0008A000u) {   /* region: not an address (a car model's key: the NTSC-U disc's; other discs fall back to the seat point) */
            c3[0] = -0.006f;  c3[1] = 0.718f;  c3[2] = -0.280f;
            box[0] = c3[0] - 0.30f;  box[3] = c3[0] + 0.30f;
            box[1] = c3[1] - 0.45f;  box[4] = c3[1] + 0.20f;
            box[2] = c3[2] - 0.35f;  box[5] = c3[2] + 0.20f;
        } else {
            box[0] = -sv[0] - 0.30f;  box[3] = -sv[0] + 0.30f;
            box[1] = sv[1] - 0.40f;   box[4] = sv[1] + 0.20f;
            box[2] = sv[2] - 0.60f;   box[5] = sv[2] + 0.05f;
        }
        if (told_key != s_curkey) {
            told_key = s_curkey;
            slog("[SEATCAM] driver box for car model 0x%08X: x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f (%s)\n", s_curkey, box[0], box[3],
                 box[1], box[4], box[2], box[5], s_curkey == 0x00090000u || s_curkey == 0x0008A000u ? "the helmet" : "the seat point");   /* region: not an address (car model keys) */
        }
        nv2a_pb_stream_hide_set(box);
    }
    memcpy(n, c, sizeof n);
    {
        /* The look: turn the car's axes about the seat. Written in the axes themselves (left, up, forward) so
         * that it does not depend on the handedness of the title's space: yaw to the right moves the forward
         * axis toward the car's right (= -left); pitch up moves it toward up. */
        float lx, ly, ty, tp, cy, sy, cp2, sp2, L[3], U[3], F[3], L2[3], F2[3], U2[3];

        look_input(&lx, &ly);
        if (s_vr && xbox_VrPose(&s_vr_pose)) {
            lx = ly = 0.0f;                                  /* the head looks, not the stick */
        } else {
            s_vr_pose.id = 0;
        }
        ty = lx * SC_LOOK_YAW;
        tp = ly * SC_LOOK_PITCH + s_pitch_bias;
        s_yaw += (ty - s_yaw) * 0.15f;                       /* eases, a frame at a time */
        s_pitch += (tp - s_pitch) * 0.15f;
        cy = cosf(s_yaw);
        sy = sinf(s_yaw);
        cp2 = cosf(s_pitch);
        sp2 = sinf(s_pitch);
        for (i = 0; i < 3; i++) {
            L[i] = car[i];
            U[i] = car[4 + i];
            F[i] = car[8 + i];
            F2[i] = cy * F[i] - sy * L[i];                   /* yaw about up */
            L2[i] = cy * L[i] + sy * F[i];
        }
        for (i = 0; i < 3; i++) {
            float f3 = cp2 * F2[i] + sp2 * U[i];             /* pitch about the (yawed) right axis */
            U2[i] = cp2 * U[i] - sp2 * F2[i];
            F2[i] = f3;
        }
        if (s_vr_pose.id) {
            /* The head: its axes in the headset's space (+x right, +y up, +z back), turned by the recentre yaw, then
             * written along the car's right (= -left), up and back (= -forward). */
            const float *q = s_vr_pose.q;
            float x = q[0], y = q[1], z = q[2], w = q[3];
            float ax[3][3] = {
                { 1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y) },      /* right */
                { 2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x) },      /* up */
                { 2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y) } };    /* back */
            float h, ch, sh, o[3][3];
            int a;

            if (!s_vr_ref_set) {
                h = atan2f(-ax[2][0], ax[2][2]);             /* the heading of the head's forward (= -back): atan2(f.x, -f.z) */
                s_vr_ref[0] = h;
                s_vr_ref[1] = s_vr_pose.p[0];
                s_vr_ref[2] = s_vr_pose.p[1];
                s_vr_ref[3] = s_vr_pose.p[2];
                s_vr_ref_set = 1;
            }
            ch = cosf(s_vr_ref[0]);
            sh = sinf(s_vr_ref[0]);
            for (a = 0; a < 3; a++) {
                float vx = ax[a][0], vy = ax[a][1], vz = ax[a][2];

                ax[a][0] = vx * ch + vz * sh;
                ax[a][1] = vy;
                ax[a][2] = -vx * sh + vz * ch;
            }
            for (a = 0; a < 3; a++)
                for (i = 0; i < 3; i++)
                    o[a][i] = ax[a][0] * -L[i] + ax[a][1] * U[i] + ax[a][2] * -F[i];
            for (i = 0; i < 3; i++) {
                L2[i] = -o[0][i];
                U2[i] = o[1][i];
                F2[i] = -o[2][i];
            }
            {
                float dx = s_vr_pose.p[0] - s_vr_ref[1], dy = s_vr_pose.p[1] - s_vr_ref[2], dz = s_vr_pose.p[2] - s_vr_ref[3];
                float rx = dx * ch + dz * sh, rz = -dx * sh + dz * ch;

                if (rx > 0.6f) rx = 0.6f; else if (rx < -0.6f) rx = -0.6f;       /* a lean, not a walk */
                if (dy > 0.6f) dy = 0.6f; else if (dy < -0.6f) dy = -0.6f;
                if (rz > 0.6f) rz = 0.6f; else if (rz < -0.6f) rz = -0.6f;
                s_vr_lean[0] = rx;
                s_vr_lean[1] = dy;
                s_vr_lean[2] = -rz;
                for (i = 0; i < 3; i++)
                    s_vr_off[i] = rx * -L[i] + dy * U[i] + rz * -F[i];
            }
        } else {
            s_vr_off[0] = s_vr_off[1] = s_vr_off[2] = 0.0f;
            s_vr_lean[0] = s_vr_lean[1] = s_vr_lean[2] = 0.0f;
        }
        /* The camera object is not a matrix (its first log dump: row 0 zero, row 1 starting 90.0, row 2 a unit
         * quaternion, row 3 the position): + 0x20 holds the orientation as a quaternion (x, y, z, w) and + 0x30
         * the position. The quaternion is the standard one of the rotation whose COLUMNS are the camera's x, y
         * and z axes in the world: the car's left, up and forward (checked on the first dump: the chase camera's
         * (0.020, -0.811, 0.028, 0.585) against the car's (-0.001, -0.810, -0.001, 0.586), the chase camera
         * tilted a little down). The rest of rows 0 to 2 (the field of view and the like) is left as the title set it. */
        {
            float m00 = L2[0], m10 = L2[1], m20 = L2[2];     /* column 0: left */
            float m01 = U2[0], m11 = U2[1], m21 = U2[2];     /* column 1: up */
            float m02 = F2[0], m12 = F2[1], m22 = F2[2];     /* column 2: forward */
            float tr = m00 + m11 + m22, qx, qy, qz, qw, sc;

            if (tr > 0.0f) {
                sc = sqrtf(tr + 1.0f) * 2.0f;
                qw = 0.25f * sc;
                qx = (m21 - m12) / sc;
                qy = (m02 - m20) / sc;
                qz = (m10 - m01) / sc;
            } else if (m00 > m11 && m00 > m22) {
                sc = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
                qw = (m21 - m12) / sc;
                qx = 0.25f * sc;
                qy = (m01 + m10) / sc;
                qz = (m02 + m20) / sc;
            } else if (m11 > m22) {
                sc = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
                qw = (m02 - m20) / sc;
                qx = (m01 + m10) / sc;
                qy = 0.25f * sc;
                qz = (m12 + m21) / sc;
            } else {
                sc = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
                qw = (m10 - m01) / sc;
                qx = (m02 + m20) / sc;
                qy = (m12 + m21) / sc;
                qz = 0.25f * sc;
            }
            if (qw < 0.0f) {
                qx = -qx;
                qy = -qy;
                qz = -qz;
                qw = -qw;
            }
            n[8] = qx;
            n[9] = qy;
            n[10] = qz;
            n[11] = qw;
        }
    }
    for (i = 0; i < 3; i++) {
        /* position: right (= -row 0) * x + up * y + forward * z, from the car's origin */
        n[12 + i] = car[12 + i] - sv[0] * car[i] + sv[1] * car[4 + i] + sv[2] * car[8 + i] + s_vr_off[i];
    }
    memcpy(s_cam.orig, c, sizeof s_cam.orig);
    memcpy(s_cam.wrote, n, sizeof s_cam.wrote);
    memcpy(c, n, sizeof n);
    s_cam.va = cp;
    s_cam.applied = 1;
    near_apply();
    if (s_vr_pose.id && !venv_is("RECOMP_VR_FOV", "BO3_VR_FOV", '0')) {
        /* The headset's field of view. The title builds the view window every frame (sub_001C8A60) from the camera's field
         * of view (+0x10, 90) and two numbers in the view: x = tan(fov / 2) * view[0x70] and y = x / view[0x74] (1.2 and 16/9:
         * the widescreen window 1.2 x 0.675), then the view matrix from it. So it is those two numbers that are set, not the
         * RwCamera's window (the first try: that did not change the picture), and they take effect from the next frame's
         * build; the layer claims the window each frame was really drawn with (below). */
        static int told;
        float *vw = SC_PTR(SC_VIEW);
        float fov = c[0x10 / 4], t = tanf(fov * 0.5f * 3.14159265f / 180.0f);
        float hx = s_vr_pose.vwx, hy = s_vr_pose.vwy;

        if (fov > 20.0f && fov < 170.0f && t > 0.2f && hx > 0.3f && hx < 3.0f && hy > 0.3f && hy < 3.0f) {
            if (!told) {
                told = 1;
                slog("[SEATCAM] view window: the title's numbers %.4f and %.4f (fov %.1f) -> the headset's %.4f x %.4f\n",
                     vw[0x70 / 4], vw[0x74 / 4], fov, hx, hy);
            }
            /* The title widens the camera's field of view while boosting (the stretched, tunnel look). Setting the numbers to the
             * headset's tangents divided by tan(fov / 2) would cancel that; without the division the window is the headset's times
             * tan(fov / 2) (1 at 90 degrees), so the boost's widening is drawn, and the layer claims the headset's own field of
             * view below, which squeezes the wider picture into it: the stretch. BO3_VR_BOOSTFX=0 keeps the picture undistorted. */
            {
                static float last_fov = 90.0f;

                if (fov > last_fov + 1.0f || fov < last_fov - 1.0f) {        /* the boost's widening, for the log */
                    last_fov = fov;
                    slog("[SEATCAM] the camera's field of view is now %.1f degrees\n", fov);
                }
            }
            vw[0x70 / 4] = s_boost_fx ? hx : hx / t;
            vw[0x74 / 4] = hx / hy;
        }
    }
    stereo_begin(SC_M32(SC_VIEW + 0x58u));
    if (s_vr_pose.id) {
        /* The layer claims the frustum the picture was really drawn with: the camera's view window now. */
        uint32_t rc = SC_M32(SC_VIEW + 0x58u);

        if (rc > 0x10000u && rc < 0x08000000u) {
            const float *w = SC_PTR(rc) + 0x68 / 4;

            float t = 1.0f, fov = c[0x10 / 4];

            if (s_boost_fx && fov > 20.0f && fov < 170.0f)
                t = tanf(fov * 0.5f * 3.14159265f / 180.0f);
            xbox_VrFrame(s_vr_pose.id, w[0] / t, w[1] / t);       /* the frustum without the title's widening */
        }
    }
}
