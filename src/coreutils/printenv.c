/*
 * printenv — print environment variables
 *
 * Usage: printenv [--null] [NAME ...]
 *   With no NAMEs, prints all variables (one per line).
 *   With NAMEs, prints the value of each; exits 1 if any not set.
 *   -0 / --null  end each line with NUL instead of newline
 *   --version / --help
 *
 * Exit: 0 = all variables found, 1 = any not set, 2 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define VERSION "1.0"

int main(int argc, char *argv[]) {
    int use_null = 0;
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("printenv %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: printenv [--null] [NAME ...]\n\n"
                "Print environment variable values.\n"
                "With no NAME, print all variables.\n\n"
                "  -0, --null   end output with NUL, not newline\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--null") || !strcmp(a, "-0")) { use_null = 1; continue; }
        if (!strcmp(a, "--")) { argi++; break; }
        /* single-char flags */
        for (const char *p = a + 1; *p; p++) {
            if (*p == '0') use_null = 1;
            else { fprintf(stderr, "printenv: invalid option -- '%c'\n", *p); return 2; }
        }
    }

    char term = use_null ? '\0' : '\n';

    if (argi >= argc) {
        /* print all */
        for (char **ep = environ; *ep; ep++) {
            fputs(*ep, stdout);
            putchar(term);
        }
        return 0;
    }

    /* print specific variables */
    int ret = 0;
    for (int i = argi; i < argc; i++) {
        const char *val = getenv(argv[i]);
        if (val) {
            fputs(val, stdout);
            putchar(term);
        } else {
            ret = 1;
        }
    }
    return ret;
}
