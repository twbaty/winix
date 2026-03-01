/*
 * cut.c — Winix coreutil
 *
 * Implements POSIX cut: select bytes (-b), characters (-c), or fields (-f)
 * from each line of input.
 *
 * Usage: cut -b LIST [FILE...]
 *        cut -c LIST [FILE...]
 *        cut -f LIST [-d DELIM] [-s] [FILE...]
 *
 * LIST: comma-separated ranges, each one of:
 *   N      single 1-based position
 *   N-M    positions N through M inclusive
 *   N-     position N through end of line
 *   -M     position 1 through M
 *
 * Compile: C99, no dependencies beyond the C standard library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Range list                                                           */
/* ------------------------------------------------------------------ */

#define MAX_RANGES 64
#define BUF_SIZE   8192

typedef struct {
    int lo;  /* 1-based start; always >= 1 */
    int hi;  /* 1-based end;   0 means "to end of line" */
} Range;

static Range ranges[MAX_RANGES];
static int   nranges = 0;

/* Returns true if 1-based position pos falls inside any range. */
static bool selected(int pos)
{
    for (int i = 0; i < nranges; i++) {
        int lo = ranges[i].lo;
        int hi = ranges[i].hi;
        if (hi == 0) {
            if (pos >= lo) return true;
        } else {
            if (pos >= lo && pos <= hi) return true;
        }
    }
    return false;
}

/*
 * Parse a LIST string like "1,3-5,7-,2-4" into the global ranges[].
 * Returns true on success, false on error (message already printed).
 */
static bool parse_list(const char *list)
{
    /* Work on a writable copy. */
    char buf[BUF_SIZE];
    strncpy(buf, list, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *token = strtok(buf, ",");
    while (token != NULL) {
        if (nranges >= MAX_RANGES) {
            fprintf(stderr, "cut: too many ranges (max %d)\n", MAX_RANGES);
            return false;
        }

        int lo = 0, hi = 0;
        char *dash = strchr(token, '-');

        if (dash == NULL) {
            /* Plain number: N */
            lo = atoi(token);
            hi = lo;
        } else if (dash == token) {
            /* Leading dash: -M  →  1 to M */
            lo = 1;
            hi = atoi(dash + 1);
            if (hi == 0) {
                fprintf(stderr, "cut: invalid range: '%s'\n", token);
                return false;
            }
        } else {
            /* N- or N-M */
            *dash = '\0';
            lo = atoi(token);
            char *end_str = dash + 1;
            if (*end_str == '\0') {
                hi = 0; /* to end */
            } else {
                hi = atoi(end_str);
            }
        }

        if (lo <= 0) {
            fprintf(stderr, "cut: invalid range: '%s'\n", token);
            return false;
        }
        if (hi != 0 && hi < lo) {
            fprintf(stderr, "cut: invalid decreasing range\n");
            return false;
        }

        ranges[nranges].lo = lo;
        ranges[nranges].hi = hi;
        nranges++;

        token = strtok(NULL, ",");
    }

    if (nranges == 0) {
        fprintf(stderr, "cut: empty list\n");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Line processing                                                      */
/* ------------------------------------------------------------------ */

/* Strip trailing \r and \n in-place; return new length. */
static int strip_crlf(char *line)
{
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
    return len;
}

/*
 * Process one line in character/byte mode (-c / -b).
 * Outputs only selected 1-based character positions, then '\n'.
 */
static void process_char_line(const char *line, int len)
{
    for (int i = 0; i < len; i++) {
        if (selected(i + 1))   /* positions are 1-based */
            putchar(line[i]);
    }
    putchar('\n');
}

/*
 * Process one line in field mode (-f).
 * Splits on delim, outputs selected fields rejoined with delim.
 * If the line contains no delimiter:
 *   - if suppress is true: skip the line entirely
 *   - otherwise: output the whole line unchanged
 */
static void process_field_line(const char *line, int len,
                                char delim, bool suppress)
{
    /* Check whether the delimiter is present. */
    bool has_delim = false;
    for (int i = 0; i < len; i++) {
        if (line[i] == delim) { has_delim = true; break; }
    }

    if (!has_delim) {
        if (!suppress) {
            /* Output whole line unchanged. */
            fputs(line, stdout);
            putchar('\n');
        }
        return;
    }

    /*
     * Walk the line, tracking which field (1-based) we are in.
     * Collect pointers and lengths for each field, then output selected ones.
     *
     * We use a simple two-pass approach: first identify all fields,
     * then output the selected ones separated by delim.
     */

    /* First pass: count fields and locate them. */
#define MAX_FIELDS 4096
    const char *fstart[MAX_FIELDS];
    int         flen[MAX_FIELDS];
    int         nfields = 0;

    const char *p   = line;
    const char *end = line + len;

    while (p <= end && nfields < MAX_FIELDS) {
        const char *start = p;
        while (p < end && *p != delim)
            p++;
        fstart[nfields] = start;
        flen[nfields]   = (int)(p - start);
        nfields++;
        if (p < end)
            p++; /* skip the delimiter */
        else
            break;
    }

    /* Second pass: output selected fields. */
    bool first_output = true;
    for (int fi = 0; fi < nfields; fi++) {
        if (selected(fi + 1)) {
            if (!first_output)
                putchar(delim);
            fwrite(fstart[fi], 1, (size_t)flen[fi], stdout);
            first_output = false;
        }
    }
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/* Per-stream driver                                                    */
/* ------------------------------------------------------------------ */

typedef enum { MODE_CHAR, MODE_FIELD } CutMode;

static int process_stream(FILE *f, CutMode mode, char delim, bool suppress)
{
    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), f)) {
        int len = strip_crlf(line);
        if (mode == MODE_CHAR)
            process_char_line(line, len);
        else
            process_field_line(line, len, delim, suppress);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Usage / version                                                      */
/* ------------------------------------------------------------------ */

static void print_usage(void)
{
    puts("Usage: cut OPTION... [FILE...]");
    puts("Print selected parts of lines from each FILE to standard output.");
    puts("");
    puts("  -b LIST   select only these bytes (treated as characters)");
    puts("  -c LIST   select only these characters");
    puts("  -f LIST   select only these fields");
    puts("  -d DELIM  use DELIM as field delimiter (default: TAB)");
    puts("  -s        with -f: suppress lines with no delimiter");
    puts("  --help    display this help and exit");
    puts("  --version output version information and exit");
    puts("");
    puts("LIST is a comma-separated set of positions and ranges:");
    puts("  N      select position N");
    puts("  N-M    select positions N through M");
    puts("  N-     select positions N through end");
    puts("  -M     select positions 1 through M");
    puts("");
    puts("Positions are 1-based.");
}

static void print_version(void)
{
    puts("cut 1.0 (Winix 1.0)");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    CutMode mode      = MODE_CHAR;   /* -c / -b default */
    char    delim     = '\t';
    bool    suppress  = false;
    bool    mode_set  = false;
    bool    list_set  = false;
    int     first_file = -1;         /* index of first file argument */

    /* ---- Argument parsing ---- */
    int i = 1;
    while (i < argc) {
        char *arg = argv[i];

        /* Long options */
        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }

        /* End-of-options marker */
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }

        /* Short options */
        if (arg[0] == '-' && arg[1] != '\0') {
            const char *p = arg + 1;
            while (*p) {
                char opt = *p;

                if (opt == 'b' || opt == 'c') {
                    /* -b and -c are identical on this platform */
                    if (mode_set && mode != MODE_CHAR) {
                        fprintf(stderr, "cut: only one of -b, -c, or -f may be specified\n");
                        return 1;
                    }
                    mode = MODE_CHAR;
                    mode_set = true;

                    /* LIST follows immediately after flag char or as next token */
                    const char *list_arg = p + 1;
                    if (*list_arg == '\0') {
                        /* LIST is the next argv token */
                        i++;
                        if (i >= argc) {
                            fprintf(stderr, "cut: option requires an argument -- '%c'\n", opt);
                            return 1;
                        }
                        list_arg = argv[i];
                    }
                    if (!parse_list(list_arg)) return 1;
                    list_set = true;
                    goto next_arg;   /* consumed rest of this argv token */

                } else if (opt == 'f') {
                    if (mode_set && mode != MODE_FIELD) {
                        fprintf(stderr, "cut: only one of -b, -c, or -f may be specified\n");
                        return 1;
                    }
                    mode = MODE_FIELD;
                    mode_set = true;

                    const char *list_arg = p + 1;
                    if (*list_arg == '\0') {
                        i++;
                        if (i >= argc) {
                            fprintf(stderr, "cut: option requires an argument -- '%c'\n", opt);
                            return 1;
                        }
                        list_arg = argv[i];
                    }
                    if (!parse_list(list_arg)) return 1;
                    list_set = true;
                    goto next_arg;

                } else if (opt == 'd') {
                    const char *delim_arg = p + 1;
                    if (*delim_arg == '\0') {
                        i++;
                        if (i >= argc) {
                            fprintf(stderr, "cut: option requires an argument -- 'd'\n");
                            return 1;
                        }
                        delim_arg = argv[i];
                    }
                    if (delim_arg[0] == '\0') {
                        fprintf(stderr, "cut: the delimiter must be a single character\n");
                        return 1;
                    }
                    if (delim_arg[1] != '\0') {
                        fprintf(stderr, "cut: the delimiter must be a single character\n");
                        return 1;
                    }
                    delim = delim_arg[0];
                    goto next_arg;

                } else if (opt == 's') {
                    suppress = true;
                    p++;

                } else {
                    fprintf(stderr, "cut: invalid option -- '%c'\n", opt);
                    return 1;
                }
            }
        } else {
            /* Non-option: first file argument */
            if (first_file < 0) first_file = i;
            i++;
            continue;
        }

        i++;
        continue;
next_arg:
        i++;
    }

    /* Remaining args after '--' are files */
    if (first_file < 0 && i < argc)
        first_file = i;

    /* Validation */
    if (!list_set) {
        fprintf(stderr, "cut: you must specify a list of bytes, characters, or fields\n");
        return 1;
    }
    if (suppress && mode != MODE_FIELD) {
        fprintf(stderr, "cut: suppressing non-delimited lines makes sense only with -f\n");
        return 1;
    }

    /* ---- Process input ---- */
    int ret = 0;

    if (first_file < 0) {
        /* No file arguments: read stdin. */
        ret = process_stream(stdin, mode, delim, suppress);
    } else {
        for (int j = first_file; j < argc; j++) {
            const char *path = argv[j];
            FILE *f;
            if (strcmp(path, "-") == 0) {
                f = stdin;
            } else {
                f = fopen(path, "r");
                if (!f) {
                    fprintf(stderr, "cut: %s: %s\n", path, strerror(errno));
                    ret = 1;
                    continue;
                }
            }
            if (process_stream(f, mode, delim, suppress) != 0)
                ret = 1;
            if (f != stdin)
                fclose(f);
        }
    }

    return ret;
}
