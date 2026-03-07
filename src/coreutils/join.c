/*
 * join — join lines of two files on a common field
 *
 * Usage: join [OPTIONS] FILE1 FILE2
 *   -1 FIELD   join on this field from FILE1 (default 1)
 *   -2 FIELD   join on this field from FILE2 (default 1)
 *   -j FIELD   equivalent to -1 FIELD -2 FIELD
 *   -t CHAR    field delimiter (default: whitespace)
 *   -i         ignore case in comparisons
 *   -a {1|2}   print unpairable lines from file 1 or 2
 *   -v {1|2}   like -a but suppress joined output
 *   -e STR     replace missing fields with STR
 *   -o FORMAT  output format (e.g. 1.2,2.3,0)
 *   --version / --help
 *
 * Both files must be sorted on the join field.
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define VERSION "1.0"
#define MAX_FIELDS 256
#define MAX_LINE   8192

static int    g_f1      = 1;    /* 1-based join field for file 1 */
static int    g_f2      = 1;    /* 1-based join field for file 2 */
static int    g_delim   = 0;    /* 0 = whitespace, else char */
static int    g_icase   = 0;
static int    g_unpair1 = 0;    /* -a 1 or -v 1 */
static int    g_unpair2 = 0;    /* -a 2 or -v 2 */
static int    g_suppress= 0;    /* -v: suppress joined output */
static char  *g_empty   = "";
static char  *g_outfmt  = NULL; /* raw -o format string */

/* ── Field splitting ─────────────────────────────────────────── */

static int split(char *line, char **fields, int maxf, int delim) {
    /* strip trailing newline */
    int len = (int)strlen(line);
    while (len > 0 && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]='\0';

    int n = 0;
    if (delim == 0) {
        /* whitespace split */
        char *p = line;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            if (n < maxf) fields[n++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) *p++ = '\0';
        }
    } else {
        char *p = line;
        while (1) {
            if (n < maxf) fields[n++] = p;
            char *q = strchr(p, delim);
            if (!q) break;
            *q = '\0'; p = q + 1;
        }
    }
    return n;
}

static const char *getfield(char **fields, int nf, int idx) {
    /* idx is 1-based */
    if (idx < 1 || idx > nf) return g_empty;
    return fields[idx - 1];
}

/* ── Output ──────────────────────────────────────────────────── */

static void print_line(char **f1, int n1, char **f2, int n2, int joined) {
    char dc = g_delim ? (char)g_delim : ' ';

    if (g_outfmt) {
        /* Parse format: comma-separated tokens like 1.2, 2.3, 0 */
        char fmt[4096];
        strncpy(fmt, g_outfmt, sizeof(fmt)-1);
        fmt[sizeof(fmt)-1] = '\0';
        char *tok = strtok(fmt, ",");
        int first = 1;
        while (tok) {
            const char *val;
            if (!strcmp(tok, "0")) {
                /* join field */
                val = joined ? getfield(f1, n1, g_f1) : (f1 ? getfield(f1, n1, g_f1) : getfield(f2, n2, g_f2));
            } else {
                int file = atoi(tok);
                char *dot = strchr(tok, '.');
                int field = dot ? atoi(dot+1) : 1;
                if (file == 1) val = getfield(f1, n1, field);
                else           val = getfield(f2, n2, field);
            }
            if (!first) putchar(dc);
            fputs(val, stdout);
            first = 0;
            tok = strtok(NULL, ",");
        }
        putchar('\n');
        return;
    }

    /* Default output: join-field, then all other fields from f1, then all from f2 */
    const char *jv = f1 ? getfield(f1, n1, g_f1) : getfield(f2, n2, g_f2);
    fputs(jv, stdout);

    /* file 1 fields except join field */
    for (int i = 1; i <= n1; i++) {
        if (i == g_f1) continue;
        putchar(dc);
        fputs(getfield(f1, n1, i), stdout);
    }
    /* file 2 fields except join field */
    for (int i = 1; i <= n2; i++) {
        if (i == g_f2) continue;
        putchar(dc);
        fputs(getfield(f2, n2, i), stdout);
    }
    putchar('\n');
}

static void print_unpaired(char **fields, int nf, int which) {
    char dc = g_delim ? (char)g_delim : ' ';
    if (g_outfmt) {
        char fmt[4096];
        strncpy(fmt, g_outfmt, sizeof(fmt)-1);
        fmt[sizeof(fmt)-1] = '\0';
        char *tok = strtok(fmt, ",");
        int first = 1;
        while (tok) {
            int file = atoi(tok);
            char *dot = strchr(tok, '.');
            int field = dot ? atoi(dot+1) : 1;
            const char *val;
            if (!strcmp(tok, "0") || file == which) val = getfield(fields, nf, field);
            else val = g_empty;
            if (!first) putchar(dc);
            fputs(val, stdout);
            first = 0;
            tok = strtok(NULL, ",");
        }
        putchar('\n');
        return;
    }
    for (int i = 0; i < nf; i++) {
        if (i) putchar(dc);
        fputs(fields[i], stdout);
    }
    putchar('\n');
}

/* ── Main ────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("join %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: join [OPTIONS] FILE1 FILE2\n\n"
                "Join lines on a common field (files must be sorted).\n\n"
                "  -1 N   join on field N of FILE1 (default 1)\n"
                "  -2 N   join on field N of FILE2 (default 1)\n"
                "  -j N   equivalent to -1 N -2 N\n"
                "  -t C   field delimiter character (default: whitespace)\n"
                "  -i     ignore case\n"
                "  -a {1|2}  also print unpaired lines from file 1 or 2\n"
                "  -v {1|2}  like -a but suppress matched output\n"
                "  -e STR    replace empty fields with STR\n"
                "  -o FMT    output format (e.g. 1.1,2.2,0)\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }

        if (!strcmp(a, "-1")) {
            if (++argi >= argc) { fprintf(stderr, "join: option requires argument -- '1'\n"); return 1; }
            g_f1 = atoi(argv[argi]);
        } else if (!strcmp(a, "-2")) {
            if (++argi >= argc) { fprintf(stderr, "join: option requires argument -- '2'\n"); return 1; }
            g_f2 = atoi(argv[argi]);
        } else if (!strcmp(a, "-j")) {
            if (++argi >= argc) { fprintf(stderr, "join: option requires argument -- 'j'\n"); return 1; }
            g_f1 = g_f2 = atoi(argv[argi]);
        } else if (!strcmp(a, "-t")) {
            if (++argi >= argc) { fprintf(stderr, "join: option requires argument -- 't'\n"); return 1; }
            g_delim = (unsigned char)argv[argi][0];
        } else if (!strcmp(a, "-i")) {
            g_icase = 1;
        } else if (!strcmp(a, "-a")) {
            if (++argi >= argc) { fprintf(stderr, "join: option requires argument -- 'a'\n"); return 1; }
            int which = atoi(argv[argi]);
            if (which == 1) g_unpair1 = 1;
            else if (which == 2) g_unpair2 = 1;
            else { fprintf(stderr, "join: invalid file number for -a: %s\n", argv[argi]); return 1; }
        } else if (!strcmp(a, "-v")) {
            if (++argi >= argc) { fprintf(stderr, "join: option requires argument -- 'v'\n"); return 1; }
            int which = atoi(argv[argi]);
            g_suppress = 1;
            if (which == 1) g_unpair1 = 1;
            else if (which == 2) g_unpair2 = 1;
            else { fprintf(stderr, "join: invalid file number for -v: %s\n", argv[argi]); return 1; }
        } else if (!strcmp(a, "-e")) {
            if (++argi >= argc) { fprintf(stderr, "join: option requires argument -- 'e'\n"); return 1; }
            g_empty = argv[argi];
        } else if (!strcmp(a, "-o")) {
            if (++argi >= argc) { fprintf(stderr, "join: option requires argument -- 'o'\n"); return 1; }
            g_outfmt = argv[argi];
        } else {
            fprintf(stderr, "join: unrecognized option '%s'\n", a); return 1;
        }
    }

    if (argi + 2 > argc) {
        fprintf(stderr, "join: missing operand\n"); return 1;
    }

    const char *path1 = argv[argi];
    const char *path2 = argv[argi+1];

    FILE *f1 = strcmp(path1, "-") ? fopen(path1, "r") : stdin;
    FILE *f2 = strcmp(path2, "-") ? fopen(path2, "r") : stdin;
    if (!f1) { perror(path1); return 1; }
    if (!f2) { perror(path2); return 1; }

    char line1[MAX_LINE], line2[MAX_LINE];
    char *fields1[MAX_FIELDS], *fields2[MAX_FIELDS];
    int n1 = 0, n2 = 0;
    int have1 = 0, have2 = 0;

    /* Read first lines */
    if (fgets(line1, sizeof(line1), f1)) { n1 = split(line1, fields1, MAX_FIELDS, g_delim); have1 = 1; }
    if (fgets(line2, sizeof(line2), f2)) { n2 = split(line2, fields2, MAX_FIELDS, g_delim); have2 = 1; }

    while (have1 || have2) {
        int cmp;
        if (!have1) cmp = 1;
        else if (!have2) cmp = -1;
        else {
            const char *k1 = getfield(fields1, n1, g_f1);
            const char *k2 = getfield(fields2, n2, g_f2);
            cmp = g_icase ? _stricmp(k1, k2) : strcmp(k1, k2);
        }

        if (cmp == 0) {
            /* matched — save key, advance both; handle multi-match by buffering */
            if (!g_suppress) print_line(fields1, n1, fields2, n2, 1);
            /* advance file1 */
            if (fgets(line1, sizeof(line1), f1)) { n1 = split(line1, fields1, MAX_FIELDS, g_delim); have1 = 1; }
            else have1 = 0;
            /* advance file2 */
            if (fgets(line2, sizeof(line2), f2)) { n2 = split(line2, fields2, MAX_FIELDS, g_delim); have2 = 1; }
            else have2 = 0;
        } else if (cmp < 0) {
            /* file1 key is smaller — unpaired from file1 */
            if (g_unpair1) print_unpaired(fields1, n1, 1);
            if (fgets(line1, sizeof(line1), f1)) { n1 = split(line1, fields1, MAX_FIELDS, g_delim); have1 = 1; }
            else have1 = 0;
        } else {
            /* file2 key is smaller — unpaired from file2 */
            if (g_unpair2) print_unpaired(fields2, n2, 2);
            if (fgets(line2, sizeof(line2), f2)) { n2 = split(line2, fields2, MAX_FIELDS, g_delim); have2 = 1; }
            else have2 = 0;
        }
    }

    if (f1 != stdin) fclose(f1);
    if (f2 != stdin) fclose(f2);
    return 0;
}
