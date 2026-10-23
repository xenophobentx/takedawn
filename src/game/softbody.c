/*
 * softbody.c - graphics.softbody (RECOMP_GFX_SOFTBODY=1, overlay Graphics
 * page): soft-body vehicle damage, drawn only. Off as shipped, read at
 * start; BO3_SOFTBODY=1 / 0 overrides the setting (tests).
 *
 * The title's own damage moves whole panels. A race car's model is its
 * .bgv, loaded whole into contiguous memory (car + 0x50 points at it) and
 * drawn straight from it: four levels, each a vertex buffer of 24-byte
 * vertices (float3 position, packed normal 11-11-10, float2 uv) and part
 * entries. Entry 0 is the body; while the car is whole, the doors, bumpers,
 * bonnet and boot are drawn from a copy of them in the body's own frame
 * (the vertices no entry names), and once a panel swings or comes off the
 * title draws its entry instead, each in its own frame (a pivot) with a
 * matrix of the car's (+ 0x590). The vertices themselves never change: the
 * GPU reads them each draw.
 *
 * Here every car gets a structure of nodes and beams in its own frame,
 * built from the full level when the car is first drawn: the welded
 * vertices of the body and the attached panels (the body nodes) are
 * clustered in SB_VOXEL cubes, one structure node each, beamed to their
 * SB_SKNN nearest, and braced to a chassis lattice whose middle is fixed
 * (the car's rigid body moves the whole). A beam pushed past its yield
 * strain takes part of the excess into its rest length, so a dent stays. An
 * impact (the title's own, see sb_impact) drives a flat ram into the car
 * along the hit's direction; the beams spread it. It moves in SB_SUB steps
 * a flip; what is held to the chassis is solved quasi-statically (steel
 * barely gives under its own inertia), while a node torn from it moves
 * under the forces the car's own motion puts on it in its frame (its
 * acceleration, its turn, gravity), so what hangs swings and what tears
 * free flies; a beam on such a node breaks past SB_F_BREAK newtons. Each
 * body node follows its SB_SKIN nearest structure nodes, their positions
 * and turns (each node's rotation from how its neighbours moved).
 *
 * Each frame the car is still moving, every level's vertices are written
 * back: position plus their body node's displacement, the normal turned by
 * the node's deformation gradient (least squares over its neighbours, F^-T).
 * A part entry's vertices take the displacement of the node at their place
 * on the car: the part's offset from its pivot is found once by matching
 * its vertices onto the attached copy, so a door that swings open later
 * keeps its dent. Wheels have no attached copy and are left alone.
 *
 * Own design throughout (a mass-spring network with plasticity, nothing
 * from another program). Hooks (config/patches.json): "sb_draw" in the race
 * car's draw (sub_0018DE00, esi the car) and "sb_impact" in the impact
 * entry (sub_0012FA40, after the frame is set: [ebp+0x10] the physics
 * body, car + 0x10, with its world matrix; [ebp+0x24] the contact,
 * [ebp+0x28] the direction, [ebp+0x2C] the strength).
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "platform/recomp_patch.h"
#include "platform/recomp_env.h"
#include "recomp_types.h"

extern RECOMP_TLS uint32_t g_eax, g_esi, g_ebp, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern uint32_t xbox_ContiguousAllocEx(uint32_t size, uint32_t alignment, uint32_t phys_lo, uint32_t phys_hi);
extern int xbox_ContiguousFree(uint32_t va);

#define SB_M32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define SB_M8(va)  (*(uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define SB_MF(va)  (*(float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define SB_PTR(va) ((uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define SB_CAR_BGV     0x50u     /* the car's .bgv in contiguous memory */
#define SB_CAR_PANELS  0x590u    /* 16 panel slots (matrices) of the title's damage */
#define SB_CAR_WRECK   0x3D0u    /* 4 while it drives, 1 or 0 once it wrecked (to the respawn or the results) */
#define SB_CAR_PHASE   0x134Cu   /* 1 before the GO, 2 in play, 3 finished (autopilot.c) */
#define SB_MUSIC_OUTER 0x00411BE0u /* the music manager's outer state (0x4115E8 + 0x5F8):
                                    * 6 in a crash mode, 5 in any other stage
                                    * (docs/split-screen.md) */
#define SB_BGV_MAGIC   0x17u

#define SB_MAX_CARS 16
#define SB_K        8            /* braces per node */
#define SB_BRACE_R  0.35f
#define SB_WELD     0.001f
#define SB_SETTLE   90           /* frames simulated after the last push */
#define SB_MAX_IMP  8            /* contacts (rams) at once */
#define SB_DEPTH_K  0.0045f      /* crush: SB_DEPTH_K * sqrt(strength - SB_S0) metres */
#define SB_S0       300.0f       /* below it a hit only scrapes */
#define SB_DEPTH_MAX 1.6f
#define SB_RAM_R0   0.45f        /* the ram's radius: R0 + R1 * min(1, strength / R_S) */
#define SB_RAM_R1   0.6f
#define SB_RAM_RS   40000.0f
#define SB_FRAMES   8            /* a ram goes in over this many steps */
#define SB_SESSION  30           /* flips a contact lasts without a new hit */
#define SB_SESSION_R 0.6f        /* a hit this near a contact belongs to it */
#define SB_CAB      0.4f         /* a ram stops this far short of the frame's centre */
#define SB_FRAME_Y  0.03f        /* the frame lattice's yield strain */
#define SB_PIN_K    0.25f        /* the frame's pull to where it is held */
#define SB_PIN_Y    0.02f        /* ... past this share of the car's size it gives */
#define SB_BEND_K   0.002f       /* a hit across the car bends its frame K * sqrt(strength - S0) */
#define SB_BEND_S0  8000.0f
#define SB_BEND_MAX 0.55f
#define SB_BEND_SIG 0.35f        /* ... most at the hit, over this share of the car's length */
#define SB_BREAK    1.5f         /* a shell beam or brace tears at this times its length */
#define SB_VOXEL    0.22f        /* structure: the body nodes clustered in cubes this big ... */
#define SB_SKNN     12           /* ... each cluster beamed to this many nearest ... */
#define SB_BEAM_MAX 0.75f        /* ... up to this long */
#define SB_NCX      3            /* the chassis lattice */
#define SB_NCY      3
#define SB_NCZ      5
#define SB_NC       (SB_NCX * SB_NCY * SB_NCZ)
#define SB_SKIN     4            /* a body node follows this many structure nodes */
#define SB_SUB      4            /* steps a flip */
#define SB_SITERS   3            /* solver passes a step */
#define SB_MASS     1200.0f      /* kg: two thirds on the body's clusters, a third on the chassis */
#define SB_F_BREAK  1000.0f      /* N: a beam on a torn node breaks past this */
#define SB_DAMP     0.02f        /* a torn node's damping a step ... */
#define SB_DAMP_HELD 0.5f        /* ... one held to the chassis */
#define SB_ACC_MAX  2000.0f      /* m/s^2: the car's motion past this is a jump (a respawn), not a force */
#define SB_GONE     12.0f        /* m: a torn node this far from its place is lost (its triangles go) */
#define SB_GROUND_F 0.6f         /* a torn node on the ground keeps this share of its speed a step (friction) */
#define SB_DT       (1.0f / 60.0f)
#define SB_FACE_N   18           /* a ram shaped by the other car: its face, a height map this many cells a side ... */
#define SB_FACE_CELL 0.15f       /* ... each this big */
#define SB_FACE_PTS 900          /* the other model's surface points kept for it */
#define SB_MASS_K_MIN 0.2f       /* a crash's depth: 2 m_other / (m_self + m_other) of it, within these */
#define SB_MASS_K_MAX 1.8f
#define SB_HITS     32           /* recent hits kept, to pair the two cars of a crash */
#define SB_FX_MAX   6            /* tears that throw sparks, a car a flip */
#define SB_FX_MGR   0x005A9A00u  /* the title's effects manager (PS2 gEffectsManager) */
#define SB_WR_AMP   0.06f        /* wrinkles: a squashed sheet goes out along its normal this far at most ... */
#define SB_WR_SCALE 0.07f        /* ... in a value noise of this cell size */
#define SB_ROOF_Y   -0.6f        /* a hit from above (d's y below this): the ground under a car on its roof ... */
#define SB_ROOF_K   0.009f       /* ... crushes it SB_ROOF_K * sqrt(strength - SB_S0), flat ... */
#define SB_ROOF_MAX 0.45f
#define SB_ROOF_CAB 0.3f         /* ... down to this short of the chassis' middle */
#define SB_WHEEL_GO 0.2f         /* in a wreck, a wheel goes when its arch moved this far (m, mean) */
#define SB_WHEEL_TILT 0.45f      /* ... one that stays tilts with its arch, at most this (rad) */
#define SB_WHEEL_SHIFT 0.35f     /* ... and moves with it, at most this far (m) */
#define SB_WHEEL_GAIN 2.5f       /* ... its tilt this many times its arch's (the arcade's broken axle) */
#define SB_WHEEL_GAIN_D 1.5f     /* ... its move this many times */
#define SB_TEAR     1.6f         /* a triangle with an edge stretched past this ... */
#define SB_TEAR_MIN 0.05f        /* ... and by more than this (m) is left out: a tear */
#define SB_TEAR2    1.3f         /* a big triangle tears sooner: past this ... */
#define SB_TEAR2_MIN 0.2f        /* ... and by more than this (m) */

typedef struct {
    uint32_t vb, n;              /* the vertex buffer (guest VA), vertices */
    float *p0;                   /* rest positions, 3n */
    uint32_t *n0;                /* rest normals, packed */
    int32_t *node;               /* each vertex's node, -1 left alone */
    int nrec;                    /* its strip records (tearing) */
    uint32_t *rec;               /* 3 per record: where (the 1.0f), indices, count as built */
    uint32_t tbuf, tcap;         /* our strips with the torn triangles left out (contiguous) */
} sb_lod;

typedef struct {
    uint32_t car, bgv;
    int nn, ncore, nb;           /* body nodes (no core: ncore 0), structure beams */
    float *rest, *x;             /* the body nodes: 3 * nn */
    uint8_t *gone;               /* body nodes that follow a lost structure node */
    /* The structure: the body's clusters first, then the chassis lattice. */
    int sn, sns, scenter;        /* nodes, the body's, the fixed middle */
    float *sx, *srest, *sv, *sw; /* positions, rest, velocities (m/s), inverse masses */
    float *sq;                   /* 4 per node: its turn (a quaternion) */
    uint8_t *sgone;              /* torn nodes that flew too far */
    int32_t *skn;                /* SB_SKIN structure nodes per body node ... */
    float *skw, *sko;            /* ... their weights, the body node's rest offset from each */
    int32_t *ba, *bb;
    float *len, *yield, *stiff;
    float *len0;                 /* the beams' first lengths (tearing) */
    uint8_t *kind;               /* 0 body, 1 brace to the chassis, 2 chassis, 3 torn */
    float pin[SB_NC * 3];        /* where each chassis node is held (plastic) */
    float size;                  /* the body's diagonal */
    float blen;                  /* ... its length */
    float mot[3][12];            /* the car's last three matrices (rows, position), newest first */
    int nmot;
    uint32_t mot_flip;
    float ground;                /* the road's height in the world (y), from where the car last stood on its wheels */
    int have_ground;
    float ymin;                  /* the body's lowest point at rest (its frame) */
    int ab;                      /* drawn as loaded for BO3_SOFTBODY_AB */
    int nfx, ntears;             /* tears this step that throw sparks, and all of them: */
    float fx[SB_FX_MAX][6];      /* where (the car's frame) and which way */
    int nloose;                  /* structure nodes torn from the chassis ... */
    int loose_moving;            /* ... and whether one still moved at the last step */
    int loose_said;              /* ... the count last logged */
    int32_t *nbr;                /* SB_K neighbours per surface node, -1 none */
    float *nmat;                 /* 9 per node: F^-T */
    sb_lod lod[4];
    int nlod;                    /* levels in the model (.bgv 4, traffic .btv 3) */
    struct {                     /* contacts: flat rams driven in over a few frames */
        float o[3], d[3], r, depth, step, target, cap, pt[3];
        float bend, bend_t, bend_d[3], bend_w[SB_NC]; /* the frame's bend: done, to do, which way, how much where */
        int shaped;              /* 1 the other car's face, 2 the ground's (flat), 0 round ... */
        float mass_k;            /* ... the depth's share by the masses (0: not set, 1) */
        float fu[3], fv[3];      /* ... across d ... */
        float face[SB_FACE_N * SB_FACE_N];   /* ... how far its surface lies behind its foremost point */
        int pushes;
        uint32_t t;              /* the flip of its last hit */
    } imp[SB_MAX_IMP];
    uint8_t *touched;            /* structure nodes a ram moved this step */
    int settle;
    uint32_t last_flip;
    int dented;
    int game_damaged;            /* the title's own panel damage seen since build */
    int traffic;                 /* built on a hit, on its own model copy */
    uint32_t phase;              /* the event phase at the last draw */
    uint32_t orig_bgv;           /* traffic: the shared model the copy came from */
    uint32_t sig[14];            /* the model's size, levels and buffers when built */
    float panels_rest[256];      /* the title's 16 panel slots as built */
    double ms_sum;               /* time in step and write-back, for the log */
    int ticks;
    uint8_t part_state[6];       /* the title's panel states last logged */
    int ntorn, tear_seen;        /* beams torn, and when the strips were last cut */
    int tearing;                 /* some record points at our strips */
    uint8_t wheel_gone;          /* wheels we let go in this wreck (bits) */
    uint8_t wheel_bent;          /* wheels whose matrix we bent for this draw (bits) ... */
    float wheel_said[6];         /* the bend each wheel was last logged at, this wreck */
    float wheel_base[6][16];     /* ... and the title's matrix, put back after it */
    uint32_t wreck_seen;         /* + 0x3D0 last logged */
    /* The full level subdivided (each body triangle into four): our
     * buffers, and what they took the place of. */
    uint32_t sub_buf, sub_strips;  /* both streams' vertices; the body's strips */
    uint32_t sub_hdr, sub_d19, sub_d22;  /* the level header, its two buffers' Data as loaded */
    int sub_nrec;
    uint32_t sub_rec[3 * 64];      /* entry 0's records: where, indices, count as loaded */
} sb_car;

static sb_car s_cars[SB_MAX_CARS];
static void car_free(sb_car *c);
static void car_tear(sb_car *c, int restore);
static int car_model_same(const sb_car *c);
static int s_log, s_test, s_traffic;
static int s_subdiv = 1;       /* BO3_SOFTBODY_SUBDIV=0: the full level as loaded */
static int s_sparks = 1;       /* BO3_SOFTBODY_SPARKS=0: no sparks where metal tears */
static int s_ab;               /* BO3_SOFTBODY_AB=<file>: while the file exists, every car as loaded (a test aid) */
static float s_wrinkle = 1.0f; /* BO3_SOFTBODY_WRINKLE: the wrinkles' depth, scaled (0 none) */
static float s_parts = 0.15f;  /* BO3_SOFTBODY_PARTS: the title's thresholds for loose and lost panels, scaled */
static uint32_t (*s_flips)(void);
static sb_car *s_drawing;   /* the hit traffic vehicle sub_000303D0 draws now */

/* ------------------------------------------------------------------ */
/* Small vector helpers                                                 */

static float v3dist2(const float *a, const float *b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

static int inv3(const float *m, float *o)
{
    float d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6])
            + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (fabsf(d) < 1e-12f)
        return 0;
    d = 1.0f / d;
    o[0] = (m[4] * m[8] - m[5] * m[7]) * d;
    o[1] = (m[2] * m[7] - m[1] * m[8]) * d;
    o[2] = (m[1] * m[5] - m[2] * m[4]) * d;
    o[3] = (m[5] * m[6] - m[3] * m[8]) * d;
    o[4] = (m[0] * m[8] - m[2] * m[6]) * d;
    o[5] = (m[2] * m[3] - m[0] * m[5]) * d;
    o[6] = (m[3] * m[7] - m[4] * m[6]) * d;
    o[7] = (m[1] * m[6] - m[0] * m[7]) * d;
    o[8] = (m[0] * m[4] - m[1] * m[3]) * d;
    return 1;
}

static void unpack_normal(uint32_t u, float *n)
{
    int x = (int)(u & 0x7FF), y = (int)((u >> 11) & 0x7FF), z = (int)((u >> 22) & 0x3FF);
    if (x >= 0x400) x -= 0x800;
    if (y >= 0x400) y -= 0x800;
    if (z >= 0x200) z -= 0x400;
    n[0] = x / 1023.0f;
    n[1] = y / 1023.0f;
    n[2] = z / 511.0f;
}

static uint32_t pack_normal(const float *n)
{
    float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    int x, y, z;
    if (l < 1e-12f)
        l = 1.0f;
    x = (int)lrintf(n[0] / l * 1023.0f);
    y = (int)lrintf(n[1] / l * 1023.0f);
    z = (int)lrintf(n[2] / l * 511.0f);
    if (x > 1023) x = 1023; else if (x < -1023) x = -1023;
    if (y > 1023) y = 1023; else if (y < -1023) y = -1023;
    if (z > 511) z = 511; else if (z < -511) z = -511;
    return ((uint32_t)x & 0x7FFu) | (((uint32_t)y & 0x7FFu) << 11) | (((uint32_t)z & 0x3FFu) << 22);
}

/* ------------------------------------------------------------------ */
/* A uniform grid over points, for neighbour queries at build time      */

typedef struct {
    float lo[3], cell;
    int dim[3];
    int32_t *head, *next;        /* bucket lists */
} sb_grid;

static int grid_cell(const sb_grid *g, const float *p, int axis)
{
    int c = (int)floorf((p[axis] - g->lo[axis]) / g->cell);
    return c < 0 ? 0 : c >= g->dim[axis] ? g->dim[axis] - 1 : c;
}

static void grid_build(sb_grid *g, const float *pts, int n, float cell)
{
    float hi[3] = { -1e9f, -1e9f, -1e9f };
    int i, a;
    g->lo[0] = g->lo[1] = g->lo[2] = 1e9f;
    for (i = 0; i < n; i++)
        for (a = 0; a < 3; a++) {
            if (pts[3 * i + a] < g->lo[a]) g->lo[a] = pts[3 * i + a];
            if (pts[3 * i + a] > hi[a]) hi[a] = pts[3 * i + a];
        }
    g->cell = cell;
    for (a = 0; a < 3; a++)
        g->dim[a] = (int)((hi[a] - g->lo[a]) / cell) + 1;
    g->head = malloc(sizeof(int32_t) * (size_t)g->dim[0] * g->dim[1] * g->dim[2]);
    g->next = malloc(sizeof(int32_t) * (size_t)(n ? n : 1));
    memset(g->head, 0xFF, sizeof(int32_t) * (size_t)g->dim[0] * g->dim[1] * g->dim[2]);
    for (i = 0; i < n; i++) {
        int c = (grid_cell(g, pts + 3 * i, 2) * g->dim[1] + grid_cell(g, pts + 3 * i, 1)) * g->dim[0]
              + grid_cell(g, pts + 3 * i, 0);
        g->next[i] = g->head[c];
        g->head[c] = i;
    }
}

static void grid_free(sb_grid *g)
{
    free(g->head);
    free(g->next);
}

/* The nearest point to p within r (squared distance in *d2), or -1. */
static int grid_nearest(const sb_grid *g, const float *pts, const float *p, float r, float *d2)
{
    int c0[3], c1[3], x, y, z, best = -1, a;
    float bd = r * r;
    for (a = 0; a < 3; a++) {
        c0[a] = (int)floorf((p[a] - r - g->lo[a]) / g->cell);
        c1[a] = (int)floorf((p[a] + r - g->lo[a]) / g->cell);
        if (c0[a] < 0) c0[a] = 0;
        if (c1[a] >= g->dim[a]) c1[a] = g->dim[a] - 1;
    }
    for (z = c0[2]; z <= c1[2]; z++)
        for (y = c0[1]; y <= c1[1]; y++)
            for (x = c0[0]; x <= c1[0]; x++) {
                int32_t i;
                for (i = g->head[(z * g->dim[1] + y) * g->dim[0] + x]; i >= 0; i = g->next[i]) {
                    float d = v3dist2(pts + 3 * i, p);
                    if (d < bd) {
                        bd = d;
                        best = i;
                    }
                }
            }
    if (d2)
        *d2 = bd;
    return best;
}

/* Up to k nearest points to point `self` within r, nearest first. */
static int grid_knn(const sb_grid *g, const float *pts, int self, float r, int k, int32_t *out)
{
    int c0[3], c1[3], x, y, z, a, n = 0, j;
    float dist[SB_K + 1];
    const float *p = pts + 3 * self;
    for (a = 0; a < 3; a++) {
        c0[a] = (int)floorf((p[a] - r - g->lo[a]) / g->cell);
        c1[a] = (int)floorf((p[a] + r - g->lo[a]) / g->cell);
        if (c0[a] < 0) c0[a] = 0;
        if (c1[a] >= g->dim[a]) c1[a] = g->dim[a] - 1;
    }
    for (z = c0[2]; z <= c1[2]; z++)
        for (y = c0[1]; y <= c1[1]; y++)
            for (x = c0[0]; x <= c1[0]; x++) {
                int32_t i;
                for (i = g->head[(z * g->dim[1] + y) * g->dim[0] + x]; i >= 0; i = g->next[i]) {
                    float d;
                    if (i == self)
                        continue;
                    d = v3dist2(pts + 3 * i, p);
                    if (d > r * r || (n == k && d >= dist[n - 1]))
                        continue;
                    j = n < k ? n++ : k - 1;
                    while (j > 0 && dist[j - 1] > d) {
                        dist[j] = dist[j - 1];
                        out[j] = out[j - 1];
                        j--;
                    }
                    dist[j] = d;
                    out[j] = i;
                }
            }
    return n;
}

/* ------------------------------------------------------------------ */
/* The .bgv                                                            */

typedef struct {
    uint32_t L, vb, n;
    int nent;
    uint32_t ent[16];
} bgv_lod;

/* The loader turns the file's offsets into addresses: the levels and the
 * entries are contiguous-window addresses (0x81...), the vertex and index
 * buffers physical ones (0x01...), a strip record's offset the address of
 * its indices. */
static void bgv_lod_read(uint32_t bgv, int k, bgv_lod *o)
{
    uint32_t L = SB_M32(bgv + 0x4C + 4u * k), h19, h22;
    int e;
    o->L = L;
    h19 = SB_M32(L + 19 * 4);
    h22 = SB_M32(L + 22 * 4);
    o->vb = h19 | 0x80000000u;
    o->n = h22 > h19 ? (h22 - h19) / 24 : 0;
    o->nent = 0;
    for (e = 0; e < 16; e++)
        if (SB_M32(L + 4u * e))
            o->ent[o->nent++] = SB_M32(L + 4u * e);
}

/* Marks with `mark` (>= 1) every vertex an entry's strips use. */
static void bgv_entry_mark(const bgv_lod *l, int e, int32_t *tag, int mark)
{
    uint32_t end = e + 1 < l->nent ? l->ent[e + 1] : l->vb, a;
    if (end > l->ent[e] + 0x400)
        end = l->ent[e] + 0x400;
    for (a = l->ent[e]; a + 16 <= end; a += 4) {
        uint32_t idx, cnt, i;
        if (SB_M32(a) != 0x3F800000u || SB_M32(a + 4) != 0)
            continue;
        idx = SB_M32(a + 8);
        cnt = SB_M32(a + 12);
        if (!cnt || cnt >= 0x10000u || idx < 0x80000000u || idx >= 0x84000000u)
            continue;
        for (i = 0; i < cnt; i++) {
            uint16_t v = *(uint16_t *)SB_PTR(idx + 2 * i);
            if (v < l->n)
                tag[v] = mark;
        }
    }
}

/* The offset that puts a part's vertices on the attached copy (pts, a grid
 * over it): candidates from a part vertex to every attached vertex, scored
 * by how many sampled part vertices land on one. 1 and out when found. */
static int part_offset(const float *part, int np, const float *pts, int n, const sb_grid *g, float *out)
{
    int best = 0, a, s, i, samples = np < 24 ? np : 24, anchors = np < 3 ? np : 3;
    for (a = 0; a < anchors && best < samples; a++) {
        const float *anchor = part + 3 * ((a * 7919) % np);
        for (i = 0; i < n; i++) {
            float t[3] = { pts[3 * i] - anchor[0], pts[3 * i + 1] - anchor[1], pts[3 * i + 2] - anchor[2] };
            int hits = 0;
            for (s = 0; s < samples; s++) {
                const float *q = part + 3 * ((s * 104729) % np);
                float p[3] = { q[0] + t[0], q[1] + t[1], q[2] + t[2] }, d2;
                if (grid_nearest(g, pts, p, 0.004f, &d2) >= 0)
                    hits++;
                else if (hits + (samples - s - 1) <= best)
                    break;
            }
            if (hits > best) {
                best = hits;
                memcpy(out, t, sizeof t);
            }
        }
    }
    return best * 10 >= samples * 9;
}

/* ------------------------------------------------------------------ */
/* Own copies of shared models                                          */

/* Traffic vehicles share models: a dent written into one would show on
 * every vehicle drawn from it. A hit vehicle gets its own copy, in
 * contiguous memory taken from the runtime's own allocator (so the title is
 * never handed it, and the GPU resolves it in the window as any other
 * buffer), preferably above 40 MB where the title's allocations do not
 * reach (high-water about 43 MB in a race and a Crash), and its + 0x50
 * points there. Only the addresses the damage model knows are moved to the
 * copy: the levels, the entries, the vertex buffers, the strip records;
 * anything else in it still points into the shared original, read only. */
static void reloc(uint32_t at, uint32_t lo, uint32_t hi, uint32_t delta)
{
    uint32_t v = SB_M32(at);
    if (v >= lo && v < hi)
        SB_M32(at) = v + delta;
}

static uint32_t model_copy(uint32_t bgv)
{
    uint32_t size = SB_M32(bgv + 8), dst, delta, phys = bgv & 0x7FFFFFFFu;
    int nlod = (int)(SB_M32(bgv + 0x10) & 0xFF) + 1, k, e;
    if (!size || size > 0x200000u || nlod < 1 || nlod > 4)
        return 0;
    size = (size + 0xFFFu) & ~0xFFFu;
    dst = xbox_ContiguousAllocEx(size, 0x1000, 0x02800000u, 0x03FFFFFFu);
    if (!dst)
        dst = xbox_ContiguousAllocEx(size, 0x1000, 0, 0x03FFFFFFu);
    if (!dst)
        return 0;
    memcpy(SB_PTR(dst), SB_PTR(bgv), SB_M32(bgv + 8));
    delta = dst - bgv;
    for (k = 0; k < nlod; k++) {
        uint32_t L;
        reloc(dst + 0x4C + 4u * k, bgv, bgv + size, delta);
        L = SB_M32(dst + 0x4C + 4u * k);
        /* Everything else in the level's header and entries that points
         * into the original (an entry's pointer to its own records, the
         * records' index addresses, ...): no vertex data lives there, so a
         * value in the model's own range is an address. */
        {
            /* Up to the first strip's indices (u16 pairs could look like an
             * address): the lowest index address any record names. */
            uint32_t end = L + 0x1000, a, first = 0xFFFFFFFFu;
            for (e = 0; e < 16; e++) {
                uint32_t ent = SB_M32(L + 4u * e);
                if (ent < bgv || ent >= bgv + size)
                    continue;
                ent += delta;
                for (a = ent; a + 16 <= ent + 0x400; a += 4)
                    if (SB_M32(a) == 0x3F800000u && SB_M32(a + 4) == 0) {
                        uint32_t idx = SB_M32(a + 8);
                        if (idx >= bgv && idx < bgv + size && idx + delta > L && idx + delta < first)
                            first = idx + delta;
                    }
            }
            if (first < end)
                end = first;
            for (a = L; a + 4 <= end; a += 4) {
                reloc(a, bgv, bgv + size, delta);
                reloc(a, phys, phys + size, delta);
            }
        }
    }
    if (s_log)
        fprintf(stderr, "[SOFTBODY] model 0x%08X copied to 0x%08X (%u bytes)\n", bgv, dst, size);
    return dst;
}

/* What tells one loaded model from the next at the same address (the title
 * loads the next event's cars where the last ones were): its size, its
 * levels and their vertex and index buffers. */
static void model_sig(uint32_t bgv, uint32_t *sig)
{
    int k;
    memset(sig, 0, sizeof(uint32_t) * 14);
    sig[0] = SB_M32(bgv + 8);
    sig[1] = SB_M32(bgv + 0x10);
    for (k = 0; k < 4; k++) {
        uint32_t L = SB_M32(bgv + 0x4C + 4u * k);
        sig[2 + 3 * k] = L;
        if (L >= 0x80000000u && L < 0x84000000u) {
            sig[3 + 3 * k] = SB_M32(L + 19 * 4);
            sig[4 + 3 * k] = SB_M32(L + 22 * 4);
        }
    }
}

/* The car's model is still the one it was built from: else nothing of it
 * may be written (its buffers are some other model's data now). */
static int car_model_same(const sb_car *c)
{
    uint32_t now[14];
    if (SB_M32(c->bgv) != SB_BGV_MAGIC)
        return 0;
    model_sig(c->bgv, now);
    return !memcmp(now, c->sig, sizeof now);
}

/* Traffic keeps no damage outside a crash event: a city car met twice
 * would drive about broken. A released vehicle draws its shared model
 * again; once none is left, the pool is emptied for the next crash. */
static void traffic_free(sb_car *t)
{
    uint32_t copy = t->bgv;
    if (SB_M32(t->car + SB_CAR_BGV) == copy)
        SB_M32(t->car + SB_CAR_BGV) = t->orig_bgv;
    car_free(t);
    xbox_ContiguousFree(copy);
}

static void traffic_release_all(void)
{
    int i, any = 0;
    for (i = 0; i < SB_MAX_CARS; i++)
        if (s_cars[i].car && s_cars[i].traffic) {
            traffic_free(&s_cars[i]);
            any = 1;
        }
    if (s_log && any)
        fprintf(stderr, "[SOFTBODY] traffic released, copies freed\n");
}

static int crash_event(void)
{
    return SB_M32(SB_MUSIC_OUTER) == 6;
}

/* ------------------------------------------------------------------ */
/* Building a car                                                       */

/* The full level's body, each triangle into four at its edges' middles:
 * one buffer for both streams (stream 0 the 24-byte vertices, stream 1 the
 * 8 bytes each after them, so the level's vertex count stays what the two
 * Data fields span), the loaded vertices first and where they were, then a
 * vertex per edge of entry 0's triangles (position, normal and uv the
 * mean of its ends, stream 1 its first end's); entry 0's records point at
 * strips of our own, each new triangle joined to the last by degenerate
 * ones at its own parity (the winding stays). The part entries and the
 * lower levels keep theirs. Before the car is built: its body nodes are
 * then the finer mesh's. */
static uint32_t sub_edge(uint32_t *key, uint32_t *val, uint32_t mask, uint32_t a, uint32_t b, uint32_t *next)
{
    uint32_t k = a < b ? (a << 16) | b : (b << 16) | a, h = (k * 2654435761u) & mask;
    while (key[h] != 0xFFFFFFFFu && key[h] != k)
        h = (h + 1) & mask;
    if (key[h] != k) {
        key[h] = k;
        val[h] = (*next)++;
    }
    return val[h];
}

static void sub_tri(uint16_t *out, uint32_t *n, uint16_t a, uint16_t b, uint16_t c)
{
    if (*n) {                       /* the last again, then this one's first at an even place */
        out[*n] = out[*n - 1];
        (*n)++;
        out[(*n)++] = a;
        if (*n & 1)
            out[(*n)++] = a;
    }
    out[(*n)++] = a;
    out[(*n)++] = b;
    out[(*n)++] = c;
}

static void car_subdivide(sb_car *c, uint32_t bgv)
{
    bgv_lod l;
    uint32_t end, a, n, next, mask = (1u << 17) - 1, *key, *val, buf, strips, off = 0, cap = 0, d22;
    uint32_t rec[3 * 64];
    int nrec = 0, r, i;

    bgv_lod_read(bgv, c->nlod - 1, &l);
    if (!l.n || !l.nent)
        return;
    n = l.n;
    d22 = SB_M32(l.L + 22 * 4);
    end = l.nent > 1 ? l.ent[1] : l.vb;
    if (end > l.ent[0] + 0x400)
        end = l.ent[0] + 0x400;
    for (a = l.ent[0]; a + 16 <= end && nrec < 64; a += 4) {
        uint32_t idx = SB_M32(a + 8), cnt = SB_M32(a + 12) & 0xFFFF;
        if (SB_M32(a) != 0x3F800000u || SB_M32(a + 4) != 0 || cnt < 3 || idx < 0x80000000u || idx >= 0x84000000u)
            continue;
        if ((cnt - 2) * 24 + 8 > 0xFFFFu)
            return;                  /* a record too long to take four times its triangles */
        rec[3 * nrec] = a;
        rec[3 * nrec + 1] = idx;
        rec[3 * nrec + 2] = cnt;
        cap += (cnt - 2) * 24 + 8;
        nrec++;
    }
    if (!nrec)
        return;
    /* The edges: a vertex each, numbered after the loaded ones. */
    key = malloc(sizeof(uint32_t) * (mask + 1));
    val = malloc(sizeof(uint32_t) * (mask + 1));
    memset(key, 0xFF, sizeof(uint32_t) * (mask + 1));
    next = n;
    for (r = 0; r < nrec; r++) {
        const uint16_t *s = (const uint16_t *)SB_PTR(rec[3 * r + 1]);
        uint32_t t;
        for (t = 0; t + 2 < rec[3 * r + 2]; t++)
            if (s[t] != s[t + 1] && s[t + 1] != s[t + 2] && s[t] != s[t + 2] &&
                s[t] < n && s[t + 1] < n && s[t + 2] < n && next < 0xFFF0u) {
                sub_edge(key, val, mask, s[t], s[t + 1], &next);
                sub_edge(key, val, mask, s[t + 1], s[t + 2], &next);
                sub_edge(key, val, mask, s[t + 2], s[t], &next);
            }
    }
    if (next >= 0xFFF0u) {
        free(key); free(val);
        return;
    }
    buf = xbox_ContiguousAllocEx((next * 32 + 0xFFFu) & ~0xFFFu, 0x1000, 0x02800000u, 0x03FFFFFFu);
    if (!buf)
        buf = xbox_ContiguousAllocEx((next * 32 + 0xFFFu) & ~0xFFFu, 0x1000, 0, 0x03FFFFFFu);
    strips = xbox_ContiguousAllocEx(cap * 2 + 16, 0x10, 0x02800000u, 0x03FFFFFFu);
    if (!strips)
        strips = xbox_ContiguousAllocEx(cap * 2 + 16, 0x10, 0, 0x03FFFFFFu);
    if (!buf || !strips) {
        if (buf) xbox_ContiguousFree(buf);
        if (strips) xbox_ContiguousFree(strips);
        free(key); free(val);
        return;
    }
    memcpy(SB_PTR(buf), SB_PTR(l.vb), 24 * n);
    memcpy(SB_PTR(buf + 24 * next), SB_PTR(d22 | 0x80000000u), 8 * n);
    for (i = 0; i <= (int)mask; i++) {
        uint32_t k = key[i], ea, eb, m = val[i];
        const uint8_t *va, *vb;
        uint8_t *vm;
        float na[3], nb[3], nm[3], ln;
        uint32_t pk;
        int q;
        if (k == 0xFFFFFFFFu)
            continue;
        ea = k >> 16;
        eb = k & 0xFFFF;
        va = SB_PTR(buf + 24 * ea);
        vb = SB_PTR(buf + 24 * eb);
        vm = SB_PTR(buf + 24 * m);
        for (q = 0; q < 3; q++)
            ((float *)vm)[q] = (((const float *)va)[q] + ((const float *)vb)[q]) / 2;
        unpack_normal(*(const uint32_t *)(va + 12), na);
        unpack_normal(*(const uint32_t *)(vb + 12), nb);
        for (q = 0; q < 3; q++)
            nm[q] = na[q] + nb[q];
        ln = sqrtf(nm[0] * nm[0] + nm[1] * nm[1] + nm[2] * nm[2]);
        for (q = 0; q < 3; q++)
            nm[q] = ln > 1e-6f ? nm[q] / ln : na[q];
        pk = pack_normal(nm);
        memcpy(vm + 12, &pk, 4);
        for (q = 0; q < 2; q++)
            ((float *)(vm + 16))[q] = (((const float *)(va + 16))[q] + ((const float *)(vb + 16))[q]) / 2;
        memcpy(SB_PTR(buf + 24 * next + 8 * m), SB_PTR(buf + 24 * next + 8 * ea), 8);
    }
    /* The strips. */
    for (r = 0; r < nrec; r++) {
        const uint16_t *s = (const uint16_t *)SB_PTR(rec[3 * r + 1]);
        uint16_t *out = (uint16_t *)SB_PTR(strips + 2 * off);
        uint32_t t, cnt = 0;
        for (t = 0; t + 2 < rec[3 * r + 2]; t++) {
            uint16_t x = s[t], y = s[t + 1], z = s[t + 2], m01, m12, m20;
            if (x == y || y == z || x == z || x >= n || y >= n || z >= n)
                continue;
            if (t & 1) { uint16_t w = x; x = y; y = w; }     /* its winding as drawn */
            m01 = (uint16_t)sub_edge(key, val, mask, x, y, &next);
            m12 = (uint16_t)sub_edge(key, val, mask, y, z, &next);
            m20 = (uint16_t)sub_edge(key, val, mask, z, x, &next);
            sub_tri(out, &cnt, x, m01, m20);
            sub_tri(out, &cnt, m01, y, m12);
            sub_tri(out, &cnt, m20, m12, z);
            sub_tri(out, &cnt, m01, m12, m20);
        }
        if (cnt < 3)
            continue;
        SB_M32(rec[3 * r] + 8) = strips + 2 * off;
        *(uint16_t *)SB_PTR(rec[3 * r] + 12) = (uint16_t)cnt;
        off += cnt + (cnt & 1);
    }
    free(key);
    free(val);
    c->sub_buf = buf;
    c->sub_strips = strips;
    c->sub_hdr = l.L;
    c->sub_d19 = SB_M32(l.L + 19 * 4);
    c->sub_d22 = d22;
    c->sub_nrec = nrec;
    memcpy(c->sub_rec, rec, sizeof(uint32_t) * 3 * nrec);
    SB_M32(l.L + 19 * 4) = buf & 0x7FFFFFFFu;
    SB_M32(l.L + 22 * 4) = (buf + 24 * next) & 0x7FFFFFFFu;
    if (s_log)
        fprintf(stderr, "[SOFTBODY] car 0x%08X full level subdivided: %u vertices -> %u, %d records\n",
                c->car, n, next, nrec);
}

/* Back as loaded (if the model is still the one we changed), and ours freed. */
static void car_unsubdivide(sb_car *c)
{
    int r;
    if (!c->sub_buf)
        return;
    if (car_model_same(c)) {
        SB_M32(c->sub_hdr + 19 * 4) = c->sub_d19;
        SB_M32(c->sub_hdr + 22 * 4) = c->sub_d22;
        for (r = 0; r < c->sub_nrec; r++) {
            SB_M32(c->sub_rec[3 * r] + 8) = c->sub_rec[3 * r + 1];
            *(uint16_t *)SB_PTR(c->sub_rec[3 * r] + 12) = (uint16_t)c->sub_rec[3 * r + 2];
        }
    }
    xbox_ContiguousFree(c->sub_buf);
    xbox_ContiguousFree(c->sub_strips);
    c->sub_buf = c->sub_strips = 0;
}

static void car_free(sb_car *c)
{
    int k;
    if (s_drawing == c)
        s_drawing = NULL;
    free(c->rest); free(c->x); free(c->gone); free(c->len0); free(c->kind);
    free(c->ba); free(c->bb); free(c->len); free(c->yield); free(c->stiff);
    free(c->nbr); free(c->nmat); free(c->touched);
    free(c->sx); free(c->srest); free(c->sv); free(c->sw); free(c->sq); free(c->sgone);
    free(c->skn); free(c->skw); free(c->sko);
    if (c->tearing && car_model_same(c))
        car_tear(c, 1);
    car_unsubdivide(c);
    for (k = 0; k < 4; k++) {
        free(c->lod[k].p0); free(c->lod[k].n0); free(c->lod[k].node); free(c->lod[k].rec);
        if (c->lod[k].tbuf)
            xbox_ContiguousFree(c->lod[k].tbuf);
    }
    memset(c, 0, sizeof *c);
}

/* The structure (sb/dyn.py): the body nodes clustered in SB_VOXEL cubes, a
 * node each at its cluster's middle, beamed to their SB_SKNN nearest up to
 * SB_BEAM_MAX; the chassis lattice inside the cabin, its neighbours beamed
 * (diagonals too), and two braces from each cluster to its nearest chassis
 * nodes; the masses; each body node's skin. */
static void car_structure(sb_car *c, const float *lo, const float *hi)
{
    int nn = c->nn, i, j, k, a, ns = 0, nb = 0, cap, dim[3], ncell;
    int32_t *cl = malloc(sizeof(int32_t) * nn), *id, *cnt, *knn;
    float *x;

    for (a = 0; a < 3; a++)
        dim[a] = (int)((hi[a] - lo[a]) / SB_VOXEL) + 1;
    ncell = dim[0] * dim[1] * dim[2];
    id = malloc(sizeof(int32_t) * ncell);
    for (i = 0; i < ncell; i++)
        id[i] = -1;
    for (i = 0; i < nn; i++) {
        int ci[3];
        for (a = 0; a < 3; a++) {
            ci[a] = (int)((c->rest[3 * i + a] - lo[a]) / SB_VOXEL);
            ci[a] = ci[a] < 0 ? 0 : ci[a] >= dim[a] ? dim[a] - 1 : ci[a];
        }
        k = (ci[2] * dim[1] + ci[1]) * dim[0] + ci[0];
        if (id[k] < 0)
            id[k] = ns++;
        cl[i] = id[k];
    }
    free(id);
    c->sns = ns;
    c->sn = ns + SB_NC;
    x = calloc((size_t)3 * c->sn, sizeof(float));
    cnt = calloc((size_t)ns, sizeof(int32_t));
    for (i = 0; i < nn; i++) {
        for (a = 0; a < 3; a++)
            x[3 * cl[i] + a] += c->rest[3 * i + a];
        cnt[cl[i]]++;
    }
    for (j = 0; j < ns; j++)
        for (a = 0; a < 3; a++)
            x[3 * j + a] /= (float)cnt[j];
    {
        static const float sc[3] = { 0.25f, 0.2f, 0.32f };
        static const int nd[3] = { SB_NCX, SB_NCY, SB_NCZ };
        int ix, iy, iz;
        for (ix = 0; ix < SB_NCX; ix++)
            for (iy = 0; iy < SB_NCY; iy++)
                for (iz = 0; iz < SB_NCZ; iz++) {
                    int g[3] = { ix, iy, iz };
                    float *p = x + 3 * (ns + (ix * SB_NCY + iy) * SB_NCZ + iz);
                    for (a = 0; a < 3; a++)
                        p[a] = (lo[a] + hi[a]) / 2 + (2.0f * g[a] / (nd[a] - 1) - 1) * (hi[a] - lo[a]) * sc[a];
                }
        c->scenter = ns + ((SB_NCX / 2) * SB_NCY + SB_NCY / 2) * SB_NCZ + SB_NCZ / 2;
    }
    c->srest = x;
    c->ymin = lo[1];
    c->sx = malloc(sizeof(float) * 3 * c->sn);
    memcpy(c->sx, x, sizeof(float) * 3 * c->sn);
    c->sv = calloc((size_t)3 * c->sn, sizeof(float));
    c->sq = malloc(sizeof(float) * 4 * c->sn);
    for (j = 0; j < c->sn; j++) {
        c->sq[4 * j] = c->sq[4 * j + 1] = c->sq[4 * j + 2] = 0;
        c->sq[4 * j + 3] = 1;
    }
    c->sgone = calloc((size_t)c->sn, 1);
    c->touched = calloc((size_t)c->sn, 1);
    c->sw = malloc(sizeof(float) * c->sn);
    for (j = 0; j < ns; j++)
        c->sw[j] = 1.0f / (SB_MASS * 2 / 3 * cnt[j] / nn);
    for (j = ns; j < c->sn; j++)
        c->sw[j] = 1.0f / (SB_MASS / 3 / SB_NC);
    c->sw[c->scenter] = 0;

    /* Beams. */
    cap = ns * SB_SKNN + ns * 2 + SB_NC * 26;
    c->kind = malloc(cap);
    c->ba = malloc(sizeof(int32_t) * cap);
    c->bb = malloc(sizeof(int32_t) * cap);
    c->len = malloc(sizeof(float) * cap);
    c->yield = malloc(sizeof(float) * cap);
    c->stiff = malloc(sizeof(float) * cap);
#define SB_ADD(A, B, K, Y, S) do { c->ba[nb] = (A); c->bb[nb] = (B); c->kind[nb] = (K); \
        c->yield[nb] = (Y); c->stiff[nb] = (S); nb++; } while (0)
    knn = malloc(sizeof(int32_t) * SB_SKNN * ns);
    for (i = 0; i < ns; i++) {
        float bd[SB_SKNN];
        int32_t *o = knn + SB_SKNN * i;
        for (k = 0; k < SB_SKNN; k++) { o[k] = -1; bd[k] = SB_BEAM_MAX * SB_BEAM_MAX; }
        for (j = 0; j < ns; j++) {
            float d = v3dist2(x + 3 * i, x + 3 * j);
            int s = SB_SKNN - 1;
            if (j == i || d >= bd[s])
                continue;
            while (s > 0 && bd[s - 1] > d) { bd[s] = bd[s - 1]; o[s] = o[s - 1]; s--; }
            bd[s] = d;
            o[s] = j;
        }
    }
    for (i = 0; i < ns; i++)
        for (k = 0; k < SB_SKNN && (j = knn[SB_SKNN * i + k]) >= 0; k++) {
            int q, mutual = 0;
            for (q = 0; q < SB_SKNN; q++)
                if (knn[SB_SKNN * j + q] == i)
                    mutual = 1;
            if (i < j || !mutual)            /* each pair once */
                SB_ADD(i, j, 0, 0.02f, 1.0f);
        }
    for (i = 0; i < ns; i++) {
        int best[2] = { -1, -1 };
        float bd[2] = { 1e9f, 1e9f };
        for (j = ns; j < c->sn; j++) {
            float d = v3dist2(x + 3 * i, x + 3 * j);
            if (d < bd[0]) { bd[1] = bd[0]; best[1] = best[0]; bd[0] = d; best[0] = j; }
            else if (d < bd[1]) { bd[1] = d; best[1] = j; }
        }
        SB_ADD(i, best[0], 1, 0.06f, 0.35f);
        SB_ADD(i, best[1], 1, 0.06f, 0.35f);
    }
    for (i = 0; i < SB_NC; i++)
        for (k = i + 1; k < SB_NC; k++) {
            int dx = abs(i / (SB_NCY * SB_NCZ) - k / (SB_NCY * SB_NCZ));
            int dy = abs(i / SB_NCZ % SB_NCY - k / SB_NCZ % SB_NCY), dz = abs(i % SB_NCZ - k % SB_NCZ);
            if (dx <= 1 && dy <= 1 && dz <= 1)
                SB_ADD(ns + i, ns + k, 2, SB_FRAME_Y, 1.0f);
        }
#undef SB_ADD
    c->nb = nb;
    c->len0 = malloc(sizeof(float) * nb);
    for (i = 0; i < nb; i++)
        c->len0[i] = c->len[i] = sqrtf(v3dist2(x + 3 * c->ba[i], x + 3 * c->bb[i]));
    memcpy(c->pin, x + 3 * ns, sizeof c->pin);

    /* The skin: each body node on its SB_SKIN nearest structure nodes. */
    c->skn = malloc(sizeof(int32_t) * SB_SKIN * nn);
    c->skw = malloc(sizeof(float) * SB_SKIN * nn);
    c->sko = malloc(sizeof(float) * 3 * SB_SKIN * nn);
    for (i = 0; i < nn; i++) {
        float bd[SB_SKIN], sum = 0;
        int32_t *o = c->skn + SB_SKIN * i;
        const float *p = c->rest + 3 * i;
        for (k = 0; k < SB_SKIN; k++) { o[k] = -1; bd[k] = 1e9f; }
        for (j = 0; j < c->sn; j++) {
            float d = v3dist2(p, x + 3 * j);
            int s = SB_SKIN - 1;
            if (d >= bd[s])
                continue;
            while (s > 0 && bd[s - 1] > d) { bd[s] = bd[s - 1]; o[s] = o[s - 1]; s--; }
            bd[s] = d;
            o[s] = j;
        }
        for (k = 0; k < SB_SKIN; k++) {
            float d = sqrtf(bd[k]) + 0.05f;
            c->skw[SB_SKIN * i + k] = 1.0f / (d * d);
            sum += c->skw[SB_SKIN * i + k];
            for (a = 0; a < 3; a++)
                c->sko[3 * (SB_SKIN * i + k) + a] = p[a] - x[3 * o[k] + a];
        }
        for (k = 0; k < SB_SKIN; k++)
            c->skw[SB_SKIN * i + k] /= sum;
    }
    free(cl);
    free(cnt);
    free(knn);
}

static int car_build(sb_car *c, uint32_t car, uint32_t bgv)
{
    bgv_lod top, l;
    int32_t *tag, *map;
    float *pts, *weld;
    int i, k, e, nbody = 0, nn = 0;
    sb_grid g, gn;
    float part_off[16][3];
    int part_ok[16];

    memset(c, 0, sizeof *c);
    c->car = car;
    c->bgv = bgv;
    model_sig(bgv, c->sig);
    for (i = 0; i < 256; i++)
        c->panels_rest[i] = SB_MF(car + SB_CAR_PANELS + 4u * i);
    c->nlod = (int)(SB_M32(bgv + 0x10) & 0xFF) + 1;
    if (c->nlod < 1 || c->nlod > 4)
        return 0;
    if (s_subdiv) {
        car_subdivide(c, bgv);
        model_sig(bgv, c->sig);      /* its buffers are ours now */
    }
    bgv_lod_read(bgv, c->nlod - 1, &top);
    if (top.n == 0 || top.n > 20000 || top.nent == 0)
        return 0;

    /* The full level: which vertices belong to part entries. */
    tag = calloc(top.n, sizeof *tag);
    for (e = 1; e < top.nent; e++)
        bgv_entry_mark(&top, e, tag, e);
    pts = malloc(sizeof(float) * 3 * top.n);
    for (i = 0; i < (int)top.n; i++)
        if (!tag[i]) {
            memcpy(pts + 3 * nbody, SB_PTR(top.vb + 24u * i), 12);
            nbody++;
        }

    /* Weld the body (entry 0 and the attached panels) into nodes. */
    grid_build(&g, pts, nbody, 0.05f);
    weld = malloc(sizeof(float) * 3 * (nbody + 27));
    map = malloc(sizeof(int32_t) * nbody);
    /* A vertex joins the node of an earlier vertex within SB_WELD (the
     * same point split for its uv or normal), or starts one. */
    for (i = 0; i < nbody; i++) {
        int c0[3], c1[3], a, x, y, z, found = -1;
        const float *p = pts + 3 * i;
        for (a = 0; a < 3; a++) {
            c0[a] = (int)floorf((p[a] - SB_WELD - g.lo[a]) / g.cell);
            c1[a] = (int)floorf((p[a] + SB_WELD - g.lo[a]) / g.cell);
            if (c0[a] < 0) c0[a] = 0;
            if (c1[a] >= g.dim[a]) c1[a] = g.dim[a] - 1;
        }
        for (z = c0[2]; z <= c1[2] && found < 0; z++)
            for (y = c0[1]; y <= c1[1] && found < 0; y++)
                for (x = c0[0]; x <= c1[0] && found < 0; x++) {
                    int32_t j;
                    for (j = g.head[(z * g.dim[1] + y) * g.dim[0] + x]; j >= 0; j = g.next[j])
                        if (j < i && v3dist2(pts + 3 * j, p) < SB_WELD * SB_WELD) {
                            found = map[j];
                            break;
                        }
                }
        if (found < 0) {
            memcpy(weld + 3 * nn, p, 12);
            found = nn++;
        }
        map[i] = found;
    }

    /* The body nodes (what the levels' vertices follow) and, under them,
     * the structure. */
    {
        float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
        int a;
        for (i = 0; i < nn; i++)
            for (a = 0; a < 3; a++) {
                if (weld[3 * i + a] < lo[a]) lo[a] = weld[3 * i + a];
                if (weld[3 * i + a] > hi[a]) hi[a] = weld[3 * i + a];
            }
        c->size = sqrtf((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                        (hi[2] - lo[2]) * (hi[2] - lo[2]));
        c->blen = hi[2] - lo[2];
        c->nn = nn;
        c->ncore = 0;
        c->rest = weld;
        c->x = malloc(sizeof(float) * 3 * nn);
        memcpy(c->x, weld, sizeof(float) * 3 * nn);
        c->gone = calloc((size_t)nn, 1);
        car_structure(c, lo, hi);
    }

    /* Each body node's SB_K nearest within SB_BRACE_R: its normal's turn. */
    grid_build(&gn, weld, nn, SB_BRACE_R);
    c->nbr = malloc(sizeof(int32_t) * SB_K * nn);
    for (i = 0; i < nn; i++) {
        int32_t nb_[SB_K];
        int m = grid_knn(&gn, weld, i, SB_BRACE_R, SB_K, nb_), j;
        for (j = 0; j < SB_K; j++)
            c->nbr[SB_K * i + j] = j < m ? nb_[j] : -1;
    }
    c->nmat = malloc(sizeof(float) * 9 * c->nn);

    /* Parts: their offset onto the attached copy, from the full level. */
    for (e = 1; e < top.nent && e < 16; e++) {
        int np = 0;
        float *pp = malloc(sizeof(float) * 3 * top.n);
        for (i = 0; i < (int)top.n; i++)
            if (tag[i] == e) {
                memcpy(pp + 3 * np, SB_PTR(top.vb + 24u * i), 12);
                np++;
            }
        part_ok[e] = np > 0 && part_offset(pp, np, pts, nbody, &g, part_off[e]);
        free(pp);
    }

    /* Every level: each vertex's node (body: where it is; a part: where it
     * sits on the car), and the rest data to put back. */
    for (k = 0; k < c->nlod; k++) {
        sb_lod *d = &c->lod[k];
        int32_t *lt;
        bgv_lod_read(bgv, k, &l);
        d->vb = l.vb;
        d->n = l.n;
        d->p0 = malloc(sizeof(float) * 3 * l.n);
        d->n0 = malloc(sizeof(uint32_t) * l.n);
        d->node = malloc(sizeof(int32_t) * l.n);
        lt = calloc(l.n, sizeof *lt);
        for (e = 1; e < l.nent; e++)
            bgv_entry_mark(&l, e, lt, e);
        for (i = 0; i < (int)l.n; i++) {
            float p[3], d2;
            memcpy(d->p0 + 3 * i, SB_PTR(l.vb + 24u * i), 12);
            d->n0[i] = SB_M32(l.vb + 24u * i + 12);
            memcpy(p, d->p0 + 3 * i, 12);
            d->node[i] = -1;
            if (lt[i]) {
                if (lt[i] >= 16 || !part_ok[lt[i]])
                    continue;
                p[0] += part_off[lt[i]][0];
                p[1] += part_off[lt[i]][1];
                p[2] += part_off[lt[i]][2];
            }
            d->node[i] = grid_nearest(&gn, weld, p, k == c->nlod - 1 ? 0.01f : 0.3f, &d2);
        }
        free(lt);
        /* Its strip records: each entry's, as bgv_entry_mark finds them. */
        for (e = 0; e < l.nent; e++) {
            uint32_t end = e + 1 < l.nent ? l.ent[e + 1] : l.vb, a;
            if (end > l.ent[e] + 0x400)
                end = l.ent[e] + 0x400;
            for (a = l.ent[e]; a + 16 <= end; a += 4) {
                uint32_t idx = SB_M32(a + 8), cnt = SB_M32(a + 12);
                if (SB_M32(a) != 0x3F800000u || SB_M32(a + 4) != 0 || cnt < 3 || cnt >= 0x10000u ||
                    idx < 0x80000000u || idx >= 0x84000000u)
                    continue;
                d->rec = realloc(d->rec, sizeof(uint32_t) * 3 * (d->nrec + 1));
                d->rec[3 * d->nrec] = a;
                d->rec[3 * d->nrec + 1] = idx;
                d->rec[3 * d->nrec + 2] = cnt;
                d->nrec++;
                d->tcap += cnt * 3 + 8;
            }
        }
    }

    grid_free(&g);
    grid_free(&gn);
    free(pts);
    free(tag);
    free(map);
    if (s_log)
        fprintf(stderr, "[SOFTBODY] car 0x%08X bgv 0x%08X: %d body nodes on %d structure nodes "
                "(%d chassis), %d beams, levels %u/%u/%u/%u\n",
                car, bgv, c->nn, c->sn, SB_NC, c->nb, c->lod[0].n, c->lod[1].n, c->lod[2].n, c->lod[3].n);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Simulation                                                           */

/* The car's motion in its own frame, from its world matrix (car + 0x10:
 * three rows, the position at + 0x30) over the last three flips: the
 * frame's acceleration, its turn and the turn's change, the world's up.
 * A jump (a respawn, a teleport) starts the history again. */
static void car_motion(sb_car *c, uint32_t flip, float *acc, float *om, float *al, float *up)
{
    const float *m = (const float *)SB_PTR(c->car + 0x10);
    float now[12], w0[3] = { 0 }, w1[3] = { 0 }, aw[3];
    int a, r;

    for (r = 0; r < 4; r++)
        memcpy(now + 3 * r, m + 4 * r, 12);
    memset(acc, 0, 12); memset(om, 0, 12); memset(al, 0, 12);
    for (r = 0; r < 3; r++) {
        float l = sqrtf(now[3 * r] * now[3 * r] + now[3 * r + 1] * now[3 * r + 1] + now[3 * r + 2] * now[3 * r + 2]);
        up[r] = l > 1e-6f ? now[3 * r + 1] / l : (r == 1);
    }
    if (flip != c->mot_flip + 1 || (c->nmot && v3dist2(now + 9, c->mot[0] + 9) > 3.0f * 3.0f))
        c->nmot = 0;
    /* The road: under the body's lowest point while the car stands on its
     * wheels (a race car driving; a traffic vehicle when first hit). */
    if (now[4] > 0.9f * sqrtf(now[3] * now[3] + now[4] * now[4] + now[5] * now[5]) &&
        (c->traffic ? !c->have_ground : SB_M32(c->car + SB_CAR_WRECK) > 1)) {
        c->ground = now[10] + c->ymin * now[4] - 0.03f;
        c->have_ground = 1;
    }
    c->mot_flip = flip;
    memmove(c->mot[1], c->mot[0], sizeof(float) * 12 * 2);
    memcpy(c->mot[0], now, sizeof now);
    if (c->nmot < 3)
        c->nmot++;
    if (c->nmot < 3)
        return;
    /* The turn: half the sum of each axis crossed with where it went. */
    for (r = 0; r < 3; r++)
        for (a = 0; a < 3; a++) {
            const float *p0 = c->mot[1] + 3 * r, *p1 = c->mot[0] + 3 * r, *q0 = c->mot[2] + 3 * r;
            int b = (a + 1) % 3, d = (a + 2) % 3;
            w0[a] += 0.5f * (p0[b] * p1[d] - p0[d] * p1[b]) / SB_DT;
            w1[a] += 0.5f * (q0[b] * p0[d] - q0[d] * p0[b]) / SB_DT;
        }
    for (a = 0; a < 3; a++)
        aw[a] = (c->mot[0][9 + a] - 2 * c->mot[1][9 + a] + c->mot[2][9 + a]) / (SB_DT * SB_DT);
    /* Into the car's frame (the rows are its axes). */
    for (r = 0; r < 3; r++) {
        const float *ax = now + 3 * r;
        float l2 = ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2] + 1e-12f;
        acc[r] = (aw[0] * ax[0] + aw[1] * ax[1] + aw[2] * ax[2]) / sqrtf(l2);
        om[r] = (w0[0] * ax[0] + w0[1] * ax[1] + w0[2] * ax[2]) / sqrtf(l2);
        al[r] = ((w0[0] - w1[0]) * ax[0] + (w0[1] - w1[1]) * ax[1] + (w0[2] - w1[2]) * ax[2]) / sqrtf(l2) / SB_DT;
    }
    if (acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2] > SB_ACC_MAX * SB_ACC_MAX) {
        memset(acc, 0, 12);
        memset(al, 0, 12);
    }
}

/* Structure nodes with no live brace to the chassis (torn). */
static int car_loose(const sb_car *c, uint8_t *loose)
{
    int i, n = 0;
    memset(loose, 1, (size_t)c->sns);
    memset(loose + c->sns, 0, (size_t)(c->sn - c->sns));
    for (i = 0; i < c->nb; i++)
        if (c->kind[i] == 1)
            loose[c->ba[i]] = 0;
    for (i = 0; i < c->sns; i++)
        n += loose[i];
    return n;
}

/* A node's turn: the rotation part of A = sum (moved) (rest)^T over its
 * live beams, by iterating from last frame's (Mueller et al., "A robust
 * method to extract the rotational part of deformations", 2016). */
static void quat_mat(const float *q, float *R)
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    R[0] = 1 - 2 * (y * y + z * z); R[1] = 2 * (x * y - z * w);     R[2] = 2 * (x * z + y * w);
    R[3] = 2 * (x * y + z * w);     R[4] = 1 - 2 * (x * x + z * z); R[5] = 2 * (y * z - x * w);
    R[6] = 2 * (x * z - y * w);     R[7] = 2 * (y * z + x * w);     R[8] = 1 - 2 * (x * x + y * y);
}

static void car_rotations(sb_car *c)
{
    float *A = calloc((size_t)9 * c->sn, sizeof(float));
    int i, k, it, a, b;
    for (i = 0; i < c->nb; i++) {
        int p = c->ba[i], q = c->bb[i];
        float d[3], r[3];
        if (c->kind[i] == 3)
            continue;
        for (a = 0; a < 3; a++) {
            d[a] = c->sx[3 * q + a] - c->sx[3 * p + a];
            r[a] = c->srest[3 * q + a] - c->srest[3 * p + a];
        }
        for (a = 0; a < 3; a++)
            for (b = 0; b < 3; b++) {
                A[9 * p + 3 * a + b] += d[a] * r[b];
                A[9 * q + 3 * a + b] += d[a] * r[b];
            }
    }
    for (k = 0; k < c->sn; k++) {
        float *q = c->sq + 4 * k, *M = A + 9 * k, R[9];
        for (it = 0; it < 4; it++) {
            float w[3] = { 0 }, den = 1e-9f, ang, s, h, dq[4], nq[4], l;
            quat_mat(q, R);
            for (a = 0; a < 3; a++) {          /* column a of R crossed with column a of A */
                float rc[3] = { R[a], R[3 + a], R[6 + a] }, mc[3] = { M[a], M[3 + a], M[6 + a] };
                w[0] += rc[1] * mc[2] - rc[2] * mc[1];
                w[1] += rc[2] * mc[0] - rc[0] * mc[2];
                w[2] += rc[0] * mc[1] - rc[1] * mc[0];
                den += rc[0] * mc[0] + rc[1] * mc[1] + rc[2] * mc[2];
            }
            for (a = 0; a < 3; a++)
                w[a] /= fabsf(den);
            ang = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            if (ang < 1e-7f)
                break;
            s = sinf(ang / 2) / ang;
            h = cosf(ang / 2);
            dq[0] = w[0] * s; dq[1] = w[1] * s; dq[2] = w[2] * s; dq[3] = h;
            nq[0] = dq[3] * q[0] + dq[0] * q[3] + dq[1] * q[2] - dq[2] * q[1];
            nq[1] = dq[3] * q[1] - dq[0] * q[2] + dq[1] * q[3] + dq[2] * q[0];
            nq[2] = dq[3] * q[2] + dq[0] * q[1] - dq[1] * q[0] + dq[2] * q[3];
            nq[3] = dq[3] * q[3] - dq[0] * q[0] - dq[1] * q[1] - dq[2] * q[2];
            l = sqrtf(nq[0] * nq[0] + nq[1] * nq[1] + nq[2] * nq[2] + nq[3] * nq[3]);
            for (a = 0; a < 4; a++)
                q[a] = nq[a] / l;
        }
    }
    free(A);
}

/* The body nodes from the structure: each the weighted sum of where its
 * structure nodes would put it (their position plus its rest offset,
 * turned with them). One on a lost node is lost with it. */
static void car_skin(sb_car *c)
{
    int i, k, a;
    float *R = malloc(sizeof(float) * 9 * c->sn);
    for (k = 0; k < c->sn; k++)
        quat_mat(c->sq + 4 * k, R + 9 * k);
    for (i = 0; i < c->nn; i++) {
        float p[3] = { 0, 0, 0 };
        c->gone[i] = 0;
        for (k = 0; k < SB_SKIN; k++) {
            int n = c->skn[SB_SKIN * i + k];
            const float *o = c->sko + 3 * (SB_SKIN * i + k), *M = R + 9 * n;
            float w = c->skw[SB_SKIN * i + k];
            if (k == 0 && c->sgone[n])
                c->gone[i] = 1;
            for (a = 0; a < 3; a++)
                p[a] += w * (c->sx[3 * n + a] + M[3 * a] * o[0] + M[3 * a + 1] * o[1] + M[3 * a + 2] * o[2]);
        }
        memcpy(c->x + 3 * i, p, 12);
    }
    free(R);
}

/* A beam tore: sparks there, thrown out from the car (its middle's way
 * out, a little up). */
static void car_fx(sb_car *c, int b)
{
    const float *pa = c->sx + 3 * c->ba[b], *pb = c->sx + 3 * c->bb[b], *m = c->srest + 3 * c->scenter;
    float *f, l;
    int a;
    c->ntears++;
    if (c->nfx >= SB_FX_MAX)
        return;
    f = c->fx[c->nfx++];
    for (a = 0; a < 3; a++) {
        f[a] = (pa[a] + pb[a]) / 2;
        f[3 + a] = f[a] - m[a];
    }
    f[4] += 0.5f;
    l = sqrtf(f[3] * f[3] + f[4] * f[4] + f[5] * f[5]);
    for (a = 3; a < 6; a++)
        f[a] /= l > 1e-6f ? l : 1;
}

/* The title's spark burst (sub_00182920, callee pops 0x1C): the manager, P
 * and two velocities (float4s, 16-aligned), the plane's normal (its own
 * (0, 1, 0)), the count (a float; AL set: seven times as many more) and
 * P's height above the road. From a mark, so every register goes back. */
extern void sub_00182920(void);
extern RECOMP_TLS uint32_t g_ebx, g_ecx, g_edx, g_edi, g_seh_ebp;
extern RECOMP_TLS int g_fp_top;

static void fx_sparks(const float *P, const float *V, float count, float h, int burst)
{
    uint32_t a = g_eax, cx = g_ecx, d = g_edx, bx = g_ebx, si = g_esi, di = g_edi, bp = g_ebp, sp = g_esp;
    uint32_t sb = g_seh_ebp, buf, u;
    int ft = g_fp_top, k;
    RecompXmm x0 = g_xmm0, x1 = g_xmm1, x2 = g_xmm2, x3 = g_xmm3, x4 = g_xmm4, x5 = g_xmm5, x6 = g_xmm6, x7 = g_xmm7;

    buf = (sp - 0x80) & ~15u;
    for (k = 0; k < 3; k++) {
        SB_MF(buf + 4u * k) = P[k];
        SB_MF(buf + 0x10 + 4u * k) = V[k];
    }
    SB_MF(buf + 0xC) = 1;
    SB_MF(buf + 0x1C) = 0;
    g_esp = buf;
    memcpy(&u, &h, 4);
    PUSH32(g_esp, u);
    memcpy(&u, &count, 4);
    PUSH32(g_esp, u);
    PUSH32(g_esp, 0x0040A8C0u);     /* the title's own up: (0, 1, 0, 0) */
    PUSH32(g_esp, buf + 0x10);
    PUSH32(g_esp, buf + 0x10);
    PUSH32(g_esp, buf);
    PUSH32(g_esp, SB_FX_MGR);
    PUSH32(g_esp, 0);               /* the return address its ret takes */
    g_eax = (g_eax & ~0xFFu) | (burst ? 1u : 0u);
    sub_00182920();
    g_eax = a; g_ecx = cx; g_edx = d; g_ebx = bx; g_esi = si; g_edi = di; g_ebp = bp; g_esp = sp;
    g_seh_ebp = sb; g_fp_top = ft;
    g_xmm0 = x0; g_xmm1 = x1; g_xmm2 = x2; g_xmm3 = x3; g_xmm4 = x4; g_xmm5 = x5; g_xmm6 = x6; g_xmm7 = x7;
}

/* The title's crash debris (sub_001834E0, callee pops 0x20: EAX the second
 * velocity; the manager, the position, the first velocity, the plane's
 * normal, the count (0..24, a float), the height, a lighting pair (its own
 * for a car is about 0.34, 0.66) and a flag): bits of metal and gravel. */
extern void sub_001834E0(void);

static void fx_debris(const float *P, const float *V, float count, float h)
{
    uint32_t a = g_eax, cx = g_ecx, d = g_edx, bx = g_ebx, si = g_esi, di = g_edi, bp = g_ebp, sp = g_esp;
    uint32_t sb = g_seh_ebp, buf, u;
    int ft = g_fp_top, k;
    RecompXmm x0 = g_xmm0, x1 = g_xmm1, x2 = g_xmm2, x3 = g_xmm3, x4 = g_xmm4, x5 = g_xmm5, x6 = g_xmm6, x7 = g_xmm7;

    buf = (sp - 0x80) & ~15u;
    for (k = 0; k < 3; k++) {
        SB_MF(buf + 4u * k) = P[k];
        SB_MF(buf + 0x10 + 4u * k) = V[k];
        SB_MF(buf + 0x20 + 4u * k) = V[k];
    }
    SB_MF(buf + 0xC) = 1;
    SB_MF(buf + 0x1C) = 0;
    SB_MF(buf + 0x2C) = 0;
    SB_MF(buf + 0x30) = 0.34f;
    SB_MF(buf + 0x34) = 0.66f;
    g_esp = buf;
    PUSH32(g_esp, 0);               /* the flag, as the title's crashes pass it */
    PUSH32(g_esp, buf + 0x30);
    memcpy(&u, &h, 4);
    PUSH32(g_esp, u);
    memcpy(&u, &count, 4);
    PUSH32(g_esp, u);
    PUSH32(g_esp, 0x0040A8C0u);
    PUSH32(g_esp, buf + 0x10);
    PUSH32(g_esp, buf);
    PUSH32(g_esp, SB_FX_MGR);
    PUSH32(g_esp, 0);
    g_eax = buf + 0x20;
    sub_001834E0();
    g_eax = a; g_ecx = cx; g_edx = d; g_ebx = bx; g_esi = si; g_edi = di; g_ebp = bp; g_esp = sp;
    g_seh_ebp = sb; g_fp_top = ft;
    g_xmm0 = x0; g_xmm1 = x1; g_xmm2 = x2; g_xmm3 = x3; g_xmm4 = x4; g_xmm5 = x5; g_xmm6 = x6; g_xmm7 = x7;
}

/* This flip's tears into the world: sparks at each, and where many beams
 * went at once (a panel ripping) the title's debris as well. */
static void car_fx_flush(sb_car *c)
{
    const float *M = c->mot[0];
    int i, a;
    if (!s_sparks || !c->nmot) {
        c->nfx = c->ntears = 0;
        return;
    }
    for (i = 0; i < c->nfx; i++) {
        const float *f = c->fx[i];
        float P[3], V[3], h;
        for (a = 0; a < 3; a++) {
            P[a] = f[0] * M[a] + f[1] * M[3 + a] + f[2] * M[6 + a] + M[9 + a];
            /* The title scales a spark's speed by 0.25..0.5 (0.375 on
             * average): 2.67 times the car's keeps them with it. */
            V[a] = (f[3] * M[a] + f[4] * M[3 + a] + f[5] * M[6 + a]) * 14.0f +
                   2.67f * (c->nmot > 1 ? (M[9 + a] - c->mot[1][9 + a]) / SB_DT : 0);
        }
        h = c->have_ground ? P[1] - c->ground : 0.3f;
        if (h < 0.05f)
            h = 0.05f;
        /* AL set: seven times as many more, into a bank the title draws
         * only while car + 0x18FA is set (a crash); else only count. */
        fx_sparks(P, V, SB_M8(c->car + 0x18FA) ? 3.0f : 12.0f, h, SB_M8(c->car + 0x18FA) && i == 0);
        if (i == 0 && c->ntears >= 6)
            fx_debris(P, V, c->ntears > 24 ? 24.0f : (float)c->ntears, h);
    }
    if (s_log && c->nfx) {
        static int said;
        if (said++ < 40)
            fprintf(stderr, "[SOFTBODY] car 0x%08X: sparks at %d tears\n", c->car, c->nfx);
    }
    c->nfx = c->ntears = 0;
}

/* One flip: SB_SUB steps of the structure (sb/dyn.py), then the body. */
static void car_step(sb_car *c, const float *acc, const float *om, const float *al, const float *up)
{
    int sub, it, i, q, a, n = c->sn, ns = c->sns;
    const float dt = SB_DT / SB_SUB;
    float *p = malloc(sizeof(float) * 3 * n), *delta = malloc(sizeof(float) * 3 * n);
    float *cnt = malloc(sizeof(float) * n), *lam = malloc(sizeof(float) * c->nb);
    uint8_t *loose = malloc((size_t)n);
    const float *ctr = c->srest + 3 * c->scenter;

    /* The rams: this flip's share of depth and bend. */
    for (q = 0; q < SB_MAX_IMP; q++)
        if (c->imp[q].pushes > 0) {
            c->imp[q].depth += c->imp[q].step;
            if (c->imp[q].depth > c->imp[q].cap)
                c->imp[q].depth = c->imp[q].cap;
            /* The bend moves where the chassis is held, a share a flip, and
             * the chassis with it (else the pins' give would undo it). */
            if (c->imp[q].bend_t > c->imp[q].bend) {
                float db = (c->imp[q].bend_t - c->imp[q].bend) / c->imp[q].pushes;
                for (i = 0; i < SB_NC; i++)
                    for (a = 0; a < 3; a++) {
                        float m = c->imp[q].bend_w[i] * db * c->imp[q].bend_d[a];
                        c->pin[3 * i + a] += m;
                        c->sx[3 * (ns + i) + a] += m;
                    }
                c->imp[q].bend += db;
                c->dented = 1;
            }
        }
    for (sub = 0; sub < SB_SUB; sub++) {
        c->nloose = car_loose(c, loose);
        memset(c->touched, 0, (size_t)n);
        /* What the car's motion puts on a torn node in its frame; the rest
         * keeps a damped share of its speed. */
        for (i = 0; i < n; i++) {
            float *v = c->sv + 3 * i;
            if (c->sw[i] == 0 || c->sgone[i]) {
                memcpy(p + 3 * i, c->sx + 3 * i, 12);
                continue;
            }
            if (loose[i]) {
                float r[3], f[3], wr[3], wwr[3], av[3], ar[3];
                for (a = 0; a < 3; a++)
                    r[a] = c->sx[3 * i + a] - ctr[a];
#define SB_CROSS(o, u, w) do { (o)[0] = (u)[1] * (w)[2] - (u)[2] * (w)[1]; \
        (o)[1] = (u)[2] * (w)[0] - (u)[0] * (w)[2]; (o)[2] = (u)[0] * (w)[1] - (u)[1] * (w)[0]; } while (0)
                SB_CROSS(ar, al, r);
                SB_CROSS(av, om, v);
                SB_CROSS(wr, om, r);
                SB_CROSS(wwr, om, wr);
#undef SB_CROSS
                for (a = 0; a < 3; a++) {
                    f[a] = -acc[a] - ar[a] - 2 * av[a] - wwr[a] - 9.81f * up[a];
                    v[a] += f[a] * dt;
                }
            }
            for (a = 0; a < 3; a++)
                p[3 * i + a] = c->sx[3 * i + a] + v[a] * dt;
        }
        /* A torn node meets the road (its height from the car's matrix: the
         * rows' y and the position's): back up along the world's up, its
         * speed taken by friction. */
        if (c->have_ground && c->nmot)
            for (i = 0; i < ns; i++) {
                const float *M = c->mot[0];
                float *x = p + 3 * i, wy;
                if (!loose[i] || c->sgone[i])
                    continue;
                wy = x[0] * M[1] + x[1] * M[4] + x[2] * M[7] + M[10];
                if (wy >= c->ground)
                    continue;
                for (a = 0; a < 3; a++) {
                    x[a] += up[a] * (c->ground - wy);
                    c->sv[3 * i + a] *= SB_GROUND_F;
                }
            }
        /* The rams: a node near the contact plane or behind it, within r of
         * the axis, goes back along d to the ram's face (rounded: the edge
         * reaches less deep). */
        for (q = 0; q < SB_MAX_IMP; q++) {
            const float *o = c->imp[q].o, *dd = c->imp[q].d, r2 = c->imp[q].r * c->imp[q].r;
            if (c->imp[q].pushes <= 0)
                continue;
            for (i = 0; i < n; i++) {
                float *x = p + 3 * i;
                float rel[3] = { x[0] - o[0], x[1] - o[1], x[2] - o[2] };
                float along = rel[0] * dd[0] + rel[1] * dd[1] + rel[2] * dd[2];
                float lat2 = rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2] - along * along, reach;
                if (c->sw[i] == 0 || along <= -0.1f)
                    continue;
                if (c->imp[q].shaped) {      /* the other car's face: pushed where it has something */
                    const float half = SB_FACE_N * SB_FACE_CELL / 2, *u = c->imp[q].fu, *v = c->imp[q].fv;
                    int iu = (int)floorf((rel[0] * u[0] + rel[1] * u[1] + rel[2] * u[2] + half) / SB_FACE_CELL);
                    int iv = (int)floorf((rel[0] * v[0] + rel[1] * v[1] + rel[2] * v[2] + half) / SB_FACE_CELL);
                    if (iu < 0 || iv < 0 || iu >= SB_FACE_N || iv >= SB_FACE_N)
                        continue;
                    reach = c->imp[q].depth - c->imp[q].face[iu * SB_FACE_N + iv];
                } else {
                    if (lat2 >= r2)
                        continue;
                    reach = c->imp[q].depth * (1.0f - lat2 / r2);
                }
                if (along >= reach)
                    continue;
                for (a = 0; a < 3; a++)
                    x[a] += (reach - along) * dd[a];
                c->touched[i] = 1;
                c->dented = 1;
            }
        }
        memset(lam, 0, sizeof(float) * c->nb);
        for (it = 0; it < SB_SITERS; it++) {
            memset(delta, 0, sizeof(float) * 3 * n);
            memset(cnt, 0, sizeof(float) * n);
            for (i = 0; i < c->nb; i++) {
                int ia = c->ba[i], ib = c->bb[i];
                float *pa = p + 3 * ia, *pb = p + 3 * ib, wa = c->sw[ia], wb = c->sw[ib];
                float d[3] = { pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2] }, l, k;
                if (c->kind[i] == 3 || wa + wb == 0)
                    continue;
                l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-9f;
                k = (l - c->len[i]) / (wa + wb) * c->stiff[i];
                lam[i] += k;
                for (a = 0; a < 3; a++) {
                    delta[3 * ia + a] += d[a] / l * k * wa;
                    delta[3 * ib + a] -= d[a] / l * k * wb;
                }
                cnt[ia] += 1;
                cnt[ib] += 1;
            }
            for (i = 0; i < n; i++)
                if (cnt[i] > 0 && c->sw[i] > 0)
                    for (a = 0; a < 3; a++)
                        p[3 * i + a] += delta[3 * i + a] / cnt[i] * 1.6f;
            /* The chassis held to its pins. */
            for (i = 0; i < SB_NC; i++)
                if (c->sw[ns + i] > 0)
                    for (a = 0; a < 3; a++)
                        p[3 * (ns + i) + a] += (c->pin[3 * i + a] - p[3 * (ns + i) + a]) * SB_PIN_K;
        }
        /* A beam on a torn node breaks by force (what holds a hanging part
         * as the car spins and stops); a ram's crush is kinematic and tears
         * by stretch, below. */
        for (i = 0; i < c->nb; i++) {
            int ia = c->ba[i], ib = c->bb[i];
            if (c->kind[i] < 2 && (loose[ia] || loose[ib]) && !c->touched[ia] && !c->touched[ib] &&
                fabsf(lam[i]) / (dt * dt) > SB_F_BREAK) {
                c->kind[i] = 3;
                c->ntorn++;
                car_fx(c, i);
            }
        }
        c->loose_moving = 0;
        for (i = 0; i < n; i++) {
            float k = loose[i] ? 1 - SB_DAMP : 1 - SB_DAMP_HELD;
            for (a = 0; a < 3; a++) {
                c->sv[3 * i + a] = c->sw[i] == 0 || c->touched[i] || c->sgone[i] ? 0 :
                                   (p[3 * i + a] - c->sx[3 * i + a]) / dt * k;
                c->sx[3 * i + a] = p[3 * i + a];
            }
            if (loose[i] && !c->sgone[i] && v3dist2(c->sv + 3 * i, (const float[3]){ 0, 0, 0 }) > 0.05f * 0.05f)
                c->loose_moving = 1;
        }
    }
    /* Plasticity: past its yield strain a beam's rest length follows; past
     * SB_BREAK of its first length a body beam or brace tears. */
    for (i = 0; i < c->nb; i++) {
        float l, strain, y;
        if (c->kind[i] == 3)
            continue;
        l = sqrtf(v3dist2(c->sx + 3 * c->ba[i], c->sx + 3 * c->bb[i]));
        strain = (l - c->len[i]) / c->len[i];
        y = c->yield[i];
        if (strain > y)
            c->len[i] += (l - c->len[i] * (1 + y)) * 0.8f;
        else if (strain < -y)
            c->len[i] += (l - c->len[i] * (1 - y)) * 0.8f;
        if (c->kind[i] < 2 && l > c->len0[i] * SB_BREAK) {
            c->kind[i] = 3;
            c->ntorn++;
            car_fx(c, i);
        }
    }
    /* The chassis gives where it was pushed far from where it is held. */
    for (i = 0; i < SB_NC; i++) {
        float *x = c->sx + 3 * (ns + i), *pn = c->pin + 3 * i, lim = SB_PIN_Y * c->size;
        float off[3] = { x[0] - pn[0], x[1] - pn[1], x[2] - pn[2] };
        float ol = sqrtf(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
        if (ol > lim)
            for (a = 0; a < 3; a++)
                pn[a] += off[a] * (1 - lim / ol) * 0.8f;
    }
    /* A torn node that flew too far is lost (stage D will make it debris). */
    for (i = 0; i < ns; i++)
        if (loose[i] && !c->sgone[i] && v3dist2(c->sx + 3 * i, c->srest + 3 * i) > SB_GONE * SB_GONE) {
            c->sgone[i] = 1;
            c->ntorn++;
        }
    for (q = 0; q < SB_MAX_IMP; q++)
        if (c->imp[q].pushes > 0)
            c->imp[q].pushes--;
    if (s_log && c->nloose != c->loose_said) {
        int lost = 0;
        for (i = 0; i < ns; i++)
            lost += c->sgone[i];
        c->loose_said = c->nloose;
        fprintf(stderr, "[SOFTBODY] car 0x%08X: %d structure nodes torn from the chassis, %d lost; "
                "motion acc %.0f %.0f %.0f turn %.1f %.1f %.1f\n", c->car, c->nloose, lost,
                acc[0], acc[1], acc[2], om[0], om[1], om[2]);
    }
    car_rotations(c);
    car_skin(c);
    free(p);
    free(delta);
    free(cnt);
    free(lam);
    free(loose);
}

/* Each node's normal matrix F^-T, F from its neighbours by least squares. */
static void car_normal_matrices(sb_car *c)
{
    int i, j, nsurf = c->nn - c->ncore;
    for (i = 0; i < nsurf; i++) {
        float A[9] = { 0 }, B[9] = { 1e-6f, 0, 0, 0, 1e-6f, 0, 0, 0, 1e-6f }, Bi[9], F[9], Fi[9];
        float *N = c->nmat + 9 * i;
        for (j = 0; j < SB_K; j++) {
            int q = c->nbr[SB_K * i + j], r, s;
            float d0[3], d1[3];
            if (q < 0)
                break;
            for (r = 0; r < 3; r++) {
                d0[r] = c->rest[3 * q + r] - c->rest[3 * i + r];
                d1[r] = c->x[3 * q + r] - c->x[3 * i + r];
            }
            for (r = 0; r < 3; r++)
                for (s = 0; s < 3; s++) {
                    A[3 * r + s] += d1[r] * d0[s];
                    B[3 * r + s] += d0[r] * d0[s];
                }
        }
        if (!inv3(B, Bi)) {
            memcpy(N, (float[9]){ 1, 0, 0, 0, 1, 0, 0, 0, 1 }, sizeof(float) * 9);
            continue;
        }
        for (j = 0; j < 9; j++) {
            int r = j / 3, s = j % 3;
            F[j] = A[3 * r] * Bi[s] + A[3 * r + 1] * Bi[3 + s] + A[3 * r + 2] * Bi[6 + s];
        }
        if (!inv3(F, Fi)) {
            memcpy(N, (float[9]){ 1, 0, 0, 0, 1, 0, 0, 0, 1 }, sizeof(float) * 9);
            continue;
        }
        for (j = 0; j < 9; j++)          /* the transpose of the inverse */
            N[j] = Fi[3 * (j % 3) + j / 3];
    }
}

/* Value noise in -1..1 over a lattice of SB_WR_SCALE (a fixed hash): the
 * pattern a sheet buckles in. */
static float wr_hash(int x, int y, int z)
{
    uint32_t n = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (float)(n & 0xFFFF) / 32767.5f - 1;
}

static float wr_noise(const float *p)
{
    float g[3], f[3], out = 0;
    int i[3], a, dx, dy, dz;
    for (a = 0; a < 3; a++) {
        g[a] = p[a] / SB_WR_SCALE;
        i[a] = (int)floorf(g[a]);
        f[a] = g[a] - i[a];
        f[a] = f[a] * f[a] * (3 - 2 * f[a]);
    }
    for (dx = 0; dx < 2; dx++)
        for (dy = 0; dy < 2; dy++)
            for (dz = 0; dz < 2; dz++)
                out += (dx ? f[0] : 1 - f[0]) * (dy ? f[1] : 1 - f[1]) * (dz ? f[2] : 1 - f[2]) *
                       wr_hash(i[0] + dx, i[1] + dy, i[2] + dz);
    return out;
}

/* Wrinkles: where the sheet is squashed (its neighbours nearer than at
 * rest), each body node's vertices go out along their normal by
 * SB_WR_AMP * squash * the noise at its rest place: drawn only, a crumple
 * finer than the structure. */
static void car_wrinkles(const sb_car *c, float *wr)
{
    int i, j;
    for (i = 0; i < c->nn; i++) {
        float lr = 0, ln = 0, sq;
        for (j = 0; j < SB_K; j++) {
            int q = c->nbr[SB_K * i + j];
            if (q < 0)
                break;
            lr += sqrtf(v3dist2(c->rest + 3 * q, c->rest + 3 * i));
            ln += sqrtf(v3dist2(c->x + 3 * q, c->x + 3 * i));
        }
        sq = lr > 0 ? 1 - ln / lr : 0;
        sq = sq < 0 ? 0 : sq > 0.5f ? 0.5f : sq;
        wr[i] = s_wrinkle * SB_WR_AMP * 2 * sq * wr_noise(c->rest + 3 * i);
    }
}

static void car_write(sb_car *c, int rest)
{
    int k;
    uint32_t i;
    float *wr = NULL;
    if (!car_model_same(c))
        return;
    if (!rest) {
        car_normal_matrices(c);
        if (s_wrinkle > 0) {
            wr = malloc(sizeof(float) * c->nn);
            car_wrinkles(c, wr);
        }
    }
    for (k = 0; k < 4; k++) {
        sb_lod *d = &c->lod[k];
        for (i = 0; i < d->n; i++) {
            int nd = d->node[i];
            uint8_t *v = SB_PTR(d->vb + 24u * i);
            float p[3], n[3], m[3];
            if (nd < 0)
                continue;
            if (rest) {
                memcpy(v, d->p0 + 3 * i, 12);
                memcpy(v + 12, &d->n0[i], 4);
                continue;
            }
            p[0] = d->p0[3 * i] + c->x[3 * nd] - c->rest[3 * nd];
            p[1] = d->p0[3 * i + 1] + c->x[3 * nd + 1] - c->rest[3 * nd + 1];
            p[2] = d->p0[3 * i + 2] + c->x[3 * nd + 2] - c->rest[3 * nd + 2];
            memcpy(v, p, 12);
            unpack_normal(d->n0[i], n);
            {
                const float *N = c->nmat + 9 * nd;
                m[0] = N[0] * n[0] + N[1] * n[1] + N[2] * n[2];
                m[1] = N[3] * n[0] + N[4] * n[1] + N[5] * n[2];
                m[2] = N[6] * n[0] + N[7] * n[1] + N[8] * n[2];
            }
            if (wr && wr[nd] != 0) {
                float l = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
                if (l > 1e-6f) {
                    p[0] += m[0] / l * wr[nd];
                    p[1] += m[1] / l * wr[nd];
                    p[2] += m[2] / l * wr[nd];
                    memcpy(v, p, 12);
                }
            }
            {
                uint32_t pk = pack_normal(m);
                memcpy(v + 12, &pk, 4);
            }
        }
    }
    free(wr);
}

/* Tearing: each level's strips drawn without the triangles stretched past
 * SB_TEAR (an edge, by more than SB_TEAR_MIN), from our own index buffers;
 * the title reads a record's indices (+ 0xC) and count (+ 0x10) at every
 * draw (sub_00031AB0 to sub_001D7D10). A run of whole triangles is copied
 * as it is and joined to the last by degenerate ones, its first triangle at
 * the same parity as in the original (the winding). restore: the records
 * as built. */
static int tri_torn(const sb_car *c, const sb_lod *d, const uint16_t *s)
{
    int q;
    if (s[0] == s[1] || s[1] == s[2] || s[0] == s[2])
        return 0;
    for (q = 0; q < 3; q++) {
        int i = s[q], j = s[(q + 1) % 3], ni, nj, a;
        float r = 0, u = 0;
        if (i >= (int)d->n || j >= (int)d->n || (ni = d->node[i]) < 0 || (nj = d->node[j]) < 0)
            continue;
        if (c->gone[ni] || c->gone[nj])
            return 1;
        for (a = 0; a < 3; a++) {
            float e0 = d->p0[3 * i + a] - d->p0[3 * j + a];
            float e1 = e0 + (c->x[3 * ni + a] - c->rest[3 * ni + a]) - (c->x[3 * nj + a] - c->rest[3 * nj + a]);
            r += e0 * e0;
            u += e1 * e1;
        }
        r = sqrtf(r);
        u = sqrtf(u);
        if ((u > r * SB_TEAR && u - r > SB_TEAR_MIN) || (u > r * SB_TEAR2 && u - r > SB_TEAR2_MIN))
            return 1;
    }
    return 0;
}

static void car_tear(sb_car *c, int restore)
{
    int k, r;
    if (!car_model_same(c))
        return;
    c->tearing = 0;
    for (k = 0; k < c->nlod; k++) {
        sb_lod *d = &c->lod[k];
        uint32_t off = 0;
        if (!restore && !d->tbuf && d->tcap) {
            d->tbuf = xbox_ContiguousAllocEx(d->tcap * 2, 0x10, 0x02800000u, 0x03FFFFFFu);
            if (!d->tbuf)
                d->tbuf = xbox_ContiguousAllocEx(d->tcap * 2, 0x10, 0, 0x03FFFFFFu);
        }
        for (r = 0; r < d->nrec; r++) {
            uint32_t a = d->rec[3 * r], idx = d->rec[3 * r + 1], cnt = d->rec[3 * r + 2], n = 0, t;
            const uint16_t *src = (const uint16_t *)SB_PTR(idx);
            uint16_t *out;
            int cut = 0;
            if (restore || !d->tbuf) {
                SB_M32(a + 8) = idx;
                *(uint16_t *)SB_PTR(a + 12) = (uint16_t)cnt;
                continue;
            }
            out = (uint16_t *)SB_PTR(d->tbuf + 2 * off);
            for (t = 0; t + 2 < cnt;) {
                uint32_t t0;
                if (tri_torn(c, d, src + t)) {
                    cut = 1;
                    t++;
                    continue;
                }
                t0 = t;
                while (t + 2 < cnt && !tri_torn(c, d, src + t))
                    t++;
                if (n) {                   /* join: the last index again, then the run's first */
                    out[n] = out[n - 1];
                    n++;
                    out[n++] = src[t0];
                    if ((n & 1) != (t0 & 1))
                        out[n++] = src[t0];
                }
                memcpy(out + n, src + t0, 2 * (t - t0 + 2));
                n += t - t0 + 2;
            }
            if (!cut) {
                SB_M32(a + 8) = idx;
                *(uint16_t *)SB_PTR(a + 12) = (uint16_t)cnt;
                continue;
            }
            if (n < 3) {                   /* all torn: one degenerate triangle */
                out[0] = out[1] = out[2] = src[0];
                n = 3;
            }
            SB_M32(a + 8) = d->tbuf + 2 * off;
            *(uint16_t *)SB_PTR(a + 12) = (uint16_t)n;
            off += cnt * 3 + 8;
            c->tearing = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Hooks                                                                */

static int car_pushing(const sb_car *c)
{
    int q;
    for (q = 0; q < SB_MAX_IMP; q++)
        if (c->imp[q].pushes > 0)
            return 1;
    return 0;
}

/* The title's panel slots as they were when the car was built (whole):
 * none of the sixteen moves while a car just drives, all can on a hit, and
 * a Retry or a repair puts them back exactly. (Only the first eight are
 * identity at rest; the others carry frames of their own.) */
static int car_panels_whole(const sb_car *c)
{
    int i;
    for (i = 0; i < 256; i++)
        if (fabsf(SB_MF(c->car + SB_CAR_PANELS + 4u * i) - c->panels_rest[i]) > 1e-4f)
            return 0;
    return 1;
}

static void car_reset(sb_car *c)
{
    int i;
    car_write(c, 1);
    memcpy(c->x, c->rest, sizeof(float) * 3 * c->nn);
    memset(c->gone, 0, (size_t)c->nn);
    memcpy(c->sx, c->srest, sizeof(float) * 3 * c->sn);
    memset(c->sv, 0, sizeof(float) * 3 * c->sn);
    memset(c->sgone, 0, (size_t)c->sn);
    for (i = 0; i < c->sn; i++) {
        c->sq[4 * i] = c->sq[4 * i + 1] = c->sq[4 * i + 2] = 0;
        c->sq[4 * i + 3] = 1;
    }
    for (i = 0; i < c->nb; i++) {
        c->len[i] = c->len0[i];
        c->kind[i] = c->bb[i] < c->sns ? 0 : c->ba[i] < c->sns ? 1 : 2;    /* torn ones mended */
    }
    memcpy(c->pin, c->srest + 3 * c->sns, sizeof c->pin);
    c->nloose = c->loose_said = 0;
    memset(c->imp, 0, sizeof c->imp);
    if (c->tearing) {
        car_tear(c, 1);
        if (s_log)
            fprintf(stderr, "[SOFTBODY] car 0x%08X: strips put back\n", c->car);
    }
    c->ntorn = c->tear_seen = 0;
    c->settle = 0;
    c->dented = 0;
}

static sb_car *car_find(uint32_t car)
{
    int i;
    for (i = 0; i < SB_MAX_CARS; i++)
        if (s_cars[i].car == car)
            return &s_cars[i];
    return NULL;
}

/* One simulation step and write-back for a car still moving, once a flip. */
static void car_tick(sb_car *c, uint32_t flip)
{
    float acc[3], om[3], al[3], up[3];
    if (flip == c->last_flip)
        return;
    c->last_flip = flip;
    car_motion(c, flip, acc, om, al, up);       /* every flip: its history */
    {
        /* BO3_SOFTBODY_AB=<file> (test aid, for pictures of the same crash
         * with and without the model): while the file exists every car is
         * drawn as loaded (its vertices and strips; the title's own panels
         * as they are) and stands still; when it goes, ours comes back. */
        static const char *ab_path;
        static int ab_init;
        static uint32_t ab_flip;
        if (!ab_init) {
            ab_init = 1;
            ab_path = getenv("BO3_SOFTBODY_AB");
        }
        if (ab_path && *ab_path && flip != ab_flip) {
            FILE *f = fopen(ab_path, "rb");
            ab_flip = flip;
            s_ab = f != NULL;
            if (f)
                fclose(f);
        }
        if (s_ab) {
            if (!c->ab) {
                c->ab = 1;
                car_write(c, 1);
                if (c->tearing)
                    car_tear(c, 1);
            }
            return;
        }
        if (c->ab) {
            c->ab = 0;
            car_write(c, 0);
            if (c->ntorn)
                car_tear(c, 0);
        }
    }
    {
        /* BO3_SOFTBODY_FXTEST=1 (test aid): sparks over player 1's roof
         * every flip, to see the title's emitter answer. */
        static int fxtest = -1;
        if (fxtest < 0)
            fxtest = getenv("BO3_SOFTBODY_FXTEST") ? atoi(getenv("BO3_SOFTBODY_FXTEST")) : 0;
        if (fxtest && c->car == 0x0073A1D0u && c->nmot) {
            const float *M = c->mot[0];
            float P[3], V[3];
            int a;
            for (a = 0; a < 3; a++) {
                P[a] = 1.6f * M[3 + a] + M[9 + a];
                V[a] = 10.0f * M[3 + a] + 2.67f * (c->nmot > 1 ? (M[9 + a] - c->mot[1][9 + a]) / SB_DT : 0);
            }
            if (fxtest == 2) {
                if (flip % 30 == 0)
                    fx_debris(P, V, 24.0f, 1.6f);
            } else
                fx_sparks(P, V, 6.0f, 1.6f, 0);
        }
    }
    /* Stepped while a ram pushes, while it settles, and while something
     * torn hangs on it and the car moves it. */
    if (!car_pushing(c) && c->settle <= 0 &&
        !(c->nloose && (acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2] > 1.0f ||
                        om[0] * om[0] + om[1] * om[1] + om[2] * om[2] > 0.04f || c->loose_moving)))
        return;
    {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        car_step(c, acc, om, al, up);
        car_fx_flush(c);
        if (!car_pushing(c))
            c->settle--;
        car_write(c, 0);
        /* The strips again when beams tore, and now and then while it settles. */
        if (c->ntorn && (c->ntorn != c->tear_seen || c->ticks % 10 == 0)) {
            if (s_log && !c->tear_seen)
                fprintf(stderr, "[SOFTBODY] car 0x%08X flip %u: %d beams torn, strips cut\n", c->car, flip, c->ntorn);
            car_tear(c, 0);
            c->tear_seen = c->ntorn;
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        c->ms_sum += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        c->ticks++;
    }
    if (s_log && c->settle % 30 == 0 && !car_pushing(c)) {
        float mx = 0;
        int i;
        for (i = 0; i < c->nn - c->ncore; i++) {
            float d = v3dist2(c->x + 3 * i, c->rest + 3 * i);
            if (d > mx)
                mx = d;
        }
        fprintf(stderr, "[SOFTBODY] car 0x%08X flip %u settle %d max moved %.3f m, %.2f ms a step (%d)\n",
                c->car, flip, c->settle, sqrtf(mx), c->ticks ? c->ms_sum / c->ticks : 0.0, c->ticks);
    }
}

/* A wheel that stays on in a wreck goes with its crushed arch: the mean
 * displacement of the nodes within 0.6 m of its hub moves it, and how that
 * displacement changes across the arch tilts it (camber from the sideways
 * part against height, toe from it against length). Only for the draw: the
 * title turns the wheel's matrix (car + 0x90 + 0x40 w: three rows, the
 * position at + 0x30) a little every step, so sb_drawn puts its own back
 * once the car is drawn. */
static void wheel_bend(sb_car *c, uint32_t w, const float *hub)
{
    float *m = (float *)SB_PTR(c->car + 0x90 + 0x40 * w), *b = c->wheel_base[w];
    double n = 0, s[3] = { 0 }, sy = 0, sz = 0, syy = 0, szz = 0, sxy = 0, sxz = 0;
    float d[3], cam, toe, cc, sc, ct, st, len;
    int j, a, r, nsurf = c->nn - c->ncore;

    for (j = 0; j < nsurf; j++) {
        const float *p = c->rest + 3 * j, *x = c->x + 3 * j;
        float dy = p[1] - hub[1], dz = p[2] - hub[2], dx = x[0] - p[0];
        if (v3dist2(p, hub) >= 0.6f * 0.6f)
            continue;
        for (a = 0; a < 3; a++)
            s[a] += x[a] - p[a];
        sy += dy; sz += dz; syy += dy * dy; szz += dz * dz;
        sxy += dx * dy; sxz += dx * dz;
        n++;
    }
    if (n < 4)
        return;
    for (a = 0; a < 3; a++)
        d[a] = (float)(s[a] / n) * SB_WHEEL_GAIN_D;
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 0.03f)
        return;
    if (len > SB_WHEEL_SHIFT)
        for (a = 0; a < 3; a++)
            d[a] *= SB_WHEEL_SHIFT / len;
    /* Least-squares slopes of the sideways displacement. */
    {
        double vy = syy / n - (sy / n) * (sy / n), vz = szz / n - (sz / n) * (sz / n);
        double my = sxy / n - (s[0] / n) * (sy / n), mz = sxz / n - (s[0] / n) * (sz / n);
        cam = vy > 1e-4 ? -atanf((float)(my / vy)) * SB_WHEEL_GAIN : 0;   /* the top out by k per metre: turned by -atan k */
        toe = vz > 1e-4 ? atanf((float)(mz / vz)) * SB_WHEEL_GAIN : 0;
    }
    if (cam > SB_WHEEL_TILT) cam = SB_WHEEL_TILT;
    if (cam < -SB_WHEEL_TILT) cam = -SB_WHEEL_TILT;
    if (toe > SB_WHEEL_TILT) toe = SB_WHEEL_TILT;
    if (toe < -SB_WHEEL_TILT) toe = -SB_WHEEL_TILT;
    memcpy(b, m, 64);
    cc = cosf(cam); sc = sinf(cam); ct = cosf(toe); st = sinf(toe);
    /* Each row (an axis in the car's frame) turned about z by the camber,
     * then about y by the toe; the position moved. */
    for (r = 0; r < 3; r++) {
        float x = b[4 * r], y = b[4 * r + 1], z = b[4 * r + 2], x1, y1;
        x1 = x * cc - y * sc;
        y1 = x * sc + y * cc;
        m[4 * r] = x1 * ct + z * st;
        m[4 * r + 1] = y1;
        m[4 * r + 2] = -x1 * st + z * ct;
    }
    for (a = 0; a < 3; a++)
        m[12 + a] = b[12 + a] + d[a];
    if (s_log && fabsf(cam) + fabsf(toe) > c->wheel_said[w] + 0.05f) {    /* each 3 degrees more */
        c->wheel_said[w] = fabsf(cam) + fabsf(toe);
        fprintf(stderr, "[SOFTBODY] car 0x%08X wheel %u bent: camber %.1f toe %.1f deg, moved (%.2f %.2f %.2f)\n",
                c->car, w, cam * 57.3f, toe * 57.3f, d[0], d[1], d[2]);
    }
    c->wheel_bent |= (uint8_t)(1u << w);
}

/* "sb_draw": a race car is about to be drawn (esi). It also moves the
 * traffic vehicles that were hit, which have no hook of their own. */
static void sb_draw(void)
{
    uint32_t car = g_esi, bgv, flip;
    sb_car *c;
    int i;

    if (!g_xbox_mem_offset || car < 0x10000u || car >= 0x04000000u)
        return;
    bgv = SB_M32(car + SB_CAR_BGV);
    if (bgv < 0x80000000u || bgv >= 0x84000000u || SB_M32(bgv) != SB_BGV_MAGIC)
        return;
    c = car_find(car);
    if (c && (c->bgv != bgv || !car_model_same(c))) {   /* another model: start again */
        car_free(c);
        c = NULL;
    }
    if (!c) {
        for (i = 0; i < SB_MAX_CARS && s_cars[i].car; i++)
            ;
        if (i == SB_MAX_CARS)
            return;
        if (!car_build(&s_cars[i], car, bgv)) {
            car_free(&s_cars[i]);
            return;
        }
        c = &s_cars[i];
        if (s_test == 1) {     /* BO3_SOFTBODY_TEST=squash: the car at 60 % height, once */
            int j;
            for (j = 0; j < c->nn - c->ncore; j++)
                c->x[3 * j + 1] = c->rest[3 * j + 1] * 0.6f;
            car_write(c, 0);
            fprintf(stderr, "[SOFTBODY] test: car 0x%08X squashed\n", car);
        } else if (s_test == 2) {  /* =dent: a hit into the rear left corner, solved at once */
            int j, f, hit = -1;
            float best = 1e9f, tgt[3] = { -0.75f, 0.45f, -2.1f }, dir[3] = { 0.42f, 0.0f, 0.91f };
            for (j = 0; j < c->nn - c->ncore; j++) {
                float d = v3dist2(c->x + 3 * j, tgt);
                if (d < best) { best = d; hit = j; }
            }
            c->imp[0].r = 0.5f;
            for (j = 0; j < 3; j++) {
                c->imp[0].d[j] = dir[j];
                c->imp[0].o[j] = c->rest[3 * hit + j];
            }
            c->imp[0].depth = 0;
            c->imp[0].pushes = 8;
            c->imp[0].step = 0.4f / 8;
            c->imp[0].target = c->imp[0].cap = 0.4f;
            for (f = 0; f < 70; f++) {
                static const float z[3] = { 0, 0, 0 }, up[3] = { 0, 1, 0 };
                car_step(c, z, z, z, up);
            }
            car_write(c, 0);
            fprintf(stderr, "[SOFTBODY] test: car 0x%08X dented at node %d\n", car, hit);
        }
    }
    if (s_test)
        return;
    /* An event starting again (a Retry, the next race): the phase goes back
     * to 1 before the GO and the title's panels are whole again. (A Crash
     * goes back to phase 1 for its results as well, with the car still a
     * wreck: the panels tell the two apart.) */
    {
        uint32_t ph = SB_M32(car + SB_CAR_PHASE);
        if (ph == 1 && c->phase > 1 && !c->traffic)
            traffic_release_all();      /* a crash event starting again */
        if (ph == 1 && c->phase > 1 && c->dented && car_panels_whole(c)) {
            car_reset(c);
            if (s_log)
                fprintf(stderr, "[SOFTBODY] car 0x%08X: event starts again: reset\n", car);
        }
        c->phase = ph;
    }
    /* The title repaired the car: so do we. */
    if (!car_panels_whole(c))
        c->game_damaged = 1;
    else if (c->game_damaged) {
        c->game_damaged = 0;
        if (c->dented) {
            car_reset(c);
            if (s_log)
                fprintf(stderr, "[SOFTBODY] car 0x%08X repaired by the title: reset\n", car);
        }
    }
    flip = s_flips ? s_flips() : 0;
    /* Wheels: only in a wreck (the car drives on with all four after the
     * respawn, which repairs it): a wheel whose arch we crushed past
     * SB_WHEEL_GO goes the title's way, its damage at the state's + 0xF78
     * (the attach points' at the model's + 0xBB0, their count at + 0xD). */
    if (s_log && SB_M32(car + SB_CAR_WRECK) != c->wreck_seen) {
        c->wreck_seen = SB_M32(car + SB_CAR_WRECK);
        fprintf(stderr, "[SOFTBODY] car 0x%08X flip %u: state +0x3D0 %u\n", car, flip, c->wreck_seen);
    }
    if (SB_M32(car + SB_CAR_WRECK) > 1)      /* driving (4); a wreck flickers between 1 and 0 */
        c->wheel_gone = 0, memset(c->wheel_said, 0, sizeof c->wheel_said);
    else if (c->dented) {
        uint32_t nw = *SB_PTR(bgv + 0xD), w;
        int nsurf = c->nn - c->ncore;
        if (nw > 6)
            nw = 6;
        for (w = 0; w < nw; w++) {
            const float *hub = (const float *)SB_PTR(bgv + 0xBB0 + 0x40 * w);
            double sum = 0;
            int j, cnt = 0;
            if (c->wheel_gone & (1u << w))
                continue;
            if (!s_ab)
                wheel_bend(c, w, hub);
            for (j = 0; j < nsurf; j++)
                if (v3dist2(c->rest + 3 * j, hub) < 0.6f * 0.6f) {
                    sum += sqrtf(v3dist2(c->x + 3 * j, c->rest + 3 * j));
                    cnt++;
                }
            if (cnt && sum / cnt > SB_WHEEL_GO) {
                c->wheel_gone |= (uint8_t)(1u << w);
                SB_MF(car + 0x90 + 0xF78 + 4 * w) = 1.0f;
                if (s_log)
                    fprintf(stderr, "[SOFTBODY] car 0x%08X flip %u: wheel %u goes (arch moved %.2f m)\n",
                            car, flip, w, sum / cnt);
            }
        }
    }
    if (s_log) {           /* the title's panel states (0 on, 1 swinging, 3 gone), on change */
        uint8_t *ps = SB_PTR(car + 0x90 + 0x4B2);
        if (memcmp(ps, c->part_state, 6)) {
            memcpy(c->part_state, ps, 6);
            fprintf(stderr, "[SOFTBODY] car 0x%08X flip %u panels %u %u %u %u %u %u damage %.3f %.3f %.3f %.3f %.3f %.3f\n",
                    car, flip, ps[0], ps[1], ps[2], ps[3], ps[4], ps[5],
                    SB_MF(car + 0x90 + 0xF90), SB_MF(car + 0x90 + 0xF94), SB_MF(car + 0x90 + 0xF98),
                    SB_MF(car + 0x90 + 0xF9C), SB_MF(car + 0x90 + 0xFA0), SB_MF(car + 0x90 + 0xFA4));
        }
    }
    car_tick(c, flip);
    if (!crash_event())
        traffic_release_all();
    for (i = 0; i < SB_MAX_CARS; i++) {
        sb_car *t = &s_cars[i];
        if (!t->car || !t->traffic)
            continue;
        if (SB_M32(t->car + SB_CAR_BGV) != t->bgv || !car_model_same(t)) {   /* respawned with another model */
            if (s_log)
                fprintf(stderr, "[SOFTBODY] traffic 0x%08X left its copy: freed\n", t->car);
            traffic_free(t);
            continue;
        }
        car_tick(t, flip);
    }
}

/* "sb_impact": the title registers a hit on a car. It comes in pairs, one
 * for each car, the direction turned round: a contact normal. The car is
 * the physics body's owner (body = car + 0x10, its world matrix: rows 0..2
 * the axes, row 3 the position, D3D order). */
/* How far a hit bends the frame: as deep as it is strong past SB_BEND_S0,
 * by the share of it that goes across the car's length (local z). */
static float bend_of(const float *ld, float strength)
{
    float b;
    if (strength <= SB_BEND_S0)
        return 0.0f;
    b = SB_BEND_K * sqrtf(strength - SB_BEND_S0);
    return (b < SB_BEND_MAX ? b : SB_BEND_MAX) * sqrtf(ld[0] * ld[0] + ld[1] * ld[1]);
}

/* "sb_drawn": the car is drawn; the wheel matrices sb_draw bent for it go
 * back to the title's. */
static void sb_drawn(void)
{
    sb_car *c = car_find(g_esi);
    uint32_t w;

    if (!c || !c->wheel_bent)
        return;
    for (w = 0; w < 6; w++)
        if (c->wheel_bent & (1u << w))
            memcpy(SB_PTR(c->car + 0x90 + 0x40 * w), c->wheel_base[w], 64);
    c->wheel_bent = 0;
}

/* A model's shape for the cars it hits: up to SB_FACE_PTS of its body's
 * vertices (the full level, the part entries left out) and a mass from its
 * size (car1, 4.2 m long, as SB_MASS). Kept per model. */
typedef struct {
    uint32_t bgv;
    int n;
    float mass;
    float pts[3 * SB_FACE_PTS];
} sb_shape;
static sb_shape s_shapes[24];
static int s_shape_next;

static const sb_shape *shape_of(uint32_t bgv)
{
    bgv_lod l;
    sb_shape *sh;
    int32_t *tag;
    int i, e, nbody = 0, nl, step;
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f }, vol;

    for (i = 0; i < 24; i++)
        if (s_shapes[i].bgv == bgv && s_shapes[i].n)
            return &s_shapes[i];
    if (bgv < 0x80000000u || bgv >= 0x84000000u || SB_M32(bgv) != SB_BGV_MAGIC)
        return NULL;
    nl = (int)(SB_M32(bgv + 0x10) & 0xFF) + 1;
    if (nl < 1 || nl > 4)
        return NULL;
    bgv_lod_read(bgv, nl - 1, &l);
    if (!l.n || l.n > 20000)
        return NULL;
    tag = calloc(l.n, sizeof *tag);
    for (e = 1; e < l.nent; e++)
        bgv_entry_mark(&l, e, tag, e);
    for (i = 0; i < (int)l.n; i++)
        nbody += !tag[i];
    sh = &s_shapes[s_shape_next];
    s_shape_next = (s_shape_next + 1) % 24;
    sh->bgv = bgv;
    sh->n = 0;
    step = nbody / SB_FACE_PTS + 1;
    for (i = 0, e = 0; i < (int)l.n; i++) {
        const float *p = (const float *)SB_PTR(l.vb + 24u * i);
        int a;
        if (tag[i])
            continue;
        for (a = 0; a < 3; a++) {
            if (p[a] < lo[a]) lo[a] = p[a];
            if (p[a] > hi[a]) hi[a] = p[a];
        }
        if (e++ % step == 0 && sh->n < SB_FACE_PTS) {
            memcpy(sh->pts + 3 * sh->n, p, 12);
            sh->n++;
        }
    }
    free(tag);
    vol = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2]);
    sh->mass = SB_MASS * vol / 17.7f;
    if (sh->mass < 300) sh->mass = 300;
    if (sh->mass > 30000) sh->mass = 30000;
    return sh;
}

/* Ram q of car c shaped by the other car (its physics body ob): the
 * other's surface points into c's frame, then looked at along the ram's
 * d from its origin, the furthest forward in each cell; one cell's
 * dilation fills the gaps of a coarse surface. The depth goes with the
 * masses. */
static void ram_shape(sb_car *c, int q, uint32_t ob)
{
    const sb_shape *sh = shape_of(SB_M32(ob - 0x10 + SB_CAR_BGV)), *me;
    float *face = c->imp[q].face, lead[SB_FACE_N * SB_FACE_N], R[9], T[3], Ro[9], To[3];
    const float *o = c->imp[q].o, *d = c->imp[q].d;
    float *u = c->imp[q].fu, *v = c->imp[q].fv, l, mx = -1e9f, k;
    int i, a, j;
    const float half = SB_FACE_N * SB_FACE_CELL / 2;

    if (!c || q < 0 || !sh || c->imp[q].shaped == 1)
        return;
    me = shape_of(c->traffic ? c->orig_bgv : c->bgv);
    for (i = 0; i < 9; i++) {
        R[i] = SB_MF(c->car + 0x10 + 4u * (4 * (i / 3) + i % 3));
        Ro[i] = SB_MF(ob + 4u * (4 * (i / 3) + i % 3));
    }
    for (i = 0; i < 3; i++) {
        T[i] = SB_MF(c->car + 0x10 + 0x30 + 4u * i);
        To[i] = SB_MF(ob + 0x30 + 4u * i);
    }
    if (fabsf(d[1]) < 0.9f) { u[0] = -d[2]; u[1] = 0; u[2] = d[0]; }      /* d x (0, 1, 0) */
    else { u[0] = 0; u[1] = d[2]; u[2] = -d[1]; }                         /* d x (1, 0, 0) */
    l = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (a = 0; a < 3; a++)
        u[a] /= l;
    v[0] = d[1] * u[2] - d[2] * u[1];
    v[1] = d[2] * u[0] - d[0] * u[2];
    v[2] = d[0] * u[1] - d[1] * u[0];
    for (i = 0; i < SB_FACE_N * SB_FACE_N; i++)
        lead[i] = -1e9f;
    for (j = 0; j < sh->n; j++) {
        const float *p = sh->pts + 3 * j;
        float w[3], lp[3], rel[3], al, pu, pv;
        int iu, iv;
        for (a = 0; a < 3; a++)      /* its frame to the world (rows: its axes) ... */
            w[a] = p[0] * Ro[a] + p[1] * Ro[3 + a] + p[2] * Ro[6 + a] + To[a] - T[a];
        for (a = 0; a < 3; a++)      /* ... to ours */
            lp[a] = R[3 * a] * w[0] + R[3 * a + 1] * w[1] + R[3 * a + 2] * w[2];
        for (a = 0; a < 3; a++)
            rel[a] = lp[a] - o[a];
        al = rel[0] * d[0] + rel[1] * d[1] + rel[2] * d[2];
        pu = rel[0] * u[0] + rel[1] * u[1] + rel[2] * u[2];
        pv = rel[0] * v[0] + rel[1] * v[1] + rel[2] * v[2];
        iu = (int)floorf((pu + half) / SB_FACE_CELL);
        iv = (int)floorf((pv + half) / SB_FACE_CELL);
        if (iu < 0 || iv < 0 || iu >= SB_FACE_N || iv >= SB_FACE_N)
            continue;
        if (al > lead[iu * SB_FACE_N + iv])
            lead[iu * SB_FACE_N + iv] = al;
    }
    for (i = 0; i < SB_FACE_N; i++)
        for (j = 0; j < SB_FACE_N; j++) {
            float b = -1e9f;
            int di, dj;
            for (di = -1; di <= 1; di++)
                for (dj = -1; dj <= 1; dj++) {
                    int ii = i + di, jj = j + dj;
                    if (ii >= 0 && jj >= 0 && ii < SB_FACE_N && jj < SB_FACE_N && lead[ii * SB_FACE_N + jj] > b)
                        b = lead[ii * SB_FACE_N + jj];
                }
            face[i * SB_FACE_N + j] = b;
            if (b > mx)
                mx = b;
        }
    if (mx < -1e8f)
        return;                      /* nothing of it in front of the ram: round */
    for (i = 0; i < SB_FACE_N * SB_FACE_N; i++)
        face[i] = face[i] < -1e8f ? 1e9f : mx - face[i];
    c->imp[q].shaped = 1;
    k = me ? 2 * sh->mass / (me->mass + sh->mass) : 1;
    k = k < SB_MASS_K_MIN ? SB_MASS_K_MIN : k > SB_MASS_K_MAX ? SB_MASS_K_MAX : k;
    c->imp[q].mass_k = k;
    c->imp[q].target *= k;
    if (c->imp[q].pushes > 0)
        c->imp[q].step = (c->imp[q].target - c->imp[q].depth) / c->imp[q].pushes;
    if (s_log)
        fprintf(stderr, "[SOFTBODY] car 0x%08X ram %d shaped by 0x%08X (%d points, %.0f kg against %.0f kg: depth x %.2f)\n",
                c->car, q, ob - 0x10, sh->n, sh->mass, me ? me->mass : 0.0f, k);
}

/* The two cars of a crash each get a hit at the same flip, near the same
 * place, the directions opposite: once both are in, each ram takes the
 * other car's shape. */
static struct {
    uint32_t body, flip;
    float wp[3], wd[3];
    sb_car *c;
    int ram;
} s_hits[SB_HITS];
static int s_hit_next;

/* c NULL: a vehicle we do not deform (its hit only shapes the other's). */
static void hit_pair(sb_car *c, int q, uint32_t body, const float *wp, const float *wd, uint32_t flip)
{
    int i;
    for (i = 0; i < SB_HITS; i++) {
        float dp = wd[0] * s_hits[i].wd[0] + wd[1] * s_hits[i].wd[1] + wd[2] * s_hits[i].wd[2];
        if (!s_hits[i].body || s_hits[i].body == body || flip - s_hits[i].flip > 1 ||
            v3dist2(wp, s_hits[i].wp) > 1.5f * 1.5f || dp > -0.2f)
            continue;
        ram_shape(c, q, s_hits[i].body);
        if (s_hits[i].c && s_hits[i].c->car == s_hits[i].body - 0x10)
            ram_shape(s_hits[i].c, s_hits[i].ram, body);   /* a car of ours: it takes this one's shape */
        break;
    }
    s_hits[s_hit_next].body = body;
    s_hits[s_hit_next].flip = flip;
    memcpy(s_hits[s_hit_next].wp, wp, 12);
    memcpy(s_hits[s_hit_next].wd, wd, 12);
    s_hits[s_hit_next].c = c;
    s_hits[s_hit_next].ram = q;
    s_hit_next = (s_hit_next + 1) % SB_HITS;
}

static void sb_impact(void)
{
    uint32_t ebp = g_ebp, body = SB_M32(ebp + 0x10);
    uint32_t pt = SB_M32(ebp + 0x24), dirp = SB_M32(ebp + 0x28);
    float strength = SB_MF(ebp + 0x2C), wd[3], ld[3], lp[3] = { 0, 0, 0 }, wp[3] = { 0, 0, 0 }, R[9], T[3], depth, r;
    uint32_t flip;
    int i, j, nsurf, hit = -1, have_pt = 0;
    sb_car *c;

    if (!body || !dirp)
        return;
    c = car_find(body - 0x10);
    if (!c && s_traffic && !s_test && SB_MF(ebp + 0x2C) > 0 && crash_event()) {
        /* A vehicle without a draw hook of ours (traffic): its own copy of
         * its model, then built like a race car. Crash events only
         * (BO3_SOFTBODY_TRAFFIC=0 turns traffic off). */
        uint32_t obj = body - 0x10, bgv = SB_M32(obj + SB_CAR_BGV), copy;
        if (bgv >= 0x80000000u && bgv < 0x84000000u && SB_M32(bgv) == SB_BGV_MAGIC) {
            for (i = 0; i < SB_MAX_CARS && s_cars[i].car; i++)
                ;
            if (i < SB_MAX_CARS && (copy = model_copy(bgv)) != 0) {
                SB_M32(obj + SB_CAR_BGV) = copy;
                if (car_build(&s_cars[i], obj, copy)) {
                    c = &s_cars[i];
                    c->traffic = 1;
                    c->orig_bgv = bgv;
                    if (s_log)
                        fprintf(stderr, "[SOFTBODY] traffic 0x%08X model 0x%08X pos (%.2f %.2f %.2f)\n", obj, bgv,
                                SB_MF(body + 0x30), SB_MF(body + 0x34), SB_MF(body + 0x38));
                } else {
                    car_free(&s_cars[i]);
                    SB_M32(obj + SB_CAR_BGV) = bgv;
                    xbox_ContiguousFree(copy);
                }
            }
        }
    }
    for (i = 0; i < 9; i++)
        R[i] = SB_MF(body + 4u * (4 * (i / 3) + i % 3));
    for (i = 0; i < 3; i++)
        T[i] = SB_MF(body + 0x30 + 4u * i);
    for (i = 0; i < 3; i++)
        wd[i] = SB_MF(dirp + 4u * i);
    /* Into the car's frame: d_local[r] = axis r . d. */
    for (i = 0; i < 3; i++)
        ld[i] = R[3 * i] * wd[0] + R[3 * i + 1] * wd[1] + R[3 * i + 2] * wd[2];
    if (pt >= 0x10000u && pt < 0x04000000u) {
        float w[3] = { SB_MF(pt) - T[0], SB_MF(pt + 4) - T[1], SB_MF(pt + 8) - T[2] };
        for (i = 0; i < 3; i++)
            lp[i] = R[3 * i] * w[0] + R[3 * i + 1] * w[1] + R[3 * i + 2] * w[2];
        have_pt = fabsf(lp[0]) < 4 && fabsf(lp[1]) < 4 && fabsf(lp[2]) < 6;
    }
    if (s_log) {
        static int shown;
        if (c && shown++ < 2000)
            fprintf(stderr, "[SOFTBODY] impact car 0x%08X%s strength %g dir local (%.2f %.2f %.2f)"
                            " point local (%.2f %.2f %.2f)%s flip %u\n", body - 0x10, c ? "" : " (not built)",
                    strength, ld[0], ld[1], ld[2], lp[0], lp[1], lp[2], have_pt ? "" : " (none)",
                    s_flips ? s_flips() : 0u);
    }
    flip = s_flips ? s_flips() : 0;
    if (have_pt)
        for (i = 0; i < 3; i++)
            wp[i] = SB_MF(pt + 4u * i);
    if (!c && have_pt && strength > SB_S0)
        hit_pair(NULL, -1, body, wp, wd, flip);
    if (!c || !(strength > 0))
        return;
    {
        float l = sqrtf(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
        if (l < 1e-6f)
            return;
        for (i = 0; i < 3; i++)
            ld[i] /= l;
    }
    /* ld points into the car at the hit (a front hit's runs backwards, the
     * other car's copy the other way): the push goes along it. Where: the
     * node nearest the contact point, or without one the node furthest out
     * against it. */
    nsurf = c->nn - c->ncore;
    {
        float best = -1e9f;
        for (j = 0; j < nsurf; j++) {
            const float *x = c->x + 3 * j;
            float sc = have_pt ? -v3dist2(x, lp) : -(x[0] * ld[0] + x[1] * ld[1] + x[2] * ld[2]);
            if (sc > best) {
                best = sc;
                hit = j;
            }
        }
    }
    if (hit < 0 || strength <= SB_S0)
        return;
    depth = SB_DEPTH_K * sqrtf(strength - SB_S0);
    if (depth > SB_DEPTH_MAX)
        depth = SB_DEPTH_MAX;
    if (ld[1] < SB_ROOF_Y) {         /* the ground, from above: a roll lands on the roof */
        depth = SB_ROOF_K * sqrtf(strength - SB_S0);
        if (depth > SB_ROOF_MAX)
            depth = SB_ROOF_MAX;
    }
    r = SB_RAM_R0 + SB_RAM_R1 * (strength < SB_RAM_RS ? strength / SB_RAM_RS : 1.0f);
    {
        /* A hit near a contact still going on (same place, same way) only
         * raises that ram's target: a scrape or a car held against another
         * hits every frame. A new contact starts from where the surface is
         * now, so crashes add up. */
        int q, use = -1;
        for (q = 0; q < SB_MAX_IMP && use < 0; q++) {
            float *im = c->imp[q].pt;
            float dp = c->imp[q].d[0] * ld[0] + c->imp[q].d[1] * ld[1] + c->imp[q].d[2] * ld[2];
            float g[3] = { im[0] - lp[0], im[1] - lp[1], im[2] - lp[2] };
            if (c->imp[q].target > 0 && flip - c->imp[q].t <= SB_SESSION && dp > 0.5f &&
                g[0] * g[0] + g[1] * g[1] + g[2] * g[2] < SB_SESSION_R * SB_SESSION_R)
                use = q;
        }
        if (use >= 0) {
            float b = bend_of(ld, strength);
            c->imp[use].t = flip;
            memcpy(c->imp[use].pt, lp, 12);
            if (have_pt)
                hit_pair(c, use, body, wp, wd, flip);
            if (c->imp[use].mass_k > 0)
                depth *= c->imp[use].mass_k;
            if (b > c->imp[use].bend_t) {
                c->imp[use].bend_t = b;
                if (c->imp[use].pushes <= 0) {
                    c->imp[use].pushes = SB_FRAMES;
                    c->imp[use].step = 0;    /* the bend only, the dent stays */
                }
            }
            if (depth <= c->imp[use].target)
                return;
            c->imp[use].target = depth;
            if (r > c->imp[use].r)
                c->imp[use].r = r;
            c->imp[use].step = (depth - c->imp[use].depth) / SB_FRAMES;
            c->imp[use].pushes = SB_FRAMES;
        } else {
            /* A free slot (done and over), or the oldest. */
            use = 0;
            for (q = 0; q < SB_MAX_IMP; q++) {
                if (c->imp[q].pushes <= 0 && (c->imp[q].target <= 0 || flip - c->imp[q].t > SB_SESSION)) {
                    use = q;
                    break;
                }
                if (c->imp[q].t < c->imp[use].t)
                    use = q;
            }
            c->imp[use].r = r;
            for (i = 0; i < 3; i++) {
                c->imp[use].d[i] = ld[i];
                c->imp[use].o[i] = c->x[3 * hit + i];
                c->imp[use].pt[i] = lp[i];
            }
            /* The cabin holds: no deeper than SB_CAB short of the frame's middle. */
            {
                const float *m = c->sx + 3 * c->scenter, *o = c->imp[use].o;
                float cap = (m[0] - o[0]) * ld[0] + (m[1] - o[1]) * ld[1] + (m[2] - o[2]) * ld[2] - SB_CAB;
                c->imp[use].cap = cap > 0.05f ? cap : 0.05f;
            }
            /* The bend: like a banana, the frame at the hit pushed in, the
             * ends left behind, all relative to the frame's middle (which
             * the car's rigid body holds). */
            {
                const float *r0 = c->srest + 3 * c->sns, *rc = c->srest + 3 * c->scenter;
                float sig = SB_BEND_SIG * c->blen, hz = c->x[3 * hit + 2];
                float bl = sqrtf(ld[0] * ld[0] + ld[1] * ld[1]), gc;
                if (have_pt)
                    hz = lp[2];
                gc = expf(-(rc[2] - hz) * (rc[2] - hz) / (sig * sig));
                for (i = 0; i < SB_NC; i++)
                    c->imp[use].bend_w[i] = expf(-(r0[3 * i + 2] - hz) * (r0[3 * i + 2] - hz) / (sig * sig)) - gc;
                c->imp[use].bend_d[0] = bl > 1e-6f ? ld[0] / bl : 0;
                c->imp[use].bend_d[1] = bl > 1e-6f ? ld[1] / bl : 0;
                c->imp[use].bend_d[2] = 0;
                c->imp[use].bend = 0;
                c->imp[use].bend_t = bend_of(ld, strength);
            }
            c->imp[use].depth = 0;
            c->imp[use].target = depth;
            c->imp[use].t = flip;
            c->imp[use].pushes = SB_FRAMES;
            c->imp[use].step = depth / SB_FRAMES;
            c->imp[use].shaped = 0;
            c->imp[use].mass_k = 1;
            if (ld[1] < SB_ROOF_Y) {
                /* The ground is flat: the whole face, everywhere at once
                 * (the roof goes down level), and deeper into the cabin. */
                const float *m = c->sx + 3 * c->scenter, *o = c->imp[use].o;
                float cap = (m[0] - o[0]) * ld[0] + (m[1] - o[1]) * ld[1] + (m[2] - o[2]) * ld[2] - SB_ROOF_CAB;
                c->imp[use].cap = cap > 0.05f ? cap : 0.05f;
                c->imp[use].shaped = 2;
                memset(c->imp[use].face, 0, sizeof c->imp[use].face);
                if (fabsf(ld[1]) < 0.9f) { c->imp[use].fu[0] = -ld[2]; c->imp[use].fu[1] = 0; c->imp[use].fu[2] = ld[0]; }
                else { c->imp[use].fu[0] = 0; c->imp[use].fu[1] = ld[2]; c->imp[use].fu[2] = -ld[1]; }
                {
                    float *u = c->imp[use].fu, *v = c->imp[use].fv;
                    float l = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
                    for (i = 0; i < 3; i++)
                        u[i] /= l;
                    v[0] = ld[1] * u[2] - ld[2] * u[1];
                    v[1] = ld[2] * u[0] - ld[0] * u[2];
                    v[2] = ld[0] * u[1] - ld[1] * u[0];
                }
                if (s_log)
                    fprintf(stderr, "[SOFTBODY] car 0x%08X ram %d: the ground from above, depth %.2f, cap %.2f\n",
                            c->car, use, depth, c->imp[use].cap);
            }
            if (have_pt)
                hit_pair(c, use, body, wp, wd, flip);
        }
    }
    c->settle = SB_SETTLE;
    if (s_log)
        fprintf(stderr, "[SOFTBODY] car 0x%08X dent at node %d (%.2f %.2f %.2f) depth %.2f bend %.2f\n",
                c->car, hit, c->x[3 * hit], c->x[3 * hit + 1], c->x[3 * hit + 2], depth, bend_of(ld, strength));
}

/* A level header of a shared model whose buffers point at a copy's while
 * one hit vehicle's draw records them (sb_stream), and what to put back. */
static uint32_t s_swap_hdr, s_swap_vb, s_swap_s1;

static void swap_restore(void)
{
    if (!s_swap_hdr)
        return;
    SB_M32(s_swap_hdr + 19 * 4) = s_swap_vb;
    SB_M32(s_swap_hdr + 22 * 4) = s_swap_s1;
    s_swap_hdr = 0;
}

/* "sb_model": a model is drawn now (sub_000303D0, after its first push:
 * [esp+0xC] the instance, its world matrix first and its model at + 0x40;
 * a race car's instance is car + 0x10). The traffic draw hands its own
 * instances, not the vehicle objects the hits name: one whose position is
 * a hit traffic vehicle's, still on the shared model, draws its copy. */
static void sb_model(void)
{
    uint32_t inst = SB_M32(g_esp + 0xC), model;
    int i;

    swap_restore();                 /* the last hit vehicle's draw is recorded */
    if (!s_traffic || inst < 0x10000u || inst >= 0x04000000u)
        return;
    model = SB_M32(inst + 0x40);
    s_drawing = NULL;
    for (i = 0; i < SB_MAX_CARS; i++) {
        sb_car *t = &s_cars[i];
        uint32_t body = t->car + 0x10;
        float dx, dy, dz;
        if (!t->car || !t->traffic)
            continue;
        if (model == t->bgv) {
            s_drawing = t;
            return;
        }
        if (model != t->orig_bgv)
            continue;
        dx = SB_MF(inst + 0x30) - SB_MF(body + 0x30);
        dy = SB_MF(inst + 0x34) - SB_MF(body + 0x34);
        dz = SB_MF(inst + 0x38) - SB_MF(body + 0x38);
        if (dx * dx + dy * dy + dz * dz < 0.01f * 0.01f) {
            SB_M32(inst + 0x40) = t->bgv;
            s_drawing = t;
            return;
        }
    }
}

/* "sb_stream": the draw sets its vertex streams from a level header
 * (sub_000315C0: stream 0 its + 0x48, stream 1 its + 0x54; eax the header,
 * after `mov ecx, edi` at 0x30531). While a hit traffic vehicle is drawn
 * (sb_model), a header of its shared original becomes the same level's
 * header in its copy, whose buffers are its own. */
static void sb_stream(void)
{
    sb_car *t = s_drawing;
    uint32_t L = g_eax, k;

    if (!t)
        return;
    for (k = 0; k < 4; k++) {
        uint32_t lo = SB_M32(t->orig_bgv + 0x4C + 4u * k), lc = SB_M32(t->bgv + 0x4C + 4u * k);
        if (L != lo && L != lc)
            continue;
        /* The streams this draw records: the copy's buffers, in the header
         * it hands over (the original's too: whichever D3D reads), put
         * back at the next draw. */
        swap_restore();
        s_swap_hdr = lo;
        s_swap_vb = SB_M32(lo + 19 * 4);
        s_swap_s1 = SB_M32(lo + 22 * 4);
        SB_M32(lo + 19 * 4) = SB_M32(lc + 19 * 4);
        SB_M32(lo + 22 * 4) = SB_M32(lc + 22 * 4);
        if (s_log) {
            static int shown;
            if (shown++ < 20000)
                fprintf(stderr, "[SOFTBODY] flip %u streams of 0x%08X: header 0x%08X, vb 0x%08X -> 0x%08X\n",
                        s_flips ? s_flips() : 0, t->car, L, s_swap_vb, SB_M32(lc + 19 * 4));
        }
        return;
    }
}

/* "sb_parts": sub_0012FEE0 (a car's damage state set up, at each start)
 * has just drawn the thresholds of its panels (up to six: bonnet, boot,
 * doors): a panel's damage (+ 0xF90, a quarter of each hit's damage, 0..1)
 * past + 0xFA8 (0.05..0.1) sets it swinging, and past + 0xFC0 (0.085..0.17)
 * it goes. Scaled by BO3_SOFTBODY_PARTS, panels come loose and off sooner:
 * a wreck that keeps its doors looks wrong next to a crushed body.
 * [ebp+8] the physics body (its model at + 0x40, the panel count at the
 * model's + 0xC), [ebp+0xC] the state. */
static void sb_parts(void)
{
    uint32_t body = SB_M32(g_ebp + 8), st = SB_M32(g_ebp + 0xC), model, n, i;
    if (s_parts >= 1.0f || body < 0x10000u || st < 0x10000u)
        return;
    model = SB_M32(body + 0x40);
    if (model < 0x10000u)
        return;
    n = *SB_PTR(model + 0xC);
    if (n > 6)
        n = 6;
    for (i = 0; i < n; i++) {
        SB_MF(st + 0xFA8 + 4 * i) *= s_parts;
        SB_MF(st + 0xFC0 + 4 * i) *= s_parts;
    }
    if (s_log)
        fprintf(stderr, "[SOFTBODY] parts of state 0x%08X (%u panels): loose past %.3f, off past %.3f (first)\n",
                st, n, n ? SB_MF(st + 0xFA8) : 0.0f, n ? SB_MF(st + 0xFC0) : 0.0f);
}

void bo3_softbody_start(uint32_t (*flips)(void))
{
    const char *e = recomp_env(RENV_GFX_SOFTBODY), *b = getenv("BO3_SOFTBODY");
    if (b && *b)                     /* the old switch still works (tests) */
        e = b;
    if (!e || !*e || *e == '0')
        return;
    s_log = getenv("BO3_SOFTBODY_LOG") != NULL;
    {
        const char *t = getenv("BO3_SOFTBODY_TRAFFIC");
        s_traffic = !(t && *t == '0');      /* on unless BO3_SOFTBODY_TRAFFIC=0 */
    }
    {
        const char *t = getenv("BO3_SOFTBODY_TEST");
        s_test = !t ? 0 : !strcmp(t, "dent") ? 2 : 1;
    }
    {
        const char *t = getenv("BO3_SOFTBODY_SUBDIV");
        if (t && *t == '0')
            s_subdiv = 0;
        t = getenv("BO3_SOFTBODY_SPARKS");
        if (t && *t == '0')
            s_sparks = 0;
        t = getenv("BO3_SOFTBODY_WRINKLE");
        if (t && *t)
            s_wrinkle = (float)atof(t);
    }
    {
        const char *t = getenv("BO3_SOFTBODY_PARTS");
        if (t && *t)
            s_parts = (float)atof(t);
    }
    s_flips = flips;
    recomp_patch_on_mark("sb_draw", sb_draw);
    recomp_patch_on_mark("sb_drawn", sb_drawn);
    recomp_patch_on_mark("sb_impact", sb_impact);
    recomp_patch_on_mark("sb_model", sb_model);
    recomp_patch_on_mark("sb_stream", sb_stream);
    recomp_patch_on_mark("sb_parts", sb_parts);
    fprintf(stderr, "[SOFTBODY] on: soft-body damage (graphics.softbody)\n");
}
