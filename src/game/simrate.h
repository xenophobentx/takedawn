/*
 * simrate.h - the simulation at the display's rate (graphics.native_rate)
 * and the loading screen's least time (game.loading_min): simrate.c.
 */
#ifndef BO3_SIMRATE_H
#define BO3_SIMRATE_H

#include <stdint.h>

typedef void (*bo3_title_fn)(void);

/* main.c, after xbox_SettingsInit: the setting read (and the three it
 * needs turned on). */
void bo3_sim_settings(void);
int bo3_sim_short(void);          /* the short step is in force (not in a split-screen race nor online) */

/* hirefresh.c: the step its frames run now (1/N, or the title's own while
 * off or frozen), the title's own, and the calls a call-counting routine
 * is due: the race audio manager's for `steps` steps; the HUD's, 0 or 1 a
 * step, or ~0 to run it as it comes (no short step). */
double bo3_sim_step_s(void);
double bo3_sim_title_step_s(void);
uint32_t bo3_sim_legacy_calls(uint32_t steps);
uint32_t bo3_sim_hud_calls(void);

/* The wraps of recomp/recomp_manual.c (recomp_manual_pal.c and
 * recomp_manual_pal_ensv.c have the PAL discs' twins). */
void bo3_sim_before_update(void);               /* before sub_000165F0, Update */
void bo3_sim_set_period(bo3_title_fn title_fn); /* sub_001B5880, the stepper's period */
void bo3_sim_timer_init(bo3_title_fn title_fn); /* sub_001B5A80, a timer's set-up */
void bo3_sim_camera(bo3_title_fn title_fn);     /* sub_0015E550, the chase camera */
void bo3_sim_camera_0(bo3_title_fn title_fn);   /* sub_0015C130, camera 0/14 */
void bo3_sim_camera_13(bo3_title_fn title_fn);  /* sub_00159A80, camera 13 */
void bo3_load_hold(bo3_title_fn title_fn);      /* sub_001AA100, the stage loader */

#endif /* BO3_SIMRATE_H */
