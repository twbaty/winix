/*
 * shuf.c — Winix coreutil
 *
 * Shuffle lines randomly.
 *
 * Usage:
 *   shuf [OPTION]... [FILE]
 *   shuf -e [OPTION]... ARG...
 *   shuf -i LO-HI [OPTION]...
 *
 * -e / --echo            treat each ARG as an input line
 * -i LO-HI / --input-range=LO-HI   shuffle integers LO to HI inclusive
 * -n N / --head-count=N  output at most N lines
 * -r / --repeat          allow repeated output (with replacement)
 * -z / --zero-terminated NUL-terminated lines
 *
 * Uses Fisher-Yates shuffle seeded with time(NULL).
 *
 * Compile: C99, no dependencies beyond the C standard library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Limits                                                               */
/* ------------------------------------------------------------------ */

#define MAX_LINES  1000000

/* ------------------------------------------------------------------ */
/* Dynamic line array                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    char  **lines;
    size_t  count;
    size_t  cap;
    bool    owns_data;   /* true → we malloc'd the strings; free on done */
} Lines;

static bool lines_push(Lines *la, char *s)
{
    if (la->count >= MAX_LINES) {
        fprintf(stderr, "shuf: too many lines (max %d)\n", MAX_LINES);
        return false;
    }
    if (la->count >= la->cap) {
        size_t newcap = (la->cap == 0) ? 256 : la->cap * 2;
        if (newcap > MAX_LINES) newcap = MAX_LINES;
        char **tmp = realloc(la->lines, newcap * sizeof(char *));
        if (!tmp) {
            fprintf(stderr, "shuf: out of memory\n");
            return false;
        }
        la->lines = tmp;
        la->cap   = newcap;
    }
    la->lines[la->count++] = s;
    return true;
}

static void lines_free(Lines *la)
{
    if (la->owns_data) {
        for (size_t i = 0; i < la->count; i++)
            free(la->lines[i]);
    }
    free(la->lines);
    la->lines = NULL;
    la->count = 0;
    la->cap   = 0;
}

/* ------------------------------------------------------------------ */
/* Fisher-Yates shuffle                                                 */
/* ------------------------------------------------------------------ */

static void shuffle(Lines *la)
{
    size_t n = la->count;
    for (size_t i = n - 1; i > 0; i--) {
        /* Pick a random index j in [0, i] */
        size_t j = (size_t)rand() % (i + 1);
        char *tmp    = la->lines[i];
        la->lines[i] = la->lines[j];
        la->lines[j] = tmp;
    }
}

/* ------------------------------------------------------------------ */
/* Read lines from a FILE (NUL-terminated or newline-terminated)        */
/* ------------------------------------------------------------------ */

static bool read_lines_from_file(FILE *f, bool zero_term, Lines *la)
{
    size_t bufsz  = 4096;
    char  *buf    = malloc(bufsz);
    if (!buf) {
        fprintf(stderr, "shuf: out of memory\n");
        return false;
    }
    size_t len = 0;

    int c;
    while ((c = fgetc(f)) != EOF) {
        unsigned char uc = (unsigned char)c;
        char term = zero_term ? '\0' : '\n';

        if ((char)uc == term) {
            /* End of this line — null-terminate and push */
            if (len + 1 > bufsz) {
                /* Grow buffer */
                char *tmp = realloc(buf, len + 1);
                if (!tmp) {
                    free(buf);
                    fprintf(stderr, "shuf: out of memory\n");
                    return false;
                }
                buf  = tmp;
                bufsz = len + 1;
            }
            buf[len] = '\0';

            /* Strip trailing \r for newline mode */
            if (!zero_term && len > 0 && buf[len-1] == '\r')
                buf[--len] = '\0';

            char *copy = strdup(buf);
            if (!copy) {
                free(buf);
                fprintf(stderr, "shuf: out of memory\n");
                return false;
            }
            if (!lines_push(la, copy)) {
                free(copy);
                free(buf);
                return false;
            }
            len = 0;
        } else {
            /* Append character, growing buffer if needed */
            if (len + 1 >= bufsz) {
                size_t newsz = bufsz * 2;
                char *tmp = realloc(buf, newsz);
                if (!tmp) {
                    free(buf);
                    fprintf(stderr, "shuf: out of memory\n");
                    return false;
                }
                buf   = tmp;
                bufsz = newsz;
            }
            buf[len++] = (char)uc;
        }
    }

    /* Flush any partial line at EOF (no trailing terminator) */
    if (len > 0) {
        if (len + 1 > bufsz) {
            char *tmp = realloc(buf, len + 1);
            if (!tmp) { free(buf); fprintf(stderr, "shuf: out of memory\n"); return false; }
            buf = tmp;
        }
        buf[len] = '\0';
        if (!zero_term && len > 0 && buf[len-1] == '\r')
            buf[--len] = '\0';
        char *copy = strdup(buf);
        if (!copy) { free(buf); fprintf(stderr, "shuf: out of memory\n"); return false; }
        if (!lines_push(la, copy)) { free(copy); free(buf); return false; }
    }

    free(buf);
    la->owns_data = true;
    return true;
}

/* ------------------------------------------------------------------ */
/* Output                                                               */
/* ------------------------------------------------------------------ */

static void output_lines(Lines *la, long head_count, bool repeat, bool zero_term)
{
    char term = zero_term ? '\0' : '\n';
    size_t n  = la->count;

    if (n == 0)
        return;

    if (repeat) {
        /* Pick with replacement */
        long out = 0;
        while (head_count <= 0 || out < head_count) {
            size_t idx = (size_t)rand() % n;
            fputs(la->lines[idx], stdout);
            putchar(term);
            out++;
            if (head_count <= 0) break; /* no -n with -r without -n: emit forever? */
        }
    } else {
        /* Already shuffled; emit up to head_count */
        size_t limit = (head_count > 0 && (size_t)head_count < n)
                       ? (size_t)head_count : n;
        for (size_t i = 0; i < limit; i++) {
            fputs(la->lines[i], stdout);
            putchar(term);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Usage / version                                                      */
/* ------------------------------------------------------------------ */

static void print_usage(void)
{
    puts("Usage: shuf [OPTION]... [FILE]");
    puts("       shuf -e [OPTION]... ARG...");
    puts("       shuf -i LO-HI [OPTION]...");
    puts("Write a random permutation of the input lines to standard output.");
    puts("");
    puts("  -e, --echo              treat each ARG as an input line");
    puts("  -i LO-HI, --input-range=LO-HI");
    puts("                          treat each number LO..HI as an input line");
    puts("  -n N, --head-count=N    output at most N lines");
    puts("  -r, --repeat            output lines can be repeated");
    puts("  -z, --zero-terminated   line delimiter is NUL, not newline");
    puts("  --help                  display this help and exit");
    puts("  --version               output version information and exit");
}

static void print_version(void)
{
    puts("shuf 1.0 (Winix 1.0)");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    bool  echo        = false;
    bool  range_mode  = false;
    long  range_lo    = 0;
    long  range_hi    = 0;
    long  head_count  = 0;  /* 0 means "all" */
    bool  repeat      = false;
    bool  zero_term   = false;

    srand((unsigned)time(NULL));

    int i = 1;
    while (i < argc) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "--echo") == 0) {
            echo = true;
            i++;
            break; /* remaining args are input lines */
        }
        if (strcmp(arg, "--repeat") == 0) {
            repeat = true;
            i++;
            continue;
        }
        if (strcmp(arg, "--zero-terminated") == 0) {
            zero_term = true;
            i++;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }
        /* --head-count=N */
        if (strncmp(arg, "--head-count=", 13) == 0) {
            char *end;
            head_count = strtol(arg + 13, &end, 10);
            if (*end != '\0' || head_count < 0) {
                fprintf(stderr, "shuf: invalid line count: '%s'\n", arg + 13);
                return 1;
            }
            i++;
            continue;
        }
        /* --input-range=LO-HI */
        if (strncmp(arg, "--input-range=", 14) == 0) {
            const char *rstr = arg + 14;
            char *dash = strchr(rstr, '-');
            if (!dash) {
                fprintf(stderr, "shuf: invalid range: '%s'\n", rstr);
                return 1;
            }
            char lo_buf[64], hi_buf[64];
            size_t lo_len = (size_t)(dash - rstr);
            if (lo_len >= sizeof(lo_buf)) { fprintf(stderr, "shuf: invalid range\n"); return 1; }
            memcpy(lo_buf, rstr, lo_len);
            lo_buf[lo_len] = '\0';
            strncpy(hi_buf, dash + 1, sizeof(hi_buf) - 1);
            hi_buf[sizeof(hi_buf)-1] = '\0';
            char *end1, *end2;
            range_lo = strtol(lo_buf, &end1, 10);
            range_hi = strtol(hi_buf, &end2, 10);
            if (*end1 || *end2 || range_lo > range_hi) {
                fprintf(stderr, "shuf: invalid range: '%s'\n", rstr);
                return 1;
            }
            range_mode = true;
            i++;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            const char *p = arg + 1;
            bool stop = false;
            while (*p && !stop) {
                char opt = *p;
                if (opt == 'e') {
                    echo = true;
                    i++;
                    stop = true;
                    goto outer; /* remaining args are lines */
                } else if (opt == 'r') {
                    repeat = true;
                    p++;
                } else if (opt == 'z') {
                    zero_term = true;
                    p++;
                } else if (opt == 'n') {
                    const char *nstr = p + 1;
                    if (*nstr == '\0') {
                        i++;
                        if (i >= argc) {
                            fprintf(stderr, "shuf: option requires an argument -- 'n'\n");
                            return 1;
                        }
                        nstr = argv[i];
                    }
                    char *end;
                    head_count = strtol(nstr, &end, 10);
                    if (*end != '\0' || head_count < 0) {
                        fprintf(stderr, "shuf: invalid line count: '%s'\n", nstr);
                        return 1;
                    }
                    stop = true;
                } else if (opt == 'i') {
                    /* -i LO-HI */
                    const char *rstr = p + 1;
                    if (*rstr == '\0') {
                        i++;
                        if (i >= argc) {
                            fprintf(stderr, "shuf: option requires an argument -- 'i'\n");
                            return 1;
                        }
                        rstr = argv[i];
                    }
                    char *dash = strchr(rstr, '-');
                    if (!dash) {
                        fprintf(stderr, "shuf: invalid range: '%s'\n", rstr);
                        return 1;
                    }
                    char lo_buf[64], hi_buf[64];
                    size_t lo_len = (size_t)(dash - rstr);
                    if (lo_len >= sizeof(lo_buf)) { fprintf(stderr, "shuf: invalid range\n"); return 1; }
                    memcpy(lo_buf, rstr, lo_len);
                    lo_buf[lo_len] = '\0';
                    strncpy(hi_buf, dash + 1, sizeof(hi_buf) - 1);
                    hi_buf[sizeof(hi_buf)-1] = '\0';
                    char *end1, *end2;
                    range_lo = strtol(lo_buf, &end1, 10);
                    range_hi = strtol(hi_buf, &end2, 10);
                    if (*end1 || *end2 || range_lo > range_hi) {
                        fprintf(stderr, "shuf: invalid range: '%s'\n", rstr);
                        return 1;
                    }
                    range_mode = true;
                    stop = true;
                } else {
                    fprintf(stderr, "shuf: invalid option -- '%c'\n", opt);
                    return 1;
                }
            }
            i++;
            continue;
        }
        break; /* non-option: file argument */
        outer: ;
    }

    /* ----------------------------------------------------------------
     * Build the line pool
     * ---------------------------------------------------------------- */
    Lines la;
    memset(&la, 0, sizeof(la));

    if (range_mode) {
        /* Generate integers LO..HI as strings */
        long total = range_hi - range_lo + 1;
        if (total > MAX_LINES) {
            fprintf(stderr, "shuf: range too large (max %d)\n", MAX_LINES);
            return 1;
        }
        la.owns_data = true;
        for (long v = range_lo; v <= range_hi; v++) {
            char num[32];
            snprintf(num, sizeof(num), "%ld", v);
            char *copy = strdup(num);
            if (!copy || !lines_push(&la, copy)) {
                if (copy) free(copy);
                lines_free(&la);
                fprintf(stderr, "shuf: out of memory\n");
                return 1;
            }
        }
    } else if (echo) {
        /* Each remaining argv is a line */
        la.owns_data = false; /* strings are argv — don't free them */
        for (; i < argc; i++) {
            if (!lines_push(&la, argv[i])) {
                lines_free(&la);
                return 1;
            }
        }
    } else {
        /* Read from file or stdin */
        la.owns_data = true;
        FILE *fin;
        bool  close_fin = false;

        if (i >= argc) {
            fin = stdin;
        } else {
            const char *path = argv[i];
            if (strcmp(path, "-") == 0) {
                fin = stdin;
            } else {
                fin = fopen(path, "r");
                if (!fin) {
                    fprintf(stderr, "shuf: %s: %s\n", path, strerror(errno));
                    return 1;
                }
                close_fin = true;
            }
        }

        if (!read_lines_from_file(fin, zero_term, &la)) {
            if (close_fin) fclose(fin);
            lines_free(&la);
            return 1;
        }
        if (close_fin) fclose(fin);
    }

    /* Shuffle (not needed for -r without prior shuffle, but harmless) */
    if (la.count > 1)
        shuffle(&la);

    /* Output */
    output_lines(&la, head_count, repeat, zero_term);

    lines_free(&la);
    return 0;
}
