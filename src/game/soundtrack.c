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
extern void BO3_SUB(sub_001D2879, sub_001D2B99)(void);
extern void BO3_SUB(sub_001D2919, sub_001D2C39)(void);
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
#define MUSIC_MANAGER   BO3_VA(0x004115E8u, 0x00411918u)
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

/* What the last change came to, for the line under the overlay's row. */
enum { NOTE_NONE, NOTE_LIVE, NOTE_NEXT_START, NOTE_FAILED };
static volatile int s_note;

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
 * library is in place; the title's count follows. -1: it was kept out
 * (swap_gate), the folder is for the next start. */
static void folder_changed(int soundtracks, int songs)
{
    (void)songs;
    if (soundtracks < 0) {
        s_note = NOTE_NEXT_START;
        fprintf(stderr, "[SOUNDTRACK] a custom soundtrack became the title's music while the folder "
                        "was read: the new folder is for the next start\n");
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
    if (!strcmp(now, dir)) {
        s_note = NOTE_NONE;
        return;
    }
    /* Saved already (the overlay). In use at once unless one of the
     * soundtracks that would go away is the title's music right now. */
    if (manager_set_up() && !title_on_ea_trax()) {
        s_note = NOTE_NEXT_START;
        fprintf(stderr, "[SOUNDTRACK] a custom soundtrack is the title's music: the new folder "
                        "is for the next start\n");
        return;
    }
    /* Set first: the scan's thread can say NOTE_NEXT_START (swap_gate)
     * before this returns. */
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
    if (s_note == NOTE_NEXT_START)
        snprintf(out, n, "Saved. It is used from the next start, because a custom soundtrack is playing now.");
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
    xbox_OverlaySetMusicFolder(&k_folder_ops);
#endif
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
        BO3_SUB(sub_001D2919, sub_001D2C39)();
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
        BO3_SUB(sub_001D2879, sub_001D2B99)();
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
