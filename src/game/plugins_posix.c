/*
 * plugins_posix.c - loading a native plugin on Linux and macOS
 * (plugins_platform.h); Android builds say plugins are not supported.
 *
 * The bytes that are approved are the bytes that are loaded: the module is
 * opened without following a link (O_NOFOLLOW), checked to be a regular
 * file, and copied while it is hashed - the copy is what dlopen loads, never
 * the mod folder's file, which may change after it was read.
 *
 *   Linux: the copy is a memfd, sealed against every change (F_SEAL_WRITE,
 *   SHRINK, GROW, SEAL) before it is loaded, through /proc/self/fd/<n>. It
 *   has no name in any folder, and nobody can change it. A loaded copy's
 *   memfd stays open for the process: dlopen hands back an object it has
 *   already loaded for a name it has seen, so a closed and reused <n> would
 *   give a later plugin an earlier one's library. A kernel or a sandbox
 *   without memfd or /proc refuses the plugin (the log says why).
 *
 *   macOS: there is no memfd; the copy is a file in a folder of its own,
 *   made for it with mkdtemp in the user's own temporary folder (0700), left
 *   read only (0500), loaded by that path and removed afterwards. The path
 *   must still name that file (device, inode) after loading. Only the user's
 *   own processes can reach it - the same who can change the mod folder.
 *
 * dlopen(RTLD_NOW | RTLD_LOCAL): every symbol is resolved at once, none is
 * shared with what loads later. A module's own dependencies are found the
 * system's way (rpath, LD_LIBRARY_PATH, DYLD_*); $ORIGIN / @loader_path name
 * the copy's place, not the mod folder (docs/native-plugins.md).
 */
#include "plugins_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ANDROID__)

int bo3_plugin_sys_supported(void) { return 0; }
bo3_plugin_file *bo3_plugin_sys_hold(const char *path, uint8_t sha[32], char *why, size_t wn)
{
    (void)path;
    (void)sha;
    snprintf(why, wn, "this system does not load plugins");
    return NULL;
}
void *bo3_plugin_sys_load(bo3_plugin_file *f, char *why, size_t wn)
{
    (void)f;
    snprintf(why, wn, "this system does not load plugins");
    return NULL;
}
void bo3_plugin_sys_release(bo3_plugin_file *f) { (void)f; }
void *bo3_plugin_sys_symbol(void *lib, const char *name)
{
    (void)lib;
    (void)name;
    return NULL;
}

#else

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>    /* memfd_create through syscall(): no _GNU_SOURCE needed (the build force-includes stdio first) */
#endif

#include "xr_sha256.h"

#if defined(__linux__)
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif
#ifndef MFD_EXEC
#define MFD_EXEC 0x0010U            /* Linux 6.3: say the copy is to be run (vm.memfd_noexec) */
#endif
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#define F_SEAL_WRITE 0x0008
#endif

static int memfd(const char *name, unsigned flags)
{
#if defined(SYS_memfd_create)
    return (int)syscall(SYS_memfd_create, name, flags);
#else
    (void)name;
    (void)flags;
    errno = ENOSYS;
    return -1;
#endif
}
#endif

struct bo3_plugin_file {
    int copy;                       /* the copy that is loaded: a sealed memfd (Linux), the private file (macOS) */
    int loaded;                     /* Linux: the copy is loaded, its memfd stays open (below) */
    char load[4200];                /* what dlopen opens */
#if defined(__APPLE__)
    char dir[1024];                 /* the private folder, removed with the copy */
    struct stat st;                 /* the copy's identity */
#endif
};

/* Every library a plugin was given, so that dlopen handing back one it
 * already gave (a name it took for an object it had loaded) is refused. */
static void *s_libs[256];
static int s_nlibs;

int bo3_plugin_sys_supported(void)
{
    return 1;
}

static int write_all(int fd, const unsigned char *p, size_t n)
{
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        p += k;
        n -= (size_t)k;
    }
    return 1;
}

/* The copy that will be loaded, ready to be written: into f->copy. */
static int make_copy(bo3_plugin_file *f, char *why, size_t wn)
{
#if defined(__linux__)
    f->copy = memfd("takedawn-plugin", MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_EXEC);
    if (f->copy < 0 && errno == EINVAL)          /* a kernel before 6.3 does not know MFD_EXEC */
        f->copy = memfd("takedawn-plugin", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (f->copy < 0) {
        snprintf(why, wn, "no sealed copy can be made (memfd_create: %s)", strerror(errno));
        return 0;
    }
    snprintf(f->load, sizeof f->load, "/proc/self/fd/%d", f->copy);
    return 1;
#else
    const char *tmp = getenv("TMPDIR");
    char base[1024];
    size_t bl;
#if defined(_CS_DARWIN_USER_TEMP_DIR)
    if (confstr(_CS_DARWIN_USER_TEMP_DIR, base, sizeof base) > 0 && base[0] == '/')
        tmp = base;
#endif
    if (!tmp || tmp[0] != '/')
        tmp = "/tmp";
    bl = strlen(tmp);
    if (snprintf(f->dir, sizeof f->dir, "%s%stakedawn-plugin.XXXXXX", tmp, bl && tmp[bl - 1] == '/' ? "" : "/")
            >= (int)sizeof f->dir || !mkdtemp(f->dir)) {
        f->dir[0] = 0;
        snprintf(why, wn, "no private folder for the copy (%s)", strerror(errno));
        return 0;
    }
    snprintf(f->load, sizeof f->load, "%s/module.dylib", f->dir);
    f->copy = open(f->load, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0700);
    if (f->copy < 0) {
        snprintf(why, wn, "the private copy cannot be made (%s)", strerror(errno));
        return 0;
    }
    return 1;
#endif
}

/* The copy is complete: make it unchangeable (Linux) or read only (macOS). */
static int seal_copy(bo3_plugin_file *f, char *why, size_t wn)
{
#if defined(__linux__)
    if (fcntl(f->copy, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) != 0) {
        snprintf(why, wn, "the copy cannot be sealed (%s)", strerror(errno));
        return 0;
    }
    return 1;
#else
    int r;
    if (fsync(f->copy) != 0 || fchmod(f->copy, 0500) != 0 || fstat(f->copy, &f->st) != 0) {
        snprintf(why, wn, "the private copy cannot be finished (%s)", strerror(errno));
        return 0;
    }
    r = close(f->copy);
    f->copy = -1;
    if (r != 0) {
        snprintf(why, wn, "the private copy cannot be finished (%s)", strerror(errno));
        return 0;
    }
    return 1;
#endif
}

bo3_plugin_file *bo3_plugin_sys_hold(const char *path, uint8_t sha[32], char *why, size_t wn)
{
    bo3_plugin_file *f;
    struct stat st;
    xr_sha256 s;
    static unsigned char buf[1 << 16];
    ssize_t got;
    int src;

    if (path[0] != '/') {
        snprintf(why, wn, "the module path is not absolute");
        return NULL;
    }
    f = calloc(1, sizeof *f);
    if (!f) {
        snprintf(why, wn, "out of memory");
        return NULL;
    }
    f->copy = -1;
    src = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (src < 0) {
        snprintf(why, wn, "the module cannot be opened (%s)", strerror(errno));
        bo3_plugin_sys_release(f);
        return NULL;
    }
    if (fstat(src, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(why, wn, "the module is not a regular file");
        goto fail;
    }
    if (!make_copy(f, why, wn))
        goto fail;
    xr_sha256_init(&s);
    while ((got = read(src, buf, sizeof buf)) != 0) {
        if (got < 0) {
            if (errno == EINTR)
                continue;
            snprintf(why, wn, "the module cannot be read (%s)", strerror(errno));
            goto fail;
        }
        xr_sha256_update(&s, buf, (size_t)got);         /* what is hashed is what is copied */
        if (!write_all(f->copy, buf, (size_t)got)) {
            snprintf(why, wn, "the copy cannot be written (%s)", strerror(errno));
            goto fail;
        }
    }
    xr_sha256_final(&s, sha);
    close(src);
    src = -1;
    if (!seal_copy(f, why, wn))
        goto fail;
    return f;
fail:
    if (src >= 0)
        close(src);
    bo3_plugin_sys_release(f);
    return NULL;
}

void *bo3_plugin_sys_load(bo3_plugin_file *f, char *why, size_t wn)
{
    void *lib = dlopen(f->load, RTLD_NOW | RTLD_LOCAL);

    if (!lib) {
        const char *e = dlerror();
        snprintf(why, wn, "the module does not load (%s)", e ? e : "no reason given");
        return NULL;
    }
    for (int i = 0; i < s_nlibs; i++)
        if (s_libs[i] == lib) {
            snprintf(why, wn, "the system handed back another plugin's library for this module (it loaded nothing new)");
            return NULL;
        }
    if (s_nlibs < (int)(sizeof s_libs / sizeof s_libs[0]))
        s_libs[s_nlibs++] = lib;            /* every library handed out, kept or refused below: never given twice */
#if defined(__linux__)
    /* dlopen takes a name it has loaded for that object again: a copy's
     * memfd stays open, so its /proc/self/fd/<n> names no other copy. */
    f->loaded = 1;
#endif
#if defined(__APPLE__)
    {
        struct stat now;
        if (lstat(f->load, &now) != 0 || !S_ISREG(now.st_mode) || now.st_dev != f->st.st_dev || now.st_ino != f->st.st_ino) {
            snprintf(why, wn, "the private copy changed while it was loaded (its start-up code has run; it stays loaded)");
            return NULL;
        }
    }
#endif
    return lib;
}

void bo3_plugin_sys_release(bo3_plugin_file *f)
{
    if (!f)
        return;
    if (f->copy >= 0 && !f->loaded)
        close(f->copy);                 /* a loaded memfd stays open for the process (bo3_plugin_sys_load) */
#if defined(__APPLE__)
    if (f->dir[0]) {
        unlink(f->load);                /* a loaded copy stays mapped */
        rmdir(f->dir);
    }
#endif
    free(f);
}

void *bo3_plugin_sys_symbol(void *lib, const char *name)
{
    return dlsym(lib, name);
}

#endif
