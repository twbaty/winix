/*
 * readlink — print value of a symbolic link or canonical file name
 *
 * Usage: readlink [OPTIONS] FILE
 *   -f  canonicalize: resolve all symlinks, make absolute (like realpath)
 *   -e  like -f but fail if any component doesn't exist
 *   -m  like -f but don't require path to exist
 *   -n  suppress trailing newline
 *   -q  suppress error messages (quiet)
 *   -v  verbose (report errors even with -q)
 *   --version / --help
 *
 * On Windows, symbolic links are NTFS reparse points.
 * We resolve them using GetFinalPathNameByHandleW.
 * Exit: 0 = success, 1 = error (link not found or not a symlink)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winioctl.h>

/* MinGW may not define REPARSE_DATA_BUFFER — define it manually if needed */
#ifndef REPARSE_DATA_BUFFER_HEADER_SIZE
typedef struct _REPARSE_DATA_BUFFER {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union {
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG  Flags;
            WCHAR  PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR  PathBuffer[1];
        } MountPointReparseBuffer;
        struct {
            UCHAR DataBuffer[1];
        } GenericReparseBuffer;
    };
} REPARSE_DATA_BUFFER;
#endif

#define VERSION "1.0"

static int g_canon    = 0;   /* -f/-e/-m */
static int g_must_exist = 0; /* -e: all components must exist */
static int g_newline  = 1;
static int g_quiet    = 0;

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] FILE\n\n"
        "Print symlink target or canonical path.\n\n"
        "  -f   canonicalize: resolve all symlinks, print absolute path\n"
        "  -e   like -f, fail if any path component does not exist\n"
        "  -m   like -f, allow non-existent path components\n"
        "  -n   do not print trailing newline\n"
        "  -q   suppress error messages\n"
        "      --version\n"
        "      --help\n",
        prog);
}

/* Return 1 if path is a reparse point (symlink or junction) */
static int is_reparse(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

/* Read the reparse target of a symlink via DeviceIoControl */
static char *read_symlink_target(const char *path) {
    wchar_t wpath[4096];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 4096);

    HANDLE h = CreateFileW(wpath,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    /* Reparse data buffer */
    char buf[16384];
    DWORD returned;
    if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT,
                         NULL, 0, buf, sizeof(buf), &returned, NULL)) {
        CloseHandle(h); return NULL;
    }
    CloseHandle(h);

    REPARSE_DATA_BUFFER *rd = (REPARSE_DATA_BUFFER *)buf;
    wchar_t *wtarget = NULL;
    int      wlen    = 0;

    if (rd->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        wtarget = (wchar_t *)((char *)rd->SymbolicLinkReparseBuffer.PathBuffer
                  + rd->SymbolicLinkReparseBuffer.PrintNameOffset);
        wlen = rd->SymbolicLinkReparseBuffer.PrintNameLength / sizeof(wchar_t);
    } else if (rd->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        wtarget = (wchar_t *)((char *)rd->MountPointReparseBuffer.PathBuffer
                  + rd->MountPointReparseBuffer.PrintNameOffset);
        wlen = rd->MountPointReparseBuffer.PrintNameLength / sizeof(wchar_t);
    } else {
        return NULL;
    }

    /* Convert to UTF-8 */
    int n = WideCharToMultiByte(CP_UTF8, 0, wtarget, wlen, NULL, 0, NULL, NULL);
    char *result = (char *)malloc((size_t)n + 1);
    if (!result) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wtarget, wlen, result, n, NULL, NULL);
    result[n] = '\0';
    /* Strip leading \??\ or \\?\ device prefixes */
    if (!strncmp(result, "\\??\\", 4) || !strncmp(result, "\\\\?\\", 4)) {
        memmove(result, result + 4, strlen(result + 4) + 1);
    }
    return result;
}

/* Resolve canonical path using GetFinalPathNameByHandleW */
static char *canonical_path(const char *path) {
    wchar_t wpath[4096];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 4096);

    DWORD flags = g_must_exist ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_BACKUP_SEMANTICS;
    HANDLE h = CreateFileW(wpath,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        flags,
        NULL);

    if (h == INVALID_HANDLE_VALUE) {
        if (g_must_exist) return NULL;
        /* For -m mode: just resolve via GetFullPathName without opening */
        wchar_t full[4096];
        DWORD n = GetFullPathNameW(wpath, 4096, full, NULL);
        if (!n) return NULL;
        int mb = WideCharToMultiByte(CP_UTF8, 0, full, -1, NULL, 0, NULL, NULL);
        char *r = (char *)malloc((size_t)mb);
        WideCharToMultiByte(CP_UTF8, 0, full, -1, r, mb, NULL, NULL);
        /* Convert backslashes to forward slashes */
        for (char *p = r; *p; p++) if (*p == '\\') *p = '/';
        return r;
    }

    wchar_t final[4096];
    DWORD n = GetFinalPathNameByHandleW(h, final, 4096, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (!n) return NULL;

    int mb = WideCharToMultiByte(CP_UTF8, 0, final, -1, NULL, 0, NULL, NULL);
    char *r = (char *)malloc((size_t)mb);
    WideCharToMultiByte(CP_UTF8, 0, final, -1, r, mb, NULL, NULL);
    /* Strip \\?\ prefix if present */
    if (!strncmp(r, "\\\\?\\", 4)) memmove(r, r + 4, strlen(r + 4) + 1);
    for (char *p = r; *p; p++) if (*p == '\\') *p = '/';
    return r;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("readlink %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help"))    { usage(argv[0]); return 0; }
        if (!strcmp(a, "--"))        { argi++; break; }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 'f': g_canon = 1; break;
                case 'e': g_canon = 1; g_must_exist = 1; break;
                case 'm': g_canon = 1; break;
                case 'n': g_newline = 0; break;
                case 'q': g_quiet   = 1; break;
                case 'v': g_quiet   = 0; break;
                default:
                    fprintf(stderr, "readlink: invalid option -- '%c'\n", *p);
                    return 1;
            }
        }
    }

    if (argi >= argc) {
        fprintf(stderr, "readlink: missing operand\n"); return 1;
    }

    int ret = 0;
    for (int i = argi; i < argc; i++) {
        const char *path = argv[i];
        char *result = NULL;

        if (g_canon) {
            result = canonical_path(path);
        } else {
            if (!is_reparse(path)) {
                if (!g_quiet)
                    fprintf(stderr, "readlink: '%s': not a symbolic link\n", path);
                ret = 1; continue;
            }
            result = read_symlink_target(path);
        }

        if (!result) {
            if (!g_quiet)
                fprintf(stderr, "readlink: '%s': failed to resolve\n", path);
            ret = 1; continue;
        }

        printf("%s", result);
        if (g_newline) putchar('\n');
        free(result);
    }
    return ret;
}
