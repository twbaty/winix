/*
 * column.c — Winix coreutil
 *
 * Usage: column [-t] [-s SEP] [FILE...]
 *
 * Without -t: arrange items (one per line) into multiple terminal columns.
 * With    -t: format lines as an aligned table (split on whitespace or SEP).
 *
 * Options:
 *   -t           table mode — align fields into columns
 *   -s SEP       field separator character(s) for -t mode (default: whitespace)
 *   --help       display this help and exit
 *   --version    output version information and exit
 *
 * Exit codes: 0 success, 1 error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_FIELDS  256
#define MAX_LINES  8192
#define TERM_WIDTH   80   /* default if we can't query the terminal */

static void usage(void) {
    puts("Usage: column [-t] [-s SEP] [FILE...]");
    puts("Format input as a table or arrange items into multiple columns.");
    puts("");
    puts("  -t       table mode: align fields into columns");
    puts("  -s SEP   field separator for -t mode (default: whitespace)");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
}

/* ── Field splitting ────────────────────────────────────────────────────── */

static int split_fields(const char *line, const char *sep,
                        char **fields, int max_fields) {
    int n = 0;
    const char *p = line;

    if (!sep) {
        /* Whitespace split — skip leading, split on runs */
        while (*p && n < max_fields) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            int len = (int)(p - start);
            fields[n] = (char *)malloc(len + 1);
            if (!fields[n]) break;
            memcpy(fields[n], start, len);
            fields[n][len] = '\0';
            n++;
        }
    } else {
        /* Character-set split — split on any char in sep */
        while (n < max_fields) {
            const char *end = strpbrk(p, sep);
            int len = end ? (int)(end - p) : (int)strlen(p);
            fields[n] = (char *)malloc(len + 1);
            if (!fields[n]) break;
            memcpy(fields[n], p, len);
            fields[n][len] = '\0';
            n++;
            if (!end) break;
            p = end + 1;
        }
    }
    return n;
}

/* ── Table mode (-t) ────────────────────────────────────────────────────── */

typedef struct {
    char **fields;
    int    nfields;
} Row;

static void table_mode(char **lines, int nlines, const char *sep) {
    Row *rows = (Row *)calloc(nlines, sizeof(Row));
    if (!rows) { fputs("column: out of memory\n", stderr); return; }

    int col_widths[MAX_FIELDS] = {0};
    int max_cols = 0;

    for (int i = 0; i < nlines; i++) {
        rows[i].fields = (char **)calloc(MAX_FIELDS, sizeof(char *));
        if (!rows[i].fields) continue;
        rows[i].nfields = split_fields(lines[i], sep, rows[i].fields, MAX_FIELDS);
        if (rows[i].nfields > max_cols) max_cols = rows[i].nfields;
        for (int j = 0; j < rows[i].nfields; j++) {
            int w = (int)strlen(rows[i].fields[j]);
            if (w > col_widths[j]) col_widths[j] = w;
        }
    }

    for (int i = 0; i < nlines; i++) {
        for (int j = 0; j < rows[i].nfields; j++) {
            if (j == rows[i].nfields - 1) {
                /* Last field: no trailing padding */
                fputs(rows[i].fields[j], stdout);
            } else {
                printf("%-*s  ", col_widths[j], rows[i].fields[j]);
            }
        }
        putchar('\n');
        for (int j = 0; j < rows[i].nfields; j++) free(rows[i].fields[j]);
        free(rows[i].fields);
    }
    free(rows);
}

/* ── Multi-column mode (default) ────────────────────────────────────────── */

static void multicolumn_mode(char **items, int nitems, int term_width) {
    if (nitems == 0) return;

    /* Find maximum item width */
    int max_w = 0;
    for (int i = 0; i < nitems; i++) {
        int w = (int)strlen(items[i]);
        if (w > max_w) max_w = w;
    }

    /* Compute how many columns fit (each col = max_w + 2-space gap) */
    int col_width = max_w + 2;
    int ncols = term_width / col_width;
    if (ncols < 1) ncols = 1;

    /* Number of rows needed */
    int nrows = (nitems + ncols - 1) / ncols;

    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            int idx = c * nrows + r;
            if (idx >= nitems) break;
            bool last_in_row = (c == ncols - 1 || (c + 1) * nrows + r >= nitems);
            if (last_in_row)
                printf("%s", items[idx]);
            else
                printf("%-*s", col_width, items[idx]);
        }
        putchar('\n');
    }
}

/* ── Read all lines from stream ─────────────────────────────────────────── */

static int read_lines(FILE *f, char **buf, int max) {
    int n = 0;
    char line[4096];
    while (n < max && fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        buf[n] = (char *)malloc(len + 1);
        if (!buf[n]) break;
        memcpy(buf[n], line, len + 1);
        n++;
    }
    return n;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    bool        table    = false;
    const char *sep      = NULL;
    int         first_file = argc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { puts("column 1.0 (Winix 1.4)"); return 0; }
        if (strcmp(argv[i], "-t") == 0) { table = true; continue; }
        if (strcmp(argv[i], "-s") == 0) {
            if (++i >= argc) { fputs("column: -s requires an argument\n", stderr); return 1; }
            sep = argv[i]; continue;
        }
        if (strncmp(argv[i], "-s", 2) == 0) { sep = argv[i] + 2; continue; }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "column: invalid option -- '%s'\n", argv[i]);
            return 1;
        }
        first_file = i;
        break;
    }

    char **lines = (char **)calloc(MAX_LINES, sizeof(char *));
    if (!lines) { fputs("column: out of memory\n", stderr); return 1; }
    int nlines = 0;

    if (first_file >= argc) {
        nlines = read_lines(stdin, lines, MAX_LINES);
    } else {
        for (int i = first_file; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) { fprintf(stderr, "column: %s: No such file or directory\n", argv[i]); continue; }
            nlines += read_lines(f, lines + nlines, MAX_LINES - nlines);
            fclose(f);
        }
    }

    if (table)
        table_mode(lines, nlines, sep);
    else
        multicolumn_mode(lines, nlines, TERM_WIDTH);

    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    return 0;
}
