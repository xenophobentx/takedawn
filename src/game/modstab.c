/*
 * modstab.c - the Mods tab of the settings overlay (BO-316).
 *
 * The same list and the same on and off as the game's own MODS page (modsmenu.c): the rows are read from
 * modfs (name, version and author from mod.toml, whether a mod can be used) and the choices live in the
 * model both views share (modsmodel.h), so a toggle here shows on that page and the other way round.
 * Turning a mod on or off changes the choice and saves it to the mod list at once (BO-338: no Apply step here;
 * it is the one apply the game's own page has, which also calls the live-apply hook, bo3_mods_set_apply_hook).
 * A saved change is used from the next start unless the game could put it to use at once, so each mod's row
 * carries the toolkit's NEXT START tag, lit while a change waits. The game's own MODS page keeps its APPLY line.
 *
 * BO-327: the first line names the mods folder (the default one modfs scans with no variable set), the
 * "Upscale menu pictures with AI" rows (upscale.c) live here, under the list the pack joins, and "Reset this
 * page" turns every mod back on.
 *
 * The page is the toolkit's host page (overlay.h): it asks for the rows every frame, and activates one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modsmodel.h"

#ifdef XBOXRECOMP_HAVE_OVERLAY
#include "install.h"
#include "overlay.h"
#include "upscale.h"
#include "platform/recomp_exe_dir.h"
#include "platform/recomp_save_dir.h"

#define FIRST_MAX  3                        /* the mods folder, how many, TAKEDAWN_MODS's folders */
#define LAST_ROWS  1                        /* a note: a restart is needed, or the mod list was not saved */
#define AI_ROWS    3                        /* a title and up to two rows (upscale.h) */
#define MAX_SHOWN  (XBOX_OVERLAY_HOSTPAGE_ROWS - FIRST_MAX - LAST_ROWS - AI_ROWS - 1)

static char s_where[420];                   /* the line naming the mods folder, made once */
static char s_extra[420];                   /* the folders TAKEDAWN_MODS adds, or "" */

static void shorten_home(char *out, size_t n, const char *path)
{
#if !defined(_WIN32)
    const char *home = getenv("HOME");
    if (home && *home && !strncmp(path, home, strlen(home)) && (path[strlen(home)] == '/' || !path[strlen(home)])) {
        snprintf(out, n, "~%s", path + strlen(home));
        return;
    }
#endif
    snprintf(out, n, "%s", path);
}

/* The mods folder: the default one modfs scans (bo3_modfs_default_dir: data/mods beside the program in
 * portable mode, else mods in the per-user data folder; on Android the app's), the same folder whether it
 * exists yet or not. TAKEDAWN_MODS's folders are an extra source, said on a line of their own. */
static void make_where(void)
{
    char dir[1024], shown[1100];
    const char *env = getenv("TAKEDAWN_MODS");
    const char *def = bo3_modfs_default_dir_name();
    int portable = 0;

    s_extra[0] = 0;
    if (env && *env) {
        size_t n = strcspn(env, ";");
        char first[1024];
        snprintf(first, sizeof first, "%.*s", (int)(n < sizeof first ? n : sizeof first - 1), env);
        shorten_home(shown, sizeof shown, first);
        snprintf(s_extra, sizeof s_extra, "Also read: %s%s (TAKEDAWN_MODS).", shown,
                 strchr(env, ';') ? " and the other folders named" : "");
    }
    if (def && *def) {
        char root[1024];
        snprintf(dir, sizeof dir, "%s", def);
        portable = recomp_portable_root(root, sizeof root);
    } else {
        char root[1024];
        if (recomp_portable_root(root, sizeof root)) {
            snprintf(dir, sizeof dir, "%s/mods", root);
            portable = 1;
        } else if (recomp_user_dir(RECOMP_DIR_DATA, BO3_USER_DIR, root, sizeof root)) {
            snprintf(dir, sizeof dir, "%s/mods", root);
        } else {
            snprintf(dir, sizeof dir, "mods");
        }
    }
    shorten_home(shown, sizeof shown, dir);
    if (env && !*env)
        snprintf(s_where, sizeof s_where, "Mods folder: %s (off: TAKEDAWN_MODS is set empty)", shown);
    else
        snprintf(s_where, sizeof s_where, "Mods folder: %s%s", shown, portable ? " (portable mode)" : "");
}

/* The lines above the list: the mods folder, how many, and TAKEDAWN_MODS's folders when it names any. */
static int first_rows(void)
{
    if (!s_where[0])
        make_where();
    return s_extra[0] ? 3 : 2;
}

static int shown_mods(void)
{
    int n = bo3_mods_count();
    return n > MAX_SHOWN ? MAX_SHOWN : n;
}

/* The upscale rows that are there now (a row whose text is NULL is not). */
static int ai_rows(int *which)
{
    int n = 0, total = bo3_upscale_row_count();
    for (int r = 0; r < total && n < AI_ROWS - 1; r++) {
        const char *t = bo3_upscale_row(r, 0);
        if (t && *t)
            which[n++] = r;
    }
    return n;
}

/* The list part: the mods, "more" and the note; none without a mod folder in use. */
static int list_rows(void)
{
    int n = 0;
    if (bo3_modfs_folder_count() <= 0)
        return 0;
    n += shown_mods();
    if (bo3_mods_count() > shown_mods())
        n++;
    if (bo3_mods_count() > 0)
        n += LAST_ROWS;
    return n;
}

static void hp_reset(void);
static xbox_overlay_hostpage s_page;

static int hp_count(void)
{
    int which[AI_ROWS], ai = ai_rows(which);
    /* A reset row only with mods to reset; asked here, as the overlay asks for the count first on every
     * frame (the mod list is made after this page is registered). */
    s_page.reset = bo3_mods_count() > 0 ? hp_reset : NULL;
    return first_rows() + list_rows() + (ai ? ai + 1 : 0);
}

static void put(xbox_overlay_hostrow *o, int kind, const char *label, const char *value, const char *hint)
{
    o->kind = kind;
    snprintf(o->label, sizeof o->label, "%s", label);
    snprintf(o->value, sizeof o->value, "%s", value ? value : "");
    snprintf(o->hint, sizeof o->hint, "%s", hint ? hint : "");
}

/* The row's place in the page: 0 the mods folder, 1 how many, 2 TAKEDAWN_MODS's folders (when set); then the mods,
 * "more", the note; then the upscale rows under their title. */
static void hp_row(int i, xbox_overlay_hostrow *o)
{
    const int nm = bo3_mods_count(), shown = shown_mods(), lr = list_rows(), fr = first_rows();
    char buf[400];

    if (i == 0) {
        put(o, XBOX_HOSTROW_INFO, s_where, NULL, NULL);
        return;
    }
    if (i == 1) {
        int active = 0;
        for (int k = 0; k < nm; k++)
            active += bo3_modfs_mod(k)->usable && bo3_modfs_mod(k)->enabled;
        if (nm)
            snprintf(buf, sizeof buf, "%d installed, %d in use now.", nm, active);
        else
            snprintf(buf, sizeof buf, "No mods are installed. Put each mod in its own folder in the mods folder,"
                     " then start the game again.");
        put(o, XBOX_HOSTROW_INFO, buf, NULL, NULL);
        return;
    }
    if (i == 2 && fr == 3) {
        put(o, XBOX_HOSTROW_INFO, s_extra, NULL, NULL);
        o->adv = 1;                         /* for those who set the variable: behind "Show advanced settings" */
        return;
    }
    i -= fr;
    if (i >= lr) {
        int which[AI_ROWS], ai = ai_rows(which);
        i -= lr;
        if (i == 0) {
            put(o, XBOX_HOSTROW_HEADER, "Sharper menu pictures", NULL, NULL);
            return;
        }
        if (i - 1 < ai) {
            const int r = which[i - 1];
            const char *t = bo3_upscale_row(r, 0);
            put(o, XBOX_HOSTROW_ACTION, t ? t : "", NULL, r % 2 == 0
                ? "Makes the game's menu pictures and loading screens sharper with an AI upscaler, here on this"
                  " computer, as a mod of their own that is switched on. It is in the mod list from the next"
                  " start."
                : "Deletes the sharper menu pictures this computer made; the game's own are back.");
        }
        return;
    }
    if (i < shown) {
        const bo3_mod *m = bo3_modfs_mod(i);
        char ver[64] = "", by[200] = "", hint[700];

        if (m->manifest.version[0])
            snprintf(ver, sizeof ver, "  v%s", m->manifest.version);
        if (m->manifest.author[0])
            snprintf(by, sizeof by, "  by %s", m->manifest.author);
        snprintf(buf, sizeof buf, "%s%s%s", bo3_mods_name(m), ver, by);
        if (!m->usable)
            snprintf(hint, sizeof hint, "This mod cannot be used: %s", m->problem[0] ? m->problem : "its id is used twice.");
        else {
            snprintf(hint, sizeof hint, "%s%s%s", m->manifest.description,
                     m->manifest.description[0] ? "  " : "",
                     m->wins + m->loses ? "It supplies files that another mod or folder has too (see the log). " : "");
            snprintf(hint + strlen(hint), sizeof hint - strlen(hint),
                     "Turning it on or off is saved at once; the game uses the change from the next start.");
        }
        put(o, XBOX_HOSTROW_TOGGLE, buf, bo3_mods_state(i), hint);
        o->on = bo3_mods_next_on(i);
        o->enabled = m->usable;
        if (m->usable) {
            o->tag = XBOX_HOSTTAG_NEXT_START;
            o->tag_hot = bo3_mods_restart_pending(i);
        }
        return;
    }
    i -= shown;
    if (nm > shown) {
        if (i == 0) {
            snprintf(buf, sizeof buf, "%d more mods are not listed here; the game's own MODS page has them.", nm - shown);
            put(o, XBOX_HOSTROW_INFO, buf, NULL, NULL);
            return;
        }
        i--;
    }
    /* The note under the list. A save that failed first (the choice stays, the next change tries again);
     * then choices made on the game's own MODS page and not applied there. */
    if (bo3_mods_save_failed())
        put(o, XBOX_HOSTROW_WARN, "The mod list could not be saved (see the log). Changing a mod tries again.",
            NULL, NULL);
    else if (bo3_mods_unsaved())
        put(o, XBOX_HOSTROW_WARN, "Choices from the game's MODS page are not saved yet: changing any mod here saves them.",
            NULL, NULL);
    else if (bo3_mods_restart_needed())
        put(o, XBOX_HOSTROW_WARN, "Restart the game to use the saved mods.", NULL, NULL);
    else
        put(o, XBOX_HOSTROW_INFO, "", NULL, NULL);
}

static void hp_activate(int i)
{
    const int shown = shown_mods(), lr = list_rows(), fr = first_rows();

    if (i < fr)
        return;
    i -= fr;
    if (i >= lr) {
        int which[AI_ROWS], ai = ai_rows(which);
        i -= lr + 1;                        /* past the title */
        if (i >= 0 && i < ai)
            bo3_upscale_row(which[i], 1);
        return;
    }
    if (i < shown) {
        /* No Apply step (BO-338): the choice is saved at once, with every other one waiting, and goes
         * through the apply hook as the Apply line of the game's page does. */
        bo3_mods_toggle(i);
        bo3_mods_apply();
    }
}

/* Reset this page: every mod that can be used back on (a mod is on unless the mod list says off), saved
 * at once as a reset is on the other pages. From the next start, as any change of the list. */
static void hp_reset(void)
{
    for (int i = 0; i < bo3_mods_count(); i++)
        if (bo3_modfs_mod(i)->usable && !bo3_mods_next_on(i))
            bo3_mods_toggle(i);
    bo3_mods_apply();
}

static xbox_overlay_hostpage s_page = { "Mods", hp_count, hp_row, hp_activate, NULL };

void bo3_modstab_start(void)
{
    xbox_OverlaySetHostPage(&s_page);
}

#else

void bo3_modstab_start(void)
{
}

#endif
