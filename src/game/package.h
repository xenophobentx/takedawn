/*
 * package.h - the programs of one package and which of them runs (BO-341).
 *
 * Every package (the macOS app, the Windows setup, the Linux AppImage, the
 * Android APK) carries one program for each supported disc
 * (tools/install/supported.json, the builds of the package "takedawn", in
 * its order):
 *
 *   ntsc-u-retail    the main program: Takedawn, takedawn, Takedawn.exe,
 *                    libmain.so
 *   pal-retail       beside it, "-pal" after its name (Takedawn-pal ...,
 *                    libmain_pal.so)
 *   pal-ensv-retail  "-ensv" (Takedawn-ensv ..., libmain_ensv.so)
 *
 * A shortcut, the Start menu, Steam and the setup window name the main
 * program; it runs the program of the disc whose data is installed
 * (install.c: bo3_package_start, and --install hands a disc to its
 * program). Each program keeps its per-user folders where its own package
 * kept them before (BO3_USER_DIR: Takedawn for NTSC-U, Takedawn PAL for both
 * PAL discs), so no save, setting, cache or install moves.
 *
 * Which disc runs when more than one disc's data is installed: the one
 * installed last. A real --install writes its build id to disc.txt in the
 * main program's per-user local folder (install.c) when it matters, that is
 * when another disc's data is installed too or the file is there already.
 *
 * This file is the choice itself, with no file system and no toolkit, so
 * that tests/package can check it.
 */
#ifndef BO3_PACKAGE_H
#define BO3_PACKAGE_H

/* The file in the main program's per-user local folder (Windows
 * %LOCALAPPDATA%\Takedawn, elsewhere its save folder) that names the disc
 * installed last. */
#define BO3_PACKAGE_RECORD "disc.txt"

/* Which build's program runs, of `count` (0..count-1, the package's order).
 * `self`: this program's. `recorded`: the build disc.txt names, or -1.
 * `whole[i]`: build i's data is installed and whole where its program looks
 * by itself.
 *
 *   1. the recorded build, when its data is whole
 *   2. this program's own, when whole (an older package: no record)
 *   3. the first other build whose data is whole
 *   4. the recorded build (its setup window then asks for its disc)
 *   5. this program
 */
int bo3_package_pick(int count, int self, int recorded, const int *whole);

/* The build id disc.txt holds: its first line that is not empty and not a
 * comment ('#'), without blanks, a byte order mark or a line end. 1 when
 * there is one. */
int bo3_package_record_parse(const char *text, char *id, unsigned n);

/* The text disc.txt gets for `id`. */
int bo3_package_record_format(const char *id, char *out, unsigned n);

/* Another program's path: `self` is this program's, whose name ends in
 * `self_suffix` (before ".exe", in any case); that name with `other_suffix`
 * in its place. 0 when `self` does not end in its suffix (a developer's
 * build of one program, named otherwise) or the result does not fit. */
int bo3_package_program(const char *self, const char *self_suffix, const char *other_suffix,
                        char *out, unsigned n);

#endif /* BO3_PACKAGE_H */
