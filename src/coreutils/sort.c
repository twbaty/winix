/*
 * sort — sort lines of text
 *
 * Usage: sort [OPTIONS] [FILE...]
 *   -n        numeric sort
 *   -r        reverse
 *   -u        unique (suppress duplicate output lines)
 *   -f        ignore case
 *   -s        stable sort
 *   -k N[,M]  sort key: field N (optionally through M); append n for numeric
 *   -t SEP    field separator (default: whitespace runs)
 *   --version / --help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>

/* WINIX_VERSION injected by CMake */
#ifndef WINIX_VERSION
#define WINIX_VERSION "unknown"
#endif

#define LINE_LEN 8192

static bool reverse_sort = false;
static bool unique_sort  = false;
static bool ignore_case  = false;
static bool numeric_sort = false;
static bool stable_sort  = false;
static char field_sep    = '\0';   /* '\0' = whitespace */
static int  key_start    = 0;      /* 1-based; 0 = whole line */
static int  key_end      = 0;      /* inclusive end field; 0 = to EOL */
static bool key_numeric  = false;  /* 'n' modifier in -k */

/* ── field helpers ──────────────────────────────────────────── */

/* Return pointer to the start of 1-based field fn in line.
 * Returns pointer past end-of-string if field doesn't exist. */
static const char *field_start(const char *line, int fn) {
    if (fn <= 0) return line;
    const char *p = line;
    int f = 1;
    if (field_sep == '\0') {
        /* skip leading whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        while (f < fn && *p) {
            while (*p && !isspace((unsigned char)*p)) p++;
            while (*p &&  isspace((unsigned char)*p)) p++;
            f++;
        }
    } else {
        while (f < fn && *p) {
            while (*p && *p != field_sep) p++;
            if (*p == field_sep) p++;
            f++;
        }
    }
    return p;
}

/* Return length of one field starting at p. */
static int field_len(const char *p) {
    int n = 0;
    if (field_sep == '\0') {
        while (p[n] && !isspace((unsigned char)p[n]) && p[n] != '\n' && p[n] != '\r') n++;
    } else {
        while (p[n] && p[n] != field_sep && p[n] != '\n' && p[n] != '\r') n++;
    }
    return n;
}

static double to_num(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return atof(s);
}

/* Extract sort key from line into buf[bufsz]. */
static void extract_key(const char *line, char *buf, size_t bufsz) {
    if (key_start <= 0) {
        /* whole line, strip trailing newline */
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) n--;
        if (n >= bufsz) n = bufsz - 1;
        memcpy(buf, line, n);
        buf[n] = '\0';
        return;
    }
    const char *fs = field_start(line, key_start);
    if (key_end > key_start) {
        const char *fe = field_start(line, key_end);
        int flen = field_len(fe);
        size_t total = (size_t)(fe - fs) + flen;
        if (total >= bufsz) total = bufsz - 1;
        memcpy(buf, fs, total);
        buf[total] = '\0';
    } else {
        int flen = field_len(fs);
        if ((size_t)flen >= bufsz) flen = (int)bufsz - 1;
        memcpy(buf, fs, flen);
        buf[flen] = '\0';
    }
}

/* ── comparator ─────────────────────────────────────────────── */

static int cmp(const void *pa, const void *pb) {
    const char *a = *(const char **)pa;
    const char *b = *(const char **)pb;
    int r;

    if (key_start > 0 || numeric_sort) {
        char ka[LINE_LEN], kb[LINE_LEN];
        extract_key(a, ka, sizeof(ka));
        extract_key(b, kb, sizeof(kb));

        if (numeric_sort || key_numeric) {
            double da = to_num(ka), db = to_num(kb);
            r = (da > db) - (da < db);
        } else {
            r = ignore_case ? _stricmp(ka, kb) : strcmp(ka, kb);
        }
    } else {
        r = ignore_case ? _stricmp(a, b) : strcmp(a, b);
    }

    /* fallback to whole-line comparison for stability key */
    if (r == 0 && key_start > 0)
        r = ignore_case ? _stricmp(a, b) : strcmp(a, b);

    return reverse_sort ? -r : r;
}

/* ── stable merge sort ──────────────────────────────────────── */

static void merge(char **arr, char **tmp, size_t lo, size_t mid, size_t hi) {
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi)
        tmp[k++] = (cmp(&arr[i], &arr[j]) <= 0) ? arr[i++] : arr[j++];
    while (i < mid) tmp[k++] = arr[i++];
    while (j < hi)  tmp[k++] = arr[j++];
    for (size_t x = lo; x < hi; x++) arr[x] = tmp[x];
}

static void msort(char **arr, char **tmp, size_t lo, size_t hi) {
    if (hi - lo < 2) return;
    size_t mid = lo + (hi - lo) / 2;
    msort(arr, tmp, lo, mid);
    msort(arr, tmp, mid, hi);
    merge(arr, tmp, lo, mid, hi);
}

static void do_stable_sort(char **arr, size_t n) {
    char **tmp = malloc(n * sizeof(char *));
    if (!tmp) { fprintf(stderr, "sort: out of memory\n"); exit(1); }
    msort(arr, tmp, 0, n);
    free(tmp);
}

/* ── line storage (dynamic) ─────────────────────────────────── */

static char  **lines = NULL;
static size_t  nlines = 0;
static size_t  cap    = 0;

static void push_line(const char *s) {
    if (nlines == cap) {
        size_t newcap = cap ? cap * 2 : 65536;
        char **nd = realloc(lines, newcap * sizeof(char *));
        if (!nd) { fprintf(stderr, "sort: out of memory\n"); exit(1); }
        lines = nd;
        cap   = newcap;
    }
    lines[nlines] = strdup(s);
    if (!lines[nlines]) { fprintf(stderr, "sort: out of memory\n"); exit(1); }
    nlines++;
}

static void read_stream(FILE *f) {
    char buf[LINE_LEN];
    while (fgets(buf, sizeof(buf), f))
        push_line(buf);
}

/* ── main ───────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    int argi = 1;

    {
        const char *wcase = getenv("WINIX_CASE");
        if (wcase && strcmp(wcase, "off") == 0) ignore_case = true;
    }

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        if (strcmp(argv[argi], "--") == 0) { argi++; break; }

        if (strcmp(argv[argi], "--version") == 0) {
            printf("sort (Winix) %s\n", WINIX_VERSION); return 0;
        }
        if (strcmp(argv[argi], "--help") == 0) {
            fprintf(stderr,
                "Usage: sort [OPTIONS] [FILE...]\n\n"
                "  -n        numeric sort\n"
                "  -r        reverse\n"
                "  -u        unique\n"
                "  -f        ignore case\n"
                "  -s        stable sort\n"
                "  -k N[,M]  sort key field N (through M); append n for numeric\n"
                "  -t SEP    field separator\n"
                "  --version / --help\n");
            return 0;
        }

        /* -t SEP */
        if (argv[argi][1] == 't') {
            if (argv[argi][2]) {
                field_sep = argv[argi][2];
            } else if (argi + 1 < argc) {
                field_sep = argv[++argi][0];
            } else {
                fprintf(stderr, "sort: option -t requires an argument\n");
                return 1;
            }
            continue;
        }

        /* -k N[n][,M[n]] */
        if (argv[argi][1] == 'k') {
            const char *spec = argv[argi][2] ? argv[argi] + 2
                             : (argi + 1 < argc ? argv[++argi] : NULL);
            if (!spec) { fprintf(stderr, "sort: option -k requires an argument\n"); return 1; }
            char *end;
            key_start = (int)strtol(spec, &end, 10);
            if (*end == 'n') { key_numeric = true; end++; }
            if (*end == ',') {
                end++;
                key_end = (int)strtol(end, &end, 10);
                if (*end == 'n') { key_numeric = true; end++; }
            }
            continue;
        }

        /* combined short flags */
        for (const char *p = argv[argi] + 1; *p; p++) {
            switch (*p) {
                case 'r': reverse_sort = true; break;
                case 'u': unique_sort  = true; break;
                case 'f': ignore_case  = true; break;
                case 'n': numeric_sort = true; break;
                case 's': stable_sort  = true; break;
                default:
                    fprintf(stderr, "sort: invalid option -- '%c'\n", *p);
                    return 1;
            }
        }
    }

    int ret = 0;
    if (argi >= argc) {
        read_stream(stdin);
    } else {
        for (int i = argi; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) {
                fprintf(stderr, "sort: %s: %s\n", argv[i], strerror(errno));
                ret = 1;
                continue;
            }
            read_stream(f);
            fclose(f);
        }
    }

    if (stable_sort || key_start > 0) {
        do_stable_sort(lines, nlines);
    } else {
        qsort(lines, nlines, sizeof(char *), cmp);
    }

    char *last = NULL;
    for (size_t i = 0; i < nlines; i++) {
        if (unique_sort && last != NULL) {
            char ka[LINE_LEN], kb[LINE_LEN];
            extract_key(lines[i], ka, sizeof(ka));
            extract_key(last,     kb, sizeof(kb));
            int eq = numeric_sort || key_numeric
                   ? (to_num(ka) == to_num(kb))
                   : (ignore_case ? _stricmp(ka, kb) : strcmp(ka, kb)) == 0;
            if (eq) { free(lines[i]); continue; }
        }
        fputs(lines[i], stdout);
        last = lines[i];
    }

    free(lines);
    return ret;
}
