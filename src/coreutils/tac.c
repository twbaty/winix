/*
 * tac.c — Winix coreutil
 *
 * Usage: tac [FILE...]
 *
 * Print each FILE to stdout with lines in reverse order.
 * If no FILE or FILE is -, read stdin.
 * Multiple files: each file is reversed independently, in file order.
 *
 * Options:
 *   --help     Print usage and exit 0
 *   --version  Print version and exit 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define INITIAL_LINES 1024
#define MAX_LINES     100000

static void usage(void) {
    puts("Usage: tac [FILE...]");
    puts("Write each FILE to standard output, last line first.");
    puts("With no FILE, or when FILE is -, read standard input.");
    puts("");
    puts("  --help     display this help and exit");
    puts("  --version  output version information and exit");
}

static int tac_stream(FILE *f) {
    char **lines = malloc(INITIAL_LINES * sizeof(char *));
    if (!lines) {
        fprintf(stderr, "tac: out of memory\n");
        return 1;
    }
    int capacity = INITIAL_LINES;
    int count    = 0;

    char buf[65536];
    while (fgets(buf, sizeof(buf), f)) {
        if (count == capacity) {
            int newcap = capacity * 2;
            if (newcap > MAX_LINES) {
                fprintf(stderr, "tac: too many lines (limit %d)\n", MAX_LINES);
                for (int i = 0; i < count; i++) free(lines[i]);
                free(lines);
                return 1;
            }
            char **tmp = realloc(lines, newcap * sizeof(char *));
            if (!tmp) {
                fprintf(stderr, "tac: out of memory\n");
                for (int i = 0; i < count; i++) free(lines[i]);
                free(lines);
                return 1;
            }
            lines    = tmp;
            capacity = newcap;
        }
        lines[count] = strdup(buf);
        if (!lines[count]) {
            fprintf(stderr, "tac: out of memory\n");
            for (int i = 0; i < count; i++) free(lines[i]);
            free(lines);
            return 1;
        }
        count++;
    }

    for (int i = count - 1; i >= 0; i--)
        fputs(lines[i], stdout);

    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
    return 0;
}

int main(int argc, char *argv[]) {
    /* Option parsing — only --help and --version */
    int argi = 1;
    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[argi], "--version") == 0) {
            puts("tac 1.0 (Winix 1.0)");
            return 0;
        }
        if (argv[argi][0] == '-' && argv[argi][1] == '-') {
            fprintf(stderr, "tac: unrecognized option '%s'\n", argv[argi]);
            return 1;
        }
        /* First non-option: stop option parsing */
        break;
    }

    int ret = 0;

    if (argi >= argc) {
        /* No file arguments: read stdin */
        ret = tac_stream(stdin);
    } else {
        for (int i = argi; i < argc; i++) {
            if (strcmp(argv[i], "-") == 0) {
                if (tac_stream(stdin) != 0) ret = 1;
                continue;
            }
            FILE *f = fopen(argv[i], "r");
            if (!f) {
                fprintf(stderr, "tac: %s: %s\n", argv[i], strerror(errno));
                ret = 1;
                continue;
            }
            if (tac_stream(f) != 0) ret = 1;
            fclose(f);
        }
    }

    return ret;
}
