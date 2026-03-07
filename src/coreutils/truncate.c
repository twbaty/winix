/*
 * truncate — shrink or extend the size of files
 *
 * Usage: truncate -s [+/-]SIZE FILE ...
 *   -s SIZE   set file to exactly SIZE bytes
 *   -s +SIZE  extend file by SIZE bytes
 *   -s -SIZE  shrink file by SIZE bytes (floors at 0)
 *   -c        do not create files that don't exist
 *   --version / --help
 *
 * SIZE suffix: k/K=1024, m/M=1024^2, g/G=1024^3, t/T=1024^4
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define VERSION "1.0"

static long long parse_size(const char *s) {
    char *end;
    long long v = strtoll(s, &end, 10);
    switch (*end | 0x20) {
        case 'k': v *= 1024LL;               break;
        case 'm': v *= 1024LL * 1024;        break;
        case 'g': v *= 1024LL * 1024 * 1024; break;
        case 't': v *= 1024LL * 1024 * 1024 * 1024; break;
    }
    return v;
}

static int do_truncate(const char *path, char mode, long long size, int no_create) {
    /* mode: '=' set, '+' extend, '-' shrink */
    HANDLE h = CreateFileA(path,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        no_create ? OPEN_EXISTING : OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "truncate: cannot open '%s': error %lu\n",
                path, GetLastError());
        return 1;
    }

    long long new_size = size;

    if (mode == '+' || mode == '-') {
        LARGE_INTEGER cur;
        if (!GetFileSizeEx(h, &cur)) {
            fprintf(stderr, "truncate: cannot stat '%s': error %lu\n",
                    path, GetLastError());
            CloseHandle(h); return 1;
        }
        new_size = (mode == '+') ? cur.QuadPart + size
                                 : cur.QuadPart - size;
        if (new_size < 0) new_size = 0;
    }

    LARGE_INTEGER li;
    li.QuadPart = new_size;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
        fprintf(stderr, "truncate: cannot set size of '%s': error %lu\n",
                path, GetLastError());
        CloseHandle(h); return 1;
    }

    CloseHandle(h);
    return 0;
}

int main(int argc, char *argv[]) {
    char     mode      = '=';
    long long size     = -1;
    int      no_create = 0;
    int      argi      = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("truncate %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: truncate -s [+/-]SIZE FILE ...\n\n"
                "Shrink or extend each FILE to the given size.\n\n"
                "  -s SIZE   set exact size (k/m/g/t suffix ok)\n"
                "  -s +SIZE  grow by SIZE bytes\n"
                "  -s -SIZE  shrink by SIZE bytes\n"
                "  -c        do not create missing files\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "-c")) { no_create = 1; continue; }
        if (!strcmp(a, "--")) { argi++; break; }

        if ((a[1] == 's') ) {
            const char *val = a[2] ? a + 2 : argv[++argi];
            if (!val) { fprintf(stderr, "truncate: -s requires argument\n"); return 1; }
            if (val[0] == '+' || val[0] == '-') { mode = val[0]; val++; }
            else mode = '=';
            size = parse_size(val);
            continue;
        }

        fprintf(stderr, "truncate: invalid option -- '%s'\n", a);
        return 1;
    }

    if (size < 0) {
        fprintf(stderr, "truncate: you must specify a size with -s\n"); return 1;
    }
    if (argi >= argc) {
        fprintf(stderr, "truncate: missing file operand\n"); return 1;
    }

    int ret = 0;
    for (int i = argi; i < argc; i++)
        ret |= do_truncate(argv[i], mode, size, no_create);
    return ret;
}
