/*
 * glyphs.h - the game's button prompts drawn for the pad in use (BO-107).
 *
 * The game keeps its prompts in three textures of Data/Global.txd: the sheet
 * "Buttons" (128x64: the B A X Y discs, the white and black buttons, the L
 * and R triggers, the Start button and the word START) and the single discs
 * "A_Button" and "B_Button" (32x32) the HUD draws. They are known by the hash
 * of their bytes on the NTSC-U disc; when one is uploaded, this project's own
 * drawings (assets/glyphs, rendered at build time) go over its prompts for
 * the family input.glyphs resolves to (the toolkit's pad_glyphs.h and
 * nv2a_texrep.h). With the Xbox family nothing is replaced.
 */
#ifndef BO3_GLYPHS_H
#define BO3_GLYPHS_H

struct nv2a_texrep_variant;

/* Once, at start, after the settings are in: registers the textures and
 * follows the input layer's choice from then on. Does nothing in a build
 * without the artwork (no SVG renderer when it was configured). */
void bo3_glyphs_init(void);

/* The prompt sheets of the drawn sets, for the overlay's footer (the
 * toolkit's xbox_OverlaySetPadSheets): variants[v - 1] is the sheet of glyph
 * set v. NULL and *n 0 in a build without the artwork. Static tables. */
const struct nv2a_texrep_variant *bo3_glyphs_sheets(unsigned *n);

/* Whether the PS2 driving layout is in force for player 1's pad now
 * (input.control_scheme = ps2 while driving, controls.c): the prompts of
 * the game's A and B (boost and Impact Time, the Crashbreaker) then show the
 * right shoulder and the right trigger of the set's pad. Any thread. */
void bo3_glyphs_ps2_driving(int on);

#endif
