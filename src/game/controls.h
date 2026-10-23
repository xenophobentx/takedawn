/*
 * controls.h - the PS2 version's driving controls on an Xbox-shaped pad
 * (input.control_scheme = ps2, BO-106; docs/controls.md).
 *
 * The Xbox build of Burnout 3 has one control layout (its Controllers screen
 * only switches the vibration; its one action table, 0x384168, has a single
 * reader). This is the PlayStation 2 build's, as its manual gives it, laid on
 * the pad by position: the bottom face button is the PS2's Cross, the left
 * one Square, the top one Triangle, the shoulders L1 and R1, the triggers L2
 * and R2.
 *
 * The functions here are pure (no guest, no host calls): tests/controls
 * checks the table and the hand-over to the menus on every host. controls.c
 * is what calls them, on the pad state the title has just read.
 */
#ifndef BO3_CONTROLS_H
#define BO3_CONTROLS_H

#include <stdint.h>
#include <string.h>
#include "xinput_xbox.h"

/* A stick's travel that reads as nothing when it stands in for a pedal, and
 * an analog button's value from which it is held (the XDK's own figure). */
#define BO3_CTL_STICK_DEAD  8000
#define BO3_CTL_HELD        XBOX_ANALOG_BUTTON_THRESHOLD

/* One pad's state between two reads. Zero it to start. */
typedef struct bo3_ctl_port {
    uint8_t ps2;        /* the last state was given in the PS2 layout */
    uint8_t latch;      /* bit n: analog button n reads released until let go */
    uint8_t esc_down;   /* Esc was down on the last read */
    uint8_t esc_start;  /* ... and its press began while driving: it is Start until let go */
} bo3_ctl_port;

/* A stick's push (1..32767) as a pedal: 0 inside the dead part, 255 at full. */
static inline uint8_t bo3_ctl_pedal(int v)
{
    if (v <= BO3_CTL_STICK_DEAD)
        return 0;
    if (v > 32767)
        v = 32767;
    return (uint8_t)(1 + (v - BO3_CTL_STICK_DEAD) * 254 / (32767 - BO3_CTL_STICK_DEAD));
}

/*
 * The PS2 layout: *g is the pad as the title would read it (the Xbox
 * layout), and is rewritten to what the title must read for the same hands
 * to do what they would on a DualShock 2:
 *
 *   PS2 control              host control             the title reads
 *   Cross: accelerate        bottom face button       right trigger 255
 *   Square: brake/reverse    left face button         left trigger 255
 *   right stick up / down    right stick up / down    right / left trigger, by travel
 *   L1: look back            left shoulder            X
 *   Triangle: change view    top face button          Y
 *   R1: boost, impact time   right shoulder           A
 *   R2: crashbreaker         right trigger            B, 255 from a pull over 30
 *   L2: next music track     left trigger             Black, likewise
 *   Circle                   right face button        nothing (as on the PS2)
 *
 * The left stick, the d-pad, Start, Back, the stick clicks and the right
 * stick's left/right are not touched. The right stick's up/down is taken (it
 * is the pedals here), so the title reads it centred.
 *
 * nintendo: input.face_layout is nintendo, so the title's A is the button on
 * the right, B the bottom one, X the top one and Y the left one. The PS2
 * layout goes by position, whatever is printed on the buttons, so the
 * positions are read back out of the mirrored state.
 */
static inline void bo3_ctl_ps2(XBOX_GAMEPAD *g, int nintendo)
{
    const uint8_t *a = g->bAnalogButtons;
    uint8_t bottom = a[nintendo ? XBOX_BUTTON_B : XBOX_BUTTON_A];
    uint8_t left   = a[nintendo ? XBOX_BUTTON_Y : XBOX_BUTTON_X];
    uint8_t top    = a[nintendo ? XBOX_BUTTON_X : XBOX_BUTTON_Y];
    uint8_t up     = bo3_ctl_pedal(g->sThumbRY);
    uint8_t down   = bo3_ctl_pedal(-(int)g->sThumbRY);
    uint8_t o[8];

    o[XBOX_BUTTON_RTRIGGER] = bottom > up ? bottom : up;
    o[XBOX_BUTTON_LTRIGGER] = left > down ? left : down;
    o[XBOX_BUTTON_X]        = a[XBOX_BUTTON_WHITE];
    o[XBOX_BUTTON_Y]        = top;
    o[XBOX_BUTTON_A]        = a[XBOX_BUTTON_BLACK];
    o[XBOX_BUTTON_B]        = a[XBOX_BUTTON_RTRIGGER] > BO3_CTL_HELD ? 255 : 0;
    o[XBOX_BUTTON_BLACK]    = a[XBOX_BUTTON_LTRIGGER] > BO3_CTL_HELD ? 255 : 0;
    o[XBOX_BUTTON_WHITE]    = 0;
    memcpy(g->bAnalogButtons, o, sizeof o);
    g->sThumbRY = 0;
}

/* input.face_layout = nintendo, on a state it has not been applied to: A
 * and B, X and Y change places, as the toolkit does while it reads a host
 * pad (xbox_InputMirrorFaceSdl / XInput, then the map). Only a pad script
 * needs it here (controls.c): a script's buttons are written into the
 * report after the host pad is read, so no face layout ever reaches them. */
static inline void bo3_ctl_mirror(XBOX_GAMEPAD *g)
{
    uint8_t t = g->bAnalogButtons[XBOX_BUTTON_A];

    g->bAnalogButtons[XBOX_BUTTON_A] = g->bAnalogButtons[XBOX_BUTTON_B];
    g->bAnalogButtons[XBOX_BUTTON_B] = t;
    t = g->bAnalogButtons[XBOX_BUTTON_X];
    g->bAnalogButtons[XBOX_BUTTON_X] = g->bAnalogButtons[XBOX_BUTTON_Y];
    g->bAnalogButtons[XBOX_BUTTON_Y] = t;
}

/*
 * One read of one pad. ps2: the PS2 layout holds for this read (the setting
 * is ps2 and the title is taking driving input). Otherwise *g is left as it
 * is, but for one thing: a button that is down when the PS2 layout ends, and
 * that the title did not see down under it, would be a press the player
 * never made (the bottom button held to accelerate becomes A, and A resumes
 * from the pause menu). Such a button reads released until it is let go.
 * Going the other way nothing is held back: the button that confirmed
 * "Resume" may stay down and accelerate.
 */
static inline void bo3_ctl_apply(bo3_ctl_port *p, XBOX_GAMEPAD *g, int ps2, int nintendo)
{
    int i;

    if (ps2) {
        bo3_ctl_ps2(g, nintendo);
        p->ps2 = 1;
        p->latch = 0;
        return;
    }
    if (p->ps2) {
        XBOX_GAMEPAD was = *g;          /* what the title saw a read ago */

        bo3_ctl_ps2(&was, nintendo);
        p->ps2 = 0;
        p->latch = 0;
        for (i = 0; i < 8; i++)
            if (g->bAnalogButtons[i] > BO3_CTL_HELD && was.bAnalogButtons[i] <= BO3_CTL_HELD)
                p->latch |= (uint8_t)(1u << i);
    }
    for (i = 0; i < 8 && p->latch; i++) {
        if (!(p->latch & (1u << i)))
            continue;
        if (g->bAnalogButtons[i] > BO3_CTL_HELD)
            g->bAnalogButtons[i] = 0;
        else
            p->latch &= (uint8_t)~(1u << i);
    }
}

/*
 * Esc pauses a race. The keyboard map gives Esc the Back button (menus: go
 * back, leave the pause menu), and Enter Start. While driving Esc is Start as
 * well, like the pause key of a PC game: esc is whether Esc is down and the
 * keyboard is where the Back in *g comes from (the caller checks both),
 * driving whether the title is taking driving input now (ctl_driving).
 *
 * The press decides, not the frame after it: a press that began while driving
 * is Start for as long as it is held. The pause menu is up a frame later, and
 * the Esc still down would otherwise arrive there as a Back, and close it.
 * An Esc that was already down when driving began (held through the countdown
 * from a menu) stays a Back: there was no press.
 *
 * Back is a driving action in the title's table (action 6, channel 13), but
 * neither manual gives it a use in a race, and a controller's Back is not
 * touched: only a held Esc is turned into Start.
 */
static inline void bo3_ctl_esc(bo3_ctl_port *p, XBOX_GAMEPAD *g, int esc, int driving)
{
    if (!esc) {
        p->esc_down = p->esc_start = 0;
        return;
    }
    if (!p->esc_down && driving)
        p->esc_start = 1;
    p->esc_down = 1;
    if (p->esc_start)
        g->wButtons = (WORD)((g->wButtons & ~XBOX_GAMEPAD_BACK) | XBOX_GAMEPAD_START);
}

/* controls.c: hooks the title's pad read. flips: the title's flip count,
 * for BO3_CTL_LOG. Call from main, before the guest runs. */
void bo3_controls_start(uint32_t (*flips)(void));

/* RECOMP_DEBUG=autopilot=1: the title's AI drives player 1 (autopilot.c, BO-130). */
void bo3_autopilot_start(uint32_t (*flips)(void));

/* graphics.softbody: soft-body vehicle damage, drawn only (softbody.c). */
void bo3_softbody_start(uint32_t (*flips)(void));

/* interface.menu_entry: TAKEDAWN SETTINGS in the title's Settings menu (menuentry.c, BO-133). */
void bo3_menu_entry_start(void);

/* A or START leaving a training film (tutorial.c). */
void bo3_tutorial_start(void);

/* display.score_board: the score board of a race's HUD as shipped or
 * fading as one (scoreboard.c, BO-165). render and flip are called from
 * main.c's uw_stage_render and uw_flip marks. */
void bo3_score_board_start(uint32_t (*flips)(void));
void bo3_score_board_render(void);
void bo3_score_board_flip(void);

/* The overlay's Unlock everything, for the session only (unlockall.c, BO-131). */
void bo3_unlock_all_start(void);

#endif /* BO3_CONTROLS_H */
