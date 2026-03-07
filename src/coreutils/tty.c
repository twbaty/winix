/*
 * tty — print the file name of the terminal connected to stdin
 *
 * Usage: tty [-s]
 *   -s  silent — no output, only exit code
 *   --version / --help
 *
 * Exit: 0 = stdin is a tty, 1 = not a tty, 2 = error
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <io.h>

#define VERSION "1.0"

int main(int argc, char *argv[]) {
    int silent = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("tty %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[i], "--help")) {
            fprintf(stderr,
                "usage: tty [-s]\n\n"
                "Print the file name of the terminal connected to stdin.\n\n"
                "  -s   silent — exit code only, no output\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(argv[i], "-s")) { silent = 1; continue; }
        if (argv[i][0] == '-') {
            fprintf(stderr, "tty: invalid option -- '%c'\n", argv[i][1]); return 2;
        }
    }

    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
        if (!silent) fprintf(stderr, "tty: error getting stdin handle\n");
        return 2;
    }

    DWORD mode;
    if (!GetConsoleMode(h, &mode)) {
        /* Not a console/tty */
        if (!silent) printf("not a tty\n");
        return 1;
    }

    /* It is a tty — get the console device name */
    if (!silent) {
        /* On Windows, the console is \\.\CON or CONIN$ */
        /* We return the standard Unix-like name */
        printf("CON\n");
    }
    return 0;
}
