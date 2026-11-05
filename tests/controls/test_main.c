/*
 * The PS2 driving layout (src/game/controls.h, BO-106): every row of the
 * table, the Nintendo face layout under it, and the hand-over when driving
 * ends (a held button must not become a press in the menu that opens).
 */
#include <stdio.h>
#include "controls.h"
#include "input_map.h"

static int g_checks, g_fail;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { A = XBOX_BUTTON_A, B = XBOX_BUTTON_B, X = XBOX_BUTTON_X, Y = XBOX_BUTTON_Y,
       BK = XBOX_BUTTON_BLACK, WH = XBOX_BUTTON_WHITE, LT = XBOX_BUTTON_LTRIGGER,
       RT = XBOX_BUTTON_RTRIGGER };

static XBOX_GAMEPAD pad(void)
{
    XBOX_GAMEPAD g;
    memset(&g, 0, sizeof g);
    return g;
}

/* The only analog button set in *g is `i` at `v` (i < 0: none is set). */
static int only(const XBOX_GAMEPAD *g, int i, int v)
{
    for (int k = 0; k < 8; k++)
        if (g->bAnalogButtons[k] != (k == i ? v : 0))
            return 0;
    return 1;
}

/* The mirror input.face_layout = nintendo does as the host pad is read
 * (xbox_InputMirrorFaceSdl / XInput, then the map): A-B and X-Y. */
static void mirror(XBOX_GAMEPAD *g)
{
    uint8_t t = g->bAnalogButtons[A];
    g->bAnalogButtons[A] = g->bAnalogButtons[B];
    g->bAnalogButtons[B] = t;
    t = g->bAnalogButtons[X];
    g->bAnalogButtons[X] = g->bAnalogButtons[Y];
    g->bAnalogButtons[Y] = t;
}

/* One host control held -> the one thing the title reads. */
static void test_table(void)
{
    static const struct { int host, value, guest, reads; const char *what; } rows[] = {
        { A,  255, RT, 255, "Cross (bottom): accelerate, full" },
        { X,  255, LT, 255, "Square (left): brake / reverse, full" },
        { WH, 255, X,  255, "L1 (left shoulder): look back" },
        { Y,  255, Y,  255, "Triangle (top): change view" },
        { BK, 255, A,  255, "R1 (right shoulder): boost / impact time" },
        { RT, 255, B,  255, "R2 (right trigger): crashbreaker" },
        { RT, 31,  B,  255, "R2 just over the threshold is a full press" },
        { RT, 30,  -1, 0,   "R2 at the threshold is not held" },
        { LT, 200, BK, 255, "L2 (left trigger): next music track" },
        { LT, 30,  -1, 0,   "L2 at the threshold is not held" },
        { B,  255, -1, 0,   "Circle (right): nothing while driving" },
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        XBOX_GAMEPAD g = pad();
        g.bAnalogButtons[rows[i].host] = (uint8_t)rows[i].value;
        bo3_ctl_ps2(&g, 0);
        CHECK(only(&g, rows[i].guest, rows[i].reads), "%s", rows[i].what);
    }

    /* What the layout leaves alone. */
    XBOX_GAMEPAD g = pad();
    g.wButtons = XBOX_GAMEPAD_START | XBOX_GAMEPAD_BACK | XBOX_GAMEPAD_DPAD_UP
               | XBOX_GAMEPAD_LEFT_THUMB | XBOX_GAMEPAD_RIGHT_THUMB;
    g.sThumbLX = -32768; g.sThumbLY = 12345; g.sThumbRX = 32767;
    bo3_ctl_ps2(&g, 0);
    CHECK(g.wButtons == (XBOX_GAMEPAD_START | XBOX_GAMEPAD_BACK | XBOX_GAMEPAD_DPAD_UP
                         | XBOX_GAMEPAD_LEFT_THUMB | XBOX_GAMEPAD_RIGHT_THUMB),
          "digital buttons changed: %04X", g.wButtons);
    CHECK(g.sThumbLX == -32768 && g.sThumbLY == 12345 && g.sThumbRX == 32767,
          "steering or the right stick's X changed");
    CHECK(only(&g, -1, 0), "sticks alone pressed a button");

    /* The right stick's up/down: the pedals, analog, and taken from the title. */
    g = pad(); g.sThumbRY = 32767; bo3_ctl_ps2(&g, 0);
    CHECK(only(&g, RT, 255) && g.sThumbRY == 0, "right stick full up: rt %d", g.bAnalogButtons[RT]);
    g = pad(); g.sThumbRY = -32768; bo3_ctl_ps2(&g, 0);
    CHECK(only(&g, LT, 255) && g.sThumbRY == 0, "right stick full down: lt %d", g.bAnalogButtons[LT]);
    g = pad(); g.sThumbRY = BO3_CTL_STICK_DEAD; bo3_ctl_ps2(&g, 0);
    CHECK(only(&g, -1, 0), "right stick inside the dead part");
    g = pad(); g.sThumbRY = BO3_CTL_STICK_DEAD + 1; bo3_ctl_ps2(&g, 0);
    CHECK(only(&g, RT, 1), "right stick just out of the dead part: rt %d", g.bAnalogButtons[RT]);
    g = pad(); g.sThumbRY = 20000; bo3_ctl_ps2(&g, 0);
    CHECK(g.bAnalogButtons[RT] > 110 && g.bAnalogButtons[RT] < 140 && !g.bAnalogButtons[LT],
          "right stick part up is a part throttle: rt %d", g.bAnalogButtons[RT]);
    /* The button wins over a smaller stick value, and they do not add up. */
    g = pad(); g.sThumbRY = 20000; g.bAnalogButtons[A] = 255; bo3_ctl_ps2(&g, 0);
    CHECK(g.bAnalogButtons[RT] == 255, "Cross with the stick part up");

    /* Everything at once: each control keeps its own meaning. */
    g = pad();
    g.bAnalogButtons[A] = g.bAnalogButtons[X] = g.bAnalogButtons[Y] = 255;
    g.bAnalogButtons[WH] = g.bAnalogButtons[BK] = 255;
    g.bAnalogButtons[LT] = g.bAnalogButtons[RT] = 255;
    bo3_ctl_ps2(&g, 0);
    CHECK(g.bAnalogButtons[RT] == 255 && g.bAnalogButtons[LT] == 255 && g.bAnalogButtons[X] == 255
          && g.bAnalogButtons[Y] == 255 && g.bAnalogButtons[A] == 255 && g.bAnalogButtons[B] == 255
          && g.bAnalogButtons[BK] == 255 && g.bAnalogButtons[WH] == 0, "all held");
}

/* input.face_layout = nintendo, then the PS2 layout: the face layout is
 * applied first (as the host pad is read), the PS2 layout after it, and the
 * PS2 layout goes by position: the bottom button accelerates on either pad. */
static void test_nintendo(void)
{
    /* Host positions, as SDL names them on any pad: A bottom, B right, X
     * left, Y top. */
    static const struct { int pos, guest; const char *what; } rows[] = {
        { A, RT, "bottom: accelerate" },
        { X, LT, "left: brake" },
        { Y, Y,  "top: change view" },
        { B, -1, "right: nothing" },
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        XBOX_GAMEPAD g = pad();
        g.bAnalogButtons[rows[i].pos] = 255;
        mirror(&g);                     /* the host read, Nintendo layout */
        bo3_ctl_ps2(&g, 1);
        CHECK(only(&g, rows[i].guest, 255) || (rows[i].guest < 0 && only(&g, -1, 0)),
              "nintendo, %s", rows[i].what);
    }
    /* Shoulders and triggers do not depend on the face layout. */
    XBOX_GAMEPAD g = pad();
    g.bAnalogButtons[BK] = 255; g.bAnalogButtons[RT] = 255;
    mirror(&g);
    bo3_ctl_ps2(&g, 1);
    CHECK(g.bAnalogButtons[A] == 255 && g.bAnalogButtons[B] == 255 && !g.bAnalogButtons[RT],
          "nintendo: R1 boost, R2 crashbreaker");

    /* In a menu the PS2 layout is off and the face layout alone holds: the
     * button printed A (on the right) is the title's A. */
    bo3_ctl_port p = { 0, 0 };
    g = pad();
    g.bAnalogButtons[B] = 255;          /* the right-hand button */
    mirror(&g);
    bo3_ctl_apply(&p, &g, 0, 1);
    CHECK(only(&g, A, 255), "nintendo, menu: the right-hand button confirms");
}

/* bo3_ctl_apply: the Xbox layout is the identity, and leaving the PS2 layout
 * holds back the buttons that would read as new presses. */
static void test_handover(void)
{
    bo3_ctl_port p = { 0, 0 };
    XBOX_GAMEPAD g, in;

    /* Xbox scheme, any state: nothing changes, ever. */
    in = pad();
    in.wButtons = XBOX_GAMEPAD_START;
    in.bAnalogButtons[A] = 255; in.bAnalogButtons[RT] = 77; in.sThumbRY = -9000;
    g = in;
    bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(!memcmp(&g, &in, sizeof g) && !p.ps2 && !p.latch, "xbox layout is not the identity");

    /* Driving with Cross (accelerate) and L1 (look back) held. */
    in = pad();
    in.bAnalogButtons[A] = 255; in.bAnalogButtons[WH] = 255;
    g = in;
    bo3_ctl_apply(&p, &g, 1, 0);
    CHECK(g.bAnalogButtons[RT] == 255 && g.bAnalogButtons[X] == 255 && !g.bAnalogButtons[A] && p.ps2,
          "driving: accelerate and look back");

    /* Start is pressed, the pause menu opens, both still held: the menu must
     * not see A pressed (it would resume at once), nor White (L1's own
     * button, which the title did not see down either). Start gets through. */
    in.wButtons = XBOX_GAMEPAD_START;
    g = in;
    bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(!p.ps2 && g.wButtons == XBOX_GAMEPAD_START, "pause: start lost");
    CHECK(only(&g, -1, 0), "pause: a held button reads as pressed (a %d wh %d)",
          g.bAnalogButtons[A], g.bAnalogButtons[WH]);
    /* Still held a frame later: still released. */
    g = in;
    bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(only(&g, -1, 0), "pause, second frame: A reads");
    /* Let go of Cross: the next press is the menu's A at once. L1 stays
     * held back on its own. */
    in.wButtons = 0; in.bAnalogButtons[A] = 0;
    g = in;
    bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(only(&g, -1, 0), "release frame");
    in.bAnalogButtons[A] = 255;
    g = in;
    bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(only(&g, A, 255), "pause menu: the A press after the release (a %d wh %d)",
          g.bAnalogButtons[A], g.bAnalogButtons[WH]);
    in.bAnalogButtons[WH] = 0;
    g = in;
    bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(p.latch == 0, "latch not cleared once everything was let go");
    in.bAnalogButtons[WH] = 255;
    g = in;
    bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(g.bAnalogButtons[WH] == 255, "White after its release");
    in.bAnalogButtons[WH] = 0;

    /* Back to driving with Cross still down (it confirmed "Resume"): it
     * accelerates at once, nothing is held back in this direction. */
    g = in;
    bo3_ctl_apply(&p, &g, 1, 0);
    CHECK(g.bAnalogButtons[RT] == 255 && g.bAnalogButtons[A] == 0, "resume: Cross does not accelerate");

    /* A button the title saw down under the PS2 layout and still sees down
     * in the menu is not a new press and is not held back: Triangle (Y both
     * ways), and A with both Cross and R1 down (A from R1 before, from Cross
     * after: down all along, as when A is held into a pause on the Xbox
     * layout). */
    p = (bo3_ctl_port){ 0, 0 };
    in = pad();
    in.bAnalogButtons[Y] = 255; in.bAnalogButtons[A] = 255; in.bAnalogButtons[BK] = 255;
    g = in;
    bo3_ctl_apply(&p, &g, 1, 0);
    g = in;
    bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(g.bAnalogButtons[Y] == 255 && g.bAnalogButtons[A] == 255, "continuity: y %d a %d",
          g.bAnalogButtons[Y], g.bAnalogButtons[A]);
    CHECK(g.bAnalogButtons[BK] == 0, "Black was not down for the title before");

    /* The triggers: R2 held (crashbreaker, B) when the results open would be
     * the right trigger pressed. Held back the same way. */
    p = (bo3_ctl_port){ 0, 0 };
    in = pad();
    in.bAnalogButtons[RT] = 255;
    g = in; bo3_ctl_apply(&p, &g, 1, 0);
    CHECK(only(&g, B, 255), "R2 driving");
    g = in; bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(only(&g, -1, 0), "R2 held into a menu");
    in.bAnalogButtons[RT] = 20;                 /* under the threshold: let go */
    g = in; bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(p.latch == 0 && g.bAnalogButtons[RT] == 20, "trigger release");

    /* The setting switched to xbox in the middle of a race is the same
     * hand-over: the bottom button does not boost until pressed again. */
    p = (bo3_ctl_port){ 0, 0 };
    in = pad();
    in.bAnalogButtons[A] = 255;
    g = in; bo3_ctl_apply(&p, &g, 1, 0);
    g = in; bo3_ctl_apply(&p, &g, 0, 0);
    CHECK(only(&g, -1, 0), "scheme switched off with Cross down");
}

/* The whole chain, as a controller's state travels it: the host pad's
 * buttons by position (SDL's names: A bottom, B right, X left, Y top;
 * XInput's bits likewise), the toolkit's face layout and map
 * (xinput_device.c: mirror when input.face_layout is nintendo, then
 * xbox_InputMapSdl / xbox_InputMapXInput), then the driving layout
 * (controls.c: bo3_ctl_apply with the same setting). The rule: in the PS2
 * layout the BOTTOM button accelerates on either pad and the right one does
 * nothing; in a menu the title's A is the bottom button on an Xbox pad and
 * the right one on a Nintendo pad. */
static XBOX_GAMEPAD chain_sdl(const XBOX_HOST_SDL_GAMEPAD *host, int nintendo, int ps2, bo3_ctl_port *p)
{
    XBOX_HOST_SDL_GAMEPAD h = *host;
    XBOX_GAMEPAD g;

    xbox_InputSetFaceLayoutNintendo(nintendo);
    if (xbox_InputFaceLayoutNintendo())
        xbox_InputMirrorFaceSdl(&h);
    xbox_InputMapSdl(&h, &g);
    bo3_ctl_apply(p, &g, ps2, xbox_InputFaceLayoutNintendo());
    xbox_InputSetFaceLayoutNintendo(0);
    return g;
}

static XBOX_GAMEPAD chain_xi(uint16_t buttons, int nintendo, int ps2, bo3_ctl_port *p)
{
    XBOX_HOST_XINPUT_GAMEPAD h;
    XBOX_GAMEPAD g;

    memset(&h, 0, sizeof h);
    h.wButtons = buttons;
    xbox_InputSetFaceLayoutNintendo(nintendo);
    if (xbox_InputFaceLayoutNintendo())
        xbox_InputMirrorFaceXInput(&h);
    xbox_InputMapXInput(&h, &g);
    bo3_ctl_apply(p, &g, ps2, xbox_InputFaceLayoutNintendo());
    xbox_InputSetFaceLayoutNintendo(0);
    return g;
}

static void test_chain(void)
{
    /* position, what the title reads driving in the PS2 layout (either face
     * layout), and in a menu with the Xbox and the Nintendo face layout. */
    static const struct { int sdl; uint16_t xi; int ps2, menu_xbox, menu_nin; const char *pos; } rows[] = {
        { XBOX_SDL_BUTTON_A, XBOX_XI_A, RT, A, B, "bottom" },
        { XBOX_SDL_BUTTON_B, XBOX_XI_B, -1, B, A, "right" },
        { XBOX_SDL_BUTTON_X, XBOX_XI_X, LT, X, Y, "left" },
        { XBOX_SDL_BUTTON_Y, XBOX_XI_Y, Y,  Y, X, "top" },
        { XBOX_SDL_BUTTON_LEFTSHOULDER,  XBOX_XI_LEFT_SHOULDER,  X, WH, WH, "left shoulder" },
        { XBOX_SDL_BUTTON_RIGHTSHOULDER, XBOX_XI_RIGHT_SHOULDER, A, BK, BK, "right shoulder" },
    };
    xbox_InputSetDeadzone(0);
    xbox_InputSetTriggerDeadzone(0);
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        for (int nin = 0; nin < 2; nin++) {
            XBOX_HOST_SDL_GAMEPAD h;
            bo3_ctl_port p = { 0, 0 };
            XBOX_GAMEPAD g;
            int menu = nin ? rows[i].menu_nin : rows[i].menu_xbox;

            memset(&h, 0, sizeof h);
            h.axis[XBOX_SDL_AXIS_LEFTY] = h.axis[XBOX_SDL_AXIS_RIGHTY] = -1;   /* SDL's rest: guest 0 */
            h.button[rows[i].sdl] = 1;
            g = chain_sdl(&h, nin, 1, &p);
            CHECK(only(&g, rows[i].ps2, 255), "sdl, %s face layout, PS2 driving: the %s button",
                  nin ? "nintendo" : "xbox", rows[i].pos);
            p = (bo3_ctl_port){ 0, 0 };
            g = chain_sdl(&h, nin, 0, &p);
            CHECK(only(&g, menu, 255), "sdl, %s face layout, menu: the %s button",
                  nin ? "nintendo" : "xbox", rows[i].pos);
            p = (bo3_ctl_port){ 0, 0 };
            g = chain_xi(rows[i].xi, nin, 1, &p);
            CHECK(only(&g, rows[i].ps2, 255), "xinput, %s face layout, PS2 driving: the %s button",
                  nin ? "nintendo" : "xbox", rows[i].pos);
            p = (bo3_ctl_port){ 0, 0 };
            g = chain_xi(rows[i].xi, nin, 0, &p);
            CHECK(only(&g, menu, 255), "xinput, %s face layout, menu: the %s button",
                  nin ? "nintendo" : "xbox", rows[i].pos);
        }
    }

    /* The triggers through the chain (SDL 0..32767 -> 0..255): R2 and L2. */
    {
        XBOX_HOST_SDL_GAMEPAD h;
        bo3_ctl_port p = { 0, 0 };
        XBOX_GAMEPAD g;

        memset(&h, 0, sizeof h);
        h.axis[XBOX_SDL_AXIS_LEFTY] = h.axis[XBOX_SDL_AXIS_RIGHTY] = -1;
        h.axis[XBOX_SDL_AXIS_TRIGGERRIGHT] = 32767;
        g = chain_sdl(&h, 1, 1, &p);
        CHECK(only(&g, B, 255), "nintendo, PS2 driving: the right trigger is the crashbreaker");
        h.axis[XBOX_SDL_AXIS_TRIGGERRIGHT] = 0;
        h.axis[XBOX_SDL_AXIS_RIGHTY] = -32768;          /* SDL: up */
        g = chain_sdl(&h, 1, 1, &p);
        CHECK(only(&g, RT, 255) && g.sThumbRY == 0, "nintendo, PS2 driving: right stick up accelerates");
    }

    /* The hand-over on a Nintendo pad: the bottom button held to accelerate
     * is the title's B in the pause menu (back: it would close the pause);
     * held back until let go, and the right-hand button then confirms. */
    {
        XBOX_HOST_SDL_GAMEPAD h;
        bo3_ctl_port p = { 0, 0 };
        XBOX_GAMEPAD g;

        memset(&h, 0, sizeof h);
        h.axis[XBOX_SDL_AXIS_LEFTY] = h.axis[XBOX_SDL_AXIS_RIGHTY] = -1;
        h.button[XBOX_SDL_BUTTON_A] = 1;
        g = chain_sdl(&h, 1, 1, &p);
        CHECK(only(&g, RT, 255), "nintendo: accelerating");
        h.button[XBOX_SDL_BUTTON_START] = 1;
        g = chain_sdl(&h, 1, 0, &p);
        CHECK(only(&g, -1, 0) && g.wButtons == XBOX_GAMEPAD_START,
              "nintendo: the bottom button held into the pause reads (a %d b %d)",
              g.bAnalogButtons[A], g.bAnalogButtons[B]);
        h.button[XBOX_SDL_BUTTON_START] = 0;
        h.button[XBOX_SDL_BUTTON_A] = 0;
        g = chain_sdl(&h, 1, 0, &p);
        h.button[XBOX_SDL_BUTTON_B] = 1;                /* the button printed A */
        g = chain_sdl(&h, 1, 0, &p);
        CHECK(only(&g, A, 255), "nintendo, pause menu: the right-hand button is the title's A");
    }

    /* A script standing in for a controller (controls.c, BO3_CTL_SCRIPTED):
     * its buttons are written as the title's and get the face layout from
     * bo3_ctl_mirror. Same result as the host chain, button for button. */
    for (int k = 0; k < 4; k++) {
        static const int sdl[4] = { XBOX_SDL_BUTTON_A, XBOX_SDL_BUTTON_B, XBOX_SDL_BUTTON_X, XBOX_SDL_BUTTON_Y };
        static const int name[4] = { A, B, X, Y };      /* the script's "a" "b" "x" "y" */
        for (int ps2 = 0; ps2 < 2; ps2++) {
            XBOX_HOST_SDL_GAMEPAD h;
            bo3_ctl_port p = { 0, 0 }, q = { 0, 0 };
            XBOX_GAMEPAD want, g = pad();

            memset(&h, 0, sizeof h);
            h.axis[XBOX_SDL_AXIS_LEFTY] = h.axis[XBOX_SDL_AXIS_RIGHTY] = -1;
            h.button[sdl[k]] = 1;
            want = chain_sdl(&h, 1, ps2, &p);
            g.bAnalogButtons[name[k]] = 255;
            bo3_ctl_mirror(&g);
            bo3_ctl_apply(&q, &g, ps2, 1);
            CHECK(!memcmp(&g, &want, sizeof g), "scripted stand-in, nintendo, %s: button %d differs from a controller",
                  ps2 ? "driving" : "menu", k);
        }
    }
}

/* Esc pauses a race (bo3_ctl_esc): the keyboard's Esc is a Back read, and
 * is Start when its press began while driving, for as long as it is held. */
static WORD esc_read(bo3_ctl_port *p, int esc_down, int driving, WORD other)
{
    XBOX_GAMEPAD g = pad();

    g.wButtons = (WORD)(other | (esc_down ? XBOX_GAMEPAD_BACK : 0));
    bo3_ctl_esc(p, &g, esc_down, driving);
    return g.wButtons;
}

static void test_esc(void)
{
    const WORD BACK = XBOX_GAMEPAD_BACK, START = XBOX_GAMEPAD_START;
    bo3_ctl_port p;
    WORD w;

    /* In a menu Esc stays Back. */
    memset(&p, 0, sizeof p);
    CHECK(esc_read(&p, 0, 0, 0) == 0, "menu, no Esc: not at rest");
    w = esc_read(&p, 1, 0, 0);
    CHECK(w == BACK, "menu, Esc: 0x%04X, wanted Back", w);
    w = esc_read(&p, 1, 0, 0);
    CHECK(w == BACK, "menu, Esc held: 0x%04X", w);
    CHECK(esc_read(&p, 0, 0, 0) == 0, "menu, Esc let go");

    /* Driving: the press is Start, and stays Start when the pause menu is up
     * (driving ends) and the key is still down, so the menu is not closed by
     * the same press. Let go, then the next press is Back there. */
    memset(&p, 0, sizeof p);
    w = esc_read(&p, 1, 1, 0);
    CHECK(w == START, "driving, Esc: 0x%04X, wanted Start", w);
    w = esc_read(&p, 1, 0, 0);
    CHECK(w == START, "pause menu up, Esc still down: 0x%04X, wanted Start (no Back)", w);
    w = esc_read(&p, 1, 0, 0);
    CHECK(w == START, "pause menu, Esc held on: 0x%04X", w);
    CHECK(esc_read(&p, 0, 0, 0) == 0, "Esc let go");
    w = esc_read(&p, 1, 0, 0);
    CHECK(w == BACK, "pause menu, second Esc: 0x%04X, wanted Back", w);
    esc_read(&p, 0, 0, 0);

    /* Held through the start of driving (down since a menu): there was no
     * press, so it is not a pause. */
    memset(&p, 0, sizeof p);
    esc_read(&p, 1, 0, 0);
    w = esc_read(&p, 1, 1, 0);
    CHECK(w == BACK, "Esc held into driving: 0x%04X, wanted Back", w);
    w = esc_read(&p, 1, 1, 0);
    CHECK(w == BACK, "Esc held into driving, next read: 0x%04X", w);
    esc_read(&p, 0, 1, 0);
    w = esc_read(&p, 1, 1, 0);
    CHECK(w == START, "driving, a new press: 0x%04X, wanted Start", w);

    /* A tap of one read, and the press that comes straight after another. */
    memset(&p, 0, sizeof p);
    CHECK(esc_read(&p, 1, 1, 0) == START, "tap: Start");
    CHECK(esc_read(&p, 0, 1, 0) == 0, "tap: let go");
    CHECK(esc_read(&p, 1, 1, 0) == START, "second tap: Start");

    /* Enter still pauses (Start is not touched), and the other buttons are
     * left alone. */
    memset(&p, 0, sizeof p);
    w = esc_read(&p, 0, 1, START | XBOX_GAMEPAD_DPAD_UP);
    CHECK(w == (START | XBOX_GAMEPAD_DPAD_UP), "Enter while driving: 0x%04X", w);
    w = esc_read(&p, 1, 1, XBOX_GAMEPAD_DPAD_UP);
    CHECK(w == (START | XBOX_GAMEPAD_DPAD_UP), "Esc and d-pad: 0x%04X", w);

    /* Not Esc (a pad's Back, with no Esc down) is never touched. */
    memset(&p, 0, sizeof p);
    w = esc_read(&p, 0, 1, BACK);
    CHECK(w == BACK, "pad Back while driving: 0x%04X, wanted Back", w);

    /* The analog buttons and sticks are never touched. */
    {
        XBOX_GAMEPAD g = pad();
        g.wButtons = BACK;
        g.bAnalogButtons[RT] = 255;
        g.sThumbLX = 32767;
        memset(&p, 0, sizeof p);
        bo3_ctl_esc(&p, &g, 1, 1);
        CHECK(only(&g, RT, 255) && g.sThumbLX == 32767 && g.wButtons == START,
              "Esc leaves the pedal and the stick");
    }
}

/* The gate (GitHub #52, #111). A title frame in play, with one thing changed. */
static bo3_ctl_title in_play(void)
{
    bo3_ctl_title t;

    memset(&t, 0, sizeof t);
    t.paused_by = 0xFFFFFFFFu;
    t.state = 4;                /* the chase camera */
    t.phase = 2;
    return t;
}

static void test_gate(void)
{
    bo3_ctl_title t = in_play();

    CHECK(bo3_ctl_driving(&t), "in play");

    /* The results byte left by an earlier event: the next one still drives. */
    t = in_play();
    t.results = 1;
    CHECK(bo3_ctl_driving(&t), "stale results byte, in play");
    t.state = 0x40;
    CHECK(bo3_ctl_driving(&t), "stale results byte, crash camera");
    t = in_play();
    t.results = 1;
    t.phase = 1;
    t.countdown = 0x40900000u;  /* 4.5 s */
    CHECK(bo3_ctl_driving(&t), "stale results byte, the countdown");

    /* The results of this event: a menu, also with the phase set back. */
    t = in_play();
    t.state = BO3_CTL_STATE_RESULTS;
    t.results = 1;
    CHECK(!bo3_ctl_driving(&t), "results up, phase 2");
    t.phase = 0;
    t.countdown = 0x40400000u;
    CHECK(!bo3_ctl_driving(&t), "results up, phase set back for a retry");
    t = in_play();
    t.state = BO3_CTL_STATE_RESULTS;
    t.phase = 3;
    CHECK(!bo3_ctl_driving(&t), "results camera on its way, phase 3");

    t = in_play();
    t.state = 0x100;            /* the replay behind the results */
    t.results = 1;
    t.no_pause = 1;
    CHECK(!bo3_ctl_driving(&t), "the replay, phase 2");

    /* The other words. */
    t = in_play(); t.paused_by = 0;
    CHECK(!bo3_ctl_driving(&t), "paused");
    t = in_play(); t.module_step = 6;
    CHECK(!bo3_ctl_driving(&t), "leaving the module");
    t = in_play(); t.no_pause = 1;
    CHECK(!bo3_ctl_driving(&t), "no pause");
    /* An online race's pause menu: nobody is paused, the race goes on, and
     * the menu takes A and the d-pad (the PS2 layout and the touch race
     * layout must be off). Closing it gives the car back. */
    t = in_play(); t.online_menu = 1;
    CHECK(!bo3_ctl_driving(&t), "online pause menu, paused_by -1");
    t.phase = 1; t.countdown = 0x40400000u;
    CHECK(!bo3_ctl_driving(&t), "online pause menu over the countdown");
    t = in_play(); t.online_menu = 0;
    CHECK(bo3_ctl_driving(&t), "online pause menu closed");
    t = in_play(); t.intro = 1;
    CHECK(!bo3_ctl_driving(&t), "fly-by or captions");
    t = in_play(); t.phase = 3;
    CHECK(!bo3_ctl_driving(&t), "finished");
    t = in_play(); t.phase = 1; t.countdown = 0xBF800000u;
    CHECK(!bo3_ctl_driving(&t), "before the countdown, -1");
    t = in_play(); t.phase = 1; t.countdown = 0;
    CHECK(!bo3_ctl_driving(&t), "before the countdown, 0");
}

int main(void)
{
    test_table();
    test_nintendo();
    test_handover();
    test_chain();
    test_esc();
    test_gate();
    printf("bo3_controls: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
