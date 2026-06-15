/*
 * touch.c — Winix coreutil
 *
 * Usage: touch [OPTION]... FILE...
 *
 * Update the access and modification timestamps of each FILE to the
 * current time.  Create the FILE if it does not exist.
 *
 * Options:
 *   --help     display this help and exit
 *   --version  output version information and exit
 *
 * Exit codes: 0 success, 1 error
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

static void usage(void) {
    puts("Usage: touch [OPTION]... FILE...");
    puts("Update timestamps of each FILE to the current time; create if absent.");
    puts("");
    puts("      --help     display this help and exit");
    puts("      --version  output version information and exit");
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[argi], "--version") == 0) { puts("touch 1.0 (Winix)"); return 0; }
        if (strcmp(argv[argi], "--") == 0)        { argi++; break; }
        if (argv[argi][0] != '-') break;
    }

    if (argi >= argc) {
        fprintf(stderr, "touch: missing operand\n");
        fprintf(stderr, "Try 'touch --help' for more information.\n");
        return 1;
    }

    int ret = 0;
    for (int i = argi; i < argc; i++) {
        FILE *f = fopen(argv[i], "ab+");
        if (!f) {
            perror(argv[i]);
            ret = 1;
            continue;
        }
        fclose(f);
    }
    return ret;
}
