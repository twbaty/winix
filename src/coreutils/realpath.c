/*
 * realpath.c — Winix coreutil
 *
 * Usage: realpath [OPTION]... FILE...
 *
 * Print the resolved absolute path of each FILE.
 * Resolves . and .. components; does not require the path to exist
 * unless -e is given.
 *
 * Options:
 *   -e   error if any path component does not exist
 *   -m   no error if final component is missing (default behaviour)
 *   -q   suppress error messages
 *   --help      display this help and exit
 *   --version   output version information and exit
 *
 * Exit codes: 0 all paths resolved, 1 one or more failed
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>

static void usage(void) {
    puts("Usage: realpath [OPTION]... FILE...");
    puts("Print the resolved absolute path of each FILE.");
    puts("");
    puts("  -e   error if path does not exist");
    puts("  -m   allow missing components (default)");
    puts("  -q   suppress error messages");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
}

int main(int argc, char *argv[]) {
    bool must_exist = false;
    bool quiet      = false;
    int  first_file = argc;
    int  rc         = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { puts("realpath 1.0 (Winix 1.4)"); return 0; }
        if (strcmp(argv[i], "-e") == 0 ||
            strcmp(argv[i], "--canonicalize-existing") == 0) { must_exist = true; continue; }
        if (strcmp(argv[i], "-m") == 0 ||
            strcmp(argv[i], "--canonicalize-missing") == 0)  { continue; }
        if (strcmp(argv[i], "-q") == 0 ||
            strcmp(argv[i], "--quiet") == 0)                  { quiet = true; continue; }
        if (argv[i][0] == '-') {
            fprintf(stderr, "realpath: invalid option -- '%s'\n", argv[i]);
            return 1;
        }
        first_file = i;
        break;
    }

    if (first_file >= argc) {
        fprintf(stderr, "realpath: missing operand\n");
        return 1;
    }

    for (int i = first_file; i < argc; i++) {
        char resolved[MAX_PATH];
        DWORD n = GetFullPathNameA(argv[i], MAX_PATH, resolved, NULL);
        if (n == 0 || n >= MAX_PATH) {
            if (!quiet) fprintf(stderr, "realpath: %s: failed to resolve\n", argv[i]);
            rc = 1;
            continue;
        }
        if (must_exist && GetFileAttributesA(resolved) == INVALID_FILE_ATTRIBUTES) {
            if (!quiet) fprintf(stderr, "realpath: %s: No such file or directory\n", argv[i]);
            rc = 1;
            continue;
        }
        /* Use forward slashes for Unix feel */
        for (char *p = resolved; *p; p++) if (*p == '\\') *p = '/';
        printf("%s\n", resolved);
    }
    return rc;
}
