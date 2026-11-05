/*
 * upscale.h - sharper menu pictures (BO-311, docs/upscale-art.md): the
 * loading screens and the menu art of the player's own disc, upscaled 4x
 * once on this machine by an AI model, and kept as a texture pack
 * (docs/mod-folders.md, Texture packs) that the game loads like any other.
 *
 *   burnout3 --upscale-art [--quality best|fast] [--force]
 *   burnout3 --upscale-art --check | --status | --remove
 *
 * The upscaler is Real-ESRGAN's realesrgan-ncnn-vulkan (MIT), a program of
 * its own that every desktop package carries with its models (upscale.c,
 * find_engine); nothing is downloaded. Progress is the line protocol of the
 * installer and the shader pre-build:
 *
 *   PROTOCOL 1 / PLAN upscale / STEP upscale BEGIN|OK|SKIP|FAIL <code> <text> /
 *   PROGRESS upscale <0..1> <text> / LOG upscale <text> / RESULT OK|FAIL ...
 *
 * "LOG upscale work <dir>" names the folder whose file "cancel" stops it
 * (looked for four times a second); a later run goes on from there.
 *
 * Android (BO-326): an app cannot start a program, so the same run is a
 * thread of the game (the MODS page's rows) or of the setup page, with ncnn
 * linked in (upscale_ncnn.h) and the fast model from the APK's assets.
 */
#ifndef BO3_UPSCALE_H
#define BO3_UPSCALE_H

#include <stddef.h>

/* Whether this machine is offered the step, and how: 2 offered and ticked by
 * default, 1 offered but not ticked (a handheld, an integrated GPU), 0 not
 * offered. quality: "best" or "fast"; why: a few words for the log.
 * TAKEDAWN_UPSCALE_OFFER=on|off|no[:best|fast] overrides it (tests). */
int bo3_upscale_offer(char *quality, size_t qn, char *why, size_t wn);

/* Whether the pack is made ("made"), the setup window's offer was turned
 * down (--upscale-art --dismiss: "declined"), or neither ("none"). */
const char *bo3_upscale_pack_state(void);

/* --upscale-art: argv from the first option after it; xbe_path is the game's
 * default.xbe (its Data folder is read). Returns the exit status. */
int bo3_upscale_main(int argc, char **argv, const char *xbe_path);

/* The folder of mods made on this machine (the pack is one of them):
 * <the per-user local folder>/generated-mods, or TAKEDAWN_GENERATED_MODS.
 * 1 with the path, 0 when there is none ("" turns it off). Not created. */
int bo3_generated_mods_dir(char *out, size_t n);

/* The MODS page's live switch (modsmenu.c): for the pack made here, turns it
 * on or off in the running game and returns 1; 0 for any other mod. */
int bo3_upscale_mods_live(const char *id, int on);

/* Packs earlier test builds made that are no longer made or loaded (the
 * experimental HUD pack, ai-upscaled-hud): deleted from the made-mods folder,
 * with a log line. At the start, before the mod folders are read. */
void bo3_upscale_retire_packs(void);

/* The overlay's rows: make, stop, make again, remove. On the Mods page
 * (BO-327, modstab.c): host_init decides whether they are offered here, then
 * row_count (0: none) and row(row, activate) as xbox_OverlaySetHostRows'
 * function: the row's text now (NULL: no such row at the moment), and with
 * activate 1 the row pressed. */
void bo3_upscale_host_init(const char *xbe_path);   /* the game's default.xbe (Android's run reads its Data) */
int bo3_upscale_row_count(void);
const char *bo3_upscale_row(int row, int activate);

#endif
