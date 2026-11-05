/*
 * soundtrack.c - the title's side of custom music (soundtrack.h, BO-108).
 *
 * The title's music player (sub_00155900, once a frame) plays a user
 * soundtrack's song like this:
 *
 *   sub_00155D60    asks its file thread to open song n of the soundtrack;
 *                   the thread (sub_001B3B40) calls XGetSoundtrackSongInfo
 *                   (sub_001D032C) and XOpenSoundtrackSong (sub_001D0554)
 *   sub_00155DB0    makes the XDK's WMA decoder for the handle:
 *                   sub_0030211F({0, handle, 0, 0x10000}, &decoder)
 *   sub_00155900    decoder->DoWork(), GetStatus(), then up to four
 *                   Process(NULL, packet) a frame, 0x2000 bytes each, into
 *                   its ring of 16 buffers, each handed to the DirectSound
 *                   stream it made in sub_00155720: PCM, 44.1 kHz, 16 bit,
 *                   stereo. A packet of 0 bytes is the end of the song
 *   Release()       when the song ends or is skipped
 *
 * So the decoder is an XMediaObject the title only pulls PCM from. For a
 * song of the user's folder the object made here has the same shape (a
 * vtable of ten stdcall methods) and Process is answered by the toolkit's
 * xst_stream; everything after it (the stream, its volume and mix bins,
 * pause, skip, shuffle) is the title's own.
 */
#include "region.h"
#include "soundtrack.h"
#include "soundtrack_start.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "platform/recomp_env.h"
#include "soundtrack/xbox_soundtrack.h"
#ifdef XBOXRECOMP_HAVE_OVERLAY
#include "xbox_settings.h"
#endif

/* The title's heap, as its own decoder uses it: sub_001D2879(size, tag)
 * and sub_001D2919(block, tag), both stdcall. */
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);
extern void BO3_SUB(sub_001D2919, sub_001D2C39, sub_001D2CD9)(void);
#define WMA_HEAP_TAG 0x6482A000u

#define S_OK           0x00000000u
#define E_NOTIMPL      0x80004001u
#define E_OUTOFMEMORY  0x8007000Eu
#define XMO_STATUSF_ACCEPT_OUTPUT_DATA 2u

/* The guest object: a vtable pointer, a mark, the host slot, the vtable. */
#define OBJ_SIZE    0x40u
#define OBJ_MARK    0x3154534Fu           /* "OST1" */
#define OBJ_VTABLE  0x10u

#define SLOTS 8
static struct { int used; xst_stream *stream; } s_slot[SLOTS];
static volatile int s_on;

/* ── the title's music manager ─────────────────────────────────────── */

/*
 * The title counts the console's soundtracks once, when its music manager
 * is set up (sub_00152E00 -> sub_00155720, during boot), and keeps the
 * number; everything else it reads from the database each time it needs it
 * (the EA GAMES TRAX screen at every step, the player at every song). So a
 * folder chosen while the game runs is in the title's menu at its next visit
 * once that number is right:
 *
 *   manager + 0x0C0   soundtracks counted (the player's +0xB8)
 *   manager + 0x558   sources: that + 1, EA GAMES TRAX being source 0;
 *                     0 until the manager is set up
 *   manager + 0x538   1 while the source in use is EA GAMES TRAX
 *   manager + 0x568   the source chosen (0: EA GAMES TRAX, n: soundtrack n)
 *
 * The number is only changed while the title is on EA GAMES TRAX: a chosen
 * soundtrack is an index into the list that is about to change.
 */
#define MUSIC_MANAGER   BO3_VA(0x004115E8u, 0x00411918u, 0x004119B8u)
#define MGR_COUNT       (MUSIC_MANAGER + 0x0C0)
#define MGR_SOURCES     (MUSIC_MANAGER + 0x558)
#define MGR_ON_EA_TRAX  (MUSIC_MANAGER + 0x538)
#define MGR_CHOSEN      (MUSIC_MANAGER + 0x568)

static int manager_set_up(void)
{
    return MEM32(MGR_SOURCES) != 0 && MEM32(MGR_SOURCES) == MEM32(MGR_COUNT) + 1;
}

static int title_on_ea_trax(void)
{
    return MEM8(MGR_ON_EA_TRAX) != 0 && (int32_t)MEM32(MGR_CHOSEN) < 1;
}

/* ── the folder, from the overlay ──────────────────────────────────── */

#ifdef XBOXRECOMP_HAVE_OVERLAY
/* BO-230, custom music at start (below, bo3_soundtrack_start_source). */
static bo3_start_state s_start;        /* soundtrack_start.h */
#endif

/* What the last change came to, for the line under the overlay's row.
 * NOTE_HELD: read, and held back while a soundtrack of the folder in use is
 * the title's music; put in place at the title's next music start
 * (held_folder_in, BO-243). */
enum { NOTE_NONE, NOTE_LIVE, NOTE_HELD, NOTE_FAILED };
static volatile int s_note;
static volatile int s_held;               /* a folder is held back (the cheap check) */

/* Asked on the scan's thread right before the library is replaced
 * (xbox_SoundtrackSetSwapGate): not while a soundtrack of the library in
 * use is the title's music. Checked then, not only when the folder was
 * chosen: the player can pick a soundtrack in EA GAMES TRAX while a big
 * folder is read (BO-127 finding 11). */
static int swap_gate(void)
{
    return !(manager_set_up() && !title_on_ea_trax());
}

/* The scan of a folder chosen in the overlay is through (its thread): the
 * library is in place; the title's count follows. -1: it was held back
 * (swap_gate) for the title's next music start (held_folder_in). */
static void folder_changed(int soundtracks, int songs)
{
    (void)songs;
    if (soundtracks < 0) {
        s_note = NOTE_HELD;
        s_held = 1;
        fprintf(stderr, "[SOUNDTRACK] a custom soundtrack is the title's music: the new folder is "
                        "held back until the title's thread runs on (held_folder_in)\n");
        return;
    }
    if (manager_set_up()) {
        MEM32(MGR_COUNT) = (uint32_t)soundtracks;
        MEM32(MGR_SOURCES) = (uint32_t)soundtracks + 1;
    }
    s_on = soundtracks > 0;
    fprintf(stderr, "[SOUNDTRACK] the title now counts %d soundtrack%s\n", soundtracks,
            soundtracks == 1 ? "" : "s");
}

#ifdef XBOXRECOMP_HAVE_OVERLAY
static void overlay_chosen(const char *dir)
{
    char now[1024];

    xbox_SoundtrackInfo(now, sizeof now, NULL, NULL);
    if (!strcmp(now, dir) && !xbox_SoundtrackPending(NULL, 0, NULL)) {
        s_note = NOTE_NONE;
        return;
    }
    /* Saved already (the overlay). Read at once; in use when the read is
     * through, or, while one of the soundtracks that would go away is the
     * title's music, at the title's next music start (swap_gate, then
     * held_folder_in). Before BO-243 nothing was read then, and the folder
     * waited for a restart: with custom music at start (BO-230) that was
     * every change. Set first: the scan's thread can say NOTE_HELD before
     * this returns. */
    s_note = NOTE_LIVE;
    if (!xbox_SoundtrackSetFolder(dir, folder_changed))
        s_note = NOTE_FAILED;
}

static void overlay_status(char *out, unsigned n)
{
    char dir[1024];
    int folders = 0, files = 0, songs = 0, st = 0;

    if (xbox_SoundtrackScanning(&folders, &files, &songs)) {
        snprintf(out, n, "Reading the folder: %d file%s in %d folder%s so far...", files,
                 files == 1 ? "" : "s", folders, folders == 1 ? "" : "s");
        return;
    }
    xbox_SoundtrackInfo(dir, sizeof dir, &st, &songs);
    if (s_note == NOTE_HELD && xbox_SoundtrackPending(NULL, 0, NULL))
        snprintf(out, n, "Read. A custom soundtrack is playing: the new folder takes its place as soon as"
                 " the game runs again (close this menu). The song playing now plays to its end.");
    else if (s_note == NOTE_FAILED)
        snprintf(out, n, "Saved. It could not be read now, so it is used from the next start.");
    else if (!st && xbox_SettingsText("audio.custom_music_dir", NULL)[0] && xbox_SoundtrackFolderUnreadable())
        snprintf(out, n, "That folder cannot be read now (moved, deleted, or on a card that is out), so the"
                 " game's own music plays. It is tried again at the next start.");
    else if (st && xbox_SoundtrackScanStopped())
        snprintf(out, n, "%d soundtrack%s, %d song%s. The folder is too big to read whole: choose a"
                 " folder of songs, or a folder of folders of songs.", st, st == 1 ? "" : "s", songs,
                 songs == 1 ? "" : "s");
    else if (st) {
        int left = xbox_SoundtrackLeftOut(NULL, NULL, NULL);
        if (left)
            snprintf(out, n, "%d soundtrack%s, %d song%s in EA GAMES TRAX (X there). %d left out, see below.",
                     st, st == 1 ? "" : "s", songs, songs == 1 ? "" : "s", left);
        else
            snprintf(out, n, "%d soundtrack%s, %d song%s. Find them in EA GAMES TRAX (Driver Details > Settings) with X.",
                     st, st == 1 ? "" : "s", songs, songs == 1 ? "" : "s");
    }
    else if (xbox_SettingsText("audio.custom_music_dir", NULL)[0] && xbox_SoundtrackScanStopped())
        snprintf(out, n, "That folder is too big to read whole (20 000 entries). Choose a folder of"
                 " songs, or a folder of folders of songs.");
    else if (xbox_SettingsText("audio.custom_music_dir", NULL)[0])
        snprintf(out, n, "No music (mp3, flac, ogg, wav) in that folder or the folders directly in it.");
    else
        out[0] = 0;
}

/* What the scan left out, a line each: the unreadable files by name with the
 * reason (three of them; the log has every one), then the limits. */
static int overlay_note(int i, char *out, unsigned n)
{
    int bad = 0, songs = 0, folders = 0, shown;
    char name[96];
    const char *why = "";

    if (xbox_SoundtrackScanning(NULL, NULL, NULL) || !xbox_SoundtrackLeftOut(&bad, &songs, &folders))
        return 0;
    shown = bad > 3 ? 2 : bad;            /* three lines for files at most */
    if (i < shown && xbox_SoundtrackLeftOutFile(i, name, sizeof name, &why)) {
        snprintf(out, n, "  %.60s%s: %s", name, strlen(name) > 60 ? "..." : "", why);
        return 1;
    }
    if (i == shown && bad > shown) {
        snprintf(out, n, "  and %d more file%s that cannot be read (the log lists them)", bad - shown,
                 bad - shown == 1 ? "" : "s");
        return 1;
    }
    if (i == shown + (bad > shown) && (songs || folders)) {
        snprintf(out, n, "  past the limits: %d song%s (500 a folder), %d folder%s (100 soundtracks)",
                 songs, songs == 1 ? "" : "s", folders, folders == 1 ? "" : "s");
        return 1;
    }
    return 0;
}

#if defined(__ANDROID__)
/* Android: the shared storage (the Music and Download folders, a card's
 * /storage/<id>) is read as plain files once the player has given the app
 * READ_MEDIA_AUDIO (Android 13+, the APK's minSdk; the manifest has it). It
 * is asked for here, when the Custom music folder row is opened, never at
 * start-up: SDL's request (SDL_AndroidRequestPermission, SDLActivity's
 * requestPermission) shows the system's dialog and blocks this thread, the
 * overlay's access thread, until the answer. Asked once a run: after a "no"
 * the row says how to allow it in Android's settings, and the next press
 * opens the browser anyway (this app's own Music folder needs nothing). A
 * permission given later in the settings is seen at the next press
 * (Context.checkSelfPermission). */
#include <jni.h>
#include <SDL.h>
#include <SDL_system.h>

#define AUDIO_PERMISSION "android.permission.READ_MEDIA_AUDIO"

/* 1 granted, 0 not, -1 could not be asked (no activity). */
static int android_has_audio_permission(void)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = env ? (jobject)SDL_AndroidGetActivity() : NULL;
    jclass cls;
    jmethodID m;
    jstring perm;
    int r = -1;

    if (!act)
        return -1;
    cls = (*env)->GetObjectClass(env, act);
    m = (*env)->GetMethodID(env, cls, "checkSelfPermission", "(Ljava/lang/String;)I");
    perm = (*env)->NewStringUTF(env, AUDIO_PERMISSION);
    if (m && perm && !(*env)->ExceptionCheck(env)) {
        jint v = (*env)->CallIntMethod(env, act, m, perm);
        if (!(*env)->ExceptionCheck(env))
            r = v == 0;                   /* PackageManager.PERMISSION_GRANTED */
    }
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    if (perm)
        (*env)->DeleteLocalRef(env, perm);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return r;
}

static int s_audio_asked;                 /* 1 asked this run, 2 and the "no" was shown */

static int overlay_access(char *line, unsigned n)
{
    int has = android_has_audio_permission();

    line[0] = 0;
    if (has != 0)
        return 1;                         /* granted, or nothing to ask with: the browser */
    if (!s_audio_asked) {
        s_audio_asked = 1;
        fprintf(stderr, "[SOUNDTRACK] asking for " AUDIO_PERMISSION "\n");
        if (SDL_AndroidRequestPermission(AUDIO_PERMISSION)) {
            fprintf(stderr, "[SOUNDTRACK] " AUDIO_PERMISSION " granted\n");
            return 1;
        }
        fprintf(stderr, "[SOUNDTRACK] " AUDIO_PERMISSION " denied\n");
    }
    snprintf(line, n, "Android does not let the game read your music files, so only this app's own Music"
             " folder can be used. To allow it: Android Settings > Apps > Takedawn > Permissions >"
             " Music and audio > Allow. Press A for the folder list.");
    if (s_audio_asked == 1) {
        s_audio_asked = 2;
        return 0;                         /* this press: the line is read first */
    }
    return 1;
}

static const xbox_music_folder_ops k_folder_ops = { overlay_status, overlay_chosen, overlay_note, overlay_access };
#else
static const xbox_music_folder_ops k_folder_ops = { overlay_status, overlay_chosen, overlay_note, NULL };
#endif
#endif

void bo3_soundtrack_init(void)
{
    /* The setting as UTF-8 (xbox_SettingsText): on Windows the environment
     * alone would give the folder's name in the ANSI code page. */
#ifdef XBOXRECOMP_HAVE_OVERLAY
    const char *dir = xbox_SettingsText("audio.custom_music_dir", NULL);
#else
    const char *dir = recomp_env(RENV_AUDIO_MUSIC_DIR);
#endif

    /* On the scan's own thread, as a folder chosen in the overlay: a start
     * never waits on the folder (a big one, a slow drive). The title counts
     * the soundtracks when its music manager is set up; a scan that ends
     * after that gives it the count then (folder_changed). */
    xbox_SoundtrackSetSwapGate(swap_gate);
    if (dir && *dir && !xbox_SoundtrackSetFolder(dir, folder_changed))
        fprintf(stderr, "[SOUNDTRACK] the folder could not be read now: custom music is off\n");
#ifdef XBOXRECOMP_HAVE_OVERLAY
    /* BO-230: with a folder, the setting on, the start switch waits for the
     * title's first music start (bo3_soundtrack_start_source). */
    s_start.pending = dir && *dir && xbox_SettingValue(XS_AUDIO_CUSTOM_AT_START) != 0.0;
#endif
#ifdef XBOXRECOMP_HAVE_OVERLAY
    xbox_OverlaySetMusicFolder(&k_folder_ops);
#endif
}

/* ── a folder held back (BO-243) ───────────────────────────────────── */

/*
 * A folder read while a soundtrack of the folder in use was the title's
 * music (swap_gate said no) is put in place on the title's thread, at the
 * first of:
 *
 *   - the decoder's Process (m_process), once a frame while a song of the
 *     folder plays: the song goes on (its file is open), and what the title
 *     holds of the old list is made the new list's: the source chosen and
 *     the source in use, the source's descriptor (as sub_00155CB0 makes it
 *     from the database: the soundtrack's id, song count, length and name,
 *     manager+0x4E4, copied there by sub_001535C0), the player's copy of
 *     the id and count (player = manager+8: +0xAC count, +0xB0 id) and the
 *     track number (manager+0x554, player+0xB4) brought into the new count.
 *     The next song is then the new soundtrack's, and the EA GAMES TRAX
 *     screen opens on the new list (its candidate is the source in use).
 *   - the entry of the title's music start (sub_00155420): it is about to
 *     stop the music and build all of that from the source chosen, so only
 *     that is made the new list's.
 *
 * A soundtrack becomes the new folder's one of the same name, else its
 * first (as the start switch does with a name the folder lacks), EA GAMES
 * TRAX when the new folder has none. Not the player's choice: not
 * remembered (s_start.seen follows).
 */
#define MGR_DESC        0x4E4u           /* the source's descriptor, 0x58 bytes */
#define MGR_IN_USE      0x534u           /* the source in use (descriptor + 0x50) */
#define MGR_TRACK       0x554u           /* the track number */
#define PLAYER          0x008u           /* the player, in the manager */
#define PL_COUNT        0xACu
#define PL_ID           0xB0u
#define PL_TRACK        0xB4u

/* The new library's index for the old library's soundtrack `source` (1..):
 * by name, else the first; -1 when the new one has none. */
static int remap(const char *name, int n)
{
    return bo3_held_target(n, name[0] ? xbox_SoundtrackIndexOfName(name) : -1);
}

static void held_folder_in(int playing)
{
    const uint32_t mgr = MUSIC_MANAGER;
    char chosen_name[96] = "", use_name[96] = "";
    int32_t chosen, in_use, chosen_new, use_new = 0;
    int n = 0, songs = 0, k, playing_custom;

    int pend_st = 0;

    if (!s_held || !manager_set_up())
        return;
    if (!xbox_SoundtrackPending(NULL, 0, &pend_st)) {
        s_held = 0;                       /* replaced by a newer choice meanwhile */
        return;
    }
    chosen = (int32_t)MEM32(MGR_CHOSEN);
    in_use = (int32_t)MEM32(mgr + MGR_IN_USE);
    playing_custom = playing && MEM8(MGR_ON_EA_TRAX) == 0 && in_use >= 1;
    /* A song plays and the new folder has no soundtrack: nothing to make
     * the music in use; the title's next music start does it (EA). */
    if (playing_custom && pend_st < 1)
        return;
    if (chosen >= 1)
        xbox_SoundtrackNameOf(chosen - 1, chosen_name, sizeof chosen_name);
    if (playing_custom)
        xbox_SoundtrackNameOf(in_use - 1, use_name, sizeof use_name);
    s_held = 0;
    if (!xbox_SoundtrackApplyPending(&n, &songs))
        return;
    MEM32(MGR_COUNT) = (uint32_t)n;
    MEM32(MGR_SOURCES) = (uint32_t)n + 1;
    s_on = n > 0;
    chosen_new = chosen;
    if (chosen >= 1) {
        k = remap(chosen_name, n);
        chosen_new = k + 1;
        MEM32(MGR_CHOSEN) = (uint32_t)chosen_new;
    }
    if (playing_custom) {
        const xst_library *lib = xbox_SoundtrackLibrary();
        k = remap(use_name, n);
        if (k >= 0 && lib && k < lib->n_st) {
            const xst_soundtrack *st = &lib->st[k];
            uint32_t count = (uint32_t)st->count, track = MEM32(mgr + MGR_TRACK);
            use_new = k + 1;
            MEM32(mgr + MGR_DESC + 0) = st->id;
            MEM32(mgr + MGR_DESC + 4) = count;
            MEM32(mgr + MGR_DESC + 8) = st->ms;
            memcpy((void *)XBOX_PTR(mgr + MGR_DESC + 0xC), st->name, sizeof st->name);
            MEM32(mgr + MGR_IN_USE) = (uint32_t)use_new;
            MEM32(mgr + PLAYER + PL_ID) = st->id;
            MEM32(mgr + PLAYER + PL_COUNT) = count;
            if (count && track != 0xFFFFFFFFu && track >= count) {
                MEM32(mgr + MGR_TRACK) = track % count;
                MEM32(mgr + PLAYER + PL_TRACK) = track % count;
            }
        }
    }
#ifdef XBOXRECOMP_HAVE_OVERLAY
    s_start.seen = chosen_new;
#endif
    s_note = NOTE_LIVE;
    fprintf(stderr, "[SOUNDTRACK] the title now counts %d soundtrack%s (the folder held back, %s); "
                    "chosen %d \"%s\" -> %d, in use %d \"%s\" -> %d\n", n, n == 1 ? "" : "s",
            playing ? "while a song plays" : "at a music start", (int)chosen, chosen_name, (int)chosen_new,
            (int)in_use, use_name, playing_custom ? (int)use_new : (int)in_use);
}

/* ── the decoder object's methods ──────────────────────────────────── */

/* A stdcall return: the HRESULT, the return address and the arguments. */
static void ret(uint32_t hr, uint32_t arg_bytes)
{
    g_eax = hr;
    g_esp += 4 + arg_bytes;
}

static int slot_of(uint32_t obj)
{
    uint32_t i;
    if (!obj || MEM32(obj + 4) != OBJ_MARK)
        return -1;
    i = MEM32(obj + 8);
    return i < SLOTS && s_slot[i].used ? (int)i : -1;
}

static void m_addref(void)        { ret(1, 4); }
static void m_get_info(void)      { ret(E_NOTIMPL, 8); }
static void m_discontinuity(void) { ret(S_OK, 4); }
static void m_flush(void)         { ret(S_OK, 4); }
static void m_seek(void)          { ret(E_NOTIMPL, 16); }
static void m_get_length(void)    { ret(E_NOTIMPL, 8); }
static void m_do_work(void)       { ret(S_OK, 4); }

static void m_release(void)
{
    uint32_t obj = MEM32(g_esp + 4);
    int i = slot_of(obj);

    if (i >= 0) {
        xst_stream_close(s_slot[i].stream);
        s_slot[i].stream = NULL;
        s_slot[i].used = 0;
        MEM32(obj + 4) = 0;
        PUSH32(g_esp, WMA_HEAP_TAG);
        PUSH32(g_esp, obj);
        PUSH32(g_esp, 0);
        BO3_SUB(sub_001D2919, sub_001D2C39, sub_001D2CD9)();
    }
    ret(0, 4);
}

static void m_get_status(void)
{
    uint32_t out = MEM32(g_esp + 8);
    if (out)
        MEM32(out) = XMO_STATUSF_ACCEPT_OUTPUT_DATA;
    ret(S_OK, 8);
}

/* Process(this, input, output): XMEDIAPACKET { buffer, max size, -> completed
 * size, -> status, ... }. Whole packets until the song ends (its last one
 * padded with silence: the title submits every packet at full size), then 0
 * bytes, which the title takes as the end. */
static void m_process(void)
{
    int i = slot_of(MEM32(g_esp + 4));
    uint32_t pkt = MEM32(g_esp + 12);

    if (s_held)
        held_folder_in(1);

    if (pkt) {
        uint32_t buf = MEM32(pkt), max = MEM32(pkt + 4) & ~3u;
        uint32_t done = MEM32(pkt + 8), status = MEM32(pkt + 12), bytes = 0;

        if (i >= 0 && s_slot[i].stream && buf && max) {
            int16_t *pcm = (int16_t *)XBOX_PTR(buf);
            size_t got = xst_stream_read(s_slot[i].stream, pcm, max / 4);
            if (got) {
                memset((char *)pcm + got * 4, 0, max - got * 4);
                bytes = max;
            }
        }
        if (done)
            MEM32(done) = bytes;
        if (status)
            MEM32(status) = S_OK;
    }
    ret(S_OK, 12);
}

/* XMediaObject (AddRef, Release, GetInfo, GetStatus, Process, Discontinuity,
 * Flush), then XFileMediaObject (Seek, GetLength, DoWork). */
static const bo3_guest_fn k_methods[10] = {
    m_addref, m_release, m_get_info, m_get_status, m_process,
    m_discontinuity, m_flush, m_seek, m_get_length, m_do_work,
};

bo3_guest_fn bo3_soundtrack_lookup(uint32_t xbox_va)
{
    if (xbox_va < BO3_SOUNDTRACK_VA_BASE || xbox_va >= BO3_SOUNDTRACK_VA_END || (xbox_va & 3))
        return (bo3_guest_fn)0;
    return k_methods[(xbox_va - BO3_SOUNDTRACK_VA_BASE) / 4];
}

/* ── the title's own track start ───────────────────────────────────── */

/*
 * sub_001537C0(eax = track index, edi = manager) starts a track of the
 * source in use. For EA GAMES TRAX it takes the track's record from a table
 * of 44 (0x3EC450, 24 bytes each: manager+0x540 = table + index * 24) and
 * never checks the index. The title has ways to arrive there with an index
 * that belongs to the list it is leaving: sub_00155420 picks the next index
 * (sub_00153660: the one after the last, or a random one of the list's
 * count) BEFORE it switches the source, and the EA GAMES TRAX screen
 * previews "the same row" after X or a cancel. From a console soundtrack of
 * more than 44 songs back to EA GAMES TRAX that index can be 44 or more:
 * the record is then whatever lies after the table, and the pop-up
 * (sub_001530D0, 0x153161) takes two of its words for string indices and
 * faults; the track-failed path (0x15305E) clears a bit in a byte of the
 * profile at 0x4AE1A0 + index. (BO-120: a folder of 142 songs, index 59.)
 *
 * So: an index EA GAMES TRAX does not have is brought into its range
 * before the title's code runs. Always: a soundtrack database copied from a
 * console with more than 44 songs in one soundtrack exposes it with no
 * music folder at all, and so does a folder changed under a chosen
 * soundtrack (BO-127 findings 11, 17). An index in range (every one the
 * title gives in a run without those) goes through untouched: the check
 * only reads two words of the manager.
 */
#define MGR_EA_FLAG   0x538u             /* this source is EA GAMES TRAX */
#define MGR_SONGS     0x4E8u             /* its number of tracks */


void bo3_soundtrack_start_track(bo3_guest_fn title_fn)
{
    uint32_t mgr = g_edi, index = g_eax, count;
    if (index != 0xFFFFFFFFu && MEM8(mgr + MGR_EA_FLAG) != 0) {
        count = MEM32(mgr + MGR_SONGS);
        if (count && index >= count) {
            static int said;
            g_eax = index % count;
            if (said < 8) {
                said++;
                fprintf(stderr, "[SOUNDTRACK] EA GAMES TRAX was asked for track %u of %u (an index "
                                "of the custom list): track %u instead\n",
                        (unsigned)index, (unsigned)count, (unsigned)g_eax);
            }
        }
    }
    title_fn();
}

/* ── custom music at start (BO-230) ────────────────────────────────── */

/*
 * What the title does, from its code (docs/custom-music.md):
 *
 *   - boot (sub_00153B00): manager+0x568 = 0, EA GAMES TRAX. Nothing in the
 *     profile holds the source: the EA GAMES TRAX screen keeps the profile's
 *     per-track flags (0x4AE1A0..) and the random-order byte (0x4AE1DA), and
 *     the source it confirms goes to the manager (0x411B50, +0x568) only.
 *     So the title starts on EA GAMES TRAX every run.
 *   - the screen: X steps its own candidate (screen+0x93C) through the
 *     sources, wrapping to 0; A (sub_00084260, 0x842A9) copies it to
 *     manager+0x568; the screen's exit then runs sub_00155420.
 *   - sub_00155420(manager): stops the music, reads manager+0x568, makes the
 *     source's descriptor (0: EA GAMES TRAX's at 0x3EC870, n: soundtrack n-1
 *     through sub_00155CB0), picks the next track and starts it.
 *
 * So the title is switched the way its screen does it: manager+0x568 = n,
 * then the title's own sub_00155420 starts it. The wrap sits at the entry of
 * sub_00155420, which is only ever called to start (or restart) a track: no
 * song is cut short for it, and the first call after boot is the first music
 * the player hears. The choice is seen there too: a value of +0x568 that is
 * not the one last seen is the player's confirm.
 */
#define MGR_SOURCE_CHOSEN   0x568u
#define SOUNDTRACK_KEY      "audio.custom_music_soundtrack"

#ifdef XBOXRECOMP_HAVE_OVERLAY
/* The soundtrack to start on: its index, or -1 for EA GAMES TRAX (the
 * player's last choice, or nothing to switch). Nothing chosen yet: the
 * first. A name the folder no longer has: the first. */
static int start_target(void)
{
    int locked = 0, n = 0;
    const char *name;

    xbox_SoundtrackInfo(NULL, 0, &n, NULL);
    name = xbox_SettingsText(SOUNDTRACK_KEY, &locked);
    return bo3_start_target(n, xbox_SettingsTextIsSet(SOUNDTRACK_KEY) || (locked && name[0]), name,
                            name[0] ? xbox_SoundtrackIndexOfName(name) : -1);
}

static void remember_source(int32_t source)
{
    char name[96] = "";

    if (source >= 1 && !xbox_SoundtrackNameOf(source - 1, name, sizeof name))
        return;                           /* a source the library does not have: not ours to note */
    if (strcmp(xbox_SettingsText(SOUNDTRACK_KEY, NULL), name) || !xbox_SettingsTextIsSet(SOUNDTRACK_KEY)) {
        xbox_SettingsSetText(SOUNDTRACK_KEY, name);
        xbox_SettingsFlush();
        fprintf(stderr, "[SOUNDTRACK] chosen in the game: %s%s%s (remembered)\n",
                name[0] ? "\"" : "EA GAMES TRAX", name, name[0] ? "\"" : "");
    }
}
#endif

void bo3_soundtrack_start_source(bo3_guest_fn title_fn)
{
    held_folder_in(0);
#ifdef XBOXRECOMP_HAVE_OVERLAY
    /* Only with a library in use and the manager counting it: the count is
     * the title's once its music manager is set up, and the scan can end
     * after the title has booted (folder_changed). */
    if (s_on && manager_set_up()) {
        uint32_t mgr = MEM32(g_esp + 4);
        int32_t cur = (int32_t)MEM32(mgr + MGR_SOURCE_CHOSEN), source = cur;

        switch (bo3_start_step(&s_start, cur, start_target, &source)) {
        case BO3_START_SWITCH: {
            char name[96] = "";
            xbox_SoundtrackNameOf(source - 1, name, sizeof name);
            MEM32(mgr + MGR_SOURCE_CHOSEN) = (uint32_t)source;
            fprintf(stderr, "[SOUNDTRACK] custom music at start: soundtrack %d \"%s\"\n", source, name);
            break;
        }
        case BO3_START_EA:
            fprintf(stderr, "[SOUNDTRACK] custom music at start: EA GAMES TRAX was the last choice\n");
            break;
        case BO3_START_REMEMBER:
            /* The player's confirm in the EA GAMES TRAX screen (before our
             * start switch, or after it). */
            remember_source(source);
            break;
        default:
            break;
        }
    }
#endif
    title_fn();
}

/* ── the two wrapped functions ─────────────────────────────────────── */

void bo3_soundtrack_open_song(bo3_guest_fn title_fn)
{
    uint32_t song;

    if (!s_on) {
        title_fn();
        return;
    }
    song = MEM32(g_esp + 4);
    title_fn();
    xbox_SoundtrackNoteOpen(g_eax, song);
}

void bo3_soundtrack_create_decoder(bo3_guest_fn title_fn)
{
    uint32_t desc, out, obj;
    xst_stream *stream;
    int ours = 0, i, k;

    if (!s_on) {
        title_fn();
        return;
    }
    desc = MEM32(g_esp + 4);
    out = MEM32(g_esp + 8);
    stream = desc ? xbox_SoundtrackOpenHandle(MEM32(desc + 4), &ours) : NULL;
    if (!ours) {
        title_fn();
        return;
    }
    /* A file that would not open still gets its object: one that is at its
     * end at once, so the title moves on to the next song. */
    for (i = 0; i < SLOTS && s_slot[i].used; i++)
        ;
    obj = 0;
    if (i < SLOTS) {
        PUSH32(g_esp, WMA_HEAP_TAG);
        PUSH32(g_esp, OBJ_SIZE);
        PUSH32(g_esp, 0);
        BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
        obj = g_eax;
    }
    if (!obj) {
        xst_stream_close(stream);
        if (out)
            MEM32(out) = 0;
        ret(E_OUTOFMEMORY, 8);
        return;
    }
    s_slot[i].used = 1;
    s_slot[i].stream = stream;
    MEM32(obj) = obj + OBJ_VTABLE;
    MEM32(obj + 4) = OBJ_MARK;
    MEM32(obj + 8) = (uint32_t)i;
    for (k = 0; k < 10; k++)
        MEM32(obj + OBJ_VTABLE + (uint32_t)k * 4) = BO3_SOUNDTRACK_VA_BASE + (uint32_t)k * 4;
    if (out)
        MEM32(out) = obj;
    ret(S_OK, 8);
}
