/*
 * bo3_upscale_ncnn - the in-process upscaler (upscale_ncnn.cpp, BO-326)
 * against the desktop's realesrgan-ncnn-vulkan; see CMakeLists.txt.
 *
 *   make <Data dir> <work>   a few pictures of each kind from the disc, decoded
 *                            and filled under alpha as the upscale step does,
 *                            into <work>/in (the desktop program's input too)
 *   run <models> <work>      the in-process model on <work>/in into <work>/ncnn,
 *                            with the time per picture (the CPU; --vulkan <loader>
 *                            the GPU; --fp16 0|1|2 as upscale_ncnn.h)
 *   diff <a> <b>             every PNG of a against b's: the largest and the
 *                            mean difference per channel, the share of
 *                            channels more than 2 and 8 apart, PSNR
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include "upscale_art.h"
#include "upscale_ncnn.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* upscale_art.c's key function needs the toolkit's; the files here are named
 * by the texture's name instead. */
int nv2a_texpack_key(const uint8_t *guest, uint32_t color, uint32_t w, uint32_t h, uint32_t pitch,
                     const uint8_t *pal, uint32_t pal_len, uint64_t *hash, uint64_t *palh)
{
    (void)guest; (void)color; (void)w; (void)h; (void)pitch; (void)pal; (void)pal_len;
    *hash = 0;
    *palh = 0;
    return 0;
}

void nv2a_texpack_key_name(uint64_t hash, uint64_t palh, char *out, size_t n)
{
    (void)hash; (void)palh;
    snprintf(out, n, "x.png");
}

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b = NULL;
    long size;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
            && (b = (uint8_t *)malloc((size_t)size + 1)) != NULL) {
        if (fread(b, 1, (size_t)size, f) != (size_t)size) {
            free(b);
            b = NULL;
        } else {
            b[size] = 0;
            *n = (size_t)size;
        }
    }
    fclose(f);
    return b;
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ── make ─────────────────────────────────────────────────────────────── */

static int s_per_group[BO3_UPS_GROUPS];

static void take(const char *src, const bo3_txd_tex *t, const char *dir, int per)
{
    int g = bo3_upscale_group(src, t);
    char p[1200];
    uint8_t *rgba;
    if (g == BO3_UPS_SKIP || s_per_group[g] >= per)
        return;
    if (!(rgba = bo3_txd_decode(t)))
        return;
    bo3_upscale_flatten(rgba, t->w, t->h);
    snprintf(p, sizeof p, "%s/%s-%s.png", dir, src, t->name);
    if (stbi_write_png(p, (int)t->w, (int)t->h, 4, rgba, (int)t->w * 4)) {
        s_per_group[g]++;
        printf("%-10s %-24s %4ux%-4u %s\n", bo3_upscale_group_name(g), t->name, t->w, t->h, p);
    }
    free(rgba);
}

static int cmd_make(const char *data, const char *work, int per)
{
    static bo3_txd_tex tex[512];
    static const uint8_t *dicts[256];
    static size_t lens[256];
    char p[1200], in[1100];
    size_t n = 0;
    uint8_t *ls, *fe;
    int nd, i, k;
    snprintf(in, sizeof in, "%s/in", work);
    mkdir(work, 0755);
    mkdir(in, 0755);
    snprintf(p, sizeof p, "%s/LoadScrn.bin", data);
    if (!(ls = slurp(p, &n)))
        return fprintf(stderr, "%s cannot be read\n", p), 1;
    nd = bo3_loadscrn_dicts(ls, n, dicts, lens, 256);
    for (i = 0; i < nd; i += 13) {      /* a few, spread over the file */
        int c = bo3_txd_list(dicts[i], lens[i], tex, 512, NULL);
        for (k = 0; k < c; k++)
            take("LoadScrn", &tex[k], in, per);
    }
    snprintf(p, sizeof p, "%s/Frontend.txd", data);
    if (!(fe = slurp(p, &n)))
        return fprintf(stderr, "%s cannot be read\n", p), 1;
    nd = bo3_txd_list(fe, n, tex, 512, NULL);
    for (k = 0; k < nd; k++)
        take("Frontend", &tex[k], in, per);
    free(ls);
    free(fe);
    return 0;
}

/* ── run ──────────────────────────────────────────────────────────────── */

static int list_png(const char *dir, char names[][256], int max)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    if (!d)
        return 0;
    while ((e = readdir(d)) != NULL && n < max) {
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcmp(e->d_name + l - 4, ".png"))
            snprintf(names[n++], 256, "%s", e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, 256, (int (*)(const void *, const void *))strcmp);
    return n;
}

static int cmd_run(const char *models, const char *work, int fp16, int tile, int threads, const char *vk)
{
    static char names[1024][256];
    char p[1200], q[1200], err[256] = "";
    size_t pn = 0, bn = 0;
    char *param;
    uint8_t *bin;
    bo3_ncnn *net;
    int n, i;
    double total = 0, mp = 0;
    snprintf(p, sizeof p, "%s/realesr-general-dn030-x4v3.param", models);
    snprintf(q, sizeof q, "%s/realesr-general-dn030-x4v3.bin", models);
    param = (char *)slurp(p, &pn);
    bin = slurp(q, &bn);
    if (!param || !bin)
        return fprintf(stderr, "no model in %s\n", models), 1;
    if (!(net = bo3_ncnn_open(param, bin, bn, vk != NULL, vk && *vk ? vk : NULL, threads, fp16, err, sizeof err)))
        return fprintf(stderr, "open: %s\n", err), 1;
    snprintf(p, sizeof p, "%s/in", work);
    n = list_png(p, names, 1024);
    snprintf(q, sizeof q, "%s/ncnn", work);
    mkdir(q, 0755);
    for (i = 0; i < n; i++) {
        int w, h, c, rc;
        uint8_t *px, *out;
        double t0;
        snprintf(p, sizeof p, "%s/in/%s", work, names[i]);
        if (!(px = stbi_load(p, &w, &h, &c, 4)))
            continue;
        out = (uint8_t *)malloc((size_t)w * h * 16 * 4);
        t0 = now();
        rc = bo3_ncnn_upscale(net, px, (uint32_t)w, (uint32_t)h, out, tile, NULL, NULL, err, sizeof err);
        t0 = now() - t0;
        if (rc == 0) {
            snprintf(q, sizeof q, "%s/ncnn/%s", work, names[i]);
            stbi_write_png(q, w * 4, h * 4, 4, out, w * 16);
            total += t0;
            mp += (double)w * h / 1e6;
            printf("%-34s %4dx%-4d %6.2f s\n", names[i], w, h, t0);
        } else {
            printf("%-34s failed %d: %s\n", names[i], rc, err);
        }
        free(out);
        stbi_image_free(px);
    }
    printf("total %.1f s for %.2f MP of source (%.1f s per MP), %s, fp16 %d, tile %d, threads %d\n", total, mp,
           mp > 0 ? total / mp : 0, bo3_ncnn_vulkan(net) ? "Vulkan" : "CPU", fp16, tile, threads);
    bo3_ncnn_close(net);
    free(param);
    free(bin);
    return 0;
}

/* ── diff ─────────────────────────────────────────────────────────────── */

static int cmd_diff(const char *a, const char *b)
{
    static char names[1024][256];
    int n = list_png(a, names, 1024), i, worst = 0;
    double all_sum = 0, all_n = 0, all_over2 = 0, all_over8 = 0, all_sq = 0;
    for (i = 0; i < n; i++) {
        char p[1200], q[1200];
        int w, h, c, w2, h2, c2, mx = 0;
        size_t k, cnt, over2 = 0, over8 = 0;
        double sum = 0, sq = 0;
        uint8_t *x, *y;
        snprintf(p, sizeof p, "%s/%s", a, names[i]);
        snprintf(q, sizeof q, "%s/%s", b, names[i]);
        x = stbi_load(p, &w, &h, &c, 3);
        y = stbi_load(q, &w2, &h2, &c2, 3);
        if (!x || !y || w != w2 || h != h2) {
            printf("%-34s missing or of another size\n", names[i]);
            stbi_image_free(x);
            stbi_image_free(y);
            continue;
        }
        cnt = (size_t)w * h * 3;
        for (k = 0; k < cnt; k++) {
            int d = abs((int)x[k] - (int)y[k]);
            if (d > mx)
                mx = d;
            sum += d;
            sq += (double)d * d;
            over2 += d > 2;
            over8 += d > 8;
        }
        printf("%-34s %4dx%-4d max %3d  mean %.3f  >2 %6.3f%%  >8 %6.3f%%  PSNR %.1f dB\n", names[i], w, h, mx,
               sum / cnt, 100.0 * over2 / cnt, 100.0 * over8 / cnt,
               sq > 0 ? 10 * log10(255.0 * 255.0 / (sq / cnt)) : 99.0);
        if (mx > worst)
            worst = mx;
        all_sum += sum;
        all_sq += sq;
        all_n += cnt;
        all_over2 += over2;
        all_over8 += over8;
        stbi_image_free(x);
        stbi_image_free(y);
    }
    if (all_n > 0)
        printf("all: max %d  mean %.3f  >2 %.3f%%  >8 %.3f%%  PSNR %.1f dB\n", worst, all_sum / all_n,
               100.0 * all_over2 / all_n, 100.0 * all_over8 / all_n,
               all_sq > 0 ? 10 * log10(255.0 * 255.0 / (all_sq / all_n)) : 99.0);
    return 0;
}

int main(int argc, char **argv)
{
    int i, fp16 = 0, tile = 0, threads = 0;
    const char *vk = NULL;
    for (i = 4; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--fp16"))
            fp16 = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--tile"))
            tile = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--threads"))
            threads = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--vulkan"))
            vk = argv[i + 1];           /* the loader to open ("" for ncnn's own search) */
    }
    if (argc >= 4 && !strcmp(argv[1], "make"))
        return cmd_make(argv[2], argv[3], argc > 4 ? atoi(argv[4]) : 2);
    if (argc >= 4 && !strcmp(argv[1], "run"))
        return cmd_run(argv[2], argv[3], fp16, tile, threads, vk);
    if (argc >= 4 && !strcmp(argv[1], "diff"))
        return cmd_diff(argv[2], argv[3]);
    fprintf(stderr, "usage: %s make <Data> <work> [per kind] | run <models> <work> [--fp16 0|1] [--tile n]"
                    " [--threads n] [--vulkan <loader>] | diff <a> <b>\n", argv[0]);
    return 2;
}
