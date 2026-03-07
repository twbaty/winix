/*
 * csplit — split a file into sections determined by context lines
 *
 * Usage: csplit [OPTIONS] FILE PATTERN ...
 *   Patterns:
 *     N          split before line N
 *     /REGEX/    split before the next line matching REGEX
 *     %REGEX%    skip to match, then split (no output for skipped part)
 *     /REGEX/+N  split N lines after match
 *     /REGEX/-N  split N lines before match
 *     {N}        repeat previous pattern N times
 *     {*}        repeat previous pattern as many times as possible
 *
 *   -f PREFIX    output file prefix (default "xx")
 *   -n N         use N digits in output filenames (default 2)
 *   -b SUFFIX    use SUFFIX as output filename suffix (printf format with %d)
 *   -k           keep output files on error
 *   -z           remove empty output files
 *   -q / -s      suppress byte counts
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Simple POSIX-subset regex: literal string matching + ^ $ . * [] */
/* We implement just enough for common csplit usage */

#define VERSION "1.0"
#define MAX_LINES 1048576
#define MAX_PATS  256

static const char *g_prefix  = "xx";
static int         g_digits  = 2;
static const char *g_suffix  = NULL; /* NULL = use digits */
static int         g_keep    = 0;
static int         g_elide   = 0;  /* -z: remove empty files */
static int         g_quiet   = 0;

/* ── Tiny regex engine ───────────────────────────────────────── */

static int re_match(const char *pat, const char *str) {
    /* Handles: literals, ., *, ^, $, [...] */
    if (*pat == '^') return re_match(pat + 1, str);  /* anchor handled by caller */
    if (!*pat) return 1;
    if (*pat == '$') return !*str;
    if (*pat == '\\' && *(pat+1)) {
        if (!*str) return 0;
        if (*str != *(pat+1)) return 0;
        return re_match(pat + 2, str + 1);
    }
    if (*(pat+1) == '*') {
        /* zero or more of pat[0] */
        if (re_match(pat + 2, str)) return 1;
        while (*str && (*pat == '.' || *pat == *str)) {
            str++;
            if (re_match(pat + 2, str)) return 1;
        }
        return 0;
    }
    if (*pat == '.') {
        if (!*str) return 0;
        return re_match(pat + 1, str + 1);
    }
    if (*pat == '[') {
        const char *end = strchr(pat + 1, ']');
        if (!end || !*str) return 0;
        int negate = (*(pat+1) == '^');
        const char *cls = pat + 1 + negate;
        int found = 0;
        while (cls < end) {
            if (*(cls+1) == '-' && cls + 2 < end) {
                if ((unsigned char)*str >= (unsigned char)*cls &&
                    (unsigned char)*str <= (unsigned char)*(cls+2)) found = 1;
                cls += 3;
            } else {
                if (*str == *cls) found = 1;
                cls++;
            }
        }
        if (found == negate) return 0;
        return re_match(end + 1, str + 1);
    }
    if (!*str || *pat != *str) return 0;
    return re_match(pat + 1, str + 1);
}

static int regex_match(const char *pat, const char *line) {
    /* Strip trailing \n */
    char buf[4096];
    strncpy(buf, line, sizeof(buf) - 1); buf[sizeof(buf)-1] = '\0';
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';

    int anchored = (*pat == '^');
    const char *p = anchored ? pat + 1 : pat;
    if (anchored) return re_match(p, buf);
    /* Try matching at every position */
    for (int i = 0; i <= len; i++)
        if (re_match(p, buf + i)) return 1;
    return 0;
}

/* ── Output file management ──────────────────────────────────── */

static int   g_filenum = 0;
static char **g_filenames = NULL;
static int   g_nfiles    = 0;
static int   g_fnames_cap = 0;

static char *make_filename(void) {
    char *buf = malloc(512);
    if (g_suffix) {
        char fmt[256];
        snprintf(fmt, sizeof(fmt), "%s%s", g_prefix, g_suffix);
        snprintf(buf, 512, fmt, g_filenum);
    } else {
        char fmt[64];
        snprintf(fmt, sizeof(fmt), "%%s%%0%dd", g_digits);
        snprintf(buf, 512, fmt, g_prefix, g_filenum);
    }
    g_filenum++;
    return buf;
}

static void register_file(const char *name) {
    if (g_nfiles >= g_fnames_cap) {
        g_fnames_cap = g_fnames_cap ? g_fnames_cap * 2 : 32;
        g_filenames = realloc(g_filenames, (size_t)g_fnames_cap * sizeof(char *));
    }
    g_filenames[g_nfiles++] = strdup(name);
}

static void cleanup_files(void) {
    for (int i = 0; i < g_nfiles; i++) {
        remove(g_filenames[i]);
        free(g_filenames[i]);
    }
    free(g_filenames);
}

/* ── Write a section ─────────────────────────────────────────── */

static int write_section(char **lines, int from, int to) {
    /* from inclusive, to exclusive */
    if (g_elide && from >= to) return 0; /* skip empty */

    char *fname = make_filename();
    register_file(fname);

    FILE *fp = fopen(fname, "w");
    if (!fp) { perror(fname); free(fname); return 1; }

    long bytes = 0;
    for (int i = from; i < to; i++) {
        fputs(lines[i], fp);
        bytes += (long)strlen(lines[i]);
    }
    fclose(fp);

    if (!g_quiet) printf("%ld\n", bytes);
    free(fname);
    return 0;
}

/* ── Pattern types ───────────────────────────────────────────── */

typedef struct {
    int   type;    /* 'N'=line num, '/'=regex keep, '%'=regex skip */
    int   linenum; /* for type 'N' */
    char  pat[512];
    int   offset;  /* +N or -N */
    int   repeat;  /* {N} or -1 for {*} */
} Pat;

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("csplit %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: csplit [OPTIONS] FILE PATTERN ...\n\n"
                "Split FILE at lines matching PATTERNs.\n\n"
                "  -f PREFIX   output prefix (default 'xx')\n"
                "  -n N        digits in output names (default 2)\n"
                "  -b FMT      suffix format string (e.g. %%02d.txt)\n"
                "  -k          keep files on error\n"
                "  -z          remove empty output files\n"
                "  -q, -s      suppress byte counts\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 'k': g_keep  = 1; break;
                case 'z': g_elide = 1; break;
                case 'q': case 's': g_quiet = 1; break;
                case 'f': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "csplit: option requires argument -- 'f'\n"); return 1; }
                    g_prefix = v; p = v + strlen(v) - 1; break;
                }
                case 'n': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "csplit: option requires argument -- 'n'\n"); return 1; }
                    g_digits = atoi(v); p = v + strlen(v) - 1; break;
                }
                case 'b': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "csplit: option requires argument -- 'b'\n"); return 1; }
                    g_suffix = v; p = v + strlen(v) - 1; break;
                }
                default: fprintf(stderr, "csplit: invalid option -- '%c'\n", *p); return 1;
            }
        }
    }

    if (argi >= argc) { fprintf(stderr, "csplit: missing operand\n"); return 1; }
    const char *filename = argv[argi++];

    /* Read input into memory */
    FILE *fp = strcmp(filename, "-") ? fopen(filename, "r") : stdin;
    if (!fp) { perror(filename); return 1; }

    char **lines = NULL;
    int nlines = 0, lcap = 0;
    char lbuf[65536];
    while (fgets(lbuf, sizeof(lbuf), fp)) {
        if (nlines >= lcap) {
            lcap = lcap ? lcap * 2 : 1024;
            lines = realloc(lines, (size_t)lcap * sizeof(char *));
        }
        lines[nlines++] = strdup(lbuf);
    }
    if (fp != stdin) fclose(fp);

    /* Parse patterns */
    Pat pats[MAX_PATS];
    int npats = 0;

    for (; argi < argc && npats < MAX_PATS; argi++) {
        const char *a = argv[argi];
        Pat *pt = &pats[npats];
        pt->repeat = 1; pt->offset = 0;

        /* Check for {N} or {*} repetition on previous pattern */
        if (*a == '{') {
            if (npats == 0) { fprintf(stderr, "csplit: no pattern to repeat\n"); return 1; }
            const char *inner = a + 1;
            if (*inner == '*') pt->repeat = -1;
            else pt->repeat = atoi(inner);
            /* Duplicate previous pattern with new repeat */
            pats[npats] = pats[npats - 1];
            pats[npats].repeat = pt->repeat;
            npats++;
            continue;
        }

        if (*a == '/' || *a == '%') {
            char delim = *a;
            pt->type = delim;
            const char *end = strrchr(a + 1, delim);
            if (!end) { fprintf(stderr, "csplit: invalid pattern '%s'\n", a); return 1; }
            int plen = (int)(end - (a + 1));
            strncpy(pt->pat, a + 1, (size_t)(plen < 511 ? plen : 511));
            pt->pat[plen < 511 ? plen : 511] = '\0';
            /* Check for offset */
            const char *after = end + 1;
            if (*after == '+' || *after == '-') pt->offset = atoi(after);
        } else if (isdigit((unsigned char)*a) || *a == '+') {
            pt->type = 'N';
            pt->linenum = atoi(a);
        } else {
            fprintf(stderr, "csplit: invalid pattern '%s'\n", a); return 1;
        }
        npats++;
    }

    /* Execute patterns */
    int cur = 0;  /* current line index (0-based) */
    int ret = 0;

    for (int pi = 0; pi < npats; pi++) {
        Pat *pt = &pats[pi];
        int reps = (pt->repeat == -1) ? nlines : pt->repeat;

        for (int r = 0; r < reps; r++) {
            int split_at = -1;

            if (pt->type == 'N') {
                split_at = pt->linenum - 1; /* convert to 0-based */
                if (split_at > nlines) split_at = nlines;
            } else {
                /* Search for regex from cur */
                int found = -1;
                for (int li = cur; li < nlines; li++) {
                    if (regex_match(pt->pat, lines[li])) { found = li; break; }
                }
                if (found < 0) {
                    if (pt->repeat == -1) goto done_pat; /* {*} exhausted */
                    fprintf(stderr, "csplit: '%s': pattern not found\n", pt->pat);
                    ret = 1;
                    if (!g_keep) { cleanup_files(); goto cleanup; }
                    goto done_pat;
                }
                split_at = found + pt->offset;
                if (split_at < cur) split_at = cur;
                if (split_at > nlines) split_at = nlines;
            }

            if (pt->type == '%') {
                /* Skip section — no output */
                cur = split_at;
            } else {
                if (write_section(lines, cur, split_at)) {
                    ret = 1;
                    if (!g_keep) { cleanup_files(); goto cleanup; }
                }
                cur = split_at;
            }
        }
        done_pat:;
    }

    /* Write final section */
    if (write_section(lines, cur, nlines)) ret = 1;

cleanup:
    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    free(g_filenames);
    return ret;
}
