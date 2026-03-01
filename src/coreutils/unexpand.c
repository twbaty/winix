/*
 * unexpand.c — Winix coreutil
 *
 * Usage: unexpand [-a] [-t N] [FILE...]
 *
 * Convert spaces to tabs in each FILE (or stdin).
 * By default, only leading spaces on each line are converted.
 * With -a, all runs of spaces are candidates for conversion.
 *
 * Options:
 *   -a, --all        convert all space runs, not just leading ones
 *   -t N, --tabs=N   set tab stop width to N (default 8)
 *   --help           display this help and exit
 *   --version        output version information and exit
 *
 * Exit codes: 0 success, 1 error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void usage(void) {
    puts("Usage: unexpand [-a] [-t N] [FILE...]");
    puts("Convert spaces to tabs in each FILE.");
    puts("");
    puts("  -a     convert all space runs (default: leading only)");
    puts("  -t N   set tab stop width to N (default 8)");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
}

static void unexpand_stream(FILE *f, int tabstop, bool all) {
    int  col     = 0;
    int  spaces  = 0; /* spaces accumulated since last non-space */
    bool leading = true;
    int  c;

    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') {
            /* Flush accumulated spaces as-is (can't tab at end of line) */
            for (int i = 0; i < spaces; i++) putchar(' ');
            spaces = 0;
            putchar('\n');
            col     = 0;
            leading = true;
        } else if (c == ' ' && (all || leading)) {
            spaces++;
            col++;
            /* If we've hit a tab stop boundary, emit a tab */
            if (col % tabstop == 0 && spaces > 0) {
                putchar('\t');
                spaces = 0;
            }
        } else if (c == '\t' && (all || leading)) {
            /* A literal tab: flush partial spaces, then move to next stop */
            for (int i = 0; i < spaces; i++) putchar(' ');
            spaces = 0;
            putchar('\t');
            col = ((col / tabstop) + 1) * tabstop;
        } else {
            /* Non-space character: flush pending spaces, output char */
            for (int i = 0; i < spaces; i++) putchar(' ');
            spaces = 0;
            putchar(c);
            col++;
            leading = false;
        }
    }
    /* Flush any trailing spaces */
    for (int i = 0; i < spaces; i++) putchar(' ');
}

int main(int argc, char *argv[]) {
    int  tabstop    = 8;
    bool all        = false;
    int  first_file = argc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { puts("unexpand 1.0 (Winix 1.4)"); return 0; }
        if (strcmp(argv[i], "-a") == 0 ||
            strcmp(argv[i], "--all") == 0) { all = true; continue; }
        if (strncmp(argv[i], "-t", 2) == 0) {
            const char *val = argv[i][2] ? argv[i] + 2 : (++i < argc ? argv[i] : NULL);
            if (!val) { fprintf(stderr, "unexpand: option requires an argument -- 't'\n"); return 1; }
            tabstop = atoi(val);
            if (tabstop <= 0) { fprintf(stderr, "unexpand: invalid tab size: %s\n", val); return 1; }
            continue;
        }
        if (strncmp(argv[i], "--tabs=", 7) == 0) {
            tabstop = atoi(argv[i] + 7);
            if (tabstop <= 0) { fprintf(stderr, "unexpand: invalid tab size\n"); return 1; }
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "unexpand: invalid option -- '%s'\n", argv[i]);
            return 1;
        }
        first_file = i;
        break;
    }

    if (first_file >= argc) {
        unexpand_stream(stdin, tabstop, all);
    } else {
        for (int i = first_file; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) { fprintf(stderr, "unexpand: %s: No such file or directory\n", argv[i]); continue; }
            unexpand_stream(f, tabstop, all);
            fclose(f);
        }
    }
    return 0;
}
