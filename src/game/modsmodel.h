/*
 * modsmodel.h - the state of the mods' on and off, shared by the game's own MODS page (modsmenu.c) and
 * the settings overlay's Mods tab (modstab.c) (BO-316).
 *
 * There is one choice per mod, kept in modsmenu.c: a toggle in either view is the same toggle, and the
 * other shows it the next time it draws (the page rebuilds its lines at its next idle frame). Choices are
 * saved by apply, to the mod list (modfs.h: bo3_modfs_set_enabled), and are used from the next start, as
 * the page has always done. The overlay pauses the title, so the two are never driven at once.
 */
#ifndef BO3_MODSMODEL_H
#define BO3_MODSMODEL_H

#include "modfs.h"

int  bo3_mods_count(void);
/* "ON", "OFF", "ON*", "OFF*" (a star: the next start differs from this one), "INVALID", "SAME ID". */
const char *bo3_mods_state(int i);
int  bo3_mods_next_on(int i);              /* on at the next start, with the player's choice */
int  bo3_mods_restart_pending(int i);      /* 1: the saved choice differs from this start's and is not in use yet (BO-338) */
const char *bo3_mods_name(const bo3_mod *m);   /* its name, or its folder when mod.toml was not read */
void bo3_mods_refresh(void);               /* read what the mod list says now */
void bo3_mods_toggle(int i);               /* as A on the mod's line; nothing for a mod that cannot be used */
int  bo3_mods_unsaved(void);               /* 1: apply would change the mod list */
int  bo3_mods_restart_needed(void);        /* 1: the mod list differs from what this start uses */
int  bo3_mods_save_failed(void);
const char *bo3_mods_apply_text(void);     /* the apply line's words: "APPLY - RESTART REQUIRED", "NO CHANGES"... */
int  bo3_mods_apply(void);                 /* save the choices; 1 when all were saved (or none to save) */

/* The one hook for putting a saved change to use at once, without a restart (the generated mods of the
 * upscaler, BO-311): called after apply saved `changes` mods (not when nothing changed or a save failed),
 * from either view. Return 1 when the changes were put to use (the line then says APPLIED, not that a
 * restart is needed), 0 to leave them for the next start. NULL: none (the default). */
void bo3_mods_set_apply_hook(int (*fn)(int changes));

#endif
