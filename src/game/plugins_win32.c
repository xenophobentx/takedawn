/*
 * plugins_win32.c - loading a native plugin on Windows (plugins_platform.h).
 *
 * The bytes that are approved are the bytes that are loaded, before the
 * library's own start-up code runs:
 *
 *   1. the module is opened with only reading shared (nobody can write,
 *      replace, rename or delete it while it is held) and hashed through
 *      that handle;
 *   2. the handle names the file's real place (GetFinalPathNameByHandleW:
 *      every junction or link on the way resolved), and every folder of that
 *      path, from the drive down, is opened without delete sharing and
 *      checked to be a plain folder: none can be renamed, deleted or turned
 *      into a junction until the module is loaded;
 *   3. the held file's real place is asked again: it must still be that path
 *      (a folder swapped before it was pinned changes it);
 *   4. LoadLibraryExW loads that path, with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
 *      LOAD_LIBRARY_SEARCH_SYSTEM32 (its dependencies from its own folder or
 *      System32, never the current folder or PATH; nothing process-wide
 *      changes).
 *
 * After loading, the image the system mapped is checked once more against
 * the held file (the kernel's name for the mapped file, GetMappedFileNameW,
 * against the held handle's): a mismatch refuses the plugin, but by then the
 * library's start-up code has run - steps 1 to 3 are what keep that from
 * happening. A writable mapping of the file made before it was held is not
 * covered (see docs/native-plugins.md).
 */
#include "plugins_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <psapi.h>

#include "xr_sha256.h"

#define PATH_W 1024                     /* wide characters: a final path may be longer than MAX_PATH */

struct bo3_plugin_file {
    HANDLE h;                           /* the module, read only shared */
    HANDLE *pins;                       /* its folders, drive down, delete not shared */
    int npins;
    wchar_t real[PATH_W];               /* the real place, X:\..., what is loaded */
};

#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

int bo3_plugin_sys_supported(void)
{
    return 1;
}

static void sys_error(const char *what, char *why, size_t wn)
{
    DWORD e = GetLastError();
    char msg[160] = "";
    size_t n;

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, msg, sizeof msg, NULL);
    for (n = strlen(msg); n && (msg[n - 1] == '\n' || msg[n - 1] == '\r' || msg[n - 1] == ' ' || msg[n - 1] == '.'); n--)
        msg[n - 1] = 0;
    snprintf(why, wn, "%s (Windows error %lu%s%s)", what, (unsigned long)e, msg[0] ? ": " : "", msg);
}

/* The held file's real place as X:\... (no \\?\), or 0. */
static int real_path(HANDLE h, wchar_t *out, DWORD n)
{
    wchar_t buf[PATH_W];
    DWORD k = GetFinalPathNameByHandleW(h, buf, PATH_W, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const wchar_t *p = buf;

    if (k == 0 || k >= PATH_W)
        return 0;
    if (!wcsncmp(p, L"\\\\?\\", 4))
        p += 4;
    if (!(((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) && p[1] == L':' && p[2] == L'\\'))
        return 0;                       /* only a drive letter path: no \\?\UNC\, no volume GUID */
    if (wcslen(p) >= n)
        return 0;
    wcscpy(out, p);
    return 1;
}

/* One folder of the real path, opened without delete sharing: it cannot be
 * renamed or deleted (and so not replaced by a junction) while held. It must
 * be a folder and not a reparse point. */
static int pin(bo3_plugin_file *f, const wchar_t *dir, char *why, size_t wn)
{
    BY_HANDLE_FILE_INFORMATION fi;
    HANDLE d = CreateFileW(dir, FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);

    if (d == INVALID_HANDLE_VALUE) {
        sys_error("a folder of the module's path cannot be held", why, wn);
        return 0;
    }
    f->pins[f->npins++] = d;
    if (!GetFileInformationByHandle(d, &fi) || !(fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            || (fi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        snprintf(why, wn, "a folder of the module's real path is not a plain folder");
        return 0;
    }
    return 1;
}

/* Every folder of f->real ("X:\a\b\m.asi": X:\, X:\a, X:\a\b), the drive down. */
static int pin_folders(bo3_plugin_file *f, char *why, size_t wn)
{
    wchar_t part[PATH_W];
    size_t n = wcslen(f->real);

    f->pins = calloc(n, sizeof *f->pins);       /* at most one per character: enough */
    if (!f->pins) {
        snprintf(why, wn, "out of memory");
        return 0;
    }
    memcpy(part, f->real, 3 * sizeof(wchar_t));  /* "X:\" */
    part[3] = 0;
    if (!pin(f, part, why, wn))
        return 0;
    for (size_t i = 3; i < n; i++) {
        if (f->real[i] != L'\\')
            continue;
        memcpy(part, f->real, i * sizeof(wchar_t));
        part[i] = 0;
        if (!pin(f, part, why, wn))
            return 0;
    }
    return 1;
}

bo3_plugin_file *bo3_plugin_sys_hold(const char *path, uint8_t sha[32], char *why, size_t wn)
{
    bo3_plugin_file *f = calloc(1, sizeof *f);
    BY_HANDLE_FILE_INFORMATION fi;
    wchar_t wpath[PATH_W], again[PATH_W];
    xr_sha256 s;
    static unsigned char buf[1 << 16];
    DWORD got;

    if (!f) {
        snprintf(why, wn, "out of memory");
        return NULL;
    }
    f->h = INVALID_HANDLE_VALUE;
    if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, wpath, PATH_W)) {
        snprintf(why, wn, "the path cannot be used");
        goto fail;
    }
    /* FILE_FLAG_OPEN_REPARSE_POINT: a reparse point is opened as itself, and
     * refused below, never followed. Only reading is shared. */
    f->h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f->h == INVALID_HANDLE_VALUE) {
        sys_error("the module cannot be opened", why, wn);
        goto fail;
    }
    if (!GetFileInformationByHandle(f->h, &fi)
            || (fi.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))
            || GetFileType(f->h) != FILE_TYPE_DISK) {
        snprintf(why, wn, "the module is not a regular file");
        goto fail;
    }
    xr_sha256_init(&s);
    for (;;) {
        if (!ReadFile(f->h, buf, sizeof buf, &got, NULL)) {
            sys_error("the module cannot be read", why, wn);
            goto fail;
        }
        if (!got)
            break;
        xr_sha256_update(&s, buf, got);
    }
    xr_sha256_final(&s, sha);
    if (!real_path(f->h, f->real, PATH_W)) {
        snprintf(why, wn, "the module's real place is not a path on a drive letter");
        goto fail;
    }
    if (!pin_folders(f, why, wn))
        goto fail;
    if (!real_path(f->h, again, PATH_W) || wcscmp(again, f->real)) {
        snprintf(why, wn, "the module's folders changed while it was checked");
        goto fail;
    }
    return f;
fail:
    bo3_plugin_sys_release(f);
    return NULL;
}

void *bo3_plugin_sys_load(bo3_plugin_file *f, char *why, size_t wn)
{
    HMODULE m = LoadLibraryExW(f->real, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    wchar_t mapped[PATH_W], held[PATH_W];
    DWORD a, b;

    if (!m) {
        sys_error("the module does not load", why, wn);
        return NULL;
    }
    /* The kernel's name of the file behind the mapped image, against the
     * held handle's: both \Device\...; the file cannot have moved since. */
    a = K32GetMappedFileNameW(GetCurrentProcess(), (LPVOID)m, mapped, PATH_W);
    b = GetFinalPathNameByHandleW(f->h, held, PATH_W, FILE_NAME_NORMALIZED | VOLUME_NAME_NT);
    if (a == 0 || a >= PATH_W || b == 0 || b >= PATH_W || CompareStringOrdinal(mapped, -1, held, -1, TRUE) != CSTR_EQUAL) {
        snprintf(why, wn, "the system mapped another file than the one that was checked (its start-up code has run; "
                 "it stays loaded)");
        return NULL;
    }
    return (void *)m;
}

void bo3_plugin_sys_release(bo3_plugin_file *f)
{
    if (!f)
        return;
    for (int i = 0; i < f->npins; i++)
        CloseHandle(f->pins[i]);
    free(f->pins);
    if (f->h && f->h != INVALID_HANDLE_VALUE)
        CloseHandle(f->h);
    free(f);
}

void *bo3_plugin_sys_symbol(void *lib, const char *name)
{
    FARPROC p = GetProcAddress((HMODULE)lib, name);
    void *v;
    memcpy(&v, &p, sizeof v);   /* a function's address as data: the same size on Windows */
    return v;
}
