/* keycap: see CMakeLists.txt. */
#include "keycap.h"

#include <stdio.h>
#include <string.h>

static int g_fail, g_checks;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                                                         printf(__VA_ARGS__); printf("\n"); } } while (0)

#define W 128
#define H 64
#define GUARD 0xDEADBEEFu

static unsigned alpha(uint32_t p) { return p >> 24; }
static unsigned red(uint32_t p) { return (p >> 16) & 255; }

/* The cells of the sheet, as src/game/glyphs.c places them. */
static const struct { int kind; float cx, cy; } k_cells[] = {
    { BO3_CAP_FACE, 15.0f, 15.5f }, { BO3_CAP_FACE, 45.0f, 15.5f }, { BO3_CAP_FACE, 75.0f, 15.5f },
    { BO3_CAP_FACE, 105.0f, 15.5f }, { BO3_CAP_SHOULDER, 15.0f, 47.0f }, { BO3_CAP_SHOULDER, 45.0f, 47.0f },
    { BO3_CAP_TRIGGER, 70.5f, 38.25f }, { BO3_CAP_TRIGGER, 70.5f, 55.25f }, { BO3_CAP_START, 104.0f, 46.5f },
};
#define NCELLS ((int)(sizeof k_cells / sizeof k_cells[0]))

static void test_labels(void)
{
    CHECK(!strcmp(bo3_keycap_label('W'), "W") && !strcmp(bo3_keycap_label('7'), "7"), "letters and digits");
    CHECK(!strcmp(bo3_keycap_label(0x20), "@space") && !strcmp(bo3_keycap_label(0x08), "@back")
          && !strcmp(bo3_keycap_label(0x10), "@shift") && !strcmp(bo3_keycap_label(0x0D), "@enter"), "symbols");
    CHECK(!strcmp(bo3_keycap_label(0x11), "CTRL") && !strcmp(bo3_keycap_label(0x09), "TAB")
          && !strcmp(bo3_keycap_label(0x1B), "ESC") && !strcmp(bo3_keycap_label(0x21), "PGUP"), "short words");
    CHECK(!strcmp(bo3_keycap_label(0x71), "F2") && !strcmp(bo3_keycap_label(0x64), "NUM4")
          && !strcmp(bo3_keycap_label(0x12), "ALT"), "F keys, the keypad, Alt");
    CHECK(!strcmp(bo3_keycap_label(0xBC), ",") && !strcmp(bo3_keycap_label(0xDC), "\\"), "punctuation");
    CHECK(!strcmp(bo3_keycap_label(0x99), "") && !strcmp(bo3_keycap_label(0), ""), "no label");
    /* Every key a player may bind has a label (keyboard.h's table of names). */
    for (int vk = 1; vk < 256; vk++) {
        const int bindable = (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || vk == 0x20 || vk == 0x0D
            || vk == 0x1B || vk == 0x08 || vk == 0x10 || vk == 0x11 || vk == 0x09 || (vk >= 0x21 && vk <= 0x24)
            || (vk >= 0x25 && vk <= 0x28) || vk == 0x2D || (vk >= 0xBA && vk <= 0xBF) || vk == 0xC0
            || (vk >= 0xDB && vk <= 0xDE);
        if (bindable)
            CHECK(bo3_keycap_label(vk)[0], "bindable key 0x%02X has a label", vk);
    }
}

static void test_rects(void)
{
    /* Cells never overlap, and each stays in the picture. */
    for (int i = 0; i < NCELLS; i++) {
        int x, y, w, h;

        bo3_keycap_rect(k_cells[i].kind, k_cells[i].cx, k_cells[i].cy, W, H, &x, &y, &w, &h);
        CHECK(x >= 0 && y >= 0 && x + w <= W && y + h <= H && w > 0 && h > 0, "cell %d inside: %d %d %d %d", i, x, y, w, h);
        for (int j = i + 1; j < NCELLS; j++) {
            int x2, y2, w2, h2;

            bo3_keycap_rect(k_cells[j].kind, k_cells[j].cx, k_cells[j].cy, W, H, &x2, &y2, &w2, &h2);
            CHECK(x + w <= x2 || x2 + w2 <= x || y + h <= y2 || y2 + h2 <= y, "cells %d and %d overlap", i, j);
        }
    }
}

/* Each cell with a label: nothing outside the rectangle is touched, the middle of the cap is opaque, the
 * outside corner is clear, and the label is there. */
static void test_draw(void)
{
    static const char *const labels[] = { "Z", "7", "@space", "CTRL", "PGDN", "NUM4", "@up", "HOME", "," };

    for (int i = 0; i < NCELLS; i++)
        for (size_t l = 0; l < sizeof labels / sizeof labels[0]; l++) {
            static uint32_t img[W * H];
            int x, y, w, h, bad = 0, lit = 0;

            for (int k = 0; k < W * H; k++)
                img[k] = GUARD;
            bo3_keycap_draw(img, W, H, k_cells[i].kind, k_cells[i].cx, k_cells[i].cy, labels[l]);
            bo3_keycap_rect(k_cells[i].kind, k_cells[i].cx, k_cells[i].cy, W, H, &x, &y, &w, &h);
            for (int py = 0; py < H; py++)
                for (int px = 0; px < W; px++) {
                    const int in = px >= x && px < x + w && py >= y && py < y + h;
                    if (!in && img[py * W + px] != GUARD)
                        bad++;
                    if (in && img[py * W + px] == GUARD)
                        bad++;
                }
            CHECK(!bad, "cell %d label %s: %d pixels wrong (outside touched or inside not drawn)", i, labels[l], bad);
            /* The cap's own middle is opaque; the rectangle's corner has nothing. */
            CHECK(alpha(img[(int)k_cells[i].cy * W + (int)k_cells[i].cx - 8]) == 255
                  || k_cells[i].kind == BO3_CAP_TRIGGER, "cell %d label %s: face opaque", i, labels[l]);
            CHECK(alpha(img[y * W + x]) == 0, "cell %d label %s: corner clear", i, labels[l]);
            /* The label's colour is in the cap: light strokes on the dark caps, dark on the light ones. */
            for (int py = y; py < y + h; py++)
                for (int px = x; px < x + w; px++) {
                    const uint32_t p = img[py * W + px];
                    if (alpha(p) == 255 && (k_cells[i].kind == BO3_CAP_SHOULDER ? red(p) < 0x40 && py > y + 4 && py < y + h - 4
                                                                              : red(p) > 0xe0))
                        lit++;
                }
            CHECK(lit > (labels[l][0] == ',' ? 0 : 3), "cell %d label %s: a label is drawn (%d pixels)", i, labels[l], lit);
        }
}

/* A long word is made smaller until it fits: no label pixel on the cap's rim or outside the face. */
static void test_fit(void)
{
    static uint32_t img[W * H];
    int x, y, w, h, minx = 999, maxx = -1;

    for (int k = 0; k < W * H; k++)
        img[k] = 0;
    bo3_keycap_draw(img, W, H, BO3_CAP_FACE, 15.0f, 15.5f, "PGDNPGDN");
    bo3_keycap_rect(BO3_CAP_FACE, 15.0f, 15.5f, W, H, &x, &y, &w, &h);
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            if (red(img[py * W + px]) > 0xe0 && alpha(img[py * W + px]) == 255) {
                if (px < minx) minx = px;
                if (px > maxx) maxx = px;
            }
    CHECK(minx >= 15 - 14 + 2 && maxx <= 15 + 14 - 2, "a long word stays inside the cap (%d..%d)", minx, maxx);
    /* A label of nothing the alphabet has draws an empty cap, not a crash. */
    bo3_keycap_draw(img, W, H, BO3_CAP_FACE, 45.0f, 15.5f, "~~");
    bo3_keycap_draw(img, W, H, BO3_CAP_FACE, 45.0f, 15.5f, "");
    bo3_keycap_draw(img, W, H, BO3_CAP_FACE, 45.0f, 15.5f, NULL);
    bo3_keycap_draw(img, W, H, BO3_CAP_FACE, 45.0f, 15.5f, "@nosuchsymbol");
    bo3_keycap_draw(NULL, W, H, BO3_CAP_FACE, 45.0f, 15.5f, "A");
    bo3_keycap_draw(img, W, H, 99, 45.0f, 15.5f, "A");
    /* A cell at the picture's edge is clipped, not written past it. */
    bo3_keycap_draw(img, W, H, BO3_CAP_FACE, 2.0f, 2.0f, "A");
    bo3_keycap_draw(img, W, H, BO3_CAP_FACE, 126.0f, 62.0f, "A");
}

int main(void)
{
    test_labels();
    test_rects();
    test_draw();
    test_fit();
    printf("keycap: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
