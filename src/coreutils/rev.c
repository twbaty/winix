/*
 * rev.c — Winix coreutil
 *
 * Usage: rev [FILE...]
 *
 * Reverse the characters on each line of FILE (or stdin).
 * The newline at the end of each line is preserved in place.
 *
 * Options:
 *   --help     Print usage and exit 0
 *   --version  Print version and exit 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_LINE 65536

static void usage(void) {
    puts("Usage: rev [FILE...]");
    puts("Reverse the characters on each line of FILE or standard input.");
    puts("");
    puts("  --help     display this help and exit");
    puts("  --version  output version information and exit");
}

static int rev_stream(FILE *f) {
    char buf[MAX_LINE];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);

        /* Determine whether the line ends with a newline */
        int has_nl = (len > 0 && buf[len - 1] == '\n');
        size_t rev_len = has_nl ? len - 1 : len;

        /* Reverse characters in-place (excluding trailing newline) */
        size_t lo = 0, hi = rev_len;
        while (lo + 1 < hi) {
            char tmp   = buf[lo];
            buf[lo]    = buf[hi - 1];
            buf[hi - 1] = tmp;
            lo++;
            hi--;
        }

        /* Write reversed content plus newline if present */
        fwrite(buf, 1, rev_len, stdout);
        if (has_nl) putchar('\n');
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;
    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[argi], "--version") == 0) {
            puts("rev 1.0 (Winix 1.0)");
            return 0;
        }
        if (argv[argi][0] == '-' && argv[argi][1] == '-') {
            fprintf(stderr, "rev: unrecognized option '%s'\n", argv[argi]);
            return 1;
        }
        break;
    }

    int ret = 0;

    if (argi >= argc) {
        ret = rev_stream(stdin);
    } else {
        for (int i = argi; i < argc; i++) {
            if (strcmp(argv[i], "-") == 0) {
                if (rev_stream(stdin) != 0) ret = 1;
                continue;
            }
            FILE *f = fopen(argv[i], "r");
            if (!f) {
                fprintf(stderr, "rev: %s: %s\n", argv[i], strerror(errno));
                ret = 1;
                continue;
            }
            if (rev_stream(f) != 0) ret = 1;
            fclose(f);
        }
    }

    return ret;
}
