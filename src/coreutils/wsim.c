/*
 * wsim — Winix similarity scorer  v0.1
 *
 * Reads a wlint --scan-json inventory and scores files for likely
 * similarity (probable duplicates, variant copies, renamed originals).
 * Never takes destructive action — read-only, no file modifications.
 *
 * Usage:
 *   wsim [options] <scan.json>
 *       --out FILE        write JSON results to FILE (default: stdout)
 *       --min-score N     minimum score to report (0.0–1.0, default: 0.65)
 *       --csv FILE        write CSV results to FILE
 *   -v  --verbose         progress output to stderr
 *       --version
 *       --help
 *
 * Scoring model (weights):
 *   basename similarity  50%
 *   extension match      15%
 *   size similarity      25%
 *   mtime proximity      10%
 *
 * Candidate reduction (blocking):
 *   Only pairs with same extension, size within ±20%,
 *   and shared first name token are scored.
 *
 * Name normalization before scoring:
 *   lowercase, strip extension, replace _ and - with spaces,
 *   remove copy markers ((1), - copy, copy of, etc.),
 *   remove version markers (v2, final, draft, etc.),
 *   collapse whitespace.
 *
 * Exit codes:  0 = no candidates found
 *              1 = candidates found
 *              2 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <io.h>

/* ── ANSI colors ─────────────────────────────────────────────── */
#define C_GRN   "\x1b[32m"
#define C_YEL   "\x1b[33m"
#define C_CYN   "\x1b[36m"
#define C_BOLD  "\x1b[1m"
#define C_RST   "\x1b[0m"

static int g_color = 0;
static const char *cc(const char *c) { return g_color ? c : ""; }
static const char *cr(void)          { return g_color ? C_RST : ""; }

#define WSIM_VERSION   "0.3"
#define SCHEMA_VERSION "1.0"
#define PATH_BUF       4096
#define NAME_BUF       512
#define EXT_BUF        64

/* ── Types ───────────────────────────────────────────────────── */

typedef struct {
    char    path[PATH_BUF];
    char    basename[NAME_BUF];
    char    ext[EXT_BUF];
    int64_t size;
    char    mtime[32];
    char    norm[NAME_BUF];   /* normalized name for scoring */
    char    tok[NAME_BUF];    /* first token for blocking    */
} FileEntry;

typedef struct {
    double total;
    double basename_sim;
    int    ext_match;
    double size_sim;
    double mtime_prox;
    double mtime_days;
} Score;

typedef struct {
    int   a, b;   /* indices into g_files */
    Score s;
} ScoredPair;

typedef struct {
    char  *scan_file;
    char  *out_file;
    double min_score;
    int    verbose;
    int    pretty;           /* --pretty: human-readable instead of JSON */
    char  *recommend_keep;   /* --recommend-keep newest|oldest|path-shortest */
    char  *csv_out;          /* --csv FILE */
} Opts;

/* ── Globals ─────────────────────────────────────────────────── */

static FileEntry *g_files  = NULL;
static int        g_nfiles = 0;
static int       *g_uf     = NULL;  /* union-find parent array */
static Opts       g_opts   = { NULL, NULL, 0.65, 0 };

/* ── JSON helpers ─────────────────────────────────────────────── */

static void json_esc(const char *src, char *dst, size_t dsz) {
    size_t j = 0;
    for (const char *p = src; *p && j + 4 < dsz; p++) {
        unsigned char c = (unsigned char)*p;
        if      (c == '\\' || c == '"') { dst[j++] = '\\'; dst[j++] = *p; }
        else if (c >= 0x20)             { dst[j++] = *p; }
    }
    dst[j] = '\0';
}

static int json_str_val(const char *buf, const char *key,
                        char *out, size_t outsz) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0; p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0; p++;
    size_t j = 0;
    while (*p && j + 1 < outsz) {
        if (*p == '\\') {
            p++;
            if      (*p == '"')  { out[j++] = '"';  p++; }
            else if (*p == '\\') { out[j++] = '\\'; p++; }
            else if (*p == 'n')  { out[j++] = '\n'; p++; }
            else if (*p == 't')  { out[j++] = '\t'; p++; }
            else                 { out[j++] = *p++; }
        } else if (*p == '"') {
            break;
        } else {
            out[j++] = *p++;
        }
    }
    out[j] = '\0';
    return 1;
}

static int json_int_val(const char *buf, const char *key, int64_t *out) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0; p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (!(*p == '-' || isdigit((unsigned char)*p))) return 0;
    char *end;
    *out = (int64_t)strtoll(p, &end, 10);
    return end != p;
}

/* ── Name normalization ───────────────────────────────────────── */

/*
 * Copy/variant markers stripped from normalized names.
 * Order matters: longer/more-specific patterns before shorter ones
 * to prevent partial matches (e.g. " - copy" before " copy").
 */
static const char * const COPY_MARKERS[] = {
    " - copy", " copy", "_copy",
    "copy of ",
    " (1)", "(1)", " (2)", "(2)", " (3)", "(3)",
    " (4)", "(4)", " (5)", "(5)",
    NULL
};

/*
 * Version/status markers stripped from normalized names.
 * Space-prefixed variants handle the common "file v2.txt" pattern.
 */
static const char * const VER_MARKERS[] = {
    " final", "_final", " draft", "_draft",
    " old",   "_old",   " new",   "_new",
    " v1", " v2", " v3", " v4", " v5",
    " v6", " v7", " v8", " v9",
    NULL
};

static void strip_markers(char *buf, const char * const *markers) {
    for (int i = 0; markers[i]; i++) {
        const char *m = markers[i];
        size_t mlen = strlen(m);
        char *p;
        while ((p = strstr(buf, m)) != NULL)
            memmove(p, p + mlen, strlen(p + mlen) + 1);
    }
}

static void normalize_name(const char *basename, const char *ext,
                           char *out, size_t outsz) {
    char buf[NAME_BUF];
    size_t blen = strlen(basename);
    size_t elen = strlen(ext);

    /* Strip extension from basename (ext always includes the leading dot) */
    if (elen > 0 && blen > elen && basename[blen - elen] == '.') {
        size_t slen = blen - elen;   /* stem length, not including the dot */
        if (slen >= sizeof(buf)) slen = sizeof(buf) - 1;
        memcpy(buf, basename, slen);
        buf[slen] = '\0';
    } else {
        strncpy(buf, basename, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }

    /* Lowercase */
    for (char *c = buf; *c; c++) *c = (char)tolower((unsigned char)*c);

    /* Replace underscores and hyphens with spaces */
    for (char *c = buf; *c; c++)
        if (*c == '_' || *c == '-') *c = ' ';

    /* Remove copy and version markers */
    strip_markers(buf, COPY_MARKERS);
    strip_markers(buf, VER_MARKERS);

    /* Collapse whitespace and trim leading/trailing spaces */
    char tmp[NAME_BUF];
    int  j = 0, in_sp = 0;
    for (int i = 0; buf[i] && j + 1 < (int)sizeof(tmp); i++) {
        if (buf[i] == ' ') {
            if (!in_sp && j > 0) { tmp[j++] = ' '; in_sp = 1; }
        } else {
            tmp[j++] = buf[i]; in_sp = 0;
        }
    }
    while (j > 0 && tmp[j - 1] == ' ') j--;
    tmp[j] = '\0';

    strncpy(out, tmp, outsz - 1);
    out[outsz - 1] = '\0';
}

static void first_token(const char *norm, char *tok, size_t toksz) {
    size_t i = 0;
    while (norm[i] && norm[i] != ' ' && i + 1 < toksz) {
        tok[i] = norm[i];
        i++;
    }
    tok[i] = '\0';
}

/* ── Levenshtein distance ─────────────────────────────────────── */

static int levenshtein(const char *a, int la, const char *b, int lb) {
    if (la == 0) return lb;
    if (lb == 0) return la;
    int *prev = (int *)malloc((size_t)(lb + 1) * sizeof(int));
    int *curr = (int *)malloc((size_t)(lb + 1) * sizeof(int));
    if (!prev || !curr) { free(prev); free(curr); return la > lb ? la : lb; }

    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        curr[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int del  = prev[j]   + 1;
            int ins  = curr[j-1] + 1;
            int sub  = prev[j-1] + cost;
            curr[j]  = del < ins ? (del < sub ? del : sub)
                                 : (ins < sub ? ins : sub);
        }
        int *t = prev; prev = curr; curr = t;
    }
    int dist = prev[lb];
    free(prev); free(curr);
    return dist;
}

static double basename_similarity(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la == 0 && lb == 0) return 1.0;
    int mx = la > lb ? la : lb;
    return 1.0 - (double)levenshtein(a, la, b, lb) / mx;
}

/* ── mtime helpers ────────────────────────────────────────────── */

/*
 * Convert "YYYY-MM-DDTHH:MM:SS" to approximate days since a fixed epoch.
 * Approximate is fine — we only need proximity in days, not exact timestamps.
 */
static long mtime_approx_days(const char *mt) {
    int y = 0, mo = 0, d = 0;
    sscanf(mt, "%d-%d-%d", &y, &mo, &d);
    return (long)y * 365L + (long)(y / 4) + (long)mo * 31L + (long)d;
}

static double mtime_proximity_score(const char *mt1, const char *mt2) {
    long d1   = mtime_approx_days(mt1);
    long d2   = mtime_approx_days(mt2);
    long diff = d1 - d2; if (diff < 0) diff = -diff;
    double s  = 1.0 - (double)diff / 365.0;
    return s < 0.0 ? 0.0 : s;
}

static double mtime_diff_days(const char *mt1, const char *mt2) {
    long diff = mtime_approx_days(mt1) - mtime_approx_days(mt2);
    return diff < 0 ? -(double)diff : (double)diff;
}

/* ── Size similarity ──────────────────────────────────────────── */

static double size_similarity(int64_t s1, int64_t s2) {
    if (s1 == s2) return 1.0;
    int64_t mx = s1 > s2 ? s1 : s2;
    if (mx == 0) return 1.0;
    int64_t diff = s1 - s2; if (diff < 0) diff = -diff;
    return 1.0 - (double)diff / (double)mx;
}

/* ── Pair scoring ─────────────────────────────────────────────── */

#define W_BASENAME  0.50
#define W_EXT       0.15
#define W_SIZE      0.25
#define W_MTIME     0.10

static int ci_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static Score score_pair(const FileEntry *a, const FileEntry *b) {
    Score s;
    s.basename_sim = basename_similarity(a->norm, b->norm);
    s.ext_match    = ci_eq(a->ext, b->ext) ? 1 : 0;
    s.size_sim     = size_similarity(a->size, b->size);
    s.mtime_prox   = mtime_proximity_score(a->mtime, b->mtime);
    s.mtime_days   = mtime_diff_days(a->mtime, b->mtime);
    s.total        = W_BASENAME * s.basename_sim
                   + W_EXT      * (s.ext_match ? 1.0 : 0.0)
                   + W_SIZE     * s.size_sim
                   + W_MTIME    * s.mtime_prox;
    return s;
}

/* ── Blocking (candidate reduction) ──────────────────────────── */

/*
 * Returns 1 if the pair should be scored.
 * Blocking criteria (all must pass):
 *   1. Same extension (case-insensitive)
 *   2. Size within ±20%
 *   3. First name token overlap (if both tokens are >= 3 chars)
 */
static int should_compare(const FileEntry *a, const FileEntry *b) {
    if (!ci_eq(a->ext, b->ext)) return 0;

    int64_t mx = a->size > b->size ? a->size : b->size;
    if (mx > 0) {
        int64_t diff = a->size - b->size; if (diff < 0) diff = -diff;
        if ((double)diff / (double)mx > 0.20) return 0;
    }

    if (strlen(a->tok) >= 3 && strlen(b->tok) >= 3)
        if (strcmp(a->tok, b->tok) != 0) return 0;

    return 1;
}

/* ── Union-Find ───────────────────────────────────────────────── */

static int uf_find(int i) {
    while (g_uf[i] != i) { g_uf[i] = g_uf[g_uf[i]]; i = g_uf[i]; }
    return i;
}

static void uf_union(int i, int j) {
    i = uf_find(i); j = uf_find(j);
    if (i != j) g_uf[i] = j;
}

/* ── Scan JSON loading ────────────────────────────────────────── */

static int load_scan_json(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "wsim: cannot open: %s\n", path); return 0; }

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)fsz + 1);
    if (!buf) { fclose(f); fprintf(stderr, "wsim: out of memory\n"); return 0; }
    fread(buf, 1, (size_t)fsz, f);
    buf[fsz] = '\0';
    fclose(f);

    /* Locate "files" array */
    const char *fa = strstr(buf, "\"files\"");
    if (!fa) {
        fprintf(stderr, "wsim: scan JSON missing 'files' array\n");
        free(buf); return 0;
    }
    const char *arr = strchr(fa, '[');
    if (!arr) { free(buf); return 0; }
    arr++;

    /* Count top-level { } objects in the array */
    int count = 0, depth = 0;
    for (const char *p = arr; *p; p++) {
        if (*p == ']' && depth == 0) break;
        if      (*p == '{') { if (++depth == 1) count++; }
        else if (*p == '}') depth--;
    }

    if (count == 0) { g_files = NULL; g_nfiles = 0; free(buf); return 1; }

    g_files = (FileEntry *)calloc((size_t)count, sizeof(FileEntry));
    if (!g_files) { free(buf); return 0; }

    /* Parse each object */
    int idx = 0;
    depth = 0;
    const char *obj_start = NULL;
    for (const char *p = arr; *p && idx < count; p++) {
        if (*p == '{') {
            if (++depth == 1) obj_start = p;
        } else if (*p == '}') {
            if (--depth == 0 && obj_start) {
                int    olen = (int)(p - obj_start) + 2;
                char  *obj  = (char *)malloc((size_t)olen + 1);
                if (obj) {
                    memcpy(obj, obj_start, (size_t)olen);
                    obj[olen] = '\0';

                    FileEntry *fe = &g_files[idx];
                    json_str_val(obj, "path",     fe->path,     sizeof(fe->path));
                    json_str_val(obj, "basename", fe->basename, sizeof(fe->basename));
                    json_str_val(obj, "ext",      fe->ext,      sizeof(fe->ext));
                    json_str_val(obj, "mtime",    fe->mtime,    sizeof(fe->mtime));
                    int64_t sz = 0;
                    json_int_val(obj, "size", &sz);
                    fe->size = sz;

                    normalize_name(fe->basename, fe->ext,
                                   fe->norm, sizeof(fe->norm));
                    first_token(fe->norm, fe->tok, sizeof(fe->tok));

                    free(obj);
                    idx++;
                }
                obj_start = NULL;
            }
        }
    }
    g_nfiles = idx;
    free(buf);
    return 1;
}

/* ── Output JSON ──────────────────────────────────────────────── */

static void output_json(FILE *fp, const char *source_scan,
                        int **gmembers, Score *gscores,
                        int *gsizes, int *gkeep, int ngroups) {
    char esc[PATH_BUF * 2], esc_base[NAME_BUF * 2], esc_ext[EXT_BUF * 2];
    char esc_src[PATH_BUF * 2];
    json_esc(source_scan ? source_scan : "", esc_src, sizeof(esc_src));

    fprintf(fp, "{\n");
    fprintf(fp, "  \"schema_version\": \"%s\",\n", SCHEMA_VERSION);
    fprintf(fp, "  \"wsim_version\": \"%s\",\n", WSIM_VERSION);
    fprintf(fp, "  \"source_scan\": \"%s\",\n", esc_src);
    if (g_opts.recommend_keep)
        fprintf(fp, "  \"recommend_keep\": \"%s\",\n", g_opts.recommend_keep);
    fprintf(fp, "  \"candidate_groups\": [\n");

    for (int g = 0; g < ngroups; g++) {
        Score *sc = &gscores[g];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"score\": %.4f,\n", sc->total);
        fprintf(fp, "      \"reasoning\": {\n");
        fprintf(fp, "        \"basename_similarity\": %.4f,\n", sc->basename_sim);
        fprintf(fp, "        \"ext_match\": %s,\n",
                sc->ext_match ? "true" : "false");
        fprintf(fp, "        \"size_similarity\": %.4f,\n", sc->size_sim);
        fprintf(fp, "        \"mtime_proximity_days\": %.0f\n", sc->mtime_days);
        fprintf(fp, "      },\n");
        fprintf(fp, "      \"files\": [\n");

        for (int fi = 0; fi < gsizes[g]; fi++) {
            FileEntry *fe = &g_files[gmembers[g][fi]];
            json_esc(fe->path,     esc,      sizeof(esc));
            json_esc(fe->basename, esc_base, sizeof(esc_base));
            json_esc(fe->ext,      esc_ext,  sizeof(esc_ext));
            const char *keep_field = "";
            if (gkeep) keep_field = (fi == gkeep[g]) ? ", \"keep\": true" : ", \"keep\": false";
            fprintf(fp,
                "        {\"path\": \"%s\", \"size\": %lld,"
                " \"mtime\": \"%s\", \"ext\": \"%s\","
                " \"basename\": \"%s\"%s}%s\n",
                esc, (long long)fe->size, fe->mtime,
                esc_ext, esc_base, keep_field,
                fi + 1 < gsizes[g] ? "," : "");
        }

        fprintf(fp, "      ]\n");
        fprintf(fp, "    }%s\n", g + 1 < ngroups ? "," : "");
    }

    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
}

/* ── Output: human-readable (--pretty) ───────────────────────── */

static void fmt_sz(int64_t s, char *buf, size_t n) {
    if      (s >= (int64_t)1 << 30) snprintf(buf, n, "%.2f GB", s / (double)(1<<30));
    else if (s >= (int64_t)1 << 20) snprintf(buf, n, "%.2f MB", s / (double)(1<<20));
    else if (s >= (int64_t)1 << 10) snprintf(buf, n, "%.2f KB", s / (double)(1<<10));
    else                             snprintf(buf, n, "%lld B",  (long long)s);
}

static void output_pretty(FILE *fp, const char *source_scan,
                          int **gmembers, Score *gscores,
                          int *gsizes, int *gkeep, int ngroups) {
    (void)source_scan;
    fprintf(fp, "%swsim%s — %d candidate group(s)  (min-score %.2f)\n\n",
            cc(C_BOLD), cr(), ngroups, g_opts.min_score);

    for (int g = 0; g < ngroups; g++) {
        Score *sc = &gscores[g];
        fprintf(fp, "%sGroup %d%s  score: %.4f\n",
                cc(C_BOLD), g + 1, cr(), sc->total);
        fprintf(fp,
            "  %sReasoning:%s  name %.2f | ext %s | size %.2f | mtime %.0fd apart\n",
            cc(C_CYN), cr(),
            sc->basename_sim,
            sc->ext_match ? "match" : "differ",
            sc->size_sim,
            sc->mtime_days);

        for (int fi = 0; fi < gsizes[g]; fi++) {
            FileEntry *fe = &g_files[gmembers[g][fi]];
            int is_keep = gkeep && (fi == gkeep[g]);
            char sz[32];
            fmt_sz(fe->size, sz, sizeof(sz));
            if (is_keep)
                fprintf(fp, "  %sKEEP%s  %-60s  %8s  %s\n",
                        cc(C_GRN), cr(), fe->path, sz, fe->mtime);
            else
                fprintf(fp, "        %-60s  %8s  %s\n",
                        fe->path, sz, fe->mtime);
        }
        fprintf(fp, "\n");
    }
}

/* ── Output: CSV ──────────────────────────────────────────────── */

/* CSV-escape a string: double any embedded double-quotes, wrap in quotes. */
static void csv_field(const char *s, char *dst, size_t dsz) {
    size_t j = 0;
    dst[j++] = '"';
    for (const char *p = s; *p && j + 4 < dsz; p++) {
        if (*p == '"') dst[j++] = '"';   /* double the quote */
        dst[j++] = *p;
    }
    dst[j++] = '"';
    dst[j]   = '\0';
}

static void output_csv(FILE *fp, int **gmembers, Score *gscores,
                       int *gsizes, int *gkeep, int ngroups) {
    char cf[PATH_BUF * 2 + 4];
    fprintf(fp, "group,score,path,size,mtime,ext,basename,keep\n");
    for (int g = 0; g < ngroups; g++) {
        for (int fi = 0; fi < gsizes[g]; fi++) {
            FileEntry *fe = &g_files[gmembers[g][fi]];
            int is_keep = gkeep && (fi == gkeep[g]);
            csv_field(fe->path, cf, sizeof(cf));
            fprintf(fp, "%d,%.4f,%s,%lld,%s,%s,\"%s\",%s\n",
                    g + 1, gscores[g].total,
                    cf, (long long)fe->size, fe->mtime,
                    fe->ext, fe->basename,
                    gkeep ? (is_keep ? "true" : "false") : "");
        }
    }
}

/* ── Usage ────────────────────────────────────────────────────── */

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] <scan.json>\n\n"
        "Score files in a wlint scan inventory for similarity.\n"
        "Read-only — never modifies or moves files.\n\n"
        "Options:\n"
        "      --out FILE           write JSON results to FILE (default: stdout)\n"
        "      --csv FILE           write CSV results to FILE\n"
        "      --min-score N        minimum score to report (0.0-1.0, default: 0.65)\n"
        "      --pretty             human-readable output (default: JSON)\n"
        "      --recommend-keep P   mark file to keep: newest|oldest|path-shortest\n"
        "  -v  --verbose            progress output to stderr\n"
        "      --version            show version\n"
        "      --help               show this help\n\n"
        "Scoring model (weights):\n"
        "  basename similarity  50%%\n"
        "  extension match      15%%\n"
        "  size similarity      25%%\n"
        "  mtime proximity      10%%\n\n"
        "Candidate reduction:\n"
        "  Only pairs with same extension, size within ±20%%,\n"
        "  and shared first name token are scored.\n\n"
        "Exit: 0=no candidates  1=candidates found  2=error\n",
        prog);
}

/* ── main ─────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    int ret = 0;

    if (argc < 2) { usage(argv[0]); return 2; }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (!strcmp(a, "--version"))                      { printf("wsim %s (Winix)\n", WSIM_VERSION); return 0; }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h"))     { usage(argv[0]); return 0; }
        else if (!strcmp(a, "--out")       && i + 1 < argc)    { g_opts.out_file  = argv[++i]; }
        else if (!strcmp(a, "--csv")       && i + 1 < argc)    { g_opts.csv_out   = argv[++i]; }
        else if (!strcmp(a, "--min-score") && i + 1 < argc)    { g_opts.min_score = atof(argv[++i]); }
        else if (!strcmp(a, "--verbose")   || !strcmp(a, "-v")) { g_opts.verbose = 1; }
        else if (!strcmp(a, "--pretty"))                       { g_opts.pretty  = 1; }
        else if (!strcmp(a, "--recommend-keep") && i + 1 < argc) {
            char *pol = argv[++i];
            if (strcmp(pol, "newest") != 0 && strcmp(pol, "oldest") != 0 &&
                strcmp(pol, "path-shortest") != 0) {
                fprintf(stderr, "wsim: unknown keep policy '%s' "
                        "(use newest|oldest|path-shortest)\n", pol);
                return 2;
            }
            g_opts.recommend_keep = pol;
        }
        else if (a[0] != '-')                                  { g_opts.scan_file = argv[i]; }
        else { fprintf(stderr, "wsim: invalid option -- '%s'\n", a); return 2; }
    }

    if (!g_opts.scan_file) {
        fprintf(stderr, "wsim: no scan file specified\n");
        usage(argv[0]);
        return 2;
    }

    /* Enable color only when output goes to a terminal */
    g_color = !g_opts.out_file && _isatty(_fileno(stdout));

    if (!load_scan_json(g_opts.scan_file)) return 2;

    if (g_opts.verbose)
        fprintf(stderr, "Loaded: %d files from %s\n",
                g_nfiles, g_opts.scan_file);

    /* Open output file early (before heavy work) */
    FILE *out_fp = stdout;
    if (g_opts.out_file) {
        out_fp = fopen(g_opts.out_file, "w");
        if (!out_fp) {
            fprintf(stderr, "wsim: cannot write: %s\n", g_opts.out_file);
            free(g_files);
            return 2;
        }
    }

    /* Trivial case: fewer than 2 files — nothing to compare */
    if (g_nfiles < 2) {
        if (g_opts.pretty)
            output_pretty(out_fp, g_opts.scan_file, NULL, NULL, NULL, NULL, 0);
        else
            output_json(out_fp, g_opts.scan_file, NULL, NULL, NULL, NULL, 0);
        if (g_opts.csv_out) {
            FILE *cf = fopen(g_opts.csv_out, "w");
            if (cf) { output_csv(cf, NULL, NULL, NULL, NULL, 0); fclose(cf); }
        }
        if (g_opts.out_file) fclose(out_fp);
        free(g_files);
        return 0;
    }

    /* Initialize union-find */
    g_uf = (int *)malloc((size_t)g_nfiles * sizeof(int));
    if (!g_uf) {
        if (g_opts.out_file) fclose(out_fp);
        free(g_files); return 2;
    }
    for (int i = 0; i < g_nfiles; i++) g_uf[i] = i;

    /* Pass 1: score all candidate pairs, union those above threshold */
    ScoredPair *pairs    = NULL;
    int         npairs   = 0;
    int         pairs_cap = 0;

    for (int i = 0; i < g_nfiles; i++) {
        for (int j = i + 1; j < g_nfiles; j++) {
            if (!should_compare(&g_files[i], &g_files[j])) continue;
            Score s = score_pair(&g_files[i], &g_files[j]);
            if (s.total < g_opts.min_score) continue;

            if (npairs == pairs_cap) {
                pairs_cap = pairs_cap ? pairs_cap * 2 : 64;
                ScoredPair *tmp = (ScoredPair *)realloc(
                    pairs, (size_t)pairs_cap * sizeof(ScoredPair));
                if (!tmp) {
                    free(pairs); free(g_uf);
                    if (g_opts.out_file) fclose(out_fp);
                    free(g_files); return 2;
                }
                pairs = tmp;
            }
            pairs[npairs].a = i;
            pairs[npairs].b = j;
            pairs[npairs].s = s;
            npairs++;
            uf_union(i, j);
        }
    }

    if (g_opts.verbose)
        fprintf(stderr, "Scored: %d qualifying pairs (min-score %.2f)\n",
                npairs, g_opts.min_score);

    /* Pass 2: find best score per final UF root (after all unions) */
    Score *best = (Score *)calloc((size_t)g_nfiles, sizeof(Score));
    if (!best) {
        free(pairs); free(g_uf);
        if (g_opts.out_file) fclose(out_fp);
        free(g_files); return 2;
    }
    for (int p = 0; p < npairs; p++) {
        int root = uf_find(pairs[p].a);
        if (pairs[p].s.total > best[root].total)
            best[root] = pairs[p].s;
    }
    free(pairs);

    /* Count members per root; identify group roots */
    int *root_sz = (int *)calloc((size_t)g_nfiles, sizeof(int));
    if (!root_sz) {
        free(best); free(g_uf);
        if (g_opts.out_file) fclose(out_fp);
        free(g_files); return 2;
    }
    for (int i = 0; i < g_nfiles; i++)
        if (best[uf_find(i)].total > 0.0)
            root_sz[uf_find(i)]++;

    int ngroups = 0;
    for (int i = 0; i < g_nfiles; i++)
        if (root_sz[i] >= 2) ngroups++;

    if (g_opts.verbose)
        fprintf(stderr, "Groups: %d candidate groups\n", ngroups);

    /* Allocate group arrays */
    int   **gmembers = NULL;
    int    *gsizes   = NULL;
    Score  *gscores  = NULL;
    int    *root2grp = NULL;
    int    *gkeep    = NULL;

    if (ngroups > 0) {
        gmembers = (int   **)calloc((size_t)ngroups, sizeof(int *));
        gsizes   = (int    *)calloc((size_t)ngroups, sizeof(int));
        gscores  = (Score  *)calloc((size_t)ngroups, sizeof(Score));
        root2grp = (int    *)malloc((size_t)g_nfiles * sizeof(int));
        if (!gmembers || !gsizes || !gscores || !root2grp) { ret = 2; goto cleanup; }

        for (int i = 0; i < g_nfiles; i++) root2grp[i] = -1;

        int gi = 0;
        for (int i = 0; i < g_nfiles; i++) {
            if (root_sz[i] >= 2) {
                root2grp[i]  = gi;
                gsizes[gi]   = root_sz[i];
                gscores[gi]  = best[i];
                gmembers[gi] = (int *)malloc((size_t)root_sz[i] * sizeof(int));
                if (!gmembers[gi]) { ret = 2; goto cleanup; }
                gi++;
            }
        }

        /* Fill group member arrays */
        int *fill = (int *)calloc((size_t)ngroups, sizeof(int));
        if (!fill) { ret = 2; goto cleanup; }
        for (int i = 0; i < g_nfiles; i++) {
            int root = uf_find(i);
            int grp  = root2grp[root];
            if (grp >= 0) gmembers[grp][fill[grp]++] = i;
        }
        free(fill);

        /* Sort groups by score descending (insertion sort) */
        for (int i = 1; i < ngroups; i++) {
            Score  sc = gscores[i];
            int   *gm = gmembers[i];
            int    sz = gsizes[i];
            int    j  = i - 1;
            while (j >= 0 && gscores[j].total < sc.total) {
                gscores[j+1]  = gscores[j];
                gmembers[j+1] = gmembers[j];
                gsizes[j+1]   = gsizes[j];
                j--;
            }
            gscores[j+1]  = sc;
            gmembers[j+1] = gm;
            gsizes[j+1]   = sz;
        }
    }

    /* Compute recommended-keep index per group */
    if (ngroups > 0 && g_opts.recommend_keep) {
        gkeep = (int *)calloc((size_t)ngroups, sizeof(int));
        if (gkeep) {
            for (int g = 0; g < ngroups; g++) {
                int best_idx = 0;
                for (int fi = 1; fi < gsizes[g]; fi++) {
                    FileEntry *ca = &g_files[gmembers[g][best_idx]];
                    FileEntry *cb = &g_files[gmembers[g][fi]];
                    if (strcmp(g_opts.recommend_keep, "newest") == 0) {
                        if (strcmp(cb->mtime, ca->mtime) > 0) best_idx = fi;
                    } else if (strcmp(g_opts.recommend_keep, "oldest") == 0) {
                        if (strcmp(cb->mtime, ca->mtime) < 0) best_idx = fi;
                    } else if (strcmp(g_opts.recommend_keep, "path-shortest") == 0) {
                        if (strlen(cb->path) < strlen(ca->path)) best_idx = fi;
                    }
                }
                gkeep[g] = best_idx;
            }
        }
    }

    if (g_opts.pretty)
        output_pretty(out_fp, g_opts.scan_file, gmembers, gscores, gsizes, gkeep, ngroups);
    else
        output_json(out_fp, g_opts.scan_file, gmembers, gscores, gsizes, gkeep, ngroups);

    if (g_opts.csv_out) {
        FILE *cf = fopen(g_opts.csv_out, "w");
        if (!cf) {
            fprintf(stderr, "wsim: cannot write: %s\n", g_opts.csv_out);
        } else {
            output_csv(cf, gmembers, gscores, gsizes, gkeep, ngroups);
            fclose(cf);
        }
    }

    ret = ngroups > 0 ? 1 : 0;

cleanup:
    if (g_opts.out_file && out_fp) fclose(out_fp);
    if (gmembers) for (int i = 0; i < ngroups; i++) free(gmembers[i]);
    free(gmembers); free(gsizes); free(gscores); free(root2grp); free(gkeep);
    free(root_sz);
    free(best);
    free(g_uf);
    free(g_files);
    return ret;
}
