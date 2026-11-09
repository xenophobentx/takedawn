/*
 * seatcam.h - the VR seat camera (seatcam.c): the race drawn from a seat in the car, as a side-by-side stereo pair for the headset
 * (the toolkit's oxr.c), or from a test seat without a headset (BO3_SEATCAM).
 */
#ifndef BO3_SEATCAM_H
#define BO3_SEATCAM_H

/* argc/argv from main: --vr, --stereo[=ipd], --seatcam[=r,u,a[,save]] and --bonnet=r,u,a[,save] (before bo3_seatcam_start). */
void bo3_seatcam_args(int argc, char **argv);
/* BO3_SEATCAM=<right>,<up>,<ahead>: the race drawn from a seat in the car. start: once at the start; apply: at the stage
 * render mark; restore: at the flip. */
void bo3_seatcam_start(void);
void bo3_seatcam_apply(void);
void bo3_seatcam_restore(void);
/* main.c's scene and UI marks: the stereo shift on for the scene, 0 for the HUD. */
void bo3_seatcam_stereo_scene(int scene);
/* softbody.c: the player car's draw begins (1) / ends (0), esi the car: the body drawn with both faces. */
void bo3_seatcam_car_draw(int begin);
/* The marks that call it, when soft-body damage is off. */
void bo3_seatcam_marks(void);

#endif /* BO3_SEATCAM_H */
