/*
 * headfont.h - the online pages' red heading reads TAKEDAWN ONLINE.
 *
 * The red heading at the top left of a page (SINGLE EVENT, SETTINGS, the
 * online pages' XBOX LIVE) is not a picture: it is the first string of the
 * heading table (Data/Head<language>.bin), drawn with the heading font that
 * is built into the executable. That font's atlas holds only the letters the
 * headings use, and none of them has a W. So two things are done, in memory
 * only, on the title's thread (the pad poll), once the title has set them up:
 *
 *   - the font gets a W: our own letter (assets/glyphs/headfont/w.svg, drawn
 *     from Anton's W, SIL OFL 1.1) is written as DXT5 blocks into an empty
 *     corner of the atlas, and the font's glyph table, copied into a block of
 *     the title's heap with the W added before the missing-letter box, takes
 *     the place of the original (the title looks a letter up through a
 *     128-entry table of pointers into it);
 *   - the heading slot that reads XBOX LIVE points to TAKEDAWN ONLINE (the
 *     same rule as the other strings, onlinebrand_text.c), in every language
 *     of every disc: all of them have XBOX LIVE there.
 *
 * The font and its atlas are the same bytes on the NTSC-U, PAL and PAL-ENSV
 * discs; only their address differs. Anything not as expected (another
 * build, the table already changed) leaves both alone, with one log line.
 * Without the rendered art (a build without an SVG renderer, glyphs.c's
 * BO3_HAVE_GLYPH_ART) nothing is changed: the heading stays XBOX LIVE rather
 * than show the box in place of the W.
 */
#ifndef BO3_HEADFONT_H
#define BO3_HEADFONT_H

#include <stdint.h>

/* Where the W goes in the 128x256 atlas: a free corner, right of the
 * missing-letter box and below Y (the font's letters are 38 texels tall). */
#define HF_W_X 92
#define HF_W_Y 200
#define HF_W_W 34
#define HF_W_H 38

/* One DXT5 block of white texels with this alpha (16 values, rows of 4). */
void bo3_headfont_dxt5_block(const uint8_t alpha[16], uint8_t out[16]);
/* The alpha of texel (x, y) of a DXT5 block. */
uint8_t bo3_headfont_dxt5_alpha(const uint8_t block[16], int x, int y);

/* The pad poll (controls.c) and the main menu's mark (mainquit.c). */
void bo3_headfont_apply(void);

#endif
