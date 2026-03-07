/*
 * shred — overwrite a file to hide its contents, then optionally delete it
 *
 * Usage: shred [OPTIONS] FILE ...
 *   -n N   number of overwrite passes (default 3)
 *   -z      add a final pass of zeros to hide shredding
 *   -u      remove file after shredding
 *   -v      verbose — show progress
 *   -f      force — change permissions if needed
 *   -x      do not round up file size to block boundary
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>

#define VERSION "1.0"

static int g_passes  = 3;
static int g_zero    = 0;
static int g_remove  = 0;
static int g_verbose = 0;
static int g_force   = 0;

/* Fill buffer with pseudo-random bytes using a simple LCG */
static uint32_t g_seed = 0;
static void fill_random(unsigned char *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        g_seed = g_seed * 1664525u + 1013904223u;
        buf[i] = (unsigned char)(g_seed >> 24);
    }
}

static int shred_file(const char *path) {
    /* If -f, make writable first */
    if (g_force) SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);

    HANDLE h = CreateFileA(path,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING,
        FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING,
        NULL);
    if (h == INVALID_HANDLE_VALUE) {
        /* Retry without NO_BUFFERING for small/odd-sized files */
        h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "shred: %s: cannot open: error %lu\n", path, GetLastError());
            return 1;
        }
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) {
        fprintf(stderr, "shred: %s: cannot get size\n", path);
        CloseHandle(h); return 1;
    }

    /* Seed with current time + path hash for variety */
    g_seed = (uint32_t)GetTickCount();
    for (const char *p = path; *p; p++) g_seed ^= (uint32_t)*p * 31u;

    unsigned char buf[65536];
    int total_passes = g_passes + (g_zero ? 1 : 0);

    for (int pass = 0; pass < total_passes; pass++) {
        int is_zero_pass = g_zero && (pass == total_passes - 1);

        if (g_verbose) {
            fprintf(stderr, "shred: %s: pass %d/%d (%s)...\n",
                path, pass + 1, total_passes,
                is_zero_pass ? "000..." : "random");
        }

        /* Rewind */
        LARGE_INTEGER zero = {0};
        SetFilePointerEx(h, zero, NULL, FILE_BEGIN);

        int64_t remaining = size.QuadPart;
        while (remaining > 0) {
            DWORD chunk = (DWORD)(remaining < (int64_t)sizeof(buf) ? remaining : sizeof(buf));
            if (is_zero_pass)
                memset(buf, 0, chunk);
            else
                fill_random(buf, chunk);
            DWORD written;
            WriteFile(h, buf, chunk, &written, NULL);
            remaining -= chunk;
        }
        FlushFileBuffers(h);
        g_seed ^= (uint32_t)(pass * 0xDEADBEEFu);
    }

    CloseHandle(h);

    if (g_remove) {
        if (!DeleteFileA(path)) {
            fprintf(stderr, "shred: %s: cannot delete: error %lu\n", path, GetLastError());
            return 1;
        }
        if (g_verbose) fprintf(stderr, "shred: %s: removed\n", path);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("shred %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: shred [OPTIONS] FILE ...\n\n"
                "Overwrite FILE(s) to hide content, optionally delete.\n\n"
                "  -f        force — make writable if needed\n"
                "  -n N      overwrite N times (default 3)\n"
                "  -u        remove file after shredding\n"
                "  -v        verbose — show progress\n"
                "  -z        final pass of zeros\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 'f': g_force   = 1; break;
                case 'u': g_remove  = 1; break;
                case 'v': g_verbose = 1; break;
                case 'z': g_zero    = 1; break;
                case 'x': /* no-op: block-rounding not applicable */ break;
                case 'n': {
                    const char *val = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!val) { fprintf(stderr, "shred: option requires argument -- 'n'\n"); return 1; }
                    g_passes = atoi(val);
                    if (g_passes < 1) g_passes = 1;
                    p = val + strlen(val) - 1;
                    break;
                }
                default: fprintf(stderr, "shred: invalid option -- '%c'\n", *p); return 1;
            }
        }
    }

    if (argi >= argc) { fprintf(stderr, "shred: missing operand\n"); return 1; }

    int ret = 0;
    for (int i = argi; i < argc; i++)
        ret |= shred_file(argv[i]);
    return ret;
}
