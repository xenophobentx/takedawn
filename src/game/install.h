/*
 * install.h - the game's data and the program's identity (BO-103).
 *
 * A release is the program alone; the game's data comes from the player's
 * own disc. This is the part of the executable that puts it in place
 * (--install, the toolkit's src/install with this disc's manifest), finds it
 * at start, and says which build this is (--version).
 *
 * The data root holds "Burnout 3 Takedown/" (the disc's files) and, beside
 * it, .complete, which the install writes last. Where the game looks, in
 * this order:
 *
 *   1. a default.xbe named on the command line: that file, as before
 *   2. --data <dir> or TAKEDAWN_DATA: that root; it must be a whole install
 *   3. the working directory, when it holds "Burnout 3 Takedown/default.xbe":
 *      a developer's checkout (and the Android app's files folder); no
 *      marker is asked for
 *   4. the folder the player chose in the setup window (Change...): the
 *      first line of data-folder.txt in the per-user local folder below
 *      (%LOCALAPPDATA%\Takedawn on Windows, the save folder elsewhere),
 *      written by --install --remember; it must be a whole install
 *   5. the per-user root, which must be a whole install:
 *        macOS    ~/Library/Application Support/Takedawn/Game
 *        Linux    $XDG_DATA_HOME/takedawn/Game, else ~/.local/share/takedawn/Game
 *        Windows  %LOCALAPPDATA%\Takedawn\Game
 *
 * For 2, 4 and 5 the root becomes the working directory, where the title and
 * the shader pre-build expect the data. Without a whole install there the
 * title is not started: exit status BO3_EXIT_NO_DATA and, on stdout,
 *
 *   PROTOCOL 1
 *   STEP data FAIL not-installed <text>
 *   RESULT FAIL data not-installed
 *
 * or "unverified" in place of "not-installed" when the files are there
 * without the marker (the source installer's): --install from that folder
 * into the same root checks them where they lie.
 *
 * A person who starts the game gets the setup window instead of those lines
 * (tools/install/gui, the program "takedawn-setup" in the executable's own
 * folder; TAKEDAWN_SETUP names another): the game becomes that process,
 *
 *   takedawn-setup --program <this executable> [--dest <root of case 2>]
 *
 * which asks for the disc, runs --install and starts the game again. Not
 * with --no-setup, --headless, RECOMP_HEADLESS or TAKEDAWN_NO_SETUP, not on
 * Linux without a display, and not on Android (its own setup page): there
 * the lines above and the status are the answer.
 */
#ifndef BO3_INSTALL_H
#define BO3_INSTALL_H

/* What the user sees, and the per-user folders (saves and settings, cache).
 * The PAL build keeps folders of its own ("Takedawn PAL"), so that it never
 * takes the NTSC-U build's game data for its own (another manifest: the
 * setup would install over it) and both can be installed side by side. */
#define BO3_APP_NAME "Takedawn"
#if defined(BO3_REGION_PAL) && BO3_REGION_PAL
#define BO3_USER_DIR BO3_APP_NAME " PAL"
#else
#define BO3_USER_DIR BO3_APP_NAME
#endif

#define BO3_EXIT_NO_DATA 3

/* Takedawn 0.1.0 "Lazy Generation" */
const char *bo3_version_line(void);
/* That and the two commits, for one line of a log or the overlay. */
const char *bo3_version_long(void);
/* Just the number ("0.1.0"): what "Check for updates" compares the newest release with. */
const char *bo3_version_number(void);
/* --version: the lines on stdout. Returns the exit status. */
int bo3_version_main(void);

/* The folders of the port's earlier name (Burnout3, burnout3, xboxrecomp)
 * are moved to the new one where that is not there yet (or is there empty),
 * and the shader cache gets the new name. Off Windows the save folder's old
 * name stays as a link to the new one. Idempotent; no file is deleted or
 * written over.
 *
 * Only two things move them: a game start that has a title to run
 * (bo3_data_enter, once it has found the data) and a real --install. Every
 * query leaves them alone and says where things are now: --version,
 * --data-status (which adds "LOG data pending-move <from> -> <to>" lines),
 * --install --check, --bug-report, a start without data, and so the setup
 * window until its Install button is pressed. */
void bo3_user_dirs_init(void);
/* How many of those moves are still to come; `text` (may be NULL) gets one
 * "<from> -> <to>" line each. Reads only. */
int bo3_user_dirs_pending(char *text, unsigned n);
/* The save and settings folder as it stands now: under the old name while
 * its move is pending. Nothing is made. */
const char *bo3_save_dir_now(void);
/* The shader cache's folder name for this port, without moving anything. */
void bo3_user_dirs_names(void);
/* The lines it printed ("" when nothing was moved), for the run log. */
const char *bo3_user_dirs_notes(void);

/* --install <image-or-folder> [--dest <dir>] [--remember] [--check]; argv
 * starts after --install. --remember keeps <dir> as case 4 above. Speaks
 * the installer's line protocol; returns the exit status. */
int bo3_install_main(int argc, char **argv);

/* Find the data (above) and go there. `xbe_named`: case 1. `data_arg`:
 * --data's, or NULL. Flags: BO3_DATA_QUIET, nothing is printed and nothing
 * fails (the bug report, which says itself what is missing);
 * BO3_DATA_NO_SETUP, never the setup window. 0 to go on, else the exit
 * status; with the setup window it does not return. */
#define BO3_DATA_QUIET    1
#define BO3_DATA_NO_SETUP 2
/* a query that needs the data's place (--build-shaders --status): found as
 * it stands, the folders of the earlier name are not moved */
#define BO3_DATA_NO_MOVE  4
int bo3_data_enter(const char *data_arg, int xbe_named, int flags);

/* --data-status [--data <dir>]: where the data is (or would be put) and
 * whether it is whole, without starting anything; what the setup window
 * asks first. On stdout, in the installer's protocol:
 *
 *   LOG data root <dir>            LOG data files <n>
 *   LOG data place named|saved|default|checkout   (case 2, 4, 5 or 3 above)
 *   LOG data default <dir>         (case 5's root)
 *   LOG data disc <title, build>   LOG data bytes <n>
 *   LOG data version <line>        LOG data free-needed <bytes>
 *   STEP data OK <text> / RESULT OK <root>                      status 0
 *   STEP data FAIL not-installed|unverified <text> / RESULT FAIL data <code>   status 3
 */
int bo3_data_status_main(int argc, char **argv);

/* The overlay's "Add Steam library art" (Linux only): whether the setup program
 * is there, and running `takedawn-setup --steam-art`, which writes the art of the
 * Takedawn shortcut Steam already has. `line` gets the sentence to show; returns
 * 1 when art was written. Blocks until the program is done. */
#if defined(__linux__) && !defined(__ANDROID__)
int bo3_steam_art_available(void);
int bo3_steam_art_run(char *line, unsigned n);
#endif

/* The folder enhance.toml is read from: the per-user settings folder
 * (RECOMP_SAVE_DIR, else the save folder above) when it has one, else the
 * executable's own folder. */
const char *bo3_enhance_root(void);

#endif /* BO3_INSTALL_H */
