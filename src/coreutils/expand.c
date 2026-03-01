/*
 * expand.c — Winix coreutil
 *
 * Usage: expand [-t N] [FILE...]
 *
 * Convert tab characters to spaces in each FILE (or stdin).
 * Each tab is expanded to the next tab stop (default every 8 columns).
 *
 * Options:
 *   -t N, --tabs=N   set tab stop width to N (default 8)
 *   --help           display this help and exit
 *   --version        output version information and exit
 *
 * Exit codes: 0 success, 1 error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    puts("Usage: expand [-t N] [FILE...]");
    puts("Convert tabs to spaces, with tab stops every N columns.");
    puts("");
    puts("  -t N   set tab stop width to N (default 8)");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
}

static void expand_stream(FILE *f, int tabstop) {
    int col = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\t') {
            int spaces = tabstop - (col % tabstop);
            for (int i = 0; i < spaces; i++) putchar(' ');
            col += spaces;
        } else {
            putchar(c);
            col = (c == '\n') ? 0 : col + 1;
        }
    }
}

int main(int argc, char *argv[]) {
    int tabstop    = 8;
    int first_file = argc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { puts("expand 1.0 (Winix 1.4)"); return 0; }
        if (strncmp(argv[i], "-t", 2) == 0) {
            const char *val = argv[i][2] ? argv[i] + 2 : (++i < argc ? argv[i] : NULL);
            if (!val) { fprintf(stderr, "expand: option requires an argument -- 't'\n"); return 1; }
            tabstop = atoi(val);
            if (tabstop <= 0) { fprintf(stderr, "expand: invalid tab size: %s\n", val); return 1; }
            continue;
        }
        if (strncmp(argv[i], "--tabs=", 7) == 0) {
            tabstop = atoi(argv[i] + 7);
            if (tabstop <= 0) { fprintf(stderr, "expand: invalid tab size\n"); return 1; }
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "expand: invalid option -- '%s'\n", argv[i]);
            return 1;
        }
        first_file = i;
        break;
    }

    if (first_file >= argc) {
        expand_stream(stdin, tabstop);
    } else {
        for (int i = first_file; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) { fprintf(stderr, "expand: %s: No such file or directory\n", argv[i]); continue; }
            expand_stream(f, tabstop);
            fclose(f);
        }
    }
    return 0;
}
