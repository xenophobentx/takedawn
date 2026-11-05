/*
 * keycap.h - the keyboard prompts' key caps, drawn at run time (BO-267).
 *
 * The keyboard prompt set (assets/glyphs/keyboard) shows the game's default keys on key caps, baked
 * from SVG at build time. With the keys changed in Settings > Input a prompt would still name the old
 * key, so the cap of a button whose first key is not the default is drawn again here, in the same
 * style and size as tools/glyphs/draw.py draws it (the same outline, face, lip and highlight, the same
 * stroke alphabet: src/game/keycap_font.inc is made from it), into the A8R8G8B8 picture the texture
 * replacement (nv2a_texrep.h) copies from. The caps with the default key are never drawn here: the
 * baked pixels are used as they are.
 *
 * Pure C: no toolkit, no allocation, any thread (each call touches only its picture).
 */
#ifndef BO3_KEYCAP_H
#define BO3_KEYCAP_H

#include <stddef.h>
#include <stdint.h>

/* The four kinds of cell the keyboard sheet and the single buttons have. */
enum bo3_keycap_kind {
    BO3_CAP_FACE,       /* 28 x 26, dark with a lip: the A B X Y buttons (and the game's 32 x 32 A and B) */
    BO3_CAP_SHOULDER,   /* 26 x 17, light with dark letters: the white and black buttons */
    BO3_CAP_TRIGGER,    /* 15 x 16.5, dark with a light rim: the triggers */
    BO3_CAP_START       /* 42 x 28, dark with a lip: the Start button */
};

/* The cell's pixels as a rectangle of the picture: [*x, *x + *w) x [*y, *y + *h), clipped to a
 * picture of img_w x img_h. Cells of one sheet never overlap. */
void bo3_keycap_rect(int kind, float cx, float cy, int img_w, int img_h, int *x, int *y, int *w, int *h);

/* Draw the cell centred on (cx, cy) with `label` (see below) into img (img_w x img_h, A8R8G8B8 rows
 * top to bottom, straight alpha): the cell's rectangle is made empty first, nothing outside it is
 * touched. A long label is made smaller until it fits the cap. */
void bo3_keycap_draw(uint32_t *img, int img_w, int img_h, int kind, float cx, float cy, const char *label);

/* The label of a Windows virtual-key code (keyboard.h): "W", "CTRL", "PGUP", or "@space" for a
 * symbol of the cap ("@space", "@back", "@shift", "@enter", "@up", "@down", "@left", "@right").
 * Static storage; "" for a code with no label. */
const char *bo3_keycap_label(int vk);

#endif
