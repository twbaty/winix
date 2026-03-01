/*
 * fold.c — Winix coreutil
 *
 * Usage: fold [-w WIDTH] [-s] [FILE...]
 *
 * Wrap each input line to fit within WIDTH columns (default 80).
 * With -s, break at the last whitespace within the width if possible.
 *
 * Options:
 *   -w N, --width=N   fold at column N (default 80)
 *   -s                break at whitespace rather than mid-word
 *   --help            display this help and exit
 *   --version         output version information and exit
 *
 * Exit codes: 0 success, 1 error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

static void usage(void) {
    puts("Usage: fold [-w WIDTH] [-s] [FILE...]");
    puts("Wrap input lines to fit within WIDTH columns.");
    puts("");
    puts("  -w N   fold at column N (default 80)");
    puts("  -s     break at whitespace when possible");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
}

static void fold_stream(FILE *f, int width, bool break_spaces) {
    char buf[65536];
    while (fgets(buf, sizeof(buf), f)) {
        int len    = (int)strlen(buf);
        int has_nl = (len > 0 && buf[len - 1] == '\n');
        if (has_nl) len--;

        int pos = 0;
        while (pos < len) {
            int chunk = (pos + width <= len) ? width : len - pos;

            if (break_spaces && pos + chunk < len) {
                /* Find last whitespace within chunk to avoid breaking words */
                int last_sp = -1;
                for (int i = chunk - 1; i >= 0; i--) {
                    if (isspace((unsigned char)buf[pos + i])) {
                        last_sp = i + 1; /* include the space character */
                        break;
                    }
                }
                if (last_sp > 0) chunk = last_sp;
            }

            fwrite(buf + pos, 1, chunk, stdout);
            pos += chunk;
            if (pos < len) putchar('\n');
        }
        if (has_nl) putchar('\n');
    }
}

int main(int argc, char *argv[]) {
    int  width        = 80;
    bool break_spaces = false;
    int  first_file   = argc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { puts("fold 1.0 (Winix 1.4)"); return 0; }
        if (strcmp(argv[i], "-s") == 0) { break_spaces = true; continue; }
        if (strncmp(argv[i], "-w", 2) == 0) {
            const char *val = argv[i][2] ? argv[i] + 2 : (++i < argc ? argv[i] : NULL);
            if (!val) { fprintf(stderr, "fold: option requires an argument -- 'w'\n"); return 1; }
            width = atoi(val);
            if (width <= 0) { fprintf(stderr, "fold: invalid width: %s\n", val); return 1; }
            continue;
        }
        if (strncmp(argv[i], "--width=", 8) == 0) {
            width = atoi(argv[i] + 8);
            if (width <= 0) { fprintf(stderr, "fold: invalid width\n"); return 1; }
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "fold: invalid option -- '%s'\n", argv[i]);
            return 1;
        }
        first_file = i;
        break;
    }

    if (first_file >= argc) {
        fold_stream(stdin, width, break_spaces);
    } else {
        for (int i = first_file; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) { fprintf(stderr, "fold: %s: No such file or directory\n", argv[i]); continue; }
            fold_stream(f, width, break_spaces);
            fclose(f);
        }
    }
    return 0;
}
