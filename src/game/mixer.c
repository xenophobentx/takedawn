/*
 * mixer.c -- Burnout 3's side of the host mixer (BO-114; the toolkit's
 * src/apu/apu_bus.h does the mixing, docs/mixer.md says what it is).
 *
 * The title plays its sounds through RenderWare Audio: every sample is a
 * named wave of a wave dictionary on the disc (sound/ *.awd, the tracks'
 * sound.awd, the cars' .hwd and .lwd), and it starts its voices from one
 * update tick, so the call stack at a voice's turn-on says nothing about
 * what the sound is. The wave does: at start-up (of the mixer) the user's
 * own disc is read once and each wave's first bytes are kept with the bus
 * its name and file put it in; a voice is then recognised by the bytes of
 * its buffer. No list of the game's data is shipped: the names come from
 * the disc, the rules below only say which names go where.
 *
 * Streams have no wave; who turned the voice on tells them apart (the music
 * manager for EA GAMES TRAX and custom music, its speech path for the DJ,
 * the film player, RenderWare's stream player).
 *
 * Nothing here runs unless the mixer is on (audio.mixer, RECOMP_AUDIO_MIXER).
 */
#include "region.h"
#include "mixer.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dirent.h>
#  include <pthread.h>
#  include <sys/stat.h>
#endif

#include "apu_bus.h"
#include "apu_level.h"

enum {
    BUS_MUSIC, BUS_DJ, BUS_FILM, BUS_STREAMS, BUS_ENGINE, BUS_EXHAUST, BUS_CARS,
    BUS_TRAFFIC, BUS_ROAD, BUS_IMPACTS, BUS_BOOST, BUS_UI, BUS_OTHER, BUS_COUNT
};
static const char *const k_bus[BUS_COUNT] = {
    "Music", "DJ", "Film", "Streams", "Player engine", "Player exhaust", "Other cars",
    "Traffic", "Tyres/road/wind", "Impacts/crash", "Boost", "UI/HUD", "Other",
};
/* A car's wave: the bus depends on how the voice is routed (below). */
#define KIND_CAR_ENGINE  100
#define KIND_CAR_EXHAUST 101

/* ── which wave goes where ─────────────────────────────────────────── */

static int has_prefix(const char *s, const char *const *list)
{
    for (; *list; list++)
        if (!strncmp(s, *list, strlen(*list))) return 1;
    return 0;
}

/* file: the dictionary's path under the game folder, lower case, '/'.
 * name: the wave's name in it, lower case. */
int bo3_mixer_wave_bus(const char *file, const char *name)
{
    static const char *const exhaust[] = { "ex_", "c9_x", "m6x", "mtx", NULL };
    static const char *const boost[] = { "boost", "flash", NULL };
    static const char *const traffic[] = { "buseng", "trkeng", "traff_horn", "truck_horn",
        "staticpass", "airbrake", "bstps", NULL };
    static const char *const road[] = { "cartar", "const", "lrgtrafskd", "smltrafskd", NULL };
    static const char *const hits[] = { "shunt", "slam", NULL };
    static const char *const surface[] = { "tar", "gvl", "snow", "squeak", NULL };
    static const char *const passing[] = { "tram", "train", "monorail", "plane", NULL };
    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;

    if (!strncmp(file, "pveh/", 5))
        return has_prefix(name, exhaust) ? KIND_CAR_EXHAUST : KIND_CAR_ENGINE;
    if (!strncmp(file, "tracks/", 7)) {
        /* A track's own dictionary: its road surfaces, the vehicles that
         * pass by, and the things one can hit. "wood" and "metal" alone are
         * surfaces (bridges); "woodenboxh..", "metalboxht.." are props. */
        if (has_prefix(name, surface) || !strcmp(name, "wood") || !strcmp(name, "metal"))
            return BUS_ROAD;
        if (has_prefix(name, passing))
            return BUS_TRAFFIC;
        return BUS_IMPACTS;
    }
    if (!strncmp(file, "sound/", 6)) {
        if (!strncmp(base, "ident", 5))
            return BUS_DJ;                      /* the radio station's idents */
        if (!strcmp(base, "fe.awd"))
            return BUS_UI;
        if (!strcmp(base, "crashmod.awd") || !strncmp(base, "g_crsh", 6))
            return BUS_IMPACTS;
        if (!strcmp(base, "generic.awd")) {
            if (has_prefix(name, boost)) return BUS_BOOST;
            if (has_prefix(name, traffic)) return BUS_TRAFFIC;
            if (has_prefix(name, road)) return BUS_ROAD;
            if (!strncmp(name, "gear", 4)) return KIND_CAR_ENGINE;
            if (!strcmp(name, "p1horn") || !strcmp(name, "crashescap")) return BUS_IMPACTS;
            return BUS_UI;                      /* select, count */
        }
        /* crash.awd, single.awd, roadrage.awd, elim.awd: the event's HUD
         * sounds, and the two contact sounds. */
        return has_prefix(name, hits) ? BUS_IMPACTS : BUS_UI;
    }
    return BUS_OTHER;
}

/* ── the wave table ────────────────────────────────────────────────── */

typedef struct {
    uint8_t head[16], at_400[16], at_440[16];
    uint32_t bytes;
    uint8_t kind;
} wave_t;

static wave_t *s_waves;
static size_t s_nwaves, s_cap;
static volatile int s_table_state;      /* 0 none, 1 loading, 2 ready */
static char s_game_dir[1024];
static unsigned s_files;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A RenderWare Audio wave dictionary: the data starts at the offset in the
 * header's third word; before it, each wave's entry begins its format block
 * with 60 2E 0F 10 (+0x0C: bytes, +0x40: offset into the data, +0x60: name). */
int bo3_mixer_read_dictionary(const char *path, const char *rel)
{
    FILE *f = fopen(path, "rb");
    uint8_t hdr[16], *dir;
    uint32_t data0;
    long size;
    int added = 0;

    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0x80 || fread(hdr, 1, 16, f) != 16) { fclose(f); return 0; }
    data0 = rd32(hdr + 8);
    if (data0 < 0x70 || data0 > (uint32_t)size || data0 > (8u << 20)) { fclose(f); return 0; }
    dir = (uint8_t *)malloc(data0);
    fseek(f, 0, SEEK_SET);
    if (!dir || fread(dir, 1, data0, f) != data0) { free(dir); fclose(f); return 0; }

    for (uint32_t i = 0; i + 0x64 < data0; i += 4) {
        char name[32];
        uint32_t bytes, off, k;
        wave_t w;
        if (dir[i] != 0x60 || dir[i + 1] != 0x2E || dir[i + 2] != 0x0F || dir[i + 3] != 0x10)
            continue;
        bytes = rd32(dir + i + 0x0C);
        off = rd32(dir + i + 0x40);
        for (k = 0; k < sizeof(name) - 1 && i + 0x60 + k < data0; k++) {
            uint8_t ch = dir[i + 0x60 + k];
            if (ch < 0x20 || ch > 0x7E) break;
            name[k] = (char)tolower(ch);
        }
        name[k] = 0;
        if (!k || dir[i + 0x60 + k] != 0 || (uint64_t)data0 + off + 16 > (uint64_t)size)
            continue;
        memset(&w, 0, sizeof(w));
        w.bytes = bytes;
        w.kind = (uint8_t)bo3_mixer_wave_bus(rel, name);
        fseek(f, (long)(data0 + off), SEEK_SET);
        if (fread(w.head, 1, 16, f) != 16) continue;
        if (bytes >= 0x410 && (uint64_t)data0 + off + 0x410 <= (uint64_t)size) {
            fseek(f, (long)(data0 + off + 0x400), SEEK_SET);
            if (fread(w.at_400, 1, 16, f) != 16) continue;
        }
        if (bytes >= 0x450 && (uint64_t)data0 + off + 0x450 <= (uint64_t)size) {
            fseek(f, (long)(data0 + off + 0x440), SEEK_SET);
            if (fread(w.at_440, 1, 16, f) != 16) continue;
        }
        if (s_nwaves == s_cap) {
            size_t cap = s_cap ? s_cap * 2 : 2048;
            wave_t *nw = (wave_t *)realloc(s_waves, cap * sizeof(wave_t));
            if (!nw) break;
            s_waves = nw;
            s_cap = cap;
        }
        s_waves[s_nwaves++] = w;
        added++;
    }
    free(dir);
    fclose(f);
    return added;
}

static int is_dictionary(const char *name)
{
    size_t n = strlen(name);
    char e[5];
    if (n < 5) return 0;
    for (int k = 0; k < 4; k++) e[k] = (char)tolower((unsigned char)name[n - 4 + k]);
    e[4] = 0;
    return !strcmp(e, ".awd") || !strcmp(e, ".hwd") || !strcmp(e, ".lwd");
}

static void scan_dir(const char *dir, const char *rel, int depth)
{
    char path[1400], sub[400];
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(path, sizeof(path), "%s\\*", dir);
    h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *nm = fd.cFileName;
        int isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        const char *nm = e->d_name;
        struct stat st;
        int isdir;
        snprintf(path, sizeof(path), "%s/%s", dir, nm);
        isdir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
        if (nm[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, nm);
        snprintf(sub, sizeof(sub), "%s%s%s", rel, *rel ? "/" : "", nm);
        for (char *c = sub; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (isdir) {
            if (depth < 4) scan_dir(path, sub, depth + 1);
        } else if (is_dictionary(nm)) {
            if (bo3_mixer_read_dictionary(path, sub)) s_files++;
        }
#if defined(_WIN32)
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    }
    closedir(d);
#endif
}

#if defined(_WIN32)
static DWORD WINAPI table_thread(LPVOID arg)
#else
static void *table_thread(void *arg)
#endif
{
    static const char *const tops[] = { "sound", "Sound", "pveh", "Pveh", "PVEH",
                                        "Tracks", "tracks", "TRACKS", "SOUND", NULL };
    char path[1200], seen[8][16];
    int nseen = 0;
    (void)arg;
    /* The three folders that hold dictionaries, whatever their case on
     * this file system (each once: a case-blind one answers to every
     * spelling). */
    for (int i = 0; tops[i]; i++) {
        char lc[16];
        int dup = 0;
#if !defined(_WIN32)
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", s_game_dir, tops[i]);
        if (stat(path, &st) || !S_ISDIR(st.st_mode)) continue;
#else
        snprintf(path, sizeof(path), "%s/%s", s_game_dir, tops[i]);
#endif
        snprintf(lc, sizeof(lc), "%s", tops[i]);
        for (char *c = lc; *c; c++) *c = (char)tolower((unsigned char)*c);
        for (int k = 0; k < nseen; k++) dup |= !strcmp(seen[k], lc);
        if (dup) continue;
        snprintf(seen[nseen++], sizeof(seen[0]), "%s", lc);
        scan_dir(path, lc, 0);
    }
    fprintf(stderr, "[MIXER] %u waves from %u wave dictionaries of the disc\n",
            (unsigned)s_nwaves, s_files);
    s_table_state = 2;
    apu_bus_reclassify();
    return 0;
}

static void table_start(void)
{
    if (s_table_state) return;
    s_table_state = 1;
#if defined(_WIN32)
    { HANDLE h = CreateThread(NULL, 0, table_thread, NULL, 0, NULL); if (h) CloseHandle(h); }
#else
    { pthread_t t; if (!pthread_create(&t, NULL, table_thread, NULL)) pthread_detach(t); }
#endif
}

/* For the unit test: the table from a folder, on this thread. */
void bo3_mixer_load_table(const char *game_dir)
{
    snprintf(s_game_dir, sizeof(s_game_dir), "%s", game_dir);
    free(s_waves);
    s_waves = NULL;
    s_nwaves = s_cap = 0;
    s_files = 0;
    s_table_state = 1;
    table_thread(NULL);
}

static int all_zero(const uint8_t *p)
{
    for (int i = 0; i < 16; i++) if (p[i]) return 0;
    return 1;
}

/* ── the classifier ────────────────────────────────────────────────── */

/* The title's code that turns a stream voice on (return addresses of the
 * NTSC-U build, docs/mixer.md). */
#define RA_MUSIC   BO3_VA(0x00152FB2u, 0x00153252u)          /* the music manager: EA GAMES TRAX */
#define RA_CUSTOM  BO3_VA(0x00155AE3u, 0x00155D83u)          /* its custom-soundtrack player (sub_00155900): the
                                         * user's own music (BO-108) starts its stream here */
/* The music manager's code: a stream started from anywhere in it that is
 * not the DJ's path is music (a call site this table does not name yet). */
#define MUSIC_LO   BO3_VA(0x00152E00u, 0x001530A0u)
#define MUSIC_HI   BO3_VA(0x00156000u, 0x001562A0u)
#define RA_DJ      BO3_VA(0x001546BAu, 0x0015495Au)          /* its speech path: the DJ */
#define RA_FILM_A  BO3_VA(0x0001507Du, 0x0001513Du)          /* the film player */
#define RA_FILM_B  BO3_VA(0x0008731Bu, 0x0008756Bu)
#define RA_RWS     BO3_VA(0x0020BD49u, 0x0020C059u)          /* RenderWare's stream player */

int bo3_mixer_classify(const apu_bus_voice *v)
{
    int two_d = 0, music = 0, dj = 0, film = 0, rws = 0;

    for (int i = 0; i < v->nstack; i++) {
        music |= v->stack[i] == RA_MUSIC || v->stack[i] == RA_CUSTOM
              || (v->stack[i] >= MUSIC_LO && v->stack[i] < MUSIC_HI);
        dj |= v->stack[i] == RA_DJ;
        film |= v->stack[i] == RA_FILM_A || v->stack[i] == RA_FILM_B;
        rws |= v->stack[i] == RA_RWS;
    }
    if (v->stream) {
        if (dj) return BUS_DJ;
        if (film) return BUS_FILM;
        if (music) return BUS_MUSIC;
        /* A stream nobody is known to have started: the mixer was turned
         * on while it played. Music is by far the likeliest. */
        return v->nstack ? BUS_OTHER : BUS_MUSIC;
    }
    /* The player's engine and exhaust as they are heard: the title's two
     * return voices, playing mixbins 17 and 18 back after the GP's
     * distortion and placing them on the speakers (gp_image.c). */
    if (v->gp_return == 1 + 17) return BUS_ENGINE;
    if (v->gp_return == 1 + 18) return BUS_EXHAUST;
    /* The title's "2D" routing: the first sends on the speaker bins and
     * one on mixbin 17 or 18. The player's own car sounds use it. */
    for (int b = 0; b < 8; b++)
        two_d |= v->bins[b] == 17 || v->bins[b] == 18;
    two_d = two_d && v->bins[0] == 0;

    if (s_table_state == 0)
        table_start();
    if (s_table_state == 2) {
        for (size_t i = 0; i < s_nwaves; i++) {
            const wave_t *w = &s_waves[i];
            int proof = 0;
            if (v->head) {
                if (memcmp(v->head, w->head, 16)) continue;
                proof += !all_zero(w->head);
            }
            if (v->at_400 && w->bytes >= 0x410) {
                if (memcmp(v->at_400, w->at_400, 16)) continue;
                proof += !all_zero(w->at_400);
            }
            if (v->at_440 && w->bytes >= 0x450) {
                if (memcmp(v->at_440, w->at_440, 16)) continue;
                proof += !all_zero(w->at_440);
            }
            if (!proof) continue;               /* silence matches anything */
            /* The player's engine and exhaust layers go to mixbins 17 and
             * 18 and to no speaker from there: such a voice is only the
             * distortion's input and stays as the title mixed it; the
             * return voices above are its bus. */
            if (w->kind == KIND_CAR_ENGINE || w->kind == KIND_CAR_EXHAUST)
                return two_d ? APU_BUS_BYPASS : BUS_CARS;
            return w->kind;
        }
    }
    /* No wave: RenderWare's streams play through buffers (the crash
     * sweeteners, ambience, the menu's bed). */
    if (rws && v->stereo) return BUS_STREAMS;
    return BUS_OTHER;
}

/* The custom-music leveller (the toolkit's apu_level.h, BO-132): the user's
 * own songs, started from the custom-soundtrack player. The title's EA
 * GAMES TRAX streams and every effect are not. */
int bo3_mixer_is_custom(const apu_bus_voice *v)
{
    for (int i = 0; i < v->nstack; i++)
        if (v->stack[i] == RA_CUSTOM)
            return 1;
    return 0;
}

/* ── presets ───────────────────────────────────────────────────────── */

/* Starting points from the study of the title's mix (docs/mixer.md has the
 * reasoning and the measurements). Each is applied over Factory. */
static const struct { const char *name, *about, *toml; } k_presets[] = {
    { "Factory", "The game's own mix, untouched: the output is the same as with the mixer off.", "" },
    { "Music front",
      "Music as a front stage: its left channel on front left, its right on front right, nothing in the centre, the rears or the LFE (pan mode: x 0, y 1, width 1). The game plays it on all four corners, the rears 1.6 dB under the fronts, and a full-range copy on the LFE.",
      "[bus.music]\nroute = \"pan\"\npan_x = 0\npan_y = 1\npan_width = 1\npan_divergence = 0\n" },
    { "Engine centred",
      "The player's car in pan mode instead of the game's own placing (engine and exhaust both on FL and FR, with LFE and a little centre and rear). Engine: front centre (x 0, y 1), width 0.25: the centre speaker at -1.1 dB, FL and FR at -9.6 dB. Exhaust: a little behind the listener (x 0, y -0.3), width 0.5: SL and SR at -4.7 dB, FL and FR at -9.4 dB, C at -10 dB. Both also send 80 Hz and below to the LFE at -9 dB.",
      "[bus.player_engine]\nroute = \"pan\"\npan_x = 0\npan_y = 1\npan_width = 0.25\npan_divergence = 0\n"
      "lfe_send_db = -9\nlfe_crossover_hz = 80\n"
      "[bus.player_exhaust]\nroute = \"pan\"\npan_x = 0\npan_y = -0.3\npan_width = 0.5\npan_divergence = 0\n"
      "lfe_send_db = -9\nlfe_crossover_hz = 80\n" },
    { "Crash weight",
      "Impacts get 80 Hz and below on the LFE at -6 dB, the LFE feed is low-passed at 120 Hz as the console's encoder did, music goes down 4 dB while impacts sound, and a peak limiter at -1 dBFS replaces the hard clamp (it adds 1.33 ms).",
      "[master]\nlimiter = \"peak\"\nceiling_db = -1\nlfe_lowpass_hz = 120\n"
      "[bus.impacts_crash]\nlfe_send_db = -6\nlfe_crossover_hz = 80\n"
      "[duck]\nenabled = true\nsource = \"impacts_crash\"\ntarget = \"music\"\n"
      "threshold_db = -30\ndepth_db = 4\nattack_ms = 20\nrelease_ms = 400\n" },
    { "Takedawn recommended 5.1 mix",
      "Tuned by ear on a 5.1 system: music 4 dB up with its own LFE at +4 dB, its rears 10 dB down and its field narrowed to the front; the menu and video streams placed slightly in front (pan mode, y 0.6); other cars' rears 5 dB down; impacts 5 dB down; the LFE speaker trimmed up 5 dB. Everything else as the game mixes it.",
      "[speaker.LFE]\ntrim_db = 5\n"
      "[bus.music]\ngain_db = 4\nlfe_db = 4\nrear_db = -10\nfield_width = 0\npan_y = 0.8\n"
      "[bus.streams]\nroute = \"pan\"\npan_x = 0\npan_y = 0.6\npan_width = 1\npan_divergence = 0\n"
      "[bus.other_cars]\nrear_db = -5\n"
      "[bus.impacts_crash]\ngain_db = -5\n" },
};

int bo3_mixer_preset_count(void) { return (int)(sizeof(k_presets) / sizeof(k_presets[0])); }
const char *bo3_mixer_preset_name(int i) { return k_presets[i].name; }
const char *bo3_mixer_preset_about(int i) { return k_presets[i].about; }
const char *bo3_mixer_preset_toml(int i) { return k_presets[i].toml; }

/* ── start ─────────────────────────────────────────────────────────── */

void bo3_mixer_init(const char *game_dir, const char *settings_path, const char *save_dir)
{
    char path[1100];
    const char *e = getenv("RECOMP_AUDIO_MIXER_FILE");

    snprintf(s_game_dir, sizeof(s_game_dir), "%s", game_dir);
    apu_bus_set_names(k_bus, BUS_COUNT);
    apu_bus_set_classifier(bo3_mixer_classify);
    apu_level_set_classifier(bo3_mixer_is_custom);
    apu_bus_set_duck_default(BUS_IMPACTS, BUS_MUSIC);
    for (int i = 0; i < bo3_mixer_preset_count(); i++)
        apu_bus_add_preset(k_presets[i].name, k_presets[i].about, k_presets[i].toml);

    /* mixer.toml beside settings.toml (else in the save folder). */
    if (e && *e) {
        snprintf(path, sizeof(path), "%s", e);
    } else if (settings_path && *settings_path) {
        const char *sl = strrchr(settings_path, '/');
#if defined(_WIN32)
        const char *bs = strrchr(settings_path, '\\');
        if (bs > sl) sl = bs;
#endif
        if (sl) snprintf(path, sizeof(path), "%.*s/mixer.toml", (int)(sl - settings_path), settings_path);
        else snprintf(path, sizeof(path), "mixer.toml");
    } else {
        snprintf(path, sizeof(path), "%s/mixer.toml", save_dir ? save_dir : ".");
    }
    /* Named now; read (and written as a neutral file to edit, when there is
     * none) only once the mixer is on. */
    apu_bus_set_file(path);
    if (apu_bus_enabled()) {
        table_start();
        fprintf(stderr, "[MIXER] on, preset file %s\n", path);
    }
}
