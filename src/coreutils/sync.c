/*
 * sync — flush filesystem write buffers
 *
 * Usage: sync [FILE ...]
 *   With no args: flushes all volumes found via GetLogicalDrives.
 *   With FILE args: flushes the volume containing each file.
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>

#define VERSION "1.0"

static int flush_volume(const char *path) {
    /* Open the volume root for the given path */
    char root[8] = "C:\\";
    if (path && path[1] == ':') { root[0] = path[0]; }
    else if (path) {
        char full[MAX_PATH];
        if (!GetFullPathNameA(path, MAX_PATH, full, NULL)) {
            fprintf(stderr, "sync: %s: cannot resolve path\n", path); return 1;
        }
        root[0] = full[0];
    }

    char vol[8];
    snprintf(vol, sizeof(vol), "\\\\.\\%c:", root[0]);
    HANDLE h = CreateFileA(vol, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        /* Non-fatal — may not have permission on all volumes */
        return 0;
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;
    for (; argi < argc && argv[argi][0] == '-'; argi++) {
        if (!strcmp(argv[argi], "--version")) { printf("sync %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[argi], "--help")) {
            fprintf(stderr,
                "usage: sync [FILE ...]\n\n"
                "Flush filesystem write buffers to disk.\n"
                "With no FILE, flushes all local volumes.\n\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(argv[argi], "--")) { argi++; break; }
        fprintf(stderr, "sync: invalid option '%s'\n", argv[argi]); return 1;
    }

    if (argi >= argc) {
        /* Flush all local volumes */
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (!(mask & (1 << i))) continue;
            char vol[8];
            snprintf(vol, sizeof(vol), "\\\\.\\%c:", 'A' + i);
            HANDLE h = CreateFileA(vol, GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                FlushFileBuffers(h);
                CloseHandle(h);
            }
        }
        return 0;
    }

    int ret = 0;
    for (int i = argi; i < argc; i++) {
        /* Flush the specific file's handle */
        HANDLE h = CreateFileA(argv[i], GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            /* Try flushing the volume instead */
            ret |= flush_volume(argv[i]);
            continue;
        }
        if (!FlushFileBuffers(h)) {
            fprintf(stderr, "sync: %s: flush failed\n", argv[i]);
            ret = 1;
        }
        CloseHandle(h);
    }
    return ret;
}
