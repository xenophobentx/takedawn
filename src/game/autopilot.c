/*
 * autopilot.c - RECOMP_DEBUG=autopilot=1: the title's own AI drives player
 * 1's car while it races (BO-130). A test driver for soaks and scripted
 * scenes; debug only, off unless set, not in the overlay.
 *
 * The title can already do this. CB3PlayerCar (PS2 names, Nahelam's symbol
 * list) keeps a byte at car + 0x27D8, "AI controlled", which the player
 * car's update (sub_0018C510, vtable 0x3B11A0 slot 1) tests first: 0 runs
 * the human path (the pad), 1 runs sub_00170820, the rivals' own update
 * (slot 0 of their vtable 0x3B1204: the racing line, the steering, the
 * lap and finish tracking (sub_0018D790, sub_001711B0) and the stuck-car
 * reset). The title sets the byte itself on the rolling start
 * (sub_001935F0) and at the finish (sub_0018C510 when the event phase,
 * car + 0x134C, is 3), each time with the same steps: the byte, the four
 * words at + 0x19D0 zeroed, and sub_00179760 with eax = car + 0x1A00 (the
 * AI driver's route state reset: no current segment, look the car up
 * again). CB3PlayerCar::SetAIControlled (sub_0018CB60) is the same.
 *
 * The car's physics reads the byte as well: sub_00117870 (vtable 0x3B1108)
 * steps the human physics (sub_0011BE50, the pad's controls) at 0 and the
 * AI's (sub_00104A90) at 1, in the simulation step after the update. So the
 * byte stays set while the AI drives: set only around the update (tried
 * first), the AI steered, the physics read an empty pad, and the car
 * coasted to a stop. Kept set, the race looks like a player's (chase
 * camera, HUD, drifts, crashes and the reset after one): the state the
 * title itself is in on the rolling start. The "ap_update" mark (after
 * 0x18C511, esi the car) sets it with the takeover steps; the autopilot
 * never writes 0 to it. It acts only while the byte is 0 (the title is not
 * driving the car itself) and the event is in play: player 1's car
 * (0x73A1D0, player index + 0x27D0 = 0), the game module running, nobody
 * paused, phase 2 (from the GO to the finish). At the finish the phase
 * turns 3 inside the AI's own update and the title goes on as it does for
 * any player (its finish takeover, the slow motion, the results).
 *
 * Nobody touches the pad, and the title pauses a race after some minutes
 * without input: each controller record (0x188 bytes at 0x4AE724) has the
 * time of its last input at + 0x184, which sub_00019A10 stamps with the
 * clock at 0x4AE200 whenever a channel moves. Without input the PAUSED menu
 * came up on the third lap (about 260 s after the GO), with nobody named
 * as the pausing player. While the autopilot drives, it stamps the four
 * records itself every update.
 *
 * The log: "[AUTOPILOT] on", "[AUTOPILOT] engaged" when it takes the car
 * in a race, and at the finish "[AUTOPILOT] result place P/N time T s best
 * lap B s laps L" from the car's race record (+ 0x10D0 the placing, 1 =
 * first; + 0x10DC the race time; + 0x10E4 the laps done; + 0x110C the best
 * lap; found from a finished race: 6th, 395.017 s, best lap 2:00.78 as the
 * results screen showed).
 */
#include "region.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "platform/recomp_env.h"
#include "platform/recomp_patch.h"
#include "recomp_types.h"

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esi, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void BO3_SUB(sub_00179760, sub_00179A00)(void);

#define AP_M8(va)  (*(uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define AP_M16(va) (*(int16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define AP_M32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define AP_MF(va)  (*(float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define AP_CAR1         BO3_VA(0x0073A1D0u, 0x0073A4E0u)     /* the world (0x60EA00) + 0x12B7D0: player 1's CB3PlayerCar */
#define AP_CAR_AI       0x27D8u         /* byte: the AI drives this player car */
#define AP_CAR_PLAYER   0x27D0u         /* the player index */
#define AP_CAR_PHASE    0x134Cu         /* 1 before the GO, 2 in play, 3 finished */
#define AP_CAR_AI_RESET 0x19D0u         /* four words the title zeroes on a takeover */
#define AP_CAR_DRIVER   0x1A00u         /* sub_00179760's eax */
#define AP_CAR_PLACE    0x10D0u         /* int16: the placing, 1 = first */
#define AP_CAR_TIME     0x10DCu         /* float: the race time, s */
#define AP_CAR_LAPS     0x10E4u         /* the laps done */
#define AP_CAR_BEST     0x110Cu         /* float: the best lap, s */
#define AP_RACERS       BO3_VA(0x0073A19Cu, 0x0073A4ACu)     /* the number of cars racing */
#define AP_CTLS         BO3_VA(0x004AE724u, 0x004AEA44u)     /* the four controller records */
#define AP_CTL_SIZE     0x188u
#define AP_CTL_ACTIVE   0x184u          /* float: the time of its last input */
#define AP_NOW          BO3_VA(0x004AE200u, 0x004AE520u)     /* float: the clock sub_00019A10 stamps it with */
#define AP_MODULE       BO3_VA(0x004D5370u, 0x004D5690u)     /* the current module */
#define AP_VT_GAME      BO3_VA(0x003A9ACCu, 0x003AA078u)     /* the game module's vtable */
#define AP_PAUSED_BY    BO3_VA(0x004D537Cu, 0x004D569Cu)     /* -1 while not paused */
#define AP_MODULE_STEP  BO3_VA(0x004D53B4u, 0x004D56D4u)     /* 6: the module is being left */

static int s_driving;       /* the autopilot drove the last update */
static uint32_t s_phase;    /* player 1's phase at the last update */
static uint32_t (*s_flips)(void);

static uint32_t ap_flip(void) { return s_flips ? s_flips() : 0; }

static int ap_in_play(uint32_t car)
{
    uint32_t mod = AP_M32(AP_MODULE);

    if (mod < 0x10000u || mod >= 0x04000000u || AP_M32(mod) != AP_VT_GAME)
        return 0;
    if (AP_M32(AP_PAUSED_BY) != 0xFFFFFFFFu || AP_M32(AP_MODULE_STEP) == 6)
        return 0;
    return AP_M32(car + AP_CAR_PHASE) == 2;
}

static void ap_update(void)
{
    uint32_t car = g_esi, phase, k;

    if (car != AP_CAR1 || !g_xbox_mem_offset || AP_M32(car + AP_CAR_PLAYER) != 0)
        return;
    phase = AP_M32(car + AP_CAR_PHASE);
    if (phase == 3 && s_phase == 2 && s_driving)
        fprintf(stderr, "[AUTOPILOT] result place %d/%u time %.3f s best lap %.3f s laps %u (flip %u)\n",
                AP_M16(car + AP_CAR_PLACE), AP_M32(AP_RACERS), AP_MF(car + AP_CAR_TIME),
                AP_MF(car + AP_CAR_BEST), AP_M32(car + AP_CAR_LAPS), ap_flip());
    s_phase = phase;
    if (!ap_in_play(car)) {
        s_driving = 0;
        return;
    }
    for (k = 0; k < 4; k++)
        AP_M32(AP_CTLS + k * AP_CTL_SIZE + AP_CTL_ACTIVE) = AP_M32(AP_NOW);
    if (AP_M8(car + AP_CAR_AI)) {       /* the AI has it already */
        /* A Retry: the title's finish takeover is still set when the race
         * starts again, so the car is the AI's from the GO. */
        if (!s_driving)
            fprintf(stderr, "[AUTOPILOT] engaged at flip %u (the title's AI had it)\n", ap_flip());
        s_driving = 1;
        return;
    }
    {
        /* The title's takeover (sub_0018C510 at 0x18C688). The mark is on
         * the title's thread between two instructions: the registers the
         * call changes beyond its own work are put back. */
        uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;

        AP_M8(car + AP_CAR_AI) = 1;
        AP_M32(car + AP_CAR_AI_RESET + 0x0) = 0;
        AP_M32(car + AP_CAR_AI_RESET + 0x4) = 0;
        AP_M32(car + AP_CAR_AI_RESET + 0x8) = 0;
        AP_M32(car + AP_CAR_AI_RESET + 0xC) = 0;
        g_eax = car + AP_CAR_DRIVER;
        PUSH32(g_esp, 0);               /* the return address its ret takes */
        BO3_SUB(sub_00179760, sub_00179A00)();
        g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
    }
    if (!s_driving)
        fprintf(stderr, "[AUTOPILOT] engaged at flip %u\n", ap_flip());
    s_driving = 1;
}

void bo3_autopilot_start(uint32_t (*flips)(void))
{
    if (!recomp_env_on(RENV_AUTOPILOT))
        return;
    s_flips = flips;
    recomp_patch_on_mark("ap_update", ap_update);
    fprintf(stderr, "[AUTOPILOT] on: the title's AI drives player 1 while racing "
                    "(RECOMP_DEBUG=autopilot)\n");
}
