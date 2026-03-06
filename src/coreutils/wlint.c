/*
 * wlint — Winix filesystem lint detector  v1.0
 *
 * Finds duplicate files, empty files, and empty directories.
 * Never deletes automatically. Outputs findings as human-readable
 * text, JSON, and/or CSV. Optionally moves non-kept duplicates
 * to a quarantine directory.
 *
 * Algorithm:
 *   1. Walk paths recursively; collect (path, size, mtime)
 *   2. Group files by exact size          (O(n log n) sort)
 *   3. SHA-256 via Windows CNG only for same-size candidates
 *   4. Group by hash -> duplicate sets
 *   5. Optional byte-for-byte verification pass
 *   6. Apply keep policy; output results
 *
 * Usage:
 *   wlint [options] <path> [path ...]
 *   -e  --empty           report empty files and directories
 *   -V  --verify          byte-for-byte verify after SHA-256 match
 *   -k  --keep POLICY     newest | oldest | first  (default: newest)
 *       --min-size BYTES  skip files smaller than BYTES (default: 1)
 *       --json FILE       write JSON report to FILE
 *       --csv  FILE       write CSV  report to FILE
 *       --quarantine DIR  move non-kept duplicates to DIR
 *   -v  --verbose         progress output
 *       --no-color        disable ANSI colors
 *       --version
 *       --help
 *
 * Exit codes:  0 = clean (no lint found)
 *              1 = lint found
 *              2 = error
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <io.h>

#define WLINT_VERSION  "1.0"
#define SHA256_BYTES   32
#define SHA256_HEX     65        /* 64 hex digits + NUL */
#define READ_BUF_SZ    65536
#define PATH_BUF       4096      /* wchar_t or char path length */

/* ── ANSI colors ─────────────────────────────────────────────── */
#define C_RED   "\x1b[31m"
#define C_GRN   "\x1b[32m"
#define C_YEL   "\x1b[33m"
#define C_CYN   "\x1b[36m"
#define C_BOLD  "\x1b[1m"
#define C_RST   "\x1b[0m"

static int g_color = 0;
static const char *cc(const char *c) { return g_color ? c : ""; }
static const char *cr(void)          { return g_color ? C_RST : ""; }

/* ── Types ───────────────────────────────────────────────────── */

typedef struct {
    char    *path;                /* heap-allocated UTF-8 */
    int64_t  size;
    FILETIME mtime;
    char     hash[SHA256_HEX];   /* filled during analysis */
} File;

typedef struct {
    File  **items;
    size_t  count, cap;
} FileVec;

typedef struct {
    File  **files;       /* files[kept_idx] = the one to keep */
    size_t  count;
    size_t  kept_idx;
    int64_t size;
    char    hash[SHA256_HEX];
    int     verified;    /* 1 = byte-for-byte confirmed */
} DupSet;

typedef struct {
    DupSet *items;
    size_t  count, cap;
} DupSetVec;

typedef enum { KEEP_NEWEST = 0, KEEP_OLDEST, KEEP_FIRST } KeepPolicy;

typedef struct {
    char      **scan_paths;
    int         nscan;
    int         do_empty;
    int         do_verify;
    KeepPolicy  keep;
    int64_t     min_size;
    char       *json_out;
    char       *csv_out;
    char       *quarantine;
    int         verbose;
} Opts;

/* ── Globals ─────────────────────────────────────────────────── */

static BCRYPT_ALG_HANDLE g_hAlg     = NULL;
static DWORD             g_cbHashObj = 0;

static FileVec   g_files;
static FileVec   g_empty_files;
static FileVec   g_empty_dirs;
static DupSetVec g_dupsets;
static Opts      g_opts;

static size_t g_dirs_scanned  = 0;
static size_t g_files_scanned = 0;
static size_t g_hashed        = 0;

/* ── Utility ─────────────────────────────────────────────────── */

static char *utf8_from_wide(const wchar_t *ws) {
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    char *s = (char *)malloc(n);
    if (s) WideCharToMultiByte(CP_UTF8, 0, ws, -1, s, n, NULL, NULL);
    return s;
}

static wchar_t *wide_from_utf8(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* Normalize wide path: forward slashes → backslashes, strip trailing sep */
static void normalize_wpath(wchar_t *p) {
    for (wchar_t *c = p; *c; c++)
        if (*c == L'/') *c = L'\\';
    size_t n = wcslen(p);
    while (n > 1 && (p[n-1] == L'\\' || p[n-1] == L'/'))
        p[--n] = L'\0';
}

static void fmt_size(int64_t n, char *out, size_t outsz) {
    if      (n >= (int64_t)1 << 30)
        snprintf(out, outsz, "%.2f GB", n / (double)(1 << 30));
    else if (n >= (int64_t)1 << 20)
        snprintf(out, outsz, "%.2f MB", n / (double)(1 << 20));
    else if (n >= (int64_t)1 << 10)
        snprintf(out, outsz, "%.2f KB", n / (double)(1 << 10));
    else
        snprintf(out, outsz, "%lld B", (long long)n);
}

static void fmt_mtime(FILETIME ft, char *out, size_t outsz) {
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    snprintf(out, outsz, "%04d-%02d-%02dT%02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
}

/* Escape a string for JSON: backslash -> \\, quote -> \", skip ctrl chars */
static void json_esc(const char *src, char *dst, size_t dsz) {
    size_t j = 0;
    for (const char *p = src; *p && j + 4 < dsz; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\\' || c == '"') { dst[j++] = '\\'; dst[j++] = *p; }
        else if (c >= 0x20)        { dst[j++] = *p; }
    }
    dst[j] = '\0';
}

/* ── SHA-256 via Windows CNG ─────────────────────────────────── */

static int bcrypt_init(void) {
    DWORD cbResult = 0;
    if (BCryptOpenAlgorithmProvider(&g_hAlg, BCRYPT_SHA256_ALGORITHM,
                                    NULL, 0) != 0)
        return -1;
    if (BCryptGetProperty(g_hAlg, BCRYPT_OBJECT_LENGTH,
                          (PBYTE)&g_cbHashObj, sizeof(DWORD),
                          &cbResult, 0) != 0)
        return -1;
    return 0;
}

static void bcrypt_fini(void) {
    if (g_hAlg) { BCryptCloseAlgorithmProvider(g_hAlg, 0); g_hAlg = NULL; }
}

static int sha256_file(const char *path, char hex[SHA256_HEX]) {
    BCRYPT_HASH_HANDLE hHash  = NULL;
    PBYTE              pbObj  = NULL;
    BYTE               digest[SHA256_BYTES];
    HANDLE             hFile  = INVALID_HANDLE_VALUE;
    wchar_t           *wpath  = NULL;
    BYTE               buf[READ_BUF_SZ];
    DWORD              nRead;
    int                ret    = -1;

    pbObj = (PBYTE)malloc(g_cbHashObj);
    if (!pbObj) goto done;

    if (BCryptCreateHash(g_hAlg, &hHash, pbObj, g_cbHashObj, NULL, 0, 0) != 0)
        goto done;

    wpath = wide_from_utf8(path);
    if (!wpath) goto done;

    hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) goto done;

    while (ReadFile(hFile, buf, sizeof(buf), &nRead, NULL) && nRead > 0) {
        if (BCryptHashData(hHash, buf, nRead, 0) != 0) goto done;
    }

    if (BCryptFinishHash(hHash, digest, SHA256_BYTES, 0) != 0) goto done;

    for (int i = 0; i < SHA256_BYTES; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[SHA256_HEX - 1] = '\0';
    ret = 0;

done:
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    if (hHash) BCryptDestroyHash(hHash);
    free(pbObj);
    free(wpath);
    return ret;
}

/* ── Byte-for-byte verification ──────────────────────────────── */

static int files_identical(const char *a, const char *b) {
    HANDLE  ha = INVALID_HANDLE_VALUE, hb = INVALID_HANDLE_VALUE;
    BYTE    bufa[4096], bufb[4096];
    DWORD   na, nb;
    wchar_t *wa = NULL, *wb = NULL;
    int     ret = 0;

    wa = wide_from_utf8(a);
    wb = wide_from_utf8(b);
    if (!wa || !wb) goto done;

    ha = CreateFileW(wa, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    hb = CreateFileW(wb, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (ha == INVALID_HANDLE_VALUE || hb == INVALID_HANDLE_VALUE) goto done;

    for (;;) {
        ReadFile(ha, bufa, sizeof(bufa), &na, NULL);
        ReadFile(hb, bufb, sizeof(bufb), &nb, NULL);
        if (na != nb)              goto done;   /* different length */
        if (na == 0) { ret = 1;   goto done; } /* both EOF: identical */
        if (memcmp(bufa, bufb, na) != 0) goto done;
    }

done:
    if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
    if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
    free(wa); free(wb);
    return ret;
}

/* ── FileVec / DupSetVec management ─────────────────────────── */

static void fvec_push(FileVec *v, File *f) {
    if (v->count == v->cap) {
        v->cap   = v->cap ? v->cap * 2 : 256;
        v->items = (File **)realloc(v->items, v->cap * sizeof(File *));
    }
    v->items[v->count++] = f;
}

static File *file_new(const char *path, int64_t size, FILETIME mtime) {
    File *f  = (File *)calloc(1, sizeof(File));
    f->path  = _strdup(path);
    f->size  = size;
    f->mtime = mtime;
    return f;
}

static void file_free(File *f) {
    if (f) { free(f->path); free(f); }
}

static void fvec_free(FileVec *v) {
    for (size_t i = 0; i < v->count; i++) file_free(v->items[i]);
    free(v->items);
    memset(v, 0, sizeof(*v));
}

static void dvec_push(DupSetVec *v, DupSet ds) {
    if (v->count == v->cap) {
        v->cap   = v->cap ? v->cap * 2 : 64;
        v->items = (DupSet *)realloc(v->items, v->cap * sizeof(DupSet));
    }
    v->items[v->count++] = ds;
}

/* ── Directory scanner ───────────────────────────────────────── */

static void scan_path(const wchar_t *dir, int depth) {
    wchar_t          pattern[PATH_BUF];
    wchar_t          child[PATH_BUF];
    WIN32_FIND_DATAW fd;
    HANDLE           h;
    int              children = 0;

    _snwprintf(pattern, PATH_BUF, L"%s\\*", dir);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        char *du = utf8_from_wide(dir);
        fprintf(stderr, "wlint: cannot open: %s\n", du ? du : "?");
        free(du);
        return;
    }

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        _snwprintf(child, PATH_BUF, L"%s\\%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            children++;
            /* skip junction points / reparse points to prevent loops */
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                scan_path(child, depth + 1);
        } else {
            int64_t size = ((int64_t)fd.nFileSizeHigh << 32) |
                           (int64_t)fd.nFileSizeLow;
            char *pu = utf8_from_wide(child);
            g_files_scanned++;
            children++;

            if (size == 0) {
                if (g_opts.do_empty && pu)
                    fvec_push(&g_empty_files,
                              file_new(pu, 0, fd.ftLastWriteTime));
            } else if (size >= g_opts.min_size && pu) {
                fvec_push(&g_files, file_new(pu, size, fd.ftLastWriteTime));
            }
            free(pu);
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    g_dirs_scanned++;

    /* empty directory? (skip the user's top-level scan roots) */
    if (children == 0 && g_opts.do_empty && depth > 0) {
        char *du = utf8_from_wide(dir);
        FILETIME ft = {0, 0};
        if (du) fvec_push(&g_empty_dirs, file_new(du, 0, ft));
        free(du);
    }

    if (g_opts.verbose)
        fprintf(stderr, "\r  Scanning... %zu dirs, %zu files   \r",
                g_dirs_scanned, g_files_scanned);
}

/* ── Analysis: size → hash → duplicate sets ─────────────────── */

static int cmp_size(const void *a, const void *b) {
    const File *fa = *(const File **)a;
    const File *fb = *(const File **)b;
    if (fa->size < fb->size) return -1;
    if (fa->size > fb->size) return  1;
    return 0;
}

static int cmp_hash(const void *a, const void *b) {
    return strcmp((*(const File **)a)->hash, (*(const File **)b)->hash);
}

static int cmp_mtime_desc(const void *a, const void *b) {
    const File *fa = *(const File **)a;
    const File *fb = *(const File **)b;
    if (fa->mtime.dwHighDateTime != fb->mtime.dwHighDateTime)
        return (fa->mtime.dwHighDateTime > fb->mtime.dwHighDateTime) ? -1 : 1;
    if (fa->mtime.dwLowDateTime  != fb->mtime.dwLowDateTime)
        return (fa->mtime.dwLowDateTime  > fb->mtime.dwLowDateTime)  ? -1 : 1;
    return 0;
}

static int cmp_mtime_asc(const void *a, const void *b) {
    return cmp_mtime_desc(b, a);
}

static int cmp_path(const void *a, const void *b) {
    return strcmp((*(const File **)a)->path, (*(const File **)b)->path);
}

static void apply_keep_policy(DupSet *ds) {
    switch (g_opts.keep) {
    case KEEP_NEWEST:
        qsort(ds->files, ds->count, sizeof(File *), cmp_mtime_desc);
        break;
    case KEEP_OLDEST:
        qsort(ds->files, ds->count, sizeof(File *), cmp_mtime_asc);
        break;
    case KEEP_FIRST:
        qsort(ds->files, ds->count, sizeof(File *), cmp_path);
        break;
    }
    ds->kept_idx = 0; /* after sort, index 0 is always the one to keep */
}

static void find_duplicates(void) {
    if (g_files.count == 0) return;

    /* phase 1: sort by size */
    qsort(g_files.items, g_files.count, sizeof(File *), cmp_size);

    /* phase 2: hash same-size runs */
    size_t i = 0;
    while (i < g_files.count) {
        size_t j = i + 1;
        while (j < g_files.count &&
               g_files.items[j]->size == g_files.items[i]->size)
            j++;

        size_t run = j - i;
        if (run > 1) {
            /* hash every file in this same-size group */
            for (size_t k = i; k < j; k++) {
                File *f = g_files.items[k];
                if (sha256_file(f->path, f->hash) != 0) {
                    /* unreadable file: assign unique sentinel so it won't match */
                    snprintf(f->hash, SHA256_HEX, "ERR%zu", k);
                    fprintf(stderr, "wlint: warning: cannot hash: %s\n", f->path);
                }
                g_hashed++;
                if (g_opts.verbose)
                    fprintf(stderr, "\r  Hashing... %zu files   \r", g_hashed);
            }

            /* phase 3: sort subrange by hash → find matching runs */
            qsort(g_files.items + i, run, sizeof(File *), cmp_hash);

            size_t m = i;
            while (m < j) {
                size_t n = m + 1;
                while (n < j && strcmp(g_files.items[n]->hash,
                                       g_files.items[m]->hash) == 0)
                    n++;

                if (n - m > 1) {
                    DupSet ds;
                    memset(&ds, 0, sizeof(ds));
                    ds.size  = g_files.items[m]->size;
                    memcpy(ds.hash, g_files.items[m]->hash, SHA256_HEX);
                    ds.count = n - m;
                    ds.files = (File **)malloc(ds.count * sizeof(File *));

                    for (size_t k = 0; k < ds.count; k++)
                        ds.files[k] = g_files.items[m + k];

                    /* optional byte-for-byte verification */
                    if (g_opts.do_verify && ds.count >= 2)
                        ds.verified = files_identical(ds.files[0]->path,
                                                      ds.files[1]->path);

                    apply_keep_policy(&ds);
                    dvec_push(&g_dupsets, ds);
                }
                m = n;
            }
        }
        i = j;
    }

    if (g_opts.verbose) fprintf(stderr, "\n");
}

/* ── Output: human-readable ──────────────────────────────────── */

static void output_pretty(void) {
    char sz[64], rec[64], mt[32];
    int64_t total_reclaimable = 0;
    size_t  total_dup_files   = 0;

    for (size_t i = 0; i < g_dupsets.count; i++) {
        DupSet *ds = &g_dupsets.items[i];
        int64_t reclaimable = (int64_t)(ds->count - 1) * ds->size;
        total_reclaimable += reclaimable;
        total_dup_files   += ds->count;

        fmt_size(ds->size,   sz,  sizeof(sz));
        fmt_size(reclaimable, rec, sizeof(rec));

        printf("%s[DUPLICATE SET]%s  %zu files | %s each | %s reclaimable%s\n",
               cc(C_BOLD), cr(),
               ds->count, sz, rec,
               ds->verified ? "  (byte-verified)" : "");

        for (size_t k = 0; k < ds->count; k++) {
            File *f = ds->files[k];
            fmt_mtime(f->mtime, mt, sizeof(mt));
            if (k == ds->kept_idx)
                printf("  %sKEEP%s  %s  %s\n", cc(C_GRN), cr(), mt, f->path);
            else
                printf("  %sDUP %s  %s  %s\n", cc(C_RED), cr(), mt, f->path);
        }
        printf("\n");
    }

    if (g_opts.do_empty && g_empty_files.count > 0) {
        printf("%s[EMPTY FILES]%s\n", cc(C_YEL), cr());
        for (size_t i = 0; i < g_empty_files.count; i++)
            printf("  %s\n", g_empty_files.items[i]->path);
        printf("\n");
    }

    if (g_opts.do_empty && g_empty_dirs.count > 0) {
        printf("%s[EMPTY DIRECTORIES]%s\n", cc(C_YEL), cr());
        for (size_t i = 0; i < g_empty_dirs.count; i++)
            printf("  %s\n", g_empty_dirs.items[i]->path);
        printf("\n");
    }

    int lint_found = (g_dupsets.count > 0) ||
                     (g_opts.do_empty &&
                      (g_empty_files.count > 0 || g_empty_dirs.count > 0));

    if (!lint_found) {
        printf("%s[CLEAN]%s  No lint found.\n", cc(C_GRN), cr());
    }

    fmt_size(total_reclaimable, sz, sizeof(sz));
    printf("%s[SUMMARY]%s\n", cc(C_BOLD), cr());
    printf("  Files scanned:   %zu\n",  g_files_scanned);
    printf("  Duplicate sets:  %zu\n",  g_dupsets.count);
    printf("  Duplicate files: %zu\n",  total_dup_files);
    printf("  Reclaimable:     %s\n",   sz);
    if (g_opts.do_empty) {
        printf("  Empty files:     %zu\n", g_empty_files.count);
        printf("  Empty dirs:      %zu\n", g_empty_dirs.count);
    }
}

/* ── Output: JSON ────────────────────────────────────────────── */

static void output_json(FILE *fp) {
    char esc[PATH_BUF * 2];
    char mt[32];
    int64_t total_rec  = 0;
    size_t  total_dups = 0;

    for (size_t i = 0; i < g_dupsets.count; i++) {
        total_rec  += (int64_t)(g_dupsets.items[i].count - 1) * g_dupsets.items[i].size;
        total_dups += g_dupsets.items[i].count;
    }

    SYSTEMTIME st;
    GetSystemTime(&st);
    char generated[32];
    snprintf(generated, sizeof(generated), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"wlint_version\": \"%s\",\n", WLINT_VERSION);
    fprintf(fp, "  \"generated\": \"%s\",\n", generated);

    fprintf(fp, "  \"scan_paths\": [");
    for (int i = 0; i < g_opts.nscan; i++) {
        json_esc(g_opts.scan_paths[i], esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"summary\": {\n");
    fprintf(fp, "    \"files_scanned\": %zu,\n",    g_files_scanned);
    fprintf(fp, "    \"duplicate_sets\": %zu,\n",   g_dupsets.count);
    fprintf(fp, "    \"duplicate_files\": %zu,\n",  total_dups);
    fprintf(fp, "    \"reclaimable_bytes\": %lld,\n", (long long)total_rec);
    fprintf(fp, "    \"empty_files\": %zu,\n",       g_empty_files.count);
    fprintf(fp, "    \"empty_dirs\": %zu\n",          g_empty_dirs.count);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"duplicate_sets\": [\n");
    for (size_t i = 0; i < g_dupsets.count; i++) {
        DupSet *ds = &g_dupsets.items[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"size\": %lld,\n",      (long long)ds->size);
        fprintf(fp, "      \"sha256\": \"%s\",\n",  ds->hash);
        fprintf(fp, "      \"verified\": %s,\n",    ds->verified ? "true" : "false");
        fprintf(fp, "      \"files\": [\n");
        for (size_t k = 0; k < ds->count; k++) {
            File *f = ds->files[k];
            json_esc(f->path, esc, sizeof(esc));
            fmt_mtime(f->mtime, mt, sizeof(mt));
            fprintf(fp, "        {\n");
            fprintf(fp, "          \"path\": \"%s\",\n",  esc);
            fprintf(fp, "          \"mtime\": \"%s\",\n", mt);
            fprintf(fp, "          \"keep\": %s\n",
                    k == ds->kept_idx ? "true" : "false");
            fprintf(fp, "        }%s\n", k + 1 < ds->count ? "," : "");
        }
        fprintf(fp, "      ]\n");
        fprintf(fp, "    }%s\n", i + 1 < g_dupsets.count ? "," : "");
    }
    fprintf(fp, "  ],\n");

    fprintf(fp, "  \"empty_files\": [");
    for (size_t i = 0; i < g_empty_files.count; i++) {
        json_esc(g_empty_files.items[i]->path, esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"empty_dirs\": [");
    for (size_t i = 0; i < g_empty_dirs.count; i++) {
        json_esc(g_empty_dirs.items[i]->path, esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "]\n");
    fprintf(fp, "}\n");
}

/* ── Output: CSV ─────────────────────────────────────────────── */

static void output_csv(FILE *fp) {
    char mt[32];
    fprintf(fp, "type,sha256,size,keep,path,mtime\n");

    for (size_t i = 0; i < g_dupsets.count; i++) {
        DupSet *ds = &g_dupsets.items[i];
        for (size_t k = 0; k < ds->count; k++) {
            File *f = ds->files[k];
            fmt_mtime(f->mtime, mt, sizeof(mt));
            fprintf(fp, "duplicate,%s,%lld,%s,\"%s\",%s\n",
                    ds->hash, (long long)ds->size,
                    k == ds->kept_idx ? "true" : "false",
                    f->path, mt);
        }
    }

    for (size_t i = 0; i < g_empty_files.count; i++) {
        fmt_mtime(g_empty_files.items[i]->mtime, mt, sizeof(mt));
        fprintf(fp, "empty_file,,0,,\"%s\",%s\n",
                g_empty_files.items[i]->path, mt);
    }

    for (size_t i = 0; i < g_empty_dirs.count; i++)
        fprintf(fp, "empty_dir,,0,,\"%s\",\n", g_empty_dirs.items[i]->path);
}

/* ── Quarantine: move non-kept duplicates ────────────────────── */

static int do_quarantine(const char *qdir) {
    wchar_t *wq = wide_from_utf8(qdir);
    if (wq) { CreateDirectoryW(wq, NULL); free(wq); }

    char logpath[PATH_BUF];
    snprintf(logpath, sizeof(logpath), "%s\\wlint_moves.json", qdir);
    FILE *log = fopen(logpath, "w");
    int   moved = 0, failed = 0, first = 1;

    if (log) {
        SYSTEMTIME st;
        GetSystemTime(&st);
        fprintf(log, "{\n  \"quarantine_date\": \"%04d-%02d-%02dT%02d:%02d:%02dZ\",\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fprintf(log, "  \"moves\": [\n");
    }

    for (size_t i = 0; i < g_dupsets.count; i++) {
        DupSet *ds = &g_dupsets.items[i];
        for (size_t k = 0; k < ds->count; k++) {
            if (k == ds->kept_idx) continue;

            File       *f     = ds->files[k];
            const char *fname = strrchr(f->path, '\\');
            if (!fname) fname = strrchr(f->path, '/');
            fname = fname ? fname + 1 : f->path;

            /* build dest path; avoid collisions with a counter suffix */
            char dst[PATH_BUF];
            int  tries = 0;
            do {
                if (tries == 0)
                    snprintf(dst, sizeof(dst), "%s\\%.8s_%s",     qdir, ds->hash, fname);
                else
                    snprintf(dst, sizeof(dst), "%s\\%.8s_%s_%d",  qdir, ds->hash, fname, tries);
                tries++;
            } while (GetFileAttributesA(dst) != INVALID_FILE_ATTRIBUTES && tries < 1000);

            wchar_t *wsrc = wide_from_utf8(f->path);
            wchar_t *wdst = wide_from_utf8(dst);

            if (wsrc && wdst && MoveFileW(wsrc, wdst)) {
                moved++;
                if (log) {
                    char esrc[PATH_BUF * 2], edst[PATH_BUF * 2];
                    json_esc(f->path, esrc, sizeof(esrc));
                    json_esc(dst,     edst, sizeof(edst));
                    fprintf(log, "%s    {\"from\": \"%s\", \"to\": \"%s\"}",
                            first ? "" : ",\n", esrc, edst);
                    first = 0;
                }
            } else {
                failed++;
                fprintf(stderr, "wlint: cannot move %s (error %lu)\n",
                        f->path, GetLastError());
            }

            free(wsrc); free(wdst);
        }
    }

    if (log) {
        fprintf(log, "\n  ],\n");
        fprintf(log, "  \"moved\": %d,\n",  moved);
        fprintf(log, "  \"failed\": %d\n",  failed);
        fprintf(log, "}\n");
        fclose(log);
    }

    printf("Quarantine: %d moved, %d failed\n", moved, failed);
    if (moved > 0) printf("Log: %s\n", logpath);
    return failed > 0 ? 1 : 0;
}

/* ── Help / version ──────────────────────────────────────────── */

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] <path> [path ...]\n\n"
        "Find duplicate files and filesystem lint.\n\n"
        "Options:\n"
        "  -e  --empty           report empty files and directories\n"
        "  -V  --verify          byte-for-byte verify after SHA-256 match\n"
        "  -k  --keep POLICY     newest|oldest|first  (default: newest)\n"
        "      --min-size BYTES  skip files smaller than BYTES (default: 1)\n"
        "      --json FILE       write JSON report to FILE\n"
        "      --csv  FILE       write CSV  report to FILE\n"
        "      --quarantine DIR  move non-kept duplicates to DIR\n"
        "  -v  --verbose         show progress\n"
        "      --no-color        disable ANSI colors\n"
        "      --version         show version\n"
        "      --help            show this help\n\n"
        "Exit: 0=clean  1=lint found  2=error\n",
        prog);
}

/* ── main ────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    int ret = 0;

    memset(&g_opts, 0, sizeof(g_opts));
    g_opts.keep     = KEEP_NEWEST;
    g_opts.min_size = 1;
    g_color         = _isatty(_fileno(stdout));

    char **paths = (char **)malloc(argc * sizeof(char *));
    if (!paths) { fprintf(stderr, "wlint: out of memory\n"); return 2; }
    int npaths = 0;

    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if      (!strcmp(a, "--help")    || !strcmp(a, "-h")) {
            usage(argv[0]); free(paths); return 0;
        }
        else if (!strcmp(a, "--version"))                     {
            printf("wlint %s (Winix)\n", WLINT_VERSION); free(paths); return 0;
        }
        else if (!strcmp(a, "-e") || !strcmp(a, "--empty"))   { g_opts.do_empty  = 1; }
        else if (!strcmp(a, "-V") || !strcmp(a, "--verify"))  { g_opts.do_verify = 1; }
        else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) { g_opts.verbose   = 1; }
        else if (!strcmp(a, "--no-color"))                    { g_color           = 0; }
        else if ((!strcmp(a, "-k") || !strcmp(a, "--keep")) && i + 1 < argc) {
            char *pol = argv[++i];
            if      (!strcmp(pol, "newest")) g_opts.keep = KEEP_NEWEST;
            else if (!strcmp(pol, "oldest")) g_opts.keep = KEEP_OLDEST;
            else if (!strcmp(pol, "first"))  g_opts.keep = KEEP_FIRST;
            else {
                fprintf(stderr, "wlint: unknown keep policy '%s' "
                        "(use newest|oldest|first)\n", pol);
                free(paths); return 2;
            }
        }
        else if (!strcmp(a, "--min-size") && i + 1 < argc) {
            g_opts.min_size = (int64_t)atoll(argv[++i]);
        }
        else if (!strcmp(a, "--json") && i + 1 < argc)      { g_opts.json_out  = argv[++i]; }
        else if (!strcmp(a, "--csv")  && i + 1 < argc)      { g_opts.csv_out   = argv[++i]; }
        else if (!strcmp(a, "--quarantine") && i + 1 < argc) { g_opts.quarantine = argv[++i]; }
        else if (a[0] == '-') {
            fprintf(stderr, "wlint: unknown option: %s\n", a);
            usage(argv[0]); free(paths); return 2;
        }
        else {
            paths[npaths++] = a;
        }
    }

    if (npaths == 0) {
        fprintf(stderr, "wlint: no paths specified\n");
        usage(argv[0]); free(paths); return 2;
    }

    g_opts.scan_paths = paths;
    g_opts.nscan      = npaths;

    if (bcrypt_init() != 0) {
        fprintf(stderr, "wlint: failed to initialize SHA-256 (BCrypt)\n");
        free(paths); return 2;
    }

    /* scan */
    for (int i = 0; i < npaths; i++) {
        wchar_t *wpath = wide_from_utf8(paths[i]);
        if (!wpath) continue;
        normalize_wpath(wpath);

        DWORD attr = GetFileAttributesW(wpath);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            fprintf(stderr, "wlint: cannot access: %s\n", paths[i]);
            free(wpath); continue;
        }

        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            scan_path(wpath, 0);
        } else {
            /* single file */
            WIN32_FILE_ATTRIBUTE_DATA fad;
            GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad);
            int64_t sz = ((int64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            g_files_scanned++;
            if (sz == 0) {
                if (g_opts.do_empty)
                    fvec_push(&g_empty_files,
                              file_new(paths[i], 0, fad.ftLastWriteTime));
            } else if (sz >= g_opts.min_size) {
                fvec_push(&g_files, file_new(paths[i], sz, fad.ftLastWriteTime));
            }
        }
        free(wpath);
    }

    if (g_opts.verbose)
        fprintf(stderr, "Scanned: %zu files in %zu dirs.\n",
                g_files_scanned, g_dirs_scanned);

    find_duplicates();

    output_pretty();

    if (g_opts.json_out) {
        FILE *fp = fopen(g_opts.json_out, "w");
        if (!fp) {
            fprintf(stderr, "wlint: cannot write: %s\n", g_opts.json_out);
            ret = 2;
        } else {
            output_json(fp);
            fclose(fp);
            if (g_opts.verbose)
                fprintf(stderr, "JSON: %s\n", g_opts.json_out);
        }
    }

    if (g_opts.csv_out) {
        FILE *fp = fopen(g_opts.csv_out, "w");
        if (!fp) {
            fprintf(stderr, "wlint: cannot write: %s\n", g_opts.csv_out);
            ret = 2;
        } else {
            output_csv(fp);
            fclose(fp);
            if (g_opts.verbose)
                fprintf(stderr, "CSV: %s\n", g_opts.csv_out);
        }
    }

    if (g_opts.quarantine && g_dupsets.count > 0)
        if (do_quarantine(g_opts.quarantine) != 0) ret = 2;

    /* set exit code if no error occurred during output */
    if (ret == 0) {
        int lint = (g_dupsets.count > 0) ||
                   (g_opts.do_empty &&
                    (g_empty_files.count > 0 || g_empty_dirs.count > 0));
        ret = lint ? 1 : 0;
    }

    bcrypt_fini();
    fvec_free(&g_files);
    fvec_free(&g_empty_files);
    fvec_free(&g_empty_dirs);
    for (size_t i = 0; i < g_dupsets.count; i++) free(g_dupsets.items[i].files);
    free(g_dupsets.items);
    free(paths);

    return ret;
}
