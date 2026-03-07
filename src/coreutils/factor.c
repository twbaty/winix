/*
 * factor — print prime factors of each NUMBER
 *
 * Usage: factor [NUMBER ...]
 *   With no arguments, reads numbers from stdin.
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#define VERSION "1.0"

static void factor(uint64_t n) {
    printf("%" PRIu64 ":", n);

    if (n == 0) { printf(" 0"); goto done; }
    if (n == 1) { printf(" 1"); goto done; }

    /* Pull out factors of 2 */
    while (n % 2 == 0) { printf(" 2"); n /= 2; }

    /* Trial division by odd numbers */
    for (uint64_t d = 3; d * d <= n; d += 2) {
        while (n % d == 0) {
            printf(" %" PRIu64, d);
            n /= d;
        }
    }
    if (n > 1) printf(" %" PRIu64, n);

done:
    putchar('\n');
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("factor %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[i], "--help")) {
            fprintf(stderr,
                "usage: factor [NUMBER ...]\n\n"
                "Print prime factors of each NUMBER.\n"
                "With no NUMBER, reads from stdin.\n\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
    }

    if (argc < 2) {
        char line[256];
        while (fgets(line, sizeof(line), stdin)) {
            char *p = line;
            while (*p) {
                while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
                if (!*p) break;
                char *end;
                uint64_t n = (uint64_t)strtoull(p, &end, 10);
                if (end == p) { p++; continue; }
                factor(n);
                p = end;
            }
        }
    } else {
        int ret = 0;
        for (int i = 1; i < argc; i++) {
            char *end;
            uint64_t n = (uint64_t)strtoull(argv[i], &end, 10);
            if (*end) {
                fprintf(stderr, "factor: invalid number '%s'\n", argv[i]);
                ret = 1; continue;
            }
            factor(n);
        }
        return ret;
    }
    return 0;
}
