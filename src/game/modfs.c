/*
 * modfs.c - optional mod folders: files the title reads from its disc can be
 * replaced by files in folders the player names (docs/mod-folders.md).
 *
 * TAKEDAWN_MODS=<folder>[;<folder>...]: up to eight folders, the first one
 * named wins. ';' separates them on every system (a ':' is part of a path).
 * Each mirrors the disc: <folder>/Tracks/US/C3_V1/static.dat is read instead
 * of the disc's Tracks\US\C3_V1\static.dat. A file a folder does not have is
 * read from the disc as before. Unset (the default): nothing is installed and
 * no path changes. On Android, where there is no shell to set it from, unset
 * means <the app's files folder>/mods when that folder exists
 * (RECOMP_ANDROID_DIR, which GameActivity sets); a TAKEDAWN_MODS line in
 * env.txt still wins, and an empty one turns the folder off.
 *
 * How: xbox_PathRedirectHook (the toolkit's kernel.h), chained to the hook
 * already there (the soundtrack's). For a D:\ or \Device\CdRom0\ path whose
 * file a mod folder has, the hook returns the host path: "\\?\<host path>" on
 * Windows, the absolute path elsewhere. No translation rule claims either
 * form, so xbox_translate_path hands it to the host as it is (kernel_path.c,
 * "Unrecognized Xbox path"). Every other path goes on down the chain
 * unchanged. The title's own loaders then read the file; nothing here parses
 * or checks what is in it.
 *
 * Case: the disc's names are not case-sensitive and the title does not spell
 * them the way the disc does (D:\Sound\DSP\... for sound/DSP). Windows and
 * macOS volumes usually fold case themselves. Elsewhere (Linux, the Steam
 * Deck, any case-sensitive volume) each part of the path is taken as written
 * when the folder has it, else the one entry of its directory that matches
 * without case (ASCII only). Two or more such entries and none as written
 * (Data and DATA for "data"): refused, whichever readdir lists first.
 *
 * What is refused (the disc's file is read instead), the same on every system:
 *   - a path part that is empty, "." or "..", ends in a dot or a space, or
 *     has ':', a character Windows does not allow in a name or one that is
 *     not plain ASCII;
 *   - a link anywhere below a mod folder (a reparse point on Windows: a
 *     symbolic link or a junction; a symbolic link elsewhere), so a folder
 *     cannot point outside itself. The folder itself may be reached through
 *     links: its full path is taken once, at the start;
 *   - anything that is not a regular file (a directory, a pipe, a device);
 *   - a host path too long for the toolkit's buffer (MODFS_HOST_MAX below).
 * A mod folder whose full path is not printable ASCII is ignored: on Windows
 * the path handed back is read in the guest's code page; elsewhere a '\' in
 * it is refused too, the toolkit turns it into '/'. On Windows a folder must
 * be on a drive letter (C:\...): a network share or a \\?\ or \\.\ path is
 * ignored, "\\?\" put in front of it would not name it.
 *
 * The folders taken and ignored are said at the start whatever the build
 * (XR_NOTICE); each replaced file is logged once with diagnostic logging on:
 * "[MOD] <guest path> -> <host path>".
 *
 * Installed mods (modfs.h, modmanifest.h): a subfolder of a mod folder that
 * holds a mod.toml is a mod; its replacement files mirror the disc in its
 * files/ subfolder. They are found once, here, one level deep, in name order
 * (never the order the system lists them in). A mod is used when its
 * manifest is valid and no other mod has its id (a shared id refuses every
 * copy). Order of the lookup: the folders as named; in each, its own loose
 * files first (as before manifests), then its mods by id. A mod's subfolder is
 * not part of its folder's loose files. A file's path through a mod is
 * checked by the same walk as a loose file's (mod_file), from the mod folder
 * down, so the mod's own folder and files/ follow the same rules. A mod the
 * mod list (mod-list.toml, below) names as off has no place in that order.
 * Without
 * manifests the same files are read as before; the log only adds a line for
 * a folder that has too many entries to be searched for mods.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "kernel.h"   /* xbox_PathRedirectHook */
#include "platform/xr_log.h"   /* XR_NOTICE; the print gate (also force-included) */
#include "modfs.h"
#include "recomp_cfg.h"   /* the mod list */

/* The longest host path handed back is MODFS_HOST_MAX - 1 characters. The
 * toolkit gives a path no rule claims to the host as it is (kernel_path.c,
 * "Unrecognized Xbox path"), into a MAX_PATH buffer with its terminator: on
 * Windows converted to MAX_PATH wide characters, with the "\\?\" in front
 * (and a failed conversion is not reported), elsewhere copied. */
#if defined(_WIN32)
#define MODFS_HOST_MAX (MAX_PATH - 4)
#else
#define MODFS_HOST_MAX MAX_PATH
#endif

#if !defined(_WIN32)
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

#define MODFS_MAX_DIRS 8
#define MODFS_SEEN     512
#if defined(_WIN32)
#define SEP_STR "\\"
#else
#define SEP_STR "/"
#endif
#define MODFS_NAME_MAX 255
#define MODFS_MAX_ENTRIES 1024   /* a folder with more entries is not searched for mods */
#define MODFS_INDEX_FILES 4096   /* files of one mod's files/ listed at the start */
#define MODFS_INDEX_DEPTH 8

/* Where a lookup looks, in order: a folder's loose files (prefix "") or a
 * mod's files ("<subfolder>/<files in its own spelling>"), both below
 * s_dirs[root]. A mod's source also has the list of the files in its files/,
 * made once at the start (lower case, '/'): a path not in it is not walked,
 * so a lookup no mod answers costs a search per mod, not a walk per mod. A
 * path in it is still walked and checked as any other (the list only skips).
 * A mod with more files or deeper folders than the list takes is walked for
 * every path, as loose files are. */
typedef struct {
    int    root;
    int    mod;                  /* index in s_mods, -1 for loose files */
    char   prefix[2 * MODFS_NAME_MAX + 2];
    int    listed;               /* files is the list; 0: no list, walk always */
    char **files;                /* sorted */
    int    nfiles;
} source;

static const char *(*s_prev)(const char *);
static char  s_dirs[MODFS_MAX_DIRS][MAX_PATH];
static int   s_dup[MODFS_MAX_DIRS];   /* the same folder as an earlier one: no mods searched */
static int   s_ndirs;
static bo3_mod s_mods[BO3_MODFS_MAX_MODS];
static int   s_nmods;
static source s_src[MODFS_MAX_DIRS + BO3_MODFS_MAX_MODS];
static int   s_nsrc;
static char  s_seen[MODFS_SEEN][160];
static int   s_nseen;
/* s_lock: the log-once list (lookups). s_state_lock: the mod list (its own,
 * so that saving it never holds a lookup up). */
#if defined(_WIN32)
static CRITICAL_SECTION s_lock, s_state_lock;
static int   s_lock_made;   /* both made by the first bo3_modfs_init */
#define MODFS_LOCK()   EnterCriticalSection(&s_lock)
#define MODFS_UNLOCK() LeaveCriticalSection(&s_lock)
#define STATE_LOCK()   EnterCriticalSection(&s_state_lock)
#define STATE_UNLOCK() LeaveCriticalSection(&s_state_lock)
#else
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_state_lock = PTHREAD_MUTEX_INITIALIZER;
static const int s_lock_made = 1;
#define MODFS_LOCK()   pthread_mutex_lock(&s_lock)
#define MODFS_UNLOCK() pthread_mutex_unlock(&s_lock)
#define STATE_LOCK()   pthread_mutex_lock(&s_state_lock)
#define STATE_UNLOCK() pthread_mutex_unlock(&s_state_lock)
#endif
/* Per thread: several threads open files at once, and the caller copies the
 * path before its next translation on the same thread. */
static _Thread_local char t_out[MAX_PATH + 8];

/* ASCII only, whatever the locale: the disc's names are plain ASCII. */
static int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int same_nocase(const char *a, const char *b)
{
    for (;; a++, b++) {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b))
            return 0;
        if (!*a)
            return 1;
    }
}

static const char *strip_device(const char *p)
{
    static const char *const pre[] = { "D:\\", "\\Device\\CdRom0\\" };
    for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++) {
        size_t n = 0;
        while (pre[i][n] && ascii_lower((unsigned char)p[n]) == ascii_lower((unsigned char)pre[i][n]))
            n++;
        if (!pre[i][n])
            return p + n;
    }
    return NULL;
}

static int printable_ascii(const char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E)
            return 0;
    return 1;
}

/* One part of a disc path, the n bytes at p: a plain name. The same rule on
 * every system, so a mod folder means the same everywhere. */
static int part_ok(const char *p, size_t n)
{
    if (n == 0 || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')
            || p[n - 1] == '.' || p[n - 1] == ' ')
        return 0;      /* Windows drops a trailing dot or space, "\\?\" does not */
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)p[i] < 0x20 || (unsigned char)p[i] > 0x7E || strchr("<>:\"|?*", p[i]))
            return 0;  /* the disc's names are plain ASCII */
    return 1;
}

#if defined(_WIN32)
/* <dir>\<rel> into out, '/' made '\'; 0 unless every part of rel is a plain
 * name and every part below dir exists without being a reparse point, and
 * the last one is a regular file. NTFS folds case itself. */
static int mod_file(const char *dir, const char *rel, char *out, size_t n, int *ambiguous)
{
    size_t dl = strlen(dir), i, start;
    const char *sep = dl && dir[dl - 1] == '\\' ? "" : "\\";   /* a drive root (E:\) has its own */
    int len = snprintf(out, n, "%s%s%s", dir, sep, rel);

    *ambiguous = 0;
    if (len <= 0 || (size_t)len >= n)
        return 0;
    start = dl + strlen(sep);
    for (i = start; out[i]; i++)
        if (out[i] == '/')
            out[i] = '\\';
    for (i = start;; i++) {
        char c = out[i];
        DWORD a;
        if (c != '\\' && c != 0)
            continue;
        if (!part_ok(out + start, i - start))
            return 0;
        out[i] = 0;
        a = GetFileAttributesA(out);
        out[i] = c;
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT))
            return 0;
        if (c == 0)
            return !(a & FILE_ATTRIBUTE_DIRECTORY);
        if (!(a & FILE_ATTRIBUTE_DIRECTORY))
            return 0;
        start = i + 1;
    }
}

/* The folder tok names, as a full path without a trailing separator. Only a
 * folder on a drive letter (X:\...): the hook hands back "\\?\" + the path,
 * which names nothing for a network share (\\server\share, whose long form
 * is \\?\UNC\...) or a path that already is \\?\ or \\.\. */
static int mod_folder(const char *tok, char *full)
{
    DWORD n = GetFullPathNameA(tok, MAX_PATH, full, NULL);
    DWORD a;
    size_t len;

    if (n && n < MAX_PATH && !(ascii_lower((unsigned char)full[0]) >= 'a'
            && ascii_lower((unsigned char)full[0]) <= 'z' && full[1] == ':' && full[2] == '\\')) {
        XR_NOTICE("[MOD] ignored (not on a drive letter: network shares and \\\\?\\ paths "
                  "are not supported): %s\n", tok);
        return 0;
    }
    a = n && n < MAX_PATH ? GetFileAttributesA(full) : INVALID_FILE_ATTRIBUTES;
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
        XR_NOTICE("[MOD] ignored (not a folder): %s\n", tok);
        return 0;
    }
    if (!printable_ascii(full)) {
        XR_NOTICE("[MOD] ignored (the path is not plain ASCII): %s\n", tok);
        return 0;
    }
    len = strlen(full);
    while (len > 3 && (full[len - 1] == '\\' || full[len - 1] == '/'))
        full[--len] = 0;
    return 1;
}
#else
/* The entry name (NAME_MAX + 1 bytes) of the open directory d: as written
 * when d has it, else the only entry that matches without case, whose
 * spelling replaces name. Its type into *st, the entry itself (a link is a
 * link). 0 when d has none, or has two or more and none as written
 * (*ambiguous set). */
static int find_entry(int d, char *name, struct stat *st, int *ambiguous)
{
    char hit[NAME_MAX + 1];
    struct dirent *de;
    DIR *dir;
    int hits = 0, fd;

    if (fstatat(d, name, st, AT_SYMLINK_NOFOLLOW) == 0)
        return 1;
    if (errno != ENOENT)
        return 0;
    fd = fcntl(d, F_DUPFD_CLOEXEC, 0);   /* fdopendir takes it over */
    if (fd < 0)
        return 0;
    dir = fdopendir(fd);
    if (!dir) {
        close(fd);
        return 0;
    }
    rewinddir(dir);
    while ((de = readdir(dir)) != NULL)
        if (same_nocase(de->d_name, name) && ++hits == 1)
            memcpy(hit, de->d_name, strlen(name) + 1);   /* the same length: ASCII folding */
    closedir(dir);
    if (hits != 1) {
        *ambiguous = hits > 1;
        return 0;
    }
    memcpy(name, hit, strlen(name) + 1);
    return fstatat(d, name, st, AT_SYMLINK_NOFOLLOW) == 0;
}

/* <dir>/<rel> into out, each part of rel in the folder's own spelling; 0
 * unless every part is a plain name, every directory below dir is one (not a
 * link to one) and the last part is a regular file. The walk goes from
 * directory descriptor to directory descriptor (openat with O_NOFOLLOW), so a
 * part replaced by a link during the walk is refused too. */
static int mod_file(const char *dir, const char *rel, char *out, size_t n, int *ambiguous)
{
    size_t len = strlen(dir);
    const char *p = rel;
    int d, ok = 0;

    *ambiguous = 0;
    if (len >= n)
        return 0;
    memcpy(out, dir, len + 1);
    if (len == 1)
        len = 0;        /* "/": the parts follow its own '/' */
    d = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (d < 0)
        return 0;
    for (;;) {
        const char *e = p;
        char name[NAME_MAX + 1];
        struct stat st;
        size_t pl;
        int sub;

        while (*e && *e != '\\' && *e != '/')
            e++;
        pl = (size_t)(e - p);
        if (!part_ok(p, pl) || pl > NAME_MAX || len + 1 + pl >= n)
            break;
        memcpy(name, p, pl);
        name[pl] = 0;
        if (!find_entry(d, name, &st, ambiguous))
            break;
        out[len++] = '/';
        memcpy(out + len, name, pl + 1);
        len += pl;
        if (!*e) {
            ok = S_ISREG(st.st_mode);
            break;
        }
        if (!S_ISDIR(st.st_mode))
            break;      /* a file, or a link */
        sub = openat(d, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(d);
        d = sub;
        if (d < 0)
            return 0;
        p = e + 1;
    }
    close(d);
    return ok;
}

/* The folder tok names, as a full path with every link in it resolved. */
static int mod_folder(const char *tok, char *full)
{
    char real[PATH_MAX];
    struct stat st;

    if (!realpath(tok, real) || stat(real, &st) != 0 || !S_ISDIR(st.st_mode)) {
        XR_NOTICE("[MOD] ignored (not a folder): %s\n", tok);
        return 0;
    }
    if (!printable_ascii(real) || strchr(real, '\\')) {
        XR_NOTICE("[MOD] ignored (the path is not plain ASCII, or has a '\\'): %s\n", tok);
        return 0;
    }
    if (strlen(real) >= MAX_PATH) {
        XR_NOTICE("[MOD] ignored (the path is too long): %s\n", tok);
        return 0;
    }
    strcpy(full, real);
    return 1;
}
#endif

/* ---- installed mods: finding them ---- */

/* A drive root (E:\) or "/" keeps its own separator, as in mod_file. */
static int ends_in_sep(const char *dir)
{
    size_t n = strlen(dir);
    return n && (dir[n - 1] == '\\' || dir[n - 1] == '/');
}

/* A mod's file list while it is made (see source). incomplete: more than
 * MODFS_INDEX_FILES files, a folder deeper than MODFS_INDEX_DEPTH, or out of
 * memory; the list is then dropped and the mod walked for every path. */
typedef struct {
    char **v;
    int n, cap, incomplete;
} file_list;

static void list_add(file_list *l, const char *rel)
{
    size_t len = strlen(rel);
    char *s;

    if (l->incomplete)
        return;
    if (l->n == MODFS_INDEX_FILES) {
        l->incomplete = 1;
        return;
    }
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        char **v = realloc(l->v, (size_t)cap * sizeof *v);
        if (!v) {
            l->incomplete = 1;
            return;
        }
        l->v = v;
        l->cap = cap;
    }
    s = malloc(len + 1);
    if (!s) {
        l->incomplete = 1;
        return;
    }
    for (size_t i = 0; i <= len; i++)
        s[i] = (char)ascii_lower((unsigned char)rel[i]);
    l->v[l->n++] = s;
}

static void list_free(char **v, int n)
{
    for (int i = 0; i < n; i++)
        free(v[i]);
    free(v);
}

static int by_string(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* rel (a disc path) in the form the lists hold: lower case, '/'. 0 when it
 * does not fit. */
static int list_key(const char *rel, char *out, size_t n)
{
    size_t i;
    for (i = 0; rel[i]; i++) {
        if (i + 1 >= n)
            return 0;
        out[i] = rel[i] == '\\' ? '/' : (char)ascii_lower((unsigned char)rel[i]);
    }
    out[i] = 0;
    return 1;
}

/* The mod subfolders of the folder being scanned: its smallest names (byte
 * order), however the system lists them, one more than room is left for so
 * that "too many" can be told. */
static char s_cand[BO3_MODFS_MAX_MODS + 1][MODFS_NAME_MAX + 1];

static void keep_smallest(int *n, int cap, const char *name)
{
    int i = *n;
    if (i == cap && strcmp(name, s_cand[cap - 1]) >= 0)
        return;
    if (i == cap)
        i--;
    for (; i > 0 && strcmp(name, s_cand[i - 1]) < 0; i--)
        memcpy(s_cand[i], s_cand[i - 1], sizeof s_cand[0]);
    memcpy(s_cand[i], name, strlen(name) + 1);
    if (*n < cap)
        (*n)++;
}

#if defined(_WIN32)
/* Subfolders of s_dirs[r] (not reparse points, plain names) that hold an entry
 * mod.toml, into s_cand: their number, or -1 when the folder cannot be listed
 * or has more than MODFS_MAX_ENTRIES entries. *total: how many there are. */
static int scan_root(int r, int cap, int *total)
{
    char pat[MAX_PATH], p[MAX_PATH];
    WIN32_FIND_DATAA fd, md;
    HANDLE h, m;
    int n = 0, entries = 0;

    *total = 0;
    if (snprintf(pat, sizeof pat, "%s%s*", s_dirs[r], ends_in_sep(s_dirs[r]) ? "" : "\\") >= (int)sizeof pat)
        return -1;
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    do {
        const char *nm = fd.cFileName;
        size_t len = strlen(nm);
        if (!strcmp(nm, ".") || !strcmp(nm, ".."))
            continue;
        if (++entries > MODFS_MAX_ENTRIES) {
            n = -1;
            break;
        }
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                || !part_ok(nm, len) || len > MODFS_NAME_MAX)
            continue;
        if (snprintf(p, sizeof p, "%s%s%s\\mod.toml", s_dirs[r], ends_in_sep(s_dirs[r]) ? "" : "\\", nm) >= (int)sizeof p)
            continue;
        /* spelled mod.toml exactly, as elsewhere (MOD.TOML is not a manifest) */
        m = FindFirstFileA(p, &md);
        if (m == INVALID_HANDLE_VALUE)
            continue;
        FindClose(m);
        if (strcmp(md.cFileName, "mod.toml"))
            continue;
        (*total)++;
        keep_smallest(&n, cap, nm);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

/* <s_dirs[r]>\<rel> opened as itself (a reparse point is not followed), for
 * listing and without sharing delete: while it is open, nothing can rename it
 * or put a junction in its place (an open that only reads attributes would
 * not take part in the sharing check). INVALID_HANDLE_VALUE unless it is a
 * folder. */
static HANDLE pin_dir(int r, const char *rel)
{
    char p[MAX_PATH];
    BY_HANDLE_FILE_INFORMATION fi;
    HANDLE h;

    if (snprintf(p, sizeof p, "%s%s%s", s_dirs[r], ends_in_sep(s_dirs[r]) ? "" : "\\", rel) >= (int)sizeof p)
        return INVALID_HANDLE_VALUE;
    h = CreateFileA(p, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h != INVALID_HANDLE_VALUE && (!GetFileInformationByHandle(h, &fi)
            || (fi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))) {
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
    return h;
}

/* mod.toml of the mod subfolder sub of s_dirs[r], at most
 * BO3_MANIFEST_MAX_BYTES + 1 bytes into buf. 0 with *why when it cannot be
 * read, is not a regular file, or it or the subfolder is a reparse point. */
static int read_manifest(int r, const char *sub, char *buf, size_t *len, const char **why)
{
    char p[MAX_PATH];
    BY_HANDLE_FILE_INFORMATION fi;
    DWORD got;
    HANDLE d, h;
    int ok = 1;

    *len = 0;
    *why = "mod.toml cannot be read";
    d = pin_dir(r, sub);
    if (d == INVALID_HANDLE_VALUE) {
        *why = "the mod folder is not a folder";
        return 0;
    }
    if (snprintf(p, sizeof p, "%s%s%s\\mod.toml", s_dirs[r], ends_in_sep(s_dirs[r]) ? "" : "\\", sub) >= (int)sizeof p) {
        CloseHandle(d);
        return 0;
    }
    got = GetFileAttributesA(p);   /* for the reason only; the open file is checked again below */
    if (got != INVALID_FILE_ATTRIBUTES
            && (got & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))) {
        *why = "mod.toml is not a regular file";
        CloseHandle(d);
        return 0;
    }
    h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        CloseHandle(d);
        return 0;
    }
    if (GetFileType(h) != FILE_TYPE_DISK || !GetFileInformationByHandle(h, &fi)
            || (fi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))) {
        *why = "mod.toml is not a regular file";
        ok = 0;
    }
    while (ok && *len < BO3_MANIFEST_MAX_BYTES + 1) {
        if (!ReadFile(h, buf + *len, (DWORD)(BO3_MANIFEST_MAX_BYTES + 1 - *len), &got, NULL)) {
            ok = 0;
            break;
        }
        if (!got)
            break;
        *len += got;
    }
    CloseHandle(h);
    CloseHandle(d);
    return ok;
}

/* A mod's files folder: the one entry of <sub> named "files" without regard to
 * case, its spelling into name (MODFS_NAME_MAX + 1 bytes). 1: found; 0: the
 * mod has none; -1: two or more spellings, or not a plain folder (*why). */
static int find_files(int r, const char *sub, char *name, const char **why)
{
    char pat[MAX_PATH], rel[2 * MODFS_NAME_MAX + 2];
    WIN32_FIND_DATAA fd;
    HANDLE h, d;
    int hits = 0;

    *why = NULL;
    if (snprintf(pat, sizeof pat, "%s%s%s\\*", s_dirs[r], ends_in_sep(s_dirs[r]) ? "" : "\\", sub) >= (int)sizeof pat)
        return 0;
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (same_nocase(fd.cFileName, "files")) {
            hits++;
            memcpy(name, fd.cFileName, sizeof "files");
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                hits = 99;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (!hits)
        return 0;
    if (hits > 1) {
        *why = hits == 99 ? "files is a link or not a folder" : "files is spelled twice";
        return -1;
    }
    snprintf(rel, sizeof rel, "%s\\%s", sub, name);
    d = pin_dir(r, rel);
    if (d == INVALID_HANDLE_VALUE) {
        *why = "files is a link or not a folder";
        return -1;
    }
    CloseHandle(d);
    return 1;
}

/* The files below <dir> (a full path) into the list, as "<rel>/<name>" in
 * lower case; reparse points, names that are not plain and other entries are
 * not listed (a lookup through them is refused anyway). */
static void index_dir(const char *dir, const char *rel, int depth, file_list *l)
{
    char pat[MAX_PATH], sub[MAX_PATH], r2[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    if (snprintf(pat, sizeof pat, "%s\\*", dir) >= (int)sizeof pat || (h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE)
        return;
    do {
        const char *nm = fd.cFileName;
        if (!strcmp(nm, ".") || !strcmp(nm, "..") || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                || !part_ok(nm, strlen(nm)))
            continue;
        if (snprintf(r2, sizeof r2, "%s%s%s", rel, *rel ? "/" : "", nm) >= (int)sizeof r2)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth + 1 > MODFS_INDEX_DEPTH) {
                l->incomplete = 1;
                continue;
            }
            if (snprintf(sub, sizeof sub, "%s\\%s", dir, nm) < (int)sizeof sub)
                index_dir(sub, r2, depth + 1, l);
        } else {
            list_add(l, r2);
        }
    } while (!l->incomplete && FindNextFileA(h, &fd));
    FindClose(h);
}

static void index_mod(int r, const char *prefix, file_list *l)
{
    char dir[MAX_PATH];
    if (snprintf(dir, sizeof dir, "%s%s%s", s_dirs[r], ends_in_sep(s_dirs[r]) ? "" : "\\", prefix) >= (int)sizeof dir) {
        l->incomplete = 1;
        return;
    }
    for (char *p = dir; *p; p++)
        if (*p == '/')
            *p = '\\';
    index_dir(dir, "", 0, l);
}

/* The folder's identity (volume, file index): the same folder reached by two
 * spellings (a junction, a short 8.3 name) is told as one. 0 when unknown. */
static int folder_id(const char *full, unsigned long long id[2])
{
    BY_HANDLE_FILE_INFORMATION fi;
    HANDLE h = CreateFileA(full, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = GetFileInformationByHandle(h, &fi);
    CloseHandle(h);
    if (!ok)
        return 0;
    id[0] = fi.dwVolumeSerialNumber;
    id[1] = ((unsigned long long)fi.nFileIndexHigh << 32) | fi.nFileIndexLow;
    return 1;
}
#else
static int each_entry(int d, void (*fn)(const char *name, void *ctx), void *ctx);

static void exact_toml(const char *nm, void *ctx)
{
    if (!strcmp(nm, "mod.toml"))
        *(int *)ctx = 1;
}

static int scan_root(int r, int cap, int *total)
{
    struct dirent *de;
    DIR *dir;
    int d, fd, n = 0, entries = 0;

    *total = 0;
    d = open(s_dirs[r], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (d < 0)
        return -1;
    fd = fcntl(d, F_DUPFD_CLOEXEC, 0);   /* fdopendir takes it over */
    dir = fd >= 0 ? fdopendir(fd) : NULL;
    if (!dir) {
        if (fd >= 0)
            close(fd);
        close(d);
        return -1;
    }
    while ((de = readdir(dir)) != NULL) {
        size_t len = strlen(de->d_name);
        struct stat st;
        int sub;

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (++entries > MODFS_MAX_ENTRIES) {
            n = -1;
            break;
        }
        if (!part_ok(de->d_name, len) || len > MODFS_NAME_MAX
                || fstatat(d, de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode))
            continue;
        sub = openat(d, de->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (sub < 0)
            continue;
        if (fstatat(sub, "mod.toml", &st, AT_SYMLINK_NOFOLLOW) == 0) {
            /* spelled mod.toml exactly, also on a volume that folds case
             * (MOD.TOML is not a manifest on any system) */
            int exact = 0;
            each_entry(sub, exact_toml, &exact);
            if (exact) {
                (*total)++;
                keep_smallest(&n, cap, de->d_name);
            }
        }
        close(sub);
    }
    closedir(dir);
    close(d);
    return n;
}

static int read_manifest(int r, const char *sub, char *buf, size_t *len, const char **why)
{
    struct stat st;
    ssize_t k = 0;
    int d, s, f;

    *len = 0;
    *why = "mod.toml cannot be read";
    d = open(s_dirs[r], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (d < 0)
        return 0;
    s = openat(d, sub, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(d);
    if (s < 0) {
        *why = "the mod folder is not a folder";
        return 0;
    }
    if (fstatat(s, "mod.toml", &st, AT_SYMLINK_NOFOLLOW) != 0) {
        close(s);
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        *why = "mod.toml is not a regular file";
        close(s);
        return 0;
    }
    /* O_NONBLOCK: a file swapped for a pipe since the check does not hang
     * the start; the type is checked again on the open file. */
    f = openat(s, "mod.toml", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    close(s);
    if (f < 0)
        return 0;
    if (fstat(f, &st) != 0 || !S_ISREG(st.st_mode)) {
        *why = "mod.toml is not a regular file";
        close(f);
        return 0;
    }
    while (*len < BO3_MANIFEST_MAX_BYTES + 1
           && (k = read(f, buf + *len, BO3_MANIFEST_MAX_BYTES + 1 - *len)) > 0)
        *len += (size_t)k;
    close(f);
    return k >= 0;
}

/* The entries of the open directory d, one call of fn each (not "." / ".."). */
static int each_entry(int d, void (*fn)(const char *name, void *ctx), void *ctx)
{
    struct dirent *de;
    DIR *dir;
    int fd = fcntl(d, F_DUPFD_CLOEXEC, 0);   /* fdopendir takes it over */

    dir = fd >= 0 ? fdopendir(fd) : NULL;
    if (!dir) {
        if (fd >= 0)
            close(fd);
        return 0;
    }
    while ((de = readdir(dir)) != NULL)
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, ".."))
            fn(de->d_name, ctx);
    closedir(dir);
    return 1;
}

typedef struct {
    int hits;
    char *name;
} files_scan;

static void files_entry(const char *nm, void *ctx)
{
    files_scan *s = ctx;
    if (same_nocase(nm, "files") && s->hits++ == 0)
        memcpy(s->name, nm, sizeof "files");
}

static int find_files(int r, const char *sub, char *name, const char **why)
{
    files_scan s = { 0, name };
    struct stat st;
    int d, m, f, ret = 0;

    *why = NULL;
    d = open(s_dirs[r], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (d < 0)
        return 0;
    m = openat(d, sub, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(d);
    if (m < 0)
        return 0;
    each_entry(m, files_entry, &s);
    if (s.hits > 1) {
        *why = "files is spelled twice";
        ret = -1;
    } else if (s.hits == 1) {
        if (fstatat(m, name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode)
                || (f = openat(m, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) < 0) {
            *why = "files is a link or not a folder";
            ret = -1;
        } else {
            close(f);
            ret = 1;
        }
    }
    close(m);
    return ret;
}

typedef struct {
    int d, depth;
    const char *rel;
    file_list *l;
} index_ctx;

static void index_dir(int d, const char *rel, int depth, file_list *l);

static void index_entry(const char *nm, void *ctx)
{
    index_ctx *c = ctx;
    struct stat st;
    char r2[MAX_PATH];
    int sub;

    if (c->l->incomplete || !part_ok(nm, strlen(nm)) || fstatat(c->d, nm, &st, AT_SYMLINK_NOFOLLOW) != 0
            || snprintf(r2, sizeof r2, "%s%s%s", c->rel, *c->rel ? "/" : "", nm) >= (int)sizeof r2)
        return;
    if (S_ISREG(st.st_mode)) {
        list_add(c->l, r2);
    } else if (S_ISDIR(st.st_mode)) {
        if (c->depth + 1 > MODFS_INDEX_DEPTH) {
            c->l->incomplete = 1;
            return;
        }
        sub = openat(c->d, nm, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (sub >= 0) {
            index_dir(sub, r2, c->depth + 1, c->l);
            close(sub);
        }
    }
}

/* The files below the open directory d into the list, as "<rel>/<name>" in
 * lower case; links, names that are not plain and other entries are not
 * listed (a lookup through them is refused anyway). */
static void index_dir(int d, const char *rel, int depth, file_list *l)
{
    index_ctx c = { d, depth, rel, l };
    each_entry(d, index_entry, &c);
}

static void index_mod(int r, const char *prefix, file_list *l)
{
    int d = open(s_dirs[r], O_RDONLY | O_DIRECTORY | O_CLOEXEC), f;
    if (d < 0) {
        l->incomplete = 1;
        return;
    }
    f = openat(d, prefix, O_RDONLY | O_DIRECTORY | O_CLOEXEC);   /* both parts checked by find_files */
    close(d);
    if (f < 0) {
        l->incomplete = 1;
        return;
    }
    index_dir(f, "", 0, l);
    close(f);
}

/* The folder's identity (device, inode): two names of one folder (a bind
 * mount, a link the full path did not show) are told as one. */
static int folder_id(const char *full, unsigned long long id[2])
{
    struct stat st;
    if (stat(full, &st) != 0)
        return 0;
    id[0] = (unsigned long long)st.st_dev;
    id[1] = (unsigned long long)st.st_ino;
    return 1;
}
#endif

static int by_root_then_id(const void *a, const void *b)
{
    const bo3_mod *x = a, *y = b;
    if (x->root != y->root)
        return x->root - y->root;
    if (x->usable != y->usable)
        return y->usable - x->usable;          /* usable first */
    return x->usable ? strcmp(x->manifest.id, y->manifest.id) : strcmp(x->folder, y->folder);
}

/* The subfolder's name (a pointer into folder). */
static const char *mod_sub(const bo3_mod *m)
{
    return m->folder + strlen(s_dirs[m->root]) + (ends_in_sep(s_dirs[m->root]) ? 0 : 1);
}

/* Reasons can quote the file (the TOML reader shows the byte it stopped at):
 * printable ASCII only in what the log and the list carry. */
static void scrub(char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E)
            *s = '?';
}

/* The shortest disc path a mod is likely to supply, "Data\x.bin" and the like:
 * a mod folder that leaves less room than this below files\ is refused, rather
 * than listed and never used (MODFS_HOST_MAX). A longer path that does not fit
 * is refused at the lookup, as for loose files. */
#define MODFS_MIN_REL 10

static void find_mods(int r)
{
    static char buf[BO3_MANIFEST_MAX_BYTES + 1];
    int total, k;

    if (s_dup[r])
        return;
    k = scan_root(r, BO3_MODFS_MAX_MODS - s_nmods + 1, &total);   /* s_cand has one spare */
    if (k < 0) {
        XR_NOTICE("[MOD] folder %d: not searched for mods (cannot be listed, or more than %d entries)\n",
                  r + 1, MODFS_MAX_ENTRIES);
        return;
    }
    for (int i = 0; i < k; i++) {
        bo3_mod *m = &s_mods[s_nmods];
        char files[MODFS_NAME_MAX + 1];
        const char *why;
        size_t len;

        if (s_nmods == BO3_MODFS_MAX_MODS) {
            XR_NOTICE("[MOD] more than %d mods: %s%s%s and %d more ignored\n", BO3_MODFS_MAX_MODS,
                      s_dirs[r], ends_in_sep(s_dirs[r]) ? "" : SEP_STR, s_cand[i], total - i - 1);
            break;
        }
        memset(m, 0, sizeof *m);
        m->root = r;
        if (snprintf(m->folder, sizeof m->folder, "%s%s%s", s_dirs[r], ends_in_sep(s_dirs[r]) ? "" : SEP_STR,
                     s_cand[i]) >= (int)sizeof m->folder)
            continue;   /* cannot happen: s_dirs < MAX_PATH, names <= NAME_MAX */
        s_nmods++;
        if (strlen(m->folder) + sizeof "\\files\\" - 1 + MODFS_MIN_REL >= MODFS_HOST_MAX) {
            snprintf(m->problem, sizeof m->problem, "its path is too long for files below it");
            continue;
        }
        if (!read_manifest(r, s_cand[i], buf, &len, &why)) {
            snprintf(m->problem, sizeof m->problem, "%s", why);
            continue;
        }
        if (!bo3_manifest_parse(buf, len, &m->manifest, m->problem, sizeof m->problem)) {
            scrub(m->problem);
            continue;
        }
        if (find_files(r, s_cand[i], files, &why) < 0) {
            snprintf(m->problem, sizeof m->problem, "%s", why);
            continue;
        }
        m->usable = 1;
    }
}

/* The source of a usable mod: its files folder in its own spelling and the
 * list of its files, read once here. A mod without files/ has none. */
static void add_mod_source(int r, int i)
{
    source *s = &s_src[s_nsrc];
    file_list l = { 0 };
    char files[MODFS_NAME_MAX + 1];
    const char *why;

    if (find_files(r, mod_sub(&s_mods[i]), files, &why) != 1)
        return;
    s->root = r;
    s->mod = i;
    snprintf(s->prefix, sizeof s->prefix, "%s/%s", mod_sub(&s_mods[i]), files);
    index_mod(r, s->prefix, &l);
    if (l.incomplete) {
        list_free(l.v, l.n);
        s->listed = 0;
        s->files = NULL;
        s->nfiles = 0;
        XR_NOTICE("[MOD] mod %s: no list of its files (more than %d files, folders more than %d deep, or out of "
                  "memory): looked up on disk for every file\n",
                  s_mods[i].manifest.id, MODFS_INDEX_FILES, MODFS_INDEX_DEPTH);
    } else {
        if (l.n)
            qsort(l.v, (size_t)l.n, sizeof *l.v, by_string);
        s->listed = 1;
        s->files = l.v;
        s->nfiles = l.n;
    }
    s_nsrc++;
}

/* ── The mod list: which mods are off ───────────────────────────────────
 * mod-list.toml (bo3_modfs_state_file), in the player's settings folder,
 * never in a mod folder: disabled = ["id", ...]. A mod it does not name is
 * on, so no file means every mod on. Ids of mods that are not there stay in
 * it. Read at the start when there is a mod, else at the first call that
 * needs it; written whole when bo3_modfs_set_enabled changes it: to a new
 * temporary file, flushed, then renamed over it. Guarded by its own lock,
 * so a save never holds up a lookup. */
#define MODFS_STATE_MAX  (64 * 1024)
#define MODFS_STATE_PATH 1100

static char s_state[MODFS_STATE_PATH];                      /* "" none */
static char s_off[BO3_MODFS_MAX_OFF][BO3_MOD_ID_MAX + 1];   /* off from the next start */
static char s_off_undo[BO3_MODFS_MAX_OFF][BO3_MOD_ID_MAX + 1];
static int  s_noff;
static int  s_state_read;  /* 1 read (or there is none), 0 not yet, -1 could not be read: tried again when needed */
static int  s_state_bad;   /* there, but not a mod list: moved to .bad by the first write */
static int  s_state_said;  /* "cannot be read" logged this session */

static int is_off(const char *id)
{
    for (int i = 0; id[0] && i < s_noff; i++)
        if (!strcmp(s_off[i], id))
            return 1;
    return 0;
}

#if defined(_WIN32)
/* The settings folder's path is UTF-8 (the player's name can be in it). */
static int widen(const char *s, wchar_t *w)
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, MODFS_STATE_PATH + 8) > 0;
}

/* 1 read, 0 no such file, -1 could not be read now, -2 not a mod list (not a
 * plain file, or larger than MODFS_STATE_MAX). */
static int state_read(const char *path, char **text, size_t *n)
{
    wchar_t w[MODFS_STATE_PATH + 8];
    LARGE_INTEGER size;
    DWORD got;
    HANDLE h;
    int r = -2;

    *text = NULL;
    *n = 0;
    if (!widen(path, w))
        return -1;
    h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? 0 : -1;
    }
    if (GetFileType(h) == FILE_TYPE_DISK && GetFileSizeEx(h, &size) && size.QuadPart <= MODFS_STATE_MAX) {
        r = -1;
        if ((*text = malloc((size_t)size.QuadPart + 1)) != NULL) {
            while (*n < (size_t)size.QuadPart
                   && ReadFile(h, *text + *n, (DWORD)((size_t)size.QuadPart - *n), &got, NULL) && got)
                *n += got;
            r = *n == (size_t)size.QuadPart ? 1 : -1;
        }
    }
    CloseHandle(h);
    return r;
}

/* path made anew (an old one, or a link planted there, removed first) with
 * text, flushed to the disk. */
static int state_new_file(const char *path, const char *text, size_t n)
{
    wchar_t w[MODFS_STATE_PATH + 8];
    DWORD put;
    HANDLE h;
    int ok;

    if (!widen(path, w))
        return 0;
    SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL);   /* a read-only one left behind is removed too */
    DeleteFileW(w);
    h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(h, text, (DWORD)n, &put, NULL) && put == n && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok)
        DeleteFileW(w);
    return ok;
}

/* from renamed over to. A virus scanner or an indexer holding either file for
 * a moment is waited out (a few tries). missing_ok: no from is success. */
static int state_move(const char *from, const char *to, int missing_ok)
{
    wchar_t wf[MODFS_STATE_PATH + 8], wt[MODFS_STATE_PATH + 8];
    if (!widen(from, wf) || !widen(to, wt))
        return 0;
    for (int tries = 0; tries < 5; tries++) {
        DWORD e;
        if (MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return 1;
        e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND)
            return missing_ok;
        if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED && e != ERROR_LOCK_VIOLATION)
            return 0;
        Sleep(20);
    }
    return 0;
}

static void state_remove(const char *path)
{
    wchar_t w[MODFS_STATE_PATH + 8];
    if (widen(path, w))
        DeleteFileW(w);
}
#else
static int state_read(const char *path, char **text, size_t *n)
{
    struct stat st;
    ssize_t k;
    int f = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC), r = -2;

    *text = NULL;
    *n = 0;
    if (f < 0)
        return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
    if (fstat(f, &st) == 0 && S_ISREG(st.st_mode) && st.st_size <= MODFS_STATE_MAX) {
        r = -1;
        if ((*text = malloc((size_t)st.st_size + 1)) != NULL) {
            while (*n < (size_t)st.st_size && (k = read(f, *text + *n, (size_t)st.st_size - *n)) > 0)
                *n += (size_t)k;
            r = *n == (size_t)st.st_size ? 1 : -1;
        }
    }
    close(f);
    return r;
}

static int state_new_file(const char *path, const char *text, size_t n)
{
    size_t put = 0;
    ssize_t k;
    int f, ok;

    unlink(path);   /* an old one, or a link planted there: O_EXCL makes a new file or fails */
    f = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (f < 0)
        return 0;
    while (put < n && (k = write(f, text + put, n - put)) > 0)
        put += (size_t)k;
    ok = put == n && fsync(f) == 0;
    ok = close(f) == 0 && ok;
    if (!ok)
        unlink(path);
    return ok;
}

static int state_move(const char *from, const char *to, int missing_ok)
{
    char dir[MODFS_STATE_PATH];
    const char *sl = strrchr(to, '/');
    int f;

    if (rename(from, to) != 0)
        return errno == ENOENT && missing_ok;
    snprintf(dir, sizeof dir, "%.*s", sl ? (sl == to ? 1 : (int)(sl - to)) : 1, sl ? to : ".");
    if ((f = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) >= 0) {   /* the rename itself to the disk */
        fsync(f);
        close(f);
    }
    return 1;
}

static void state_remove(const char *path)
{
    unlink(path);
}
#endif

/* A table key ([key] with keys in it, or key.x = ...): the reader names its
 * keys, not the table. */
static int has_table(const recomp_cfg *cfg, const char *key)
{
    size_t n = strlen(key);
    for (size_t i = 0; i < recomp_cfg_count(cfg); i++) {
        const char *k = recomp_cfg_key_at(cfg, i);
        if (k && !strncmp(k, key, n) && k[n] == '.')
            return 1;
    }
    return 0;
}

/* Under the mod list's lock. */
static void read_state(void)
{
    char why[160], *text;
    size_t n;
    recomp_cfg *cfg = NULL;
    recomp_cfg_type t;
    int r, skipped = 0, over = 0;

    s_noff = 0;
    s_state_bad = 0;
    s_state_read = 1;
    if (!s_state[0])
        return;
    r = state_read(s_state, &text, &n);
    if (r == 0)
        return;
    if (r == -1) {
        if (!s_state_said)   /* once a session: it is tried again at each call */
            XR_NOTICE("[MOD] mod list cannot be read (another program may have it open): %s: taken as every mod on; "
                      "not saved until it can be read\n", s_state);
        s_state_said = 1;
        s_state_read = -1;
        free(text);
        return;
    }
    why[0] = 0;
    if (r < 0)
        snprintf(why, sizeof why, "not a file, or larger than %d bytes", MODFS_STATE_MAX);
    else if ((text[n] = 0, memchr(text, 0, n)) || strstr(text, "\\u0000") || strstr(text, "\\U00000000"))
        snprintf(why, sizeof why, "a NUL byte or \\u0000");
    else if (!(cfg = recomp_cfg_load_string(text, "mod-list.toml", why, sizeof why)))
        scrub(why);
    else if ((t = recomp_cfg_type_of(cfg, "disabled")) != RCFG_NONE ? t != RCFG_ARRAY : has_table(cfg, "disabled"))
        snprintf(why, sizeof why, "disabled must be a list of ids");
    if (why[0] || !cfg) {
        XR_NOTICE("[MOD] mod list not used (%s): %s: every mod is on\n", why[0] ? why : "not TOML", s_state);
        s_state_bad = 1;
    } else {
        for (size_t i = 0; i < recomp_cfg_array_len(cfg, "disabled"); i++) {
            const char *id = recomp_cfg_array_string(cfg, "disabled", i, "");
            if (recomp_cfg_array_type(cfg, "disabled", i) != RCFG_STRING || !bo3_manifest_id_ok(id))
                skipped++;
            else if (is_off(id))
                ;
            else if (s_noff == BO3_MODFS_MAX_OFF)
                over++;
            else
                memcpy(s_off[s_noff++], id, strlen(id) + 1);
        }
        if (skipped)
            XR_NOTICE("[MOD] mod list: %d entries that are not mod ids skipped (dropped when it is saved): %s\n",
                      skipped, s_state);
        if (over)
            XR_NOTICE("[MOD] mod list: more than %d ids, %d ignored (dropped when it is saved): %s\n",
                      BO3_MODFS_MAX_OFF, over, s_state);
    }
    recomp_cfg_free(cfg);
    free(text);
}

/* Under the mod list's lock: read when it was not (yet); 1 when it is known. */
static int state_ready(void)
{
    if (s_state_read <= 0)
        read_state();
    return s_state_read > 0;
}

/* Under the mod list's lock. The old file stays until the new one is
 * complete on the disk; one that was not a mod list is kept as .bad. */
static int write_state(void)
{
    size_t cap = 128 + (size_t)s_noff * (BO3_MOD_ID_MAX + 4), n;
    char *text = malloc(cap), tmp[MODFS_STATE_PATH + 8], bad[MODFS_STATE_PATH + 8];
    int ok = 0, moved = 0;

    if (!text)
        goto out;
    n = (size_t)snprintf(text, cap, "# Takedawn: mods that are off, by id (docs/mod-folders.md). Read at start.\n"
                                    "disabled = [");
    for (int i = 0; i < s_noff; i++)
        n += (size_t)snprintf(text + n, cap - n, "%s\"%s\"", i ? ", " : "", s_off[i]);
    n += (size_t)snprintf(text + n, cap - n, "]\n");
    snprintf(tmp, sizeof tmp, "%s.tmp", s_state);
    snprintf(bad, sizeof bad, "%s.bad", s_state);
    if (!state_new_file(tmp, text, n))
        goto out;
    if (s_state_bad && !(moved = state_move(s_state, bad, 1))) {
        state_remove(tmp);
        goto out;
    }
    if (!state_move(tmp, s_state, 0)) {
        state_remove(tmp);
        if (moved && !state_move(bad, s_state, 0)) {   /* put it back: the file as it was */
            XR_NOTICE("[MOD] mod list not saved, and the old one is left as %s.bad\n", s_state);
            free(text);
            return 0;
        }
        goto out;
    }
    s_state_bad = 0;
    ok = 1;
out:
    if (!ok)
        XR_NOTICE("[MOD] mod list not saved (it is as it was): %s\n", s_state);
    free(text);
    return ok;
}

static void list_mods(void)
{
    /* A shared id refuses every copy: no order decides which one is used. */
    for (int i = 0; i < s_nmods; i++)
        for (int j = i + 1; j < s_nmods; j++)
            if (s_mods[i].manifest.id[0] && !strcmp(s_mods[i].manifest.id, s_mods[j].manifest.id)) {
                if (s_mods[i].usable)
                    snprintf(s_mods[i].problem, sizeof s_mods[i].problem, "id %.64s is also used by %.70s",
                             s_mods[i].manifest.id, s_mods[j].folder);
                if (s_mods[j].usable)
                    snprintf(s_mods[j].problem, sizeof s_mods[j].problem, "id %.64s is also used by %.70s",
                             s_mods[j].manifest.id, s_mods[i].folder);
                s_mods[i].usable = s_mods[j].usable = 0;
            }
    qsort(s_mods, (size_t)s_nmods, sizeof s_mods[0], by_root_then_id);
    for (int i = 0; i < s_nmods; i++)
        s_mods[i].enabled = !is_off(s_mods[i].manifest.id);
    s_nsrc = 0;
    for (int r = 0; r < s_ndirs; r++) {
        if (s_dup[r])
            continue;   /* taken once, in its first place: its files and mods are there */
        s_src[s_nsrc].root = r;
        s_src[s_nsrc].mod = -1;
        s_src[s_nsrc].listed = 0;
        s_src[s_nsrc].files = NULL;
        s_src[s_nsrc].nfiles = 0;
        s_src[s_nsrc++].prefix[0] = 0;
        for (int i = 0; i < s_nmods; i++) {
            if (s_mods[i].root != r)
                continue;
            if (s_mods[i].usable) {
                if (s_mods[i].enabled)
                    add_mod_source(r, i);
                XR_NOTICE("[MOD] mod %s%s%s: %s%s\n", s_mods[i].manifest.id, s_mods[i].manifest.version[0] ? " " : "",
                          s_mods[i].manifest.version, s_mods[i].folder, s_mods[i].enabled ? "" : " (off)");
            } else {
                XR_NOTICE("[MOD] mod ignored (%s): %s\n", s_mods[i].problem, s_mods[i].folder);
            }
        }
    }
}

/* rel's first part names a mod subfolder of folder r: not one of its loose
 * files (a mod's own files are reached through its files/ only). */
static int is_mod_sub(int r, const char *rel)
{
    size_t n = strcspn(rel, "\\/");
    for (int i = 0; i < s_nmods; i++) {
        const char *sub = mod_sub(&s_mods[i]);
        if (s_mods[i].root == r && strlen(sub) == n) {
            size_t k = 0;
            while (k < n && ascii_lower((unsigned char)sub[k]) == ascii_lower((unsigned char)rel[k]))
                k++;
            if (k == n)
                return 1;
        }
    }
    return 0;
}

/* ── Conflicts: files more than one source has ──────────────────────────
 * Found once, at the start, for the log and the counts in bo3_mod: the files
 * of the enabled mods' lists, each looked for in every source in lookup
 * order, with the walk a lookup makes; the first one that has it is the one
 * used, as in redirect. Over MODFS_CONFLICT_WALK different files, nothing is
 * walked: mods with a list are compared by their lists alone, the others not.
 * Files that no enabled mod's list has are not looked at: two loose folders,
 * or a loose folder and a mod without a list, with the same file are not
 * told. */
#define MODFS_CONFLICT_WALK 1024
#define MODFS_CONFLICT_LINES 32

static int s_nconf;

/* Whether a lookup of key would be answered by s: its list says so first,
 * then the walk redirect makes decides (a listed file can still be refused:
 * a path too long, names that differ only in case). walk 0: the list only. */
static int source_has(const source *s, const char *key, int walk, int *ambiguous)
{
    char q[MAX_PATH + MODFS_NAME_MAX + 8], host[MAX_PATH];

    *ambiguous = 0;
    if (s->listed && (!s->nfiles || !bsearch(&key, s->files, (size_t)s->nfiles, sizeof *s->files, by_string)))
        return 0;
    if (s->listed && !walk)
        return 1;
    if (s->mod < 0 ? is_mod_sub(s->root, key) || snprintf(q, sizeof q, "%s", key) >= (int)sizeof q
                   : snprintf(q, sizeof q, "%s/%s", s->prefix, key) >= (int)sizeof q)
        return 0;
    return mod_file(s_dirs[s->root], q, host, MODFS_HOST_MAX, ambiguous);
}

static void source_name(const source *s, char *out, size_t n)
{
    if (s->mod < 0)
        snprintf(out, n, "folder %d", s->root + 1);
    else
        snprintf(out, n, "mod %s", s_mods[s->mod].manifest.id);
}

static void find_conflicts(void)
{
    const char **keys;
    size_t nk = 0, cap = 0, distinct = 0;
    int walk, more = 0;

    s_nconf = 0;
    for (int i = 0; i < s_nsrc; i++)
        if (s_src[i].listed)
            cap += (size_t)s_src[i].nfiles;
    if (!cap)
        return;
    keys = malloc(cap * sizeof *keys);
    if (!keys) {
        XR_NOTICE("[MOD] conflicts not looked for (out of memory)\n");
        return;
    }
    for (int i = 0; i < s_nsrc; i++)
        for (int f = 0; s_src[i].listed && f < s_src[i].nfiles; f++)
            keys[nk++] = s_src[i].files[f];
    qsort(keys, nk, sizeof *keys, by_string);
    for (size_t k = 0; k < nk; k++)
        distinct += !k || strcmp(keys[k], keys[k - 1]);
    walk = distinct <= MODFS_CONFLICT_WALK;
    if (!walk)
        XR_NOTICE("[MOD] mods have more than %d files: conflicts with loose files and with mods looked up on disk "
                  "not looked for\n", MODFS_CONFLICT_WALK);
    for (size_t k = 0; k < nk; k++) {
        char line[600], name[BO3_MOD_ID_MAX + 8];
        int first = -1, losers = 0;
        size_t len = 0;

        if (k && !strcmp(keys[k], keys[k - 1]))
            continue;
        for (int i = 0; i < s_nsrc; i++) {
            int amb, has;
            if (!s_src[i].listed && !walk)
                continue;
            has = source_has(&s_src[i], keys[k], walk, &amb);
            if (!has) {
                if (amb)
                    break;   /* the lookup stops here too */
                continue;
            }
            source_name(&s_src[i], name, sizeof name);
            if (first < 0) {
                first = i;
                len = (size_t)snprintf(line, sizeof line, "%s over", name);
                continue;
            }
            losers++;
            if (s_src[i].mod >= 0)
                s_mods[s_src[i].mod].loses++;
            if (len < sizeof line)
                len += (size_t)snprintf(line + len, sizeof line - len, "%s %s", losers > 1 ? "," : "", name);
        }
        if (!losers)
            continue;
        if (len >= sizeof line)
            memcpy(line + sizeof line - 4, "...", 4);   /* more places than the line holds */
        s_nconf++;
        if (s_src[first].mod >= 0)
            s_mods[s_src[first].mod].wins++;
        if (s_nconf <= MODFS_CONFLICT_LINES)
            XR_NOTICE("[MOD] conflict: %s: %s\n", keys[k], line);
        else
            more++;
    }
    if (more)
        XR_NOTICE("[MOD] ... and %d more conflicts\n", more);
    free(keys);
}

int bo3_modfs_mod_count(void)
{
    return s_nmods;
}

const bo3_mod *bo3_modfs_mod(int i)
{
    return i >= 0 && i < s_nmods ? &s_mods[i] : NULL;
}

void bo3_modfs_state_file(const char *path)
{
    if (path && strlen(path) + sizeof ".tmp" > sizeof s_state) {
        XR_NOTICE("[MOD] mod list path too long, no mod list: %.200s...\n", path);
        path = NULL;
    }
    snprintf(s_state, sizeof s_state, "%s", path ? path : "");
    s_state_read = 0;   /* read again, at the start or when needed */
}

int bo3_modfs_enabled_next(const char *id)
{
    int on = 1;
    if (!id || !s_lock_made)
        return 1;
    STATE_LOCK();
    if (state_ready())
        on = !is_off(id);
    STATE_UNLOCK();
    return on;
}

int bo3_modfs_set_enabled(const char *id, int on)
{
    int i, ok = 1;

    if (!bo3_manifest_id_ok(id) || !s_state[0] || !s_lock_made)
        return 0;
    STATE_LOCK();
    if (!state_ready()) {   /* never write a list that could not be read: it would lose its ids */
        STATE_UNLOCK();
        return 0;
    }
    for (i = 0; i < s_noff && strcmp(s_off[i], id); i++)
        ;
    if (on ? i < s_noff : i == s_noff) {   /* else it already is so: nothing to write */
        int n = s_noff;
        memcpy(s_off_undo, s_off, sizeof s_off);
        if (on) {
            memmove(s_off[i], s_off[i + 1], (size_t)(s_noff - i - 1) * sizeof s_off[0]);
            s_noff--;
        } else if (s_noff < BO3_MODFS_MAX_OFF) {
            memcpy(s_off[s_noff++], id, strlen(id) + 1);
        } else {
            ok = 0;
        }
        if (ok && !write_state()) {
            memcpy(s_off, s_off_undo, sizeof s_off);
            s_noff = n;
            ok = 0;
        }
    }
    STATE_UNLOCK();
    return ok;
}

int bo3_modfs_conflicts(void)
{
    return s_nconf;
}

/* host NULL: guest's file was refused because names in a folder differ only
 * in case. */
static void note_once(const char *guest, const char *host)
{
    int fresh = 1;
    MODFS_LOCK();
    for (int i = 0; i < s_nseen && fresh; i++)
        if (same_nocase(s_seen[i], guest))
            fresh = 0;
    if (fresh && s_nseen < MODFS_SEEN) {
        strncpy(s_seen[s_nseen], guest, sizeof s_seen[0] - 1);
        s_seen[s_nseen][sizeof s_seen[0] - 1] = 0;
        s_nseen++;
    }
    MODFS_UNLOCK();
    if (fresh) {
        if (host)
            fprintf(stderr, "[MOD] %s -> %s\n", guest, host);
        else
            fprintf(stderr, "[MOD] %s: refused, names in a mod folder differ only in case; "
                    "the disc's file is read\n", guest);
        fflush(stderr);
    }
}

static const char *redirect(const char *path)
{
    const char *rel = path ? strip_device(path) : NULL;

    if (rel && *rel) {
        for (int i = 0; i < s_nsrc; i++) {
            const source *s = &s_src[i];
            char host[MAX_PATH], in_mod[MAX_PATH + MODFS_NAME_MAX + 8];
            const char *q = rel;
            int ambiguous;

            if (s->mod < 0) {
                if (is_mod_sub(s->root, rel))
                    continue;
            } else {
                char key[MAX_PATH];
                const char *kp = key;
                if (s->listed && (!s->nfiles || !list_key(rel, key, sizeof key)
                        || !bsearch(&kp, s->files, (size_t)s->nfiles, sizeof *s->files, by_string)))
                    continue;   /* not one of the mod's files: no walk */
                if (snprintf(in_mod, sizeof in_mod, "%s/%s", s->prefix, rel) >= (int)sizeof in_mod)
                    continue;
                q = in_mod;
            }
            if (mod_file(s_dirs[s->root], q, host, MODFS_HOST_MAX, &ambiguous)) {
#if defined(_WIN32)
                snprintf(t_out, sizeof t_out, "\\\\?\\%s", host);
#else
                snprintf(t_out, sizeof t_out, "%s", host);
#endif
                note_once(path, host);
                return t_out;
            }
            if (ambiguous) {
                note_once(path, NULL);
                break;      /* not a later folder's file either: the disc's */
            }
        }
    }
    return s_prev ? s_prev(path) : NULL;
}

/* Folder r is the same folder as an earlier one, by identity rather than by
 * name (a junction, a short name or a bind mount names it again). It keeps its
 * place among the eight as before manifests, but its mods are only searched
 * once: otherwise every one of them would meet itself as a shared id. */
static unsigned long long s_fid[MODFS_MAX_DIRS][2];
static int same_as_earlier(int r)
{
    if (!folder_id(s_dirs[r], s_fid[r])) {
        s_fid[r][0] = s_fid[r][1] = ~0ull - (unsigned)r;   /* unknown: unlike every other */
        return 0;
    }
    for (int i = 0; i < r; i++)
        if (s_fid[i][0] == s_fid[r][0] && s_fid[i][1] == s_fid[r][1])
            return 1;
    return 0;
}

/* After xbox_path_init and the soundtrack's hook, before the title runs.
 * Called again (the unit test), it replaces the folder list; the hook is
 * installed once. */
void bo3_modfs_init(void)
{
    const char *e = getenv("TAKEDAWN_MODS");
    char buf[MODFS_MAX_DIRS * MAX_PATH];
#if defined(__ANDROID__)
    char app_mods[MAX_PATH];
    if (!e) {
        const char *app = getenv("RECOMP_ANDROID_DIR");
        struct stat st;
        if (app && *app && snprintf(app_mods, sizeof app_mods, "%s/mods", app) < (int)sizeof app_mods
                && stat(app_mods, &st) == 0 && S_ISDIR(st.st_mode))
            e = app_mods;
    }
#endif

#if defined(_WIN32)
    if (!s_lock_made) {
        InitializeCriticalSection(&s_lock);
        InitializeCriticalSection(&s_state_lock);
        s_lock_made = 1;
    }
#endif
    for (int i = 0; i < s_nsrc; i++)    /* a second call (the unit test) */
        list_free(s_src[i].files, s_src[i].nfiles);
    s_ndirs = 0;
    s_nseen = 0;
    s_nmods = 0;
    s_nsrc = 0;
    s_nconf = 0;
    s_noff = 0;            /* before the title runs: no other thread yet */
    s_state_read = 0;      /* the mod list: read below when there is a mod, else when asked */
    s_state_bad = 0;
    s_state_said = 0;
    if (!e || !*e)
        return;
    strncpy(buf, e, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    /* Split at ';' by hand: strtok is not thread-safe, and bionic marks it
     * deprecated. Empty items (";;") are skipped. */
    for (char *tok = buf, *end; *tok; tok = end) {
        end = tok + strcspn(tok, ";");
        if (*end)
            *end++ = 0;
        if (!*tok)
            continue;
        if (s_ndirs == MODFS_MAX_DIRS) {
            XR_NOTICE("[MOD] more than %d folders: %s and the rest ignored\n",
                      MODFS_MAX_DIRS, tok);
            break;
        }
        if (!mod_folder(tok, s_dirs[s_ndirs]))
            continue;
        s_dup[s_ndirs] = same_as_earlier(s_ndirs);
        s_ndirs++;
        XR_NOTICE("[MOD] folder %d: %s\n", s_ndirs, s_dirs[s_ndirs - 1]);
    }
    for (int r = 0; r < s_ndirs; r++)
        find_mods(r);
    if (s_nmods) {          /* the mod list is read at the start only when there is a mod */
        STATE_LOCK();
        read_state();
        STATE_UNLOCK();
    }
    list_mods();
    find_conflicts();
    fflush(stderr);
    if (!s_ndirs || xbox_PathRedirectHook == redirect)
        return;
    s_prev = xbox_PathRedirectHook;
    xbox_PathRedirectHook = redirect;
}
