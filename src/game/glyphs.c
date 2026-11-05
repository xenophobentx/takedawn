/*
 * glyphs.c - the game's button prompts drawn for the pad in use (BO-107).
 * See glyphs.h.
 */
#include "glyphs.h"

#ifdef BO3_HAVE_GLYPH_ART

#include "bo3_glyph_art.h"          /* made at build time by tools/glyphs/build.py */
#include "input/keyboard.h"
#include "input/pad_glyphs.h"
#include "keycap.h"
#include "nv2a_texrep.h"

#include <stdio.h>
#include <string.h>

#define R(x, y, w, h) { (x), (y), (w), (h) }

/* "Buttons", 128x64 DXT5. The discs' own pixels are 28 wide from (1, 1); the
 * rectangles are the cells around them (tools/glyphs/draw.py LAYOUT). */
static const struct nv2a_texrep_rect k_sheet_all[] = {
    R(0, 0, 128, 64)
};
/* The later Xbox pads print A B X Y as the first Xbox pad does: the top row stays the game's own picture,
 * in place, untouched. (Valve's pads print them as light letters on dark buttons: they replace the whole sheet.) */
static const struct nv2a_texrep_rect k_sheet_lower[] = {
    R(0, 30, 62, 34),      /* the white and black buttons: L1, R1 */
    R(62, 29, 18, 35),     /* the L and R triggers: L2, R2 */
    R(80, 29, 48, 35)      /* the Start button and the word under it */
};
/* The original Xbox pad's discs when the face layout swaps them: only the top row is drawn over (the
 * white and black buttons, the triggers and Start stay the game's own). */
static const struct nv2a_texrep_rect k_sheet_top[] = {
    R(0, 0, 128, 29)
};
/* The same with the modern lower row: the top row, then what LOWER has. */
static const struct nv2a_texrep_rect k_sheet_top_lower[] = {
    R(0, 0, 128, 29),
    R(0, 30, 62, 34),
    R(62, 29, 18, 35),
    R(80, 29, 48, 35)
};
static const struct nv2a_texrep_rect k_single[] = {
    R(0, 0, 32, 32)
};

/* "dpad", 32x16: the triangles the d-pad prompt is drawn from (down and right, mirrored for up and left). */
static const struct nv2a_texrep_rect k_dpad_all[] = {
    R(0, 0, 32, 16)
};

#define ALL(img)   { (img), k_sheet_all, 1 }
#define LOWER(img) { (img), k_sheet_lower, 3 }
#define TOP(img)   { (img), k_sheet_top, 1 }
#define TOP_LOWER(img) { (img), k_sheet_top_lower, 4 }
#define ONE(img)   { (img), k_single, 1 }
#define DPAD(img)  { (img), k_dpad_all, 1 }
#define KEEP       { 0, 0, 0 }

/* variants[set - 1], set = xbox_glyph_set (pad_glyphs.h). The last five are the sets drawn for a pad read
 * by letter: the face layout (Input > Face buttons = Nintendo) makes the game's A the right-hand button,
 * so the prompts show what that button is (pad_glyphs.h has the table). */
static struct nv2a_texrep_variant k_sheet[] = {
    ALL(bo3_glyph_playstation_buttons),
    ALL(bo3_glyph_nintendo_position_buttons),
    ALL(bo3_glyph_nintendo_label_buttons),
    ALL(bo3_glyph_steam_buttons),
    LOWER(bo3_glyph_xbox_modern_buttons),
    ALL(bo3_glyph_positional_buttons),
    ALL(bo3_glyph_positional_swapped_buttons),
    ALL(bo3_glyph_playstation5_buttons),
    ALL(bo3_glyph_keyboard_buttons),
    TOP(bo3_glyph_xbox_swapped_buttons),
    ALL(bo3_glyph_playstation_swapped_buttons),
    ALL(bo3_glyph_playstation5_swapped_buttons),
    ALL(bo3_glyph_steam_swapped_buttons),
    TOP_LOWER(bo3_glyph_xbox_modern_swapped_buttons)
};
static struct nv2a_texrep_variant k_a[] = {
    ONE(bo3_glyph_playstation_a_button),
    ONE(bo3_glyph_nintendo_position_a_button),
    ONE(bo3_glyph_nintendo_label_a_button),
    ONE(bo3_glyph_steam_a_button),
    KEEP,
    ONE(bo3_glyph_positional_a_button),
    ONE(bo3_glyph_positional_swapped_a_button),
    ONE(bo3_glyph_playstation5_a_button),
    ONE(bo3_glyph_keyboard_a_button),
    ONE(bo3_glyph_xbox_swapped_a_button),
    ONE(bo3_glyph_playstation_swapped_a_button),
    ONE(bo3_glyph_playstation5_swapped_a_button),
    ONE(bo3_glyph_steam_swapped_a_button),
    ONE(bo3_glyph_xbox_modern_swapped_a_button)
};
static struct nv2a_texrep_variant k_b[] = {
    ONE(bo3_glyph_playstation_b_button),
    ONE(bo3_glyph_nintendo_position_b_button),
    ONE(bo3_glyph_nintendo_label_b_button),
    ONE(bo3_glyph_steam_b_button),
    KEEP,
    ONE(bo3_glyph_positional_b_button),
    ONE(bo3_glyph_positional_swapped_b_button),
    ONE(bo3_glyph_playstation5_b_button),
    ONE(bo3_glyph_keyboard_b_button),
    ONE(bo3_glyph_xbox_swapped_b_button),
    ONE(bo3_glyph_playstation_swapped_b_button),
    ONE(bo3_glyph_playstation5_swapped_b_button),
    ONE(bo3_glyph_steam_swapped_b_button),
    ONE(bo3_glyph_xbox_modern_swapped_b_button)
};

/* Only the keyboard changes it: arrow keys in place of the triangles. */
static const struct nv2a_texrep_variant k_dpad[] = {
    KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP,
    DPAD(bo3_glyph_keyboard_dpad),
    KEEP, KEEP, KEEP, KEEP, KEEP
};

_Static_assert(XBOX_GLYPH_SET_PLAYSTATION == 1 && XBOX_GLYPH_SET_NINTENDO_POSITION == 2
               && XBOX_GLYPH_SET_NINTENDO_LABEL == 3 && XBOX_GLYPH_SET_STEAM == 4
               && XBOX_GLYPH_SET_XBOX_MODERN == 5 && XBOX_GLYPH_SET_POSITIONAL == 6
               && XBOX_GLYPH_SET_POSITIONAL_SWAPPED == 7 && XBOX_GLYPH_SET_PLAYSTATION5 == 8
               && XBOX_GLYPH_SET_KEYBOARD == 9 && XBOX_GLYPH_SET_XBOX_SWAPPED == 10
               && XBOX_GLYPH_SET_PLAYSTATION_SWAPPED == 11 && XBOX_GLYPH_SET_PLAYSTATION5_SWAPPED == 12
               && XBOX_GLYPH_SET_STEAM_SWAPPED == 13 && XBOX_GLYPH_SET_XBOX_MODERN_SWAPPED == 14
               && XBOX_GLYPH_SET_COUNT == 15,
               "the variant tables are in xbox_glyph_set order");

#define NV2A_DXT45 0x0Fu

/* Data/Global.txd of the NTSC-U disc (the hashes are nv2a_texrep_hash of the
 * DXT5 bytes, as RECOMP_DEBUG=texrep_dump prints them). Another disc's
 * textures hash differently and are left alone. */
static const struct nv2a_texrep_tex k_tex[] = {
    { "Buttons",  0x9181d035e5fbb70eull, NV2A_DXT45, 128, 64, k_sheet, sizeof k_sheet / sizeof k_sheet[0] },
    { "A_Button", 0xefb4a0ec2d843383ull, NV2A_DXT45, 32, 32, k_a, sizeof k_a / sizeof k_a[0] },
    { "B_Button", 0xc883b79bdaf9ba47ull, NV2A_DXT45, 32, 32, k_b, sizeof k_b / sizeof k_b[0] },
    { "dpad",     0x21d72a610f043563ull, NV2A_DXT45, 32, 16, k_dpad, sizeof k_dpad / sizeof k_dpad[0] },
};

static void on_set(int set)
{
    nv2a_texrep_set_variant(set);
}

/* ── the keyboard set follows the player's keys (BO-267) ──────────────
 *
 * The keyboard variants' pictures show the game's default keys. For a button whose first key is another
 * one (Settings > Input), its cap is drawn again (keycap.c) over a copy of the baked picture, and the
 * variant points at the copy. Buttons with their default key keep the baked pixels, so with the
 * defaults the variants point at the baked arrays themselves, as before. A few copies a picture are
 * kept in turn: a backend that is still reading the last one is never written under. */
#define KB_COPIES 3

struct kb_pic {
    struct nv2a_texrep_variant *variant;       /* k_sheet[8], k_a[8], k_b[8] */
    const uint32_t *baked;
    int w, h;
    uint32_t *copy[KB_COPIES];
    unsigned next;
};

static uint32_t s_sheet_copy[KB_COPIES][128 * 64], s_a_copy[KB_COPIES][32 * 32], s_b_copy[KB_COPIES][32 * 32];
static struct kb_pic s_pic[3];
#define KB_SHEET 0
#define KB_A 1
#define KB_B 2

/* A cell: which picture, what kind of cap and where, and the button whose first key it shows. */
static const struct { int pic, kind; float cx, cy; int action; } k_cell[] = {
    { KB_SHEET, BO3_CAP_FACE, 15.0f, 15.5f, XBOX_KEYACT_CRASHBREAKER },     /* the sheet's B */
    { KB_SHEET, BO3_CAP_FACE, 45.0f, 15.5f, XBOX_KEYACT_BOOST },            /* A */
    { KB_SHEET, BO3_CAP_FACE, 75.0f, 15.5f, XBOX_KEYACT_LOOK_BACK },        /* X */
    { KB_SHEET, BO3_CAP_FACE, 105.0f, 15.5f, XBOX_KEYACT_CHANGE_VIEW },     /* Y */
    { KB_SHEET, BO3_CAP_SHOULDER, 15.0f, 47.0f, XBOX_KEYACT_WHITE },
    { KB_SHEET, BO3_CAP_SHOULDER, 45.0f, 47.0f, XBOX_KEYACT_BLACK },
    { KB_SHEET, BO3_CAP_TRIGGER, 70.5f, 38.25f, XBOX_KEYACT_BRAKE },        /* L trigger */
    { KB_SHEET, BO3_CAP_TRIGGER, 70.5f, 55.25f, XBOX_KEYACT_ACCELERATE },   /* R trigger */
    { KB_SHEET, BO3_CAP_START, 104.0f, 46.5f, XBOX_KEYACT_START },
    { KB_A, BO3_CAP_FACE, 16.0f, 16.5f, XBOX_KEYACT_BOOST },
    { KB_B, BO3_CAP_FACE, 16.0f, 16.5f, XBOX_KEYACT_CRASHBREAKER },
};

/* The key a button's prompt shows: its first key (the second when the first is empty), 0 for none. */
static int kb_shown(int action)
{
    const int k = xbox_InputKeyBinding(action, 0);
    return k ? k : xbox_InputKeyBinding(action, 1);
}

static int kb_default_shown(int action)
{
    return xbox_InputKeyDefault(action, 0);
}

static void kb_regenerate(void)
{
    int changed[3] = { 0, 0, 0 };

    if (!s_pic[0].variant)
        return;
    for (size_t i = 0; i < sizeof k_cell / sizeof k_cell[0]; i++)
        if (kb_shown(k_cell[i].action) != kb_default_shown(k_cell[i].action))
            changed[k_cell[i].pic] = 1;
    for (int p = 0; p < 3; p++) {
        struct kb_pic *pic = &s_pic[p];

        if (!changed[p]) {
            pic->variant->image = pic->baked;           /* the defaults: the baked art itself */
            continue;
        }
        {
            uint32_t *img = pic->copy[pic->next++ % KB_COPIES];

            memcpy(img, pic->baked, (size_t)pic->w * pic->h * sizeof *img);
            for (size_t i = 0; i < sizeof k_cell / sizeof k_cell[0]; i++) {
                const int a = k_cell[i].action;

                if (k_cell[i].pic != p || kb_shown(a) == kb_default_shown(a))
                    continue;
                bo3_keycap_draw(img, pic->w, pic->h, k_cell[i].kind, k_cell[i].cx, k_cell[i].cy,
                                bo3_keycap_label(kb_shown(a)));
            }
            pic->variant->image = img;
        }
    }
    nv2a_texrep_refresh();
}

static void kb_init(void)
{
    const int kb = XBOX_GLYPH_SET_KEYBOARD - 1;

    s_pic[KB_SHEET] = (struct kb_pic){ &k_sheet[kb], bo3_glyph_keyboard_buttons, 128, 64, { 0 }, 0 };
    s_pic[KB_A] = (struct kb_pic){ &k_a[kb], bo3_glyph_keyboard_a_button, 32, 32, { 0 }, 0 };
    s_pic[KB_B] = (struct kb_pic){ &k_b[kb], bo3_glyph_keyboard_b_button, 32, 32, { 0 }, 0 };
    for (int c = 0; c < KB_COPIES; c++) {
        s_pic[KB_SHEET].copy[c] = s_sheet_copy[c];
        s_pic[KB_A].copy[c] = s_a_copy[c];
        s_pic[KB_B].copy[c] = s_b_copy[c];
    }
    xbox_InputKeyBindingsSetListener(kb_regenerate);
    kb_regenerate();                    /* the keys from settings.toml, if they are not the default */
}

void bo3_glyphs_init(void)
{
    nv2a_texrep_register(k_tex, sizeof k_tex / sizeof k_tex[0]);
    xbox_InputSetGlyphListener(on_set);
    kb_init();
}

const struct nv2a_texrep_variant *bo3_glyphs_sheets(unsigned *n)
{
    *n = sizeof k_sheet / sizeof k_sheet[0];
    return k_sheet;
}

#else

void bo3_glyphs_init(void)
{
}

const struct nv2a_texrep_variant *bo3_glyphs_sheets(unsigned *n)
{
    *n = 0;
    return 0;
}

#endif
