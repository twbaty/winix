/*
 * cmp.c — Winix coreutil
 *
 * Usage: cmp [-l] [-s] FILE1 FILE2
 *
 * Compare FILE1 and FILE2 byte by byte.
 * With no options, print the byte/line of the first difference.
 *
 * Options:
 *   -l   list all differing bytes (offset, octal values of each)
 *   -s   silent; report nothing, only set exit code
 *   --help      display this help and exit
 *   --version   output version information and exit
 *
 * Exit codes: 0 identical, 1 differ, 2 error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void usage(void) {
    puts("Usage: cmp [-l] [-s] FILE1 FILE2");
    puts("Compare two files byte by byte.");
    puts("");
    puts("  -l   list all differing bytes (offset and octal values)");
    puts("  -s   suppress all output; only set exit code");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
}

int main(int argc, char *argv[]) {
    bool list_all  = false;
    bool silent    = false;
    int  first_file = argc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { puts("cmp 1.0 (Winix 1.4)"); return 0; }
        if (argv[i][0] == '-' && argv[i][1] != '\0' && argv[i][1] != '-') {
            for (char *p = argv[i] + 1; *p; p++) {
                if (*p == 'l') list_all = true;
                else if (*p == 's') silent = true;
                else { fprintf(stderr, "cmp: invalid option -- '%c'\n", *p); return 2; }
            }
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "cmp: invalid option -- '%s'\n", argv[i]);
            return 2;
        }
        first_file = i;
        break;
    }

    if (first_file + 1 >= argc) {
        if (!silent) {
            fprintf(stderr, "cmp: missing operand\n");
            fprintf(stderr, "Try 'cmp --help' for more information.\n");
        }
        return 2;
    }

    const char *path1 = argv[first_file];
    const char *path2 = argv[first_file + 1];

    FILE *f1 = strcmp(path1, "-") == 0 ? stdin : fopen(path1, "rb");
    FILE *f2 = strcmp(path2, "-") == 0 ? stdin : fopen(path2, "rb");

    if (!f1) {
        if (!silent) fprintf(stderr, "cmp: %s: No such file or directory\n", path1);
        if (f2 && f2 != stdin) fclose(f2);
        return 2;
    }
    if (!f2) {
        if (!silent) fprintf(stderr, "cmp: %s: No such file or directory\n", path2);
        if (f1 != stdin) fclose(f1);
        return 2;
    }

    long long byte_pos = 0;
    long long line_num = 1;
    int       diff     = 0;

    for (;;) {
        int c1 = fgetc(f1);
        int c2 = fgetc(f2);

        if (c1 == EOF && c2 == EOF) break;
        byte_pos++;

        if (c1 == EOF) {
            if (!silent)
                fprintf(stderr, "cmp: EOF on %s after byte %lld\n", path1, byte_pos - 1);
            diff = 1; break;
        }
        if (c2 == EOF) {
            if (!silent)
                fprintf(stderr, "cmp: EOF on %s after byte %lld\n", path2, byte_pos - 1);
            diff = 1; break;
        }

        if (c1 != c2) {
            diff = 1;
            if (list_all) {
                if (!silent)
                    printf("%lld %o %o\n", byte_pos, (unsigned)c1, (unsigned)c2);
            } else {
                if (!silent)
                    printf("%s %s differ: byte %lld, line %lld\n",
                           path1, path2, byte_pos, line_num);
                break;
            }
        }
        if (c1 == '\n') line_num++;
    }

    if (f1 != stdin) fclose(f1);
    if (f2 != stdin) fclose(f2);
    return diff ? 1 : 0;
}
