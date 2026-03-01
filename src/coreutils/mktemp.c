/*
 * mktemp.c — Winix coreutil
 *
 * Usage: mktemp [-d] [-q] [TEMPLATE]
 *
 * Create a unique temporary file (or directory with -d) and print its path.
 * TEMPLATE must end in at least 3 X's (e.g. tmpXXXXXX).
 * If no template is given, the system temp directory is used.
 *
 * Options:
 *   -d          create a directory instead of a file
 *   -q          suppress error messages
 *   --help      print usage and exit 0
 *   --version   print version and exit 0
 *
 * Exit codes: 0 success, 1 failure
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>

static void usage(void) {
    puts("Usage: mktemp [-d] [-q] [TEMPLATE]");
    puts("Create a unique temporary file or directory and print its path.");
    puts("");
    puts("  -d          create a directory instead of a file");
    puts("  -q          suppress error messages");
    puts("  TEMPLATE    string ending in 3+ X's (e.g. tmpXXXXXX)");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
}

/* Replace trailing X's in tmpl with random alphanumeric characters.
 * Returns false if fewer than 3 trailing X's found. */
static bool instantiate(char *tmpl) {
    int len = (int)strlen(tmpl);
    int nx  = 0;
    for (int i = len - 1; i >= 0 && tmpl[i] == 'X'; i--) nx++;
    if (nx < 3) return false;

    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    int nc = (int)(sizeof(chars) - 1);
    srand((unsigned)(GetCurrentProcessId() ^ GetTickCount()));
    for (int i = len - nx; i < len; i++)
        tmpl[i] = chars[rand() % nc];
    return true;
}

int main(int argc, char *argv[]) {
    bool make_dir  = false;
    bool quiet     = false;
    char *tmpl_arg = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { puts("mktemp 1.0 (Winix 1.4)"); return 0; }
        if (strcmp(argv[i], "-d") == 0) { make_dir = true; continue; }
        if (strcmp(argv[i], "-q") == 0) { quiet    = true; continue; }
        if (argv[i][0] == '-') {
            if (!quiet) fprintf(stderr, "mktemp: invalid option -- '%s'\n", argv[i]);
            return 1;
        }
        tmpl_arg = argv[i];
    }

    char path[MAX_PATH];

    if (tmpl_arg) {
        strncpy(path, tmpl_arg, MAX_PATH - 1);
        path[MAX_PATH - 1] = '\0';
        if (!instantiate(path)) {
            if (!quiet) fprintf(stderr, "mktemp: template must end in at least 3 X's\n");
            return 1;
        }
        for (int try = 0; try < 32; try++) {
            if (make_dir) {
                if (CreateDirectoryA(path, NULL)) { printf("%s\n", path); return 0; }
            } else {
                HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                                       CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
                if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); printf("%s\n", path); return 0; }
            }
            instantiate(path);
        }
        if (!quiet) fprintf(stderr, "mktemp: failed to create unique path\n");
        return 1;
    }

    /* No template — use system temp dir */
    char tmpdir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpdir);

    if (make_dir) {
        for (int try = 0; try < 32; try++) {
            snprintf(path, MAX_PATH, "%swinix_%08lx%02d", tmpdir,
                     (unsigned long)(GetCurrentProcessId() ^ GetTickCount()), try);
            if (CreateDirectoryA(path, NULL)) { printf("%s\n", path); return 0; }
        }
        if (!quiet) fprintf(stderr, "mktemp: failed to create temp directory\n");
        return 1;
    } else {
        char tmp_name[MAX_PATH];
        if (GetTempFileNameA(tmpdir, "nix", 0, tmp_name) == 0) {
            if (!quiet) fprintf(stderr, "mktemp: failed to create temp file\n");
            return 1;
        }
        printf("%s\n", tmp_name);
        return 0;
    }
}
