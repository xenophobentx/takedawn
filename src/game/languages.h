/* languages.h - which languages the Language row offers (game.language):
 * the ones the installed disc has, and that this program can show.
 *
 * Bit i of a mask is the toolkit's game.language choice i: 0 English,
 * 1 Japanese, 2 German, 3 French, 4 Spanish, 5 Italian (XC_LANGUAGE - 1;
 * 6..8, Korean, Chinese and Portuguese, have no files in this title), and
 * 9 Dutch, 10 Swedish, which no dashboard has: the En/Es/Nl/Sv disc's
 * program sets them in the title itself (recomp/recomp_manual_pal_ensv.c).
 *
 * A language is on the disc when its text file, Data/Global<xx>.bin, is in
 * the disc's manifest and installed: NTSC-U English (GlobalUs), the Europe
 * Fr/De/It disc French, German and Italian (no English), the
 * Europe/Australia disc English (GlobalEn), Spanish, Dutch and Swedish. No
 * game data here: file names only. */
#ifndef BO3_LANGUAGES_H
#define BO3_LANGUAGES_H

#define BO3_LANG_ENGLISH   0
#define BO3_LANG_SPANISH   4
#define BO3_LANG_DUTCH     9
#define BO3_LANG_SWEDISH   10
#define BO3_LANG_COUNT     11

/* Whether the disc has the file `path` (as on the disc: "Data/GlobalFr.bin"). */
typedef int (*bo3_lang_has_fn)(const char *path, void *ctx);

/* The languages to offer. `title_set`: the program can set the title's own
 * languages (Dutch, Swedish), which the En/Es/Nl/Sv disc's program can and
 * no other does. */
unsigned bo3_language_mask(bo3_lang_has_fn has, void *ctx, int title_set);

/* game.language's value as a choice: a name ("dutch") or a dashboard number
 * (1..9); -1 when unset or not one of them. */
int bo3_language_index(const char *setting);

/* The choice's name ("dutch"), or "?" */
const char *bo3_language_name(int index);

#endif
