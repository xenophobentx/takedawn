/*
 * keycap.c - key caps drawn at run time; see keycap.h.
 *
 * Every cell is a stack of layers (rounded rectangles with a flat colour or a vertical gradient, a
 * highlight line, strokes), the same as tools/glyphs/draw.py puts in the SVG, evaluated at 4 x 4
 * points a pixel and averaged: edges come out smooth without a vector renderer. Strokes are the
 * stroke alphabet's polylines at a pixel size, with round ends and joins (the distance from the point
 * to the nearest piece is at most half the stroke).
 */
#include "keycap.h"

#include <math.h>
#include <string.h>

#include "keycap_font.inc"

#define SS 4                                    /* samples a side */
#define MAX_SEGS 400

struct seg { float x0, y0, x1, y1; };

struct layer_rgb { float r, g, b; };

/* A premultiplied colour being built up. */
struct px { float r, g, b, a; };

static void over(struct px *p, float r, float g, float b, float a)
{
    p->r = r * a + p->r * (1.0f - a);
    p->g = g * a + p->g * (1.0f - a);
    p->b = b * a + p->b * (1.0f - a);
    p->a = a + p->a * (1.0f - a);
}

/* Signed distance to a rounded rectangle (x, y, w, h, radius r): negative inside. */
static float rrect_sd(float px, float py, float x, float y, float w, float h, float r)
{
    const float hx = w * 0.5f, hy = h * 0.5f;
    float qx = fabsf(px - (x + hx)) - (hx - r), qy = fabsf(py - (y + hy)) - (hy - r);
    const float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;
    return sqrtf(mx * mx + my * my) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
}

static float seg_dist(const struct seg *s, float px, float py)
{
    const float dx = s->x1 - s->x0, dy = s->y1 - s->y0;
    const float l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((px - s->x0) * dx + (py - s->y0) * dy) / l2 : 0.0f;
    float qx, qy;

    t = t < 0 ? 0 : t > 1 ? 1 : t;
    qx = s->x0 + t * dx - px;
    qy = s->y0 + t * dy - py;
    return sqrtf(qx * qx + qy * qy);
}

static float min_dist(const struct seg *s, int n, float px, float py)
{
    float best = 1e9f;
    for (int i = 0; i < n; i++) {
        /* A cheap reject: the box of the piece, grown by what is already found. */
        const float x0 = s[i].x0 < s[i].x1 ? s[i].x0 : s[i].x1, x1 = s[i].x0 < s[i].x1 ? s[i].x1 : s[i].x0;
        const float y0 = s[i].y0 < s[i].y1 ? s[i].y0 : s[i].y1, y1 = s[i].y0 < s[i].y1 ? s[i].y1 : s[i].y0;
        if (px < x0 - best || px > x1 + best || py < y0 - best || py > y1 + best)
            continue;
        const float d = seg_dist(&s[i], px, py);
        if (d < best)
            best = d;
    }
    return best;
}

/* ── the label as pieces ─────────────────────────────────────────── */

static int find_glyph(char c)
{
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 32);
    for (size_t i = 0; i < sizeof kf_glyphs / sizeof kf_glyphs[0]; i++)
        if (kf_glyphs[i].c == c)
            return (int)i;
    return -1;
}

static int find_symbol(const char *name)
{
    for (size_t i = 0; i < sizeof kf_symbols / sizeof kf_symbols[0]; i++)
        if (!strcmp(kf_symbols[i].name, name))
            return (int)i;
    return -1;
}

static int add_poly(struct seg *out, int n, int poly, float ox, float oy, float k)
{
    const unsigned first = kf_polys[poly].first, np = kf_polys[poly].n;

    for (unsigned i = 1; i < np && n < MAX_SEGS; i++) {
        out[n].x0 = ox + kf_pts[(first + i - 1) * 2] * k;
        out[n].y0 = oy + kf_pts[(first + i - 1) * 2 + 1] * k;
        out[n].x1 = ox + kf_pts[(first + i) * 2] * k;
        out[n].y1 = oy + kf_pts[(first + i) * 2 + 1] * k;
        n++;
    }
    return n;
}

/* A label's pieces, centred on (cx, cy): text of the stroke alphabet at cap height `height` (kind's
 * own for a word, or smaller to fit `max_w`), or a symbol. *stroke is the width, shrunk with the
 * height. Returns the piece count. */
static int label_segs(struct seg *out, const char *label, float cx, float cy, float height, float stroke,
                      float gap, float max_w, float max_h, float *stroke_out)
{
    int n = 0;

    if (label[0] == '@') {
        const int si = find_symbol(label + 1);
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, k = 1.0f;

        *stroke_out = stroke;
        if (si < 0)
            return 0;
        for (unsigned p = 0; p < kf_symbols[si].n; p++)
            for (unsigned i = 0; i < kf_polys[kf_symbols[si].first + p].n; i++) {
                const unsigned at = kf_polys[kf_symbols[si].first + p].first + i;
                if (kf_pts[at * 2] < x0) x0 = kf_pts[at * 2];
                if (kf_pts[at * 2] > x1) x1 = kf_pts[at * 2];
                if (kf_pts[at * 2 + 1] < y0) y0 = kf_pts[at * 2 + 1];
                if (kf_pts[at * 2 + 1] > y1) y1 = kf_pts[at * 2 + 1];
            }
        /* The picture is made at the size of the default cap; a smaller cell shrinks it to fit. */
        if (x1 - x0 > max_w)
            k = max_w / (x1 - x0);
        if ((y1 - y0) * k > max_h)
            k = max_h / (y1 - y0);
        *stroke_out = kf_symbols[si].stroke * (k < 1.0f ? (k > 0.6f ? k : 0.6f) : 1.0f);
        for (unsigned p = 0; p < kf_symbols[si].n; p++)
            n = add_poly(out, n, (int)(kf_symbols[si].first + p), cx, cy, k);
        return n;
    }

    {
        float adv[16], total = 0.0f, k, g, x;
        int gi[16], len = 0;

        for (const char *c = label; *c && len < 16; c++) {
            const int g2 = find_glyph(*c);
            if (g2 < 0)
                continue;
            gi[len] = g2;
            adv[len] = kf_glyphs[g2].adv;
            len++;
        }
        *stroke_out = stroke;
        if (!len)
            return 0;
        for (int i = 0; i < len; i++)
            total += adv[i];
        /* The width at this height; made smaller (and the stroke with it) until it fits. */
        k = height / 10.0f;
        g = gap * height;
        if ((total * k + g * (len - 1)) > max_w) {
            const float f = max_w / (total * k + g * (len - 1));
            k *= f;
            g *= f;
            height *= f;
            stroke *= f > 0.75f ? f : 0.75f;
            if (stroke < 1.1f)
                stroke = 1.1f;
            *stroke_out = stroke;
        }
        x = cx - (total * k + g * (len - 1)) * 0.5f;
        for (int i = 0; i < len; i++) {
            for (unsigned p = 0; p < kf_glyphs[gi[i]].n; p++)
                n = add_poly(out, n, (int)(kf_glyphs[gi[i]].first + p), x, cy - height * 0.5f, k);
            x += adv[i] * k + g;
        }
    }
    return n;
}

/* ── the cells ───────────────────────────────────────────────────── */

struct cell_def {
    float w, h;           /* the cap */
    float rx;
    float lip_dy;         /* the label's centre above the cap's (the lip is under the face) */
};

static const struct cell_def k_def[] = {
    /* FACE */     { 28.0f, 26.0f, 6.0f, 1.8f },
    /* SHOULDER */ { 26.0f, 17.0f, 5.5f, 0.0f },
    /* TRIGGER */  { 15.0f, 16.5f, 2.6f, 0.0f },
    /* START */    { 42.0f, 28.0f, 7.0f, 1.8f },
};

void bo3_keycap_rect(int kind, float cx, float cy, int img_w, int img_h, int *x, int *y, int *w, int *h)
{
    const struct cell_def *d = &k_def[kind];
    int x0 = (int)floorf(cx - d->w * 0.5f) - 1, x1 = (int)ceilf(cx + d->w * 0.5f) + 1;
    int y0 = (int)floorf(cy - d->h * 0.5f) - 1, y1 = (int)ceilf(cy + d->h * 0.5f) + 1;

    if (kind == BO3_CAP_TRIGGER) {          /* the two triggers' rims touch: no margin, or one clears the other's */
        x0++; y0++; x1--; y1--;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > img_w) x1 = img_w;
    if (y1 > img_h) y1 = img_h;
    *x = x0;
    *y = y0;
    *w = x1 > x0 ? x1 - x0 : 0;
    *h = y1 > y0 ? y1 - y0 : 0;
}

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static void hex(unsigned v, float *r, float *g, float *b)
{
    *r = (float)((v >> 16) & 255) / 255.0f;
    *g = (float)((v >> 8) & 255) / 255.0f;
    *b = (float)(v & 255) / 255.0f;
}

/* The colour of the cell at one point. */
static struct px sample(int kind, float px, float py, float cx, float cy, const struct seg *segs, int nseg,
                        float hw)
{
    const struct cell_def *d = &k_def[kind];
    const float x = cx - d->w * 0.5f, y = cy - d->h * 0.5f, w = d->w, h = d->h, rx = d->rx;
    struct px c = { 0, 0, 0, 0 };
    float r, g, b, r2, g2, b2, t;

    if (kind == BO3_CAP_FACE || kind == BO3_CAP_START) {
        hex(0x07080b, &r, &g, &b); /* region: not an address (a colour) */
        if (rrect_sd(px, py, x, y, w, h, rx) <= 0.0f)
            over(&c, r, g, b, 1.0f);
        hex(0x0e1015, &r, &g, &b); /* region: not an address (a colour) */
        if (rrect_sd(px, py, x + 1.4f, y + 1.4f, w - 2.8f, h - 2.8f, rx - 1.4f > 1.0f ? rx - 1.4f : 1.0f) <= 0.0f)
            over(&c, r, g, b, 1.0f);
        if (rrect_sd(px, py, x + 1.4f, y + 1.4f, w - 2.8f, h - 5.0f, rx - 1.8f > 1.0f ? rx - 1.8f : 1.0f) <= 0.0f) {
            hex(0x454a57, &r, &g, &b); /* region: not an address (a colour) */
            hex(0x1b1d25, &r2, &g2, &b2); /* region: not an address (a colour) */
            t = (py - y) / h;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            over(&c, lerp(r, r2, t), lerp(g, g2, t), lerp(b, b2, t), 1.0f);
        }
        {   /* the highlight along the top of the face */
            const struct seg hl = { x + rx, y + 2.0f, x + w - rx, y + 2.0f };
            if (seg_dist(&hl, px, py) <= 0.5f)
                over(&c, 1.0f, 1.0f, 1.0f, 0.22f);
        }
        if (nseg && min_dist(segs, nseg, px, py) <= hw) {
            hex(0xf4f5f7, &r, &g, &b);
            over(&c, r, g, b, 1.0f);
        }
    } else if (kind == BO3_CAP_SHOULDER) {
        hex(0x07080b, &r, &g, &b); /* region: not an address (a colour) */
        if (rrect_sd(px, py, x, y, w, h, rx) <= 0.0f)
            over(&c, r, g, b, 1.0f);
        if (rrect_sd(px, py, x + 1.2f, y + 1.2f, w - 2.4f, h - 2.4f, rx - 1.2f > 0.5f ? rx - 1.2f : 0.5f) <= 0.0f) {
            hex(0xe9ebef, &r, &g, &b);
            hex(0xa9adb8, &r2, &g2, &b2);
            t = (py - y) / h;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            over(&c, lerp(r, r2, t), lerp(g, g2, t), lerp(b, b2, t), 1.0f);
        }
        if (nseg && min_dist(segs, nseg, px, py) <= hw) {
            hex(0x101218, &r, &g, &b); /* region: not an address (a colour) */
            over(&c, r, g, b, 1.0f);
        }
    } else {                                    /* TRIGGER: dark face, a light rim, light label */
        const float sd = rrect_sd(px, py, x + 0.5f, y + 0.5f, w - 1.0f, h - 1.0f, rx);
        if (sd <= 0.0f) {
            hex(0x3a3e49, &r, &g, &b); /* region: not an address (a colour) */
            hex(0x15171d, &r2, &g2, &b2); /* region: not an address (a colour) */
            t = (py - y) / h;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            over(&c, lerp(r, r2, t), lerp(g, g2, t), lerp(b, b2, t), 1.0f);
        }
        if (fabsf(sd) <= 0.5f) {
            hex(0xc9ccd6, &r, &g, &b);
            over(&c, r, g, b, 1.0f);
        }
        if (nseg && min_dist(segs, nseg, px, py) <= hw) {
            hex(0xf4f5f7, &r, &g, &b);
            over(&c, r, g, b, 1.0f);
        }
    }
    return c;
}

void bo3_keycap_draw(uint32_t *img, int img_w, int img_h, int kind, float cx, float cy, const char *label)
{
    const struct cell_def *d = &k_def[kind];
    struct seg segs[MAX_SEGS];
    int rx0, ry0, rw, rh, nseg;
    float stroke, height, base_stroke, gap, max_w, max_h, ly;

    if (!img || kind < 0 || kind > BO3_CAP_START)
        return;
    bo3_keycap_rect(kind, cx, cy, img_w, img_h, &rx0, &ry0, &rw, &rh);
    if (!label)
        label = "";
    /* The size a label has on each kind of cap (draw.py: the CTRL cap, the Q key, the S key, the
     * Start cap); a one-letter label is drawn larger on the dark caps. */
    {
        int chars = 0;
        for (const char *c = label; *c; c++)
            chars++;
        switch (kind) {
        case BO3_CAP_SHOULDER: height = 9.6f; base_stroke = 2.3f; gap = 0.22f; break;
        case BO3_CAP_TRIGGER:  height = 7.6f; base_stroke = 1.45f; gap = 0.24f; break;
        case BO3_CAP_START:    height = chars > 1 ? 9.0f : 13.0f; base_stroke = chars > 1 ? 2.0f : 2.6f; gap = 0.25f; break;
        default:               height = chars > 1 ? 7.6f : 12.0f; base_stroke = chars > 1 ? 1.8f : 2.4f; gap = chars > 1 ? 0.22f : 0.25f; break;
        }
    }
    /* What the label may fill: the face inside the outline, with a margin. */
    max_w = d->w - (kind == BO3_CAP_TRIGGER ? 4.5f : kind == BO3_CAP_SHOULDER ? 5.0f : 5.5f);
    max_h = d->h - (kind == BO3_CAP_TRIGGER ? 5.5f : kind == BO3_CAP_SHOULDER ? 6.0f : 8.0f);
    ly = cy - d->lip_dy;
    nseg = label_segs(segs, label, cx, ly, height, base_stroke, gap, max_w, max_h, &stroke);
    (void)height;

    for (int py = ry0; py < ry0 + rh; py++)
        for (int pxl = rx0; pxl < rx0 + rw; pxl++) {
            struct px acc = { 0, 0, 0, 0 };
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++) {
                    const struct px s = sample(kind, (float)pxl + ((float)sx + 0.5f) / SS,
                                               (float)py + ((float)sy + 0.5f) / SS, cx, cy, segs, nseg,
                                               stroke * 0.5f);
                    acc.r += s.r; acc.g += s.g; acc.b += s.b; acc.a += s.a;
                }
            {
                const float n = (float)(SS * SS);
                const float a = acc.a / n;
                unsigned ia = (unsigned)(a * 255.0f + 0.5f), ir, ig, ib;

                if (acc.a > 0.0f) {                  /* straight alpha: the premultiplied sum over its alpha */
                    ir = (unsigned)(acc.r / acc.a * 255.0f + 0.5f);
                    ig = (unsigned)(acc.g / acc.a * 255.0f + 0.5f);
                    ib = (unsigned)(acc.b / acc.a * 255.0f + 0.5f);
                } else {                             /* nothing here: the outline's colour, as the baked art bleeds */
                    ir = 0x07; ig = 0x08; ib = 0x0b;
                }
                if (ir > 255) ir = 255;
                if (ig > 255) ig = 255;
                if (ib > 255) ib = 255;
                img[py * img_w + pxl] = (ia << 24) | (ir << 16) | (ig << 8) | ib;
            }
        }
}

/* ── labels of keys ──────────────────────────────────────────────── */

const char *bo3_keycap_label(int vk)
{
    static char one[8][2];
    static unsigned n;
    static const struct { int vk; const char *label; } k_named[] = {
        { 0x20, "@space" }, { 0x08, "@back" }, { 0x10, "@shift" }, { 0x0D, "@enter" },
        { 0x26, "@up" }, { 0x28, "@down" }, { 0x25, "@left" }, { 0x27, "@right" },
        { 0x11, "CTRL" }, { 0x12, "ALT" }, { 0x1B, "ESC" }, { 0x09, "TAB" },
        { 0x21, "PGUP" }, { 0x22, "PGDN" }, { 0x23, "END" }, { 0x24, "HOME" }, { 0x2D, "INS" },
        { 0xBA, ";" }, { 0xBB, "=" }, { 0xBC, "," }, { 0xBD, "-" }, { 0xBE, "." }, { 0xBF, "/" },
        { 0xC0, "`" }, { 0xDB, "[" }, { 0xDC, "\\" }, { 0xDD, "]" }, { 0xDE, "'" },
    };
    static const char *const k_f[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
    static const char *const k_num[] = { "NUM0", "NUM1", "NUM2", "NUM3", "NUM4", "NUM5", "NUM6", "NUM7", "NUM8", "NUM9" };
    char *o;

    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        o = one[n++ & 7];
        o[0] = (char)vk;
        o[1] = 0;
        return o;
    }
    for (size_t i = 0; i < sizeof k_named / sizeof k_named[0]; i++)
        if (k_named[i].vk == vk)
            return k_named[i].label;
    if (vk >= 0x70 && vk <= 0x7B)
        return k_f[vk - 0x70];
    if (vk >= 0x60 && vk <= 0x69)
        return k_num[vk - 0x60];
    return "";
}
