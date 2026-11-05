/*
 * upscale_ncnn.cpp - the fast model in the game's process, with ncnn; see
 * upscale_ncnn.h. The tiles, and what goes in and out of the model, are
 * upscale_tile.c's (realesrgan-ncnn-vulkan's own cut); this file only hands
 * them to ncnn.
 */
#include "upscale_ncnn.h"

extern "C" {
#include "upscale_tile.h"
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include "cpu.h"
#include "net.h"
#if NCNN_VULKAN
#include "gpu.h"
#endif

struct bo3_ncnn {
    ncnn::Net net;
    unsigned char *bin;                 /* ncnn reads the weights in place */
    int vulkan;
    bo3_ncnn() : bin(NULL), vulkan(0) {}
    ~bo3_ncnn() { net.clear(); free(bin); }
};

static void say(char *err, size_t en, const char *what)
{
    if (err && en)
        snprintf(err, en, "%s", what);
}

#if NCNN_VULKAN
static int s_gpu_made;                  /* create_gpu_instance asked once per process */
#endif

extern "C" bo3_ncnn *bo3_ncnn_open(const char *param, const uint8_t *bin, size_t bin_len, int vulkan,
                                   const char *vk_lib, int threads, int fp16, char *err, size_t en)
{
    bo3_ncnn *n = new (std::nothrow) bo3_ncnn;
    if (!n) {
        say(err, en, "no memory for the model");
        return NULL;
    }
    n->vulkan = 0;
    if (vulkan) {
#if NCNN_VULKAN
        /* The loader ncnn opens is the library named (the game's, whose
         * vkGetInstanceProcAddr is the loader the game itself opened: the
         * app's Turnip through libadrenotools); ncnn makes an instance and a
         * device of its own on it. */
        if (!s_gpu_made) {
            s_gpu_made = 1;
            if (ncnn::create_gpu_instance(vk_lib) != 0) {
                say(err, en, "ncnn found no Vulkan instance on the game's loader");
                delete n;
                return NULL;
            }
        }
        if (ncnn::get_gpu_count() <= 0) {
            say(err, en, "ncnn found no Vulkan device");
            delete n;
            return NULL;
        }
        n->net.opt.use_vulkan_compute = true;
        /* As realesrgan-ncnn-vulkan: half-precision storage, single-precision
         * arithmetic. */
        n->net.opt.use_fp16_packed = true;
        n->net.opt.use_fp16_storage = true;
        n->net.opt.use_fp16_arithmetic = false;
        n->net.set_vulkan_device(ncnn::get_default_gpu_index());
        n->vulkan = 1;
#else
        (void)vk_lib;
        say(err, en, "this build's ncnn has no Vulkan");
        delete n;
        return NULL;
#endif
    } else {
        n->net.opt.use_vulkan_compute = false;
        n->net.opt.num_threads = threads > 0 ? threads : ncnn::get_physical_big_cpu_count();
        if (n->net.opt.num_threads < 1)
            n->net.opt.num_threads = 1;
        n->net.opt.use_fp16_packed = fp16 >= 1;
        n->net.opt.use_fp16_storage = fp16 >= 1;
        n->net.opt.use_fp16_arithmetic = fp16 >= 2;
    }
    /* Threads that sleep as soon as a layer is done: the game runs beside
     * them. */
    n->net.opt.openmp_blocktime = 0;
    n->net.opt.lightmode = true;
    /* (ncnn is built without exceptions, as its own build makes it.) */
    if (!(n->bin = (unsigned char *)malloc(bin_len ? bin_len : 1))) {
        say(err, en, "no memory for the model");
        delete n;
        return NULL;
    }
    memcpy(n->bin, bin, bin_len);
    if (n->net.load_param_mem(param) != 0) {
        say(err, en, "the model's .param could not be read");
        delete n;
        return NULL;
    }
    if (n->net.load_model(n->bin) == 0) {
        say(err, en, "the model's .bin could not be read");
        delete n;
        return NULL;
    }
    return n;
}

extern "C" int bo3_ncnn_vulkan(const bo3_ncnn *n)
{
    return n && n->vulkan;
}

extern "C" int bo3_ncnn_upscale(bo3_ncnn *n, const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out, int tile,
                                int (*poll)(void *ctx, double fraction), void *ctx, char *err, size_t en)
{
    const int pad = BO3_NCNN_PAD, scale = BO3_NCNN_SCALE;
    int count, i;
    if (tile <= 0)
        tile = BO3_NCNN_TILE;
    count = bo3_tile_count((int)w, (int)h, tile);
    if (count <= 0) {
        say(err, en, "an empty picture");
        return -2;
    }
    for (i = 0; i < count; i++) {
        bo3_tile t;
        float in_max = 0.0f;
        size_t k, plane;
        int iw, ih;
        if (poll && poll(ctx, (double)i / count))
            return -1;
        bo3_tile_get((int)w, (int)h, tile, i, &t);
        iw = t.w + 2 * pad;
        ih = t.h + 2 * pad;
        ncnn::Mat in(iw, ih, 3);
        if (in.empty()) {
            say(err, en, "no memory for a tile");
            return -3;
        }
        bo3_tile_input(rgba, (int)w, (int)h, &t, pad, (float *)in.data, in.cstep);
        plane = (size_t)iw * ih;
        for (k = 0; k < plane; k++) {
            const float *p = (const float *)in.data;
            float v = p[k] > p[in.cstep + k] ? p[k] : p[in.cstep + k];
            v = v > p[2 * in.cstep + k] ? v : p[2 * in.cstep + k];
            if (v > in_max)
                in_max = v;
        }
        ncnn::Mat o;
        {
            ncnn::Extractor ex = n->net.create_extractor();
            if (ex.input("data", in) != 0 || ex.extract("output", o) != 0) {
                say(err, en, n->vulkan ? "the model failed on the GPU (a Vulkan error)" : "the model failed");
                return -2;
            }
        }
        if (o.dims != 3 || o.c != 3 || o.w != iw * scale || o.h != ih * scale || o.elemsize != 4 || o.elempack != 1) {
            char msg[160];
            snprintf(msg, sizeof msg, "the model gave %dx%dx%d (%d bytes, pack %d) for a %dx%d tile", o.w, o.h, o.c,
                     (int)o.elemsize, o.elempack, iw, ih);
            say(err, en, msg);
            return -2;
        }
        if (bo3_tile_blank((const float *)o.data, o.cstep, (size_t)o.w * o.h, in_max)) {
            say(err, en, "the model gave a black tile (a lost graphics device?)");
            return -2;
        }
        bo3_tile_output((const float *)o.data, o.cstep, &t, pad, scale, out, (int)w);
    }
    if (poll)
        poll(ctx, 1.0);
    return 0;
}

extern "C" void bo3_ncnn_close(bo3_ncnn *n)
{
    /* The GPU instance stays for the process (ncnn's own advice: one, made
     * once; a second open would make it again). */
    delete n;
}
