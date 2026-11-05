/*
 * upscale_ncnn.h - the model run in the game's own process (BO-326): ncnn
 * (Tencent, BSD-3-Clause) linked in, for Android, where the app cannot start
 * the desktop packages' realesrgan-ncnn-vulkan. The same model as the
 * desktop's fast quality (realesr-general-x4v3 at denoise 0.3), the same
 * tiles (upscale_tile.h), on the CPU (NEON) or on Vulkan.
 */
#ifndef BO3_UPSCALE_NCNN_H
#define BO3_UPSCALE_NCNN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bo3_ncnn bo3_ncnn;

/* The model from memory: its .param text (NUL-terminated) and .bin (copied).
 * vulkan 1: on the GPU, through the Vulkan loader of vk_lib (a library that
 * exports vkGetInstanceProcAddr: the game's own, so ncnn reaches the driver
 * the game opened); 0: the CPU with `threads` threads (0: ncnn's choice, the
 * big cores). fp16 (the CPU): 0 single precision, 1 half-precision storage
 * (as the desktop's GPU runs it), 2 half-precision arithmetic too where the
 * CPU has it (ARMv8.2: faster, further from the desktop's pictures). NULL
 * with a reason in err. */
bo3_ncnn *bo3_ncnn_open(const char *param, const uint8_t *bin, size_t bin_len, int vulkan,
                        const char *vk_lib, int threads, int fp16, char *err, size_t en);

/* 1 when it runs on the GPU. */
int bo3_ncnn_vulkan(const bo3_ncnn *n);

/* One picture: rgba (w x h, RGBA8; alpha is not read) upscaled 4x into out
 * (4w x 4h RGBA8, alpha 255), in tiles of `tile` pixels (realesrgan's 200
 * when 0). poll(ctx, fraction) is called before every tile with how much of
 * the picture is done; non-zero from it stops. 0 done, -1 stopped by poll,
 * -2 the model failed (a Vulkan error, or a black result: on the GPU the
 * caller goes on with the CPU), -3 no memory. */
int bo3_ncnn_upscale(bo3_ncnn *n, const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out, int tile,
                     int (*poll)(void *ctx, double fraction), void *ctx, char *err, size_t en);

void bo3_ncnn_close(bo3_ncnn *n);

#define BO3_NCNN_SCALE    4
#define BO3_NCNN_PAD      10        /* realesrgan-ncnn-vulkan's prepadding for its models/ */
#define BO3_NCNN_TILE     200       /* its tile on a GPU with more than 1.9 GB of heap */

#ifdef __cplusplus
}
#endif

#endif
