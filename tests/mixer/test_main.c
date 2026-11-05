/* The title's side of the host mixer (src/game/mixer.c, BO-114). */
#include "mixer.h"
#include "region.h"          /* BO3_VA: the return addresses of the build's disc */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  define MKDIR(p) mkdir(p, 0755)
#endif

/* The buses, in mixer.c's order. */
enum { MUSIC, DJ, FILM, STREAMS, ENGINE, EXHAUST, CARS, TRAFFIC, ROAD, IMPACTS, BOOST, UI, OTHER };

static int s_fail;
#define CHECK(c, ...) do { if (!(c)) { s_fail++; fprintf(stderr, "FAIL line %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* A wave's bytes: a pattern of its seed, so every wave differs. A seed of
 * 0 is silence at the start (the first 0x400 bytes), as many real ones. */
#define WAVE_BYTES 0x500
static void wave_data(unsigned seed, unsigned char *out)
{
    unsigned x = seed * 2654435761u + 1;
    for (int i = 0; i < WAVE_BYTES; i++) {
        x = x * 1664525u + 1013904223u;
        out[i] = (unsigned char)(x >> 24);
    }
    if (seed >= 1000) memset(out, 0, 0x400);
}

/* A wave dictionary as RenderWare Audio lays it out, as far as mixer.c
 * reads it: the data offset in the header, an entry per wave. */
static void make_dictionary(const char *path, const char *const *names, const unsigned *seeds, int n)
{
    static unsigned char dir[0x1000], data[WAVE_BYTES];
    FILE *f = fopen(path, "wb");
    memset(dir, 0, sizeof(dir));
    put32(dir + 8, sizeof(dir));
    for (int i = 0; i < n; i++) {
        unsigned char *e = dir + 0x80 + i * 0x90;
        e[0] = 0x60; e[1] = 0x2E; e[2] = 0x0F; e[3] = 0x10;
        put32(e + 4, 22050);
        put32(e + 0x0C, WAVE_BYTES);
        put32(e + 0x40, (unsigned)i * WAVE_BYTES);
        snprintf((char *)e + 0x60, 0x20, "%s", names[i]);
    }
    fwrite(dir, 1, sizeof(dir), f);
    for (int i = 0; i < n; i++) {
        wave_data(seeds[i], data);
        fwrite(data, 1, sizeof(data), f);
    }
    fclose(f);
}

static const int k_2d[8] = { 0, 1, 4, 5, 2, 17, 3, 6 };
static const int k_2dx[8] = { 0, 1, 4, 5, 2, 18, 3, 6 };
static const int k_pos[8] = { 19, 20, 16, 24, 14, 15, 11, 0 };
static const int k_spk[8] = { 0, 1, 4, 5, 2, 3, 6, 7 };

static int classify(unsigned seed, const int *bins, int page_split)
{
    static unsigned char data[WAVE_BYTES];
    apu_bus_voice v;
    memset(&v, 0, sizeof(v));
    wave_data(seed, data);
    for (int b = 0; b < 8; b++) v.bins[b] = (unsigned char)bins[b];
    v.bytes = WAVE_BYTES;
    v.head = data;
    /* The VP gives the 16 bytes at +0x400 or, when that range crosses a
     * page, the ones at +0x440. */
    if (page_split) v.at_440 = data + 0x440; else v.at_400 = data + 0x400;
    return bo3_mixer_classify(&v);
}

static int stream(unsigned ra, int is_stream)
{
    unsigned stack[4] = { 0x0001FFFF, 0x001CECA1, ra, 0x00016C17 };
    apu_bus_voice v;
    memset(&v, 0, sizeof(v));
    v.stream = is_stream;
    v.stereo = 1;
    for (int b = 0; b < 8; b++) v.bins[b] = (unsigned char)k_spk[b];
    v.stack = stack;
    v.nstack = ra ? 4 : 0;
    return bo3_mixer_classify(&v);
}

int main(void)
{
    char root[512], path[700];
    const char *tmp = getenv("TMPDIR");

    /* 1. The rules: file and name to bus (the names are the disc's own). */
    CHECK(bo3_mixer_wave_bus("sound/generic.awd", "boostloop") == BOOST, "boostloop");
    CHECK(bo3_mixer_wave_bus("sound/generic.awd", "traff_horn14") == TRAFFIC, "traff_horn14");
    CHECK(bo3_mixer_wave_bus("sound/generic.awd", "cartar03") == ROAD, "cartar03");
    CHECK(bo3_mixer_wave_bus("sound/generic.awd", "const01") == ROAD, "const01");
    CHECK(bo3_mixer_wave_bus("sound/generic.awd", "select") == UI, "select");
    CHECK(bo3_mixer_wave_bus("sound/fe.awd", "menuin") == UI, "menuin");
    CHECK(bo3_mixer_wave_bus("sound/crashmod.awd", "impactfata16") == IMPACTS, "impactfata16");
    CHECK(bo3_mixer_wave_bus("sound/g_crsh24.awd", "glasswinds12") == IMPACTS, "glasswinds12");
    CHECK(bo3_mixer_wave_bus("sound/single.awd", "slam______12") == IMPACTS, "slam");
    CHECK(bo3_mixer_wave_bus("sound/single.awd", "tdown") == UI, "tdown");
    CHECK(bo3_mixer_wave_bus("sound/ident3.awd", "ident") == DJ, "ident");
    CHECK(bo3_mixer_wave_bus("tracks/us/c1_v1/sound.awd", "tar") == ROAD, "tar");
    CHECK(bo3_mixer_wave_bus("tracks/us/c1_v1/sound.awd", "wood") == ROAD, "wood");
    CHECK(bo3_mixer_wave_bus("tracks/us/c1_v1/sound.awd", "woodenboxh12") == IMPACTS, "woodenboxh12");
    CHECK(bo3_mixer_wave_bus("tracks/as/c2_v1/sound.awd", "tram") == TRAFFIC, "tram");
    CHECK(bo3_mixer_wave_bus("somewhere/else.awd", "thing") == OTHER, "an unknown file");

    /* 2. A disc made here: three folders, their case as on the real one. */
    snprintf(root, sizeof(root), "%s/bo3_mixer_test_%ld", tmp && *tmp ? tmp : ".", (long)getpid());
    MKDIR(root);
    snprintf(path, sizeof(path), "%s/sound", root); MKDIR(path);
    snprintf(path, sizeof(path), "%s/pveh", root); MKDIR(path);
    snprintf(path, sizeof(path), "%s/pveh/COMP", root); MKDIR(path);
    snprintf(path, sizeof(path), "%s/Tracks", root); MKDIR(path);
    snprintf(path, sizeof(path), "%s/Tracks/US", root); MKDIR(path);
    snprintf(path, sizeof(path), "%s/Tracks/US/C1_V1", root); MKDIR(path);
    {
        static const char *const n1[] = { "boostin", "traff_horn14", "Select", "quietstart" };
        static const unsigned s1[] = { 1, 2, 3, 1000 };
        static const char *const n2[] = { "eng_2873", "ex_2873", "gear______11" };
        static const unsigned s2[] = { 10, 11, 12 };
        static const char *const n3[] = { "tar", "woodenboxh12" };
        static const unsigned s3[] = { 20, 21 };
        static const char *const n4[] = { "impactfata16", "quiet2" };
        static const unsigned s4[] = { 30, 1001 };
        snprintf(path, sizeof(path), "%s/sound/Generic.awd", root); make_dictionary(path, n1, s1, 4);
        snprintf(path, sizeof(path), "%s/pveh/COMP/Car1.hwd", root); make_dictionary(path, n2, s2, 3);
        snprintf(path, sizeof(path), "%s/Tracks/US/C1_V1/SOUND.AWD", root); make_dictionary(path, n3, s3, 2);
        snprintf(path, sizeof(path), "%s/sound/crashmod.awd", root); make_dictionary(path, n4, s4, 2);
        snprintf(path, sizeof(path), "%s/sound/notes.txt", root);
        { FILE *f = fopen(path, "wb"); fputs("not a dictionary", f); fclose(f); }
    }
    bo3_mixer_load_table(root);

    /* 3. Voices: the wave by its bytes, the car by its routing. */
    CHECK(classify(1, k_pos, 0) == BOOST, "boostin by its bytes");
    CHECK(classify(2, k_pos, 1) == TRAFFIC, "traff_horn14, second chunk at +0x440");
    CHECK(classify(3, k_2d, 0) == UI, "Select (a name with a capital)");
    CHECK(classify(10, k_2d, 0) == APU_BUS_BYPASS, "the engine layer on mixbin 17: the distortion's input, no bus");
    CHECK(classify(11, k_2dx, 0) == APU_BUS_BYPASS, "the exhaust layer on mixbin 18: likewise");
    {
        /* The return voices: the GP's export of bins 17 and 18 played
         * back. Their buffers hold live audio, never a wave. */
        apu_bus_voice v;
        memset(&v, 0, sizeof(v));
        for (int b = 0; b < 8; b++) v.bins[b] = (unsigned char)k_pos[b];
        v.gp_return = 1 + 17;
        CHECK(bo3_mixer_classify(&v) == ENGINE, "the return voice of mixbin 17: the player's engine");
        v.gp_return = 1 + 18;
        CHECK(bo3_mixer_classify(&v) == EXHAUST, "the return voice of mixbin 18: the player's exhaust");
    }
    CHECK(classify(10, k_pos, 0) == CARS && classify(11, k_pos, 0) == CARS, "the same waves, positioned: another car");
    CHECK(classify(12, k_pos, 0) == CARS, "gear change");
    CHECK(classify(20, k_pos, 0) == ROAD, "tar from a track's dictionary");
    CHECK(classify(21, k_pos, 0) == IMPACTS, "a prop");
    CHECK(classify(30, k_pos, 0) == IMPACTS, "impactfata16");
    CHECK(classify(999, k_pos, 0) == OTHER, "a wave that is in no dictionary");
    /* Two waves that start with the same silence: the bytes further in
     * tell them apart. */
    CHECK(classify(1000, k_pos, 0) == UI, "quietstart (generic.awd) by its second chunk");
    CHECK(classify(1001, k_pos, 0) == IMPACTS, "quiet2 (crashmod.awd) by its second chunk");
    {
        /* Nothing but silence to go by: not guessed. */
        static unsigned char zero[16];
        apu_bus_voice v;
        memset(&v, 0, sizeof(v));
        v.head = zero;
        v.bytes = 64;
        for (int b = 0; b < 8; b++) v.bins[b] = (unsigned char)k_pos[b];
        CHECK(bo3_mixer_classify(&v) == OTHER, "a silent head alone must not match");
    }

    /* 4. Streams: who turned the voice on. */
    CHECK(stream(BO3_VA(0x00152FB2, 0x00153252, 0x001532F2), 1) == MUSIC, "the music manager's stream (EA GAMES TRAX, custom music)");
    CHECK(stream(BO3_VA(0x00155AE3, 0x00155D83, 0x00155E23), 1) == MUSIC, "the custom-soundtrack player's stream (the user's own music, BO-108)");
    CHECK(stream(BO3_VA(0x00153F03, 0x001541A3, 0x00154243), 1) == MUSIC, "a stream from elsewhere in the music manager");
    CHECK(stream(BO3_VA(0x001546BA, 0x0015495A, 0x001549FA), 1) == DJ, "its speech path");
    CHECK(stream(BO3_VA(0x0001507D, 0x0001513D, 0x0001513D), 1) == FILM, "the film player");
    CHECK(stream(BO3_VA(0x0020BD49, 0x0020C059, 0x0020C0F9), 0) == STREAMS, "a RenderWare stream's buffer voice");
    CHECK(stream(0x00012345, 1) == OTHER, "a stream from unknown code");
    CHECK(stream(0, 1) == MUSIC, "a stream with no turn-on record");
    CHECK(stream(0x00012345, 0) == OTHER, "an unknown buffer");

    /* 4b. The leveller's voices (BO-132): the custom-soundtrack player's
     * stream, and no other music, speech, film or effect. */
    {
        static const unsigned ras[] = { BO3_VA(0x00155AE3, 0x00155D83, 0x00155E23), BO3_VA(0x00152FB2, 0x00153252, 0x001532F2), BO3_VA(0x00153F03, 0x001541A3, 0x00154243), BO3_VA(0x001546BA, 0x0015495A, 0x001549FA), BO3_VA(0x0001507D, 0x0001513D, 0x0001513D), 0x00012345, 0 };
        for (unsigned k = 0; k < sizeof ras / sizeof ras[0]; k++) {
            unsigned stack[4] = { 0x0001FFFF, 0x001CECA1, ras[k], 0x00016C17 };
            apu_bus_voice v;
            memset(&v, 0, sizeof(v));
            v.stream = 1;
            v.stack = stack;
            v.nstack = ras[k] ? 4 : 0;
            CHECK(bo3_mixer_is_custom(&v) == (ras[k] == BO3_VA(0x00155AE3, 0x00155D83, 0x00155E23)), "the leveller's voice for return address %08X", ras[k]);
        }
    }

    /* 5. The presets parse against the buses and only Factory is neutral. */
    {
        static const char *const names[13] = {
            "Music", "DJ", "Film", "Streams", "Player engine", "Player exhaust", "Other cars",
            "Traffic", "Tyres/road/wind", "Impacts/crash", "Boost", "UI/HUD", "Other" };
        apu_bus_set_names(names, 13);
        for (int i = 0; i < bo3_mixer_preset_count(); i++) {
            apu_bus_config c;
            char err[160];
            apu_bus_config_neutral(&c);
            CHECK(apu_bus_config_parse(bo3_mixer_preset_toml(i), &c, err, sizeof(err)) == 0 && !*err,
                  "preset %s: %s", bo3_mixer_preset_name(i), err);
            CHECK(apu_bus_config_is_neutral(&c) == (i == 0), "preset %s neutrality", bo3_mixer_preset_name(i));
        }
    }

    {
        /* Tidy up. */
        static const char *const files[] = { "sound/Generic.awd", "sound/crashmod.awd", "sound/notes.txt",
            "pveh/COMP/Car1.hwd", "Tracks/US/C1_V1/SOUND.AWD" };
        static const char *const dirs[] = { "Tracks/US/C1_V1", "Tracks/US", "Tracks", "pveh/COMP", "pveh", "sound", "" };
        for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
            snprintf(path, sizeof(path), "%s/%s", root, files[i]);
            remove(path);
        }
        for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
            snprintf(path, sizeof(path), "%s/%s", root, dirs[i]);
            rmdir(path);
        }
    }
    if (s_fail) { fprintf(stderr, "bo3_mixer: %d failed\n", s_fail); return 1; }
    puts("bo3_mixer: rules, wave table, voices, streams and presets ok");
    return 0;
}
