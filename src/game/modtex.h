/*
 * modtex.h - texture packs of the mod folders (BO-312, docs/mod-folders.md,
 * Texture packs): the textures/ folder of each folder and mod that is on,
 * handed to the toolkit's nv2a_texpack in mod order.
 */
#ifndef BO3_MODTEX_H
#define BO3_MODTEX_H

#define BO3_MODTEX_PACK_FILE_MAX 4096

/* A pack.toml's two keys. alpha: 0 auto, 1 original, 2 png. compress: 0 auto,
 * 1 off. 1 when text is usable (unknown keys are ignored); 0 with a reason in
 * why: the pack then uses the defaults. text may be empty. */
int bo3_modtex_parse(const char *text, int *alpha, int *compress, char *why, unsigned why_n);

/* After bo3_modfs_init: reads every pack.toml and hands the folders to the
 * toolkit (nv2a_texpack_set). Does nothing without a textures/ folder. */
void bo3_modtex_init(void);

/* A texture-only mod turned on or off while the game runs (the menu pictures
 * made on this computer, upscale.h): the packs of this start without that
 * mod's, and with its textures/ last when on, handed to the toolkit again.
 * The mod's folder is the one this start found for id, else fallback_dir (a
 * mod made after the start). Textures already on the GPU go back to the
 * title's and are looked up again (nv2a_texpack_set's generation). Returns
 * the keys now indexed, -1 when there is no folder. */
int bo3_modtex_live(const char *id, const char *fallback_dir, int on);

#endif
