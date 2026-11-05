/*
 * langpack.h - the European discs' languages read in place of the North
 * American disc's English (langpack.c).
 */
#ifndef BO3_LANGPACK_H
#define BO3_LANGPACK_H

#include "disc_install.h"               /* xr_manifest */

/* The two European (PAL) retail discs whose languages can be added: 0 the
 * Fr,De,It one, 1 the En,Es,Nl,Sv one. Each goes to its own folder under
 * <data>/Languages. */
#define BO3_LANGPACK_DIR   "Languages"
#define BO3_LANGPACK_DISCS 2

/* Disc `disc`'s list (install.c --install-languages copies and checks it),
 * its folder relative to the data root, and what it brings in words ("the
 * French, German and Italian files"); NULL past the last. */
const xr_manifest *bo3_langpack_disc(unsigned disc, const char **dir, const char **what);

/* A bit for each disc whose languages are installed in the data root (the
 * working directory); always 0 on the PAL programs, which have their own. */
unsigned bo3_langpack_installed(void);

/* From bo3_languages_init, with the languages the disc has (bit i for
 * game.language's choice i) and the one chosen: adds the languages of each
 * European disc installed to *mask; when the choice is one of them, keeps
 * it for bo3_langpack_init and returns -1 (game.language unset: the kernel
 * reports English to the program), else returns `want` as it was (a choice
 * index, languages.h, or -1). */
int bo3_langpack_languages(unsigned *mask, int want);

/* Once, in the data root (the working directory), before bo3_modfs_init so
 * that a mod still replaces a file: reads the chosen language's files from
 * there and gives the tutorials its narration. */
void bo3_langpack_init(void);

#endif /* BO3_LANGPACK_H */
