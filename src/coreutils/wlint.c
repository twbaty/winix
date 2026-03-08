/*
 * wlint — Winix filesystem lint detector  v1.3
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
 *   4. Group by hash -> duplicate groups
 *   5. Optional byte-for-byte verification pass
 *   6. Apply keep policy; output results
 *
 * Usage:
 *   wlint [options] <path> [path ...]
 *   -e  --empty           report empty files and directories
 *   -t  --temp            report temp/junk files (.tmp .bak .swp ~$ etc.)
 *   -V  --verify          byte-for-byte verify after SHA-256 match
 *   -k  --keep POLICY     newest | oldest | first  (default: newest)
 *       --min-size BYTES  skip files smaller than BYTES (default: 1)
 *       --json FILE       write JSON report to FILE
 *       --csv  FILE       write CSV  report to FILE
 *       --scan-json FILE  write full file inventory JSON for wsim
 *       --quarantine DIR  move non-kept duplicates to DIR
 *       --dry-run         show quarantine plan without moving files
 *       --undo MANIFEST   restore quarantined files from move manifest
 *   -v  --verbose         progress output
 *       --no-color        disable ANSI colors
 *       --version
 *       --help
 *
 * Behavior contract:
 *   newest  Highest FILETIME kept; mtime ties -> lex-first path kept
 *   oldest  Lowest FILETIME kept; same tie-break
 *   first   Lex-first path by byte order (case-sensitive strcmp)
 *   Hidden/system files  Included
 *   Reparse points       Skipped, never followed, never reported
 *   Access denied/locked Warning to stderr; file skipped; counted in summary
 *   Output order         Duplicate groups: reclaimable bytes descending
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
#include <ctype.h>

#define WLINT_VERSION  "1.8"
#define SCHEMA_VERSION "1.0"
#define SHA256_BYTES   32
#define SHA256_HEX     65        /* 64 hex digits + NUL */
#define READ_BUF_SZ    1048576   /* 1 MiB I/O buffer */
#define SAMPLE_SIZE    1048576LL /* 1 MiB quick-hash sample */
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
    int         do_temp;         /* --temp: report temp/junk files */
    int         do_verify;
    KeepPolicy  keep;
    int64_t     min_size;
    char       *json_out;
    char       *csv_out;
    char       *quarantine;
    int         dry_run;         /* --dry-run: show quarantine plan, no moves */
    char       *undo_manifest;   /* --undo <path>: restore from manifest      */
    int         verbose;
    char      **include_pats;   int ninclude;   int include_cap;
    char      **exclude_pats;   int nexclude;   int exclude_cap;
    int64_t     max_size;       /* 0 = no limit */
    int         do_stats;
    char       *scan_json_out;  /* --scan-json FILE */
    char       *log_out;        /* --log FILE       */
    int         threads;        /* --threads N (default 2) */
    int         age_days;       /* --age N: flag files not modified in N days */
} Opts;

/* ── Globals ─────────────────────────────────────────────────── */

static BCRYPT_ALG_HANDLE g_hAlg     = NULL;
static DWORD             g_cbHashObj = 0;

static FileVec   g_files;
static FileVec   g_empty_files;
static FileVec   g_empty_dirs;
static FileVec   g_temp_files;
static FileVec   g_stale_files;
static DupSetVec g_dupsets;
static Opts      g_opts;

static size_t g_dirs_scanned  = 0;
static size_t g_files_scanned = 0;
static size_t g_hashed        = 0;
static size_t g_warnings      = 0;

static ULONGLONG g_start_ms         = 0;
static ULONGLONG g_elapsed_ms       = 0;
static int64_t   g_bytes_in_pool    = 0;
static size_t    g_same_size_groups = 0;
static size_t    g_sha256_ops       = 0;
static size_t    g_partial_ops      = 0;   /* quick-hash (1 MiB sample) ops */
static size_t    g_verify_ops       = 0;

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

/* Normalize wide path: forward slashes -> backslashes, strip trailing sep */
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

static const char *path_basename(const char *p) {
    const char *s = p;
    for (const char *c = p; *c; c++)
        if (*c == '\\' || *c == '/') s = c + 1;
    return s;
}

static const char *path_ext(const char *basename) {
    const char *dot = NULL;
    for (const char *c = basename; *c; c++)
        if (*c == '.') dot = c;
    return dot ? dot : "";
}

/* ── Glob matcher ────────────────────────────────────────────── */

/* Case-insensitive glob: '*' matches any chars (including '\'), '?' matches one char */
static int glob_match_ci(const char *pat, const char *str) {
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;          /* collapse ** */
            if (!*pat) return 1;                /* trailing star */
            while (*str)
                if (glob_match_ci(pat, str++)) return 1;
            return 0;
        } else if (*pat == '?') {
            if (!*str) return 0;
            pat++; str++;
        } else {
            if (tolower((unsigned char)*pat) != tolower((unsigned char)*str)) return 0;
            pat++; str++;
        }
    }
    return *str == '\0';
}

static int file_accepted(const char *path) {
    if (g_opts.ninclude > 0) {
        int ok = 0;
        for (int i = 0; i < g_opts.ninclude && !ok; i++)
            ok = glob_match_ci(g_opts.include_pats[i], path);
        if (!ok) return 0;
    }
    for (int i = 0; i < g_opts.nexclude; i++)
        if (glob_match_ci(g_opts.exclude_pats[i], path)) return 0;
    return 1;
}

/* Returns 1 if the file looks like a temp/junk file by extension or name pattern. */
static int is_temp_file(const char *basename) {
    static const char * const TEMP_EXTS[] = {
        ".tmp", ".temp", ".bak", ".old", ".orig",
        ".swp", ".swo", ".cache",
        ".crdownload", ".part", ".partial", ".dmp",
        NULL
    };
    const char *ext = path_ext(basename);
    for (int i = 0; TEMP_EXTS[i]; i++)
        if (_stricmp(ext, TEMP_EXTS[i]) == 0) return 1;

    /* Office/editor lock files: ~$*.docx, *.c~ etc. */
    size_t blen = strlen(basename);
    if (blen >= 2 && basename[0] == '~' && basename[1] == '$') return 1;
    if (blen >= 1 && basename[blen - 1] == '~') return 1;

    return 0;
}

static void push_include(const char *pat) {
    if (g_opts.ninclude == g_opts.include_cap) {
        g_opts.include_cap = g_opts.include_cap ? g_opts.include_cap * 2 : 8;
        g_opts.include_pats = (char **)realloc(g_opts.include_pats,
                                               g_opts.include_cap * sizeof(char *));
    }
    char *p = _strdup(pat);
    for (char *c = p; *c; c++) if (*c == '/') *c = '\\';
    g_opts.include_pats[g_opts.ninclude++] = p;
}

static void push_exclude(const char *pat) {
    if (g_opts.nexclude == g_opts.exclude_cap) {
        g_opts.exclude_cap = g_opts.exclude_cap ? g_opts.exclude_cap * 2 : 8;
        g_opts.exclude_pats = (char **)realloc(g_opts.exclude_pats,
                                               g_opts.exclude_cap * sizeof(char *));
    }
    char *p = _strdup(pat);
    for (char *c = p; *c; c++) if (*c == '/') *c = '\\';
    g_opts.exclude_pats[g_opts.nexclude++] = p;
}

static void parse_ext(const char *list) {
    char buf[1024]; strncpy(buf, list, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
    char *tok = strtok(buf, ",");
    while (tok) {
        while (*tok == ' ') tok++;
        char pat[128];
        snprintf(pat, sizeof(pat), tok[0]=='.' ? "*%s" : "*.%s", tok);
        push_include(pat);
        tok = strtok(NULL, ",");
    }
}

/* ── JSON parsing helpers ────────────────────────────────────── */

/* Extract a string value for "key": "value" from a JSON buffer.
 * Handles \" and \\ escape sequences. Returns 1 on success. */
static int json_str_val(const char *buf, const char *key,
                        char *out, size_t outsz) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;
    size_t j = 0;
    while (*p && j + 1 < outsz) {
        if (*p == '\\') {
            p++;
            if      (*p == '"')  { out[j++] = '"';  p++; }
            else if (*p == '\\') { out[j++] = '\\'; p++; }
            else                 { out[j++] = *p++;      }
        } else if (*p == '"') {
            break;
        } else {
            out[j++] = *p++;
        }
    }
    out[j] = '\0';
    return 1;
}

/* Extract an integer value for "key": number from a JSON buffer.
 * Returns 1 on success. */
static int json_int64_val(const char *buf, const char *key, int64_t *out) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '-' && (*p < '0' || *p > '9')) return 0;
    *out = (int64_t)_strtoi64(p, NULL, 10);
    return 1;
}

/* Iterate {...} objects in buf[0..buflen).
 * *pos starts at 0; each call fills block[0..blocksz) with the next object
 * and advances *pos past it. Returns 1 while objects remain, 0 when done.
 * Correctly tracks string context so braces inside strings are ignored. */
static int next_json_obj(const char *buf, size_t buflen, size_t *pos,
                         char *block, size_t blocksz) {
    /* find opening brace */
    while (*pos < buflen && buf[*pos] != '{') (*pos)++;
    if (*pos >= buflen) return 0;

    size_t start = *pos;
    int depth = 0;
    int in_str = 0;
    size_t i = start;

    while (i < buflen) {
        char c = buf[i];
        if (in_str) {
            if (c == '\\') { i += 2; continue; }
            if (c == '"')  { in_str = 0; }
        } else {
            if      (c == '"') { in_str = 1; }
            else if (c == '{') { depth++; }
            else if (c == '}') { depth--; if (depth == 0) { i++; break; } }
        }
        i++;
    }

    size_t len = i - start;
    if (len == 0 || len >= blocksz) { *pos = i; return 0; }
    memcpy(block, buf + start, len);
    block[len] = '\0';
    *pos = i;
    return 1;
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

/* Hash up to max_bytes of path into hex.  max_bytes=0 means the whole file. */
static int sha256_file(const char *path, char hex[SHA256_HEX], int64_t max_bytes) {
    BCRYPT_HASH_HANDLE hHash  = NULL;
    PBYTE              pbObj  = NULL;
    BYTE               digest[SHA256_BYTES];
    HANDLE             hFile  = INVALID_HANDLE_VALUE;
    wchar_t           *wpath  = NULL;
    BYTE              *buf    = NULL;
    DWORD              nRead;
    int64_t            total  = 0;
    int                ret    = -1;

    buf = (BYTE *)malloc(READ_BUF_SZ);
    if (!buf) goto done;

    pbObj = (PBYTE)malloc(g_cbHashObj);
    if (!pbObj) goto done;

    if (BCryptCreateHash(g_hAlg, &hHash, pbObj, g_cbHashObj, NULL, 0, 0) != 0)
        goto done;

    wpath = wide_from_utf8(path);
    if (!wpath) goto done;

    hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) goto done;

    while (ReadFile(hFile, buf, READ_BUF_SZ, &nRead, NULL) && nRead > 0) {
        DWORD to_hash = nRead;
        if (max_bytes > 0 && total + (int64_t)nRead > max_bytes)
            to_hash = (DWORD)(max_bytes - total);
        if (BCryptHashData(hHash, buf, to_hash, 0) != 0) goto done;
        total += to_hash;
        if (max_bytes > 0 && total >= max_bytes) break;
    }

    if (BCryptFinishHash(hHash, digest, SHA256_BYTES, 0) != 0) goto done;

    for (int i = 0; i < SHA256_BYTES; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[SHA256_HEX - 1] = '\0';
    ret = 0;

done:
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    if (hHash) BCryptDestroyHash(hHash);
    free(buf);
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
        g_warnings++;
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
            } else if (size >= g_opts.min_size &&
                       (g_opts.max_size == 0 || size <= g_opts.max_size) && pu) {
                if (file_accepted(pu)) {
                    fvec_push(&g_files, file_new(pu, size, fd.ftLastWriteTime));
                    g_bytes_in_pool += size;
                }
                if (g_opts.do_temp) {
                    char *base = (char *)path_basename(pu);
                    if (is_temp_file(base))
                        fvec_push(&g_temp_files,
                                  file_new(pu, size, fd.ftLastWriteTime));
                }
                if (g_opts.age_days > 0) {
                    FILETIME now_ft;
                    GetSystemTimeAsFileTime(&now_ft);
                    ULONGLONG now_u  = ((ULONGLONG)now_ft.dwHighDateTime << 32) |
                                       now_ft.dwLowDateTime;
                    ULONGLONG file_u = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) |
                                       fd.ftLastWriteTime.dwLowDateTime;
                    /* 100-ns intervals per day = 864000000000 */
                    int64_t age = (int64_t)((now_u - file_u) / 864000000000ULL);
                    if (age >= g_opts.age_days && pu)
                        fvec_push(&g_stale_files,
                                  file_new(pu, size, fd.ftLastWriteTime));
                }
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

/* ── Analysis: size -> hash -> duplicate groups ─────────────── */

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
    return strcmp(fa->path, fb->path);  /* tie-break: lex-first path -> index 0 -> KEEP */
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

/* ── Parallel hash worker pool ────────────────────────────────── */

typedef struct {
    File           **files;
    size_t           nfiles;
    int64_t          max_bytes;
    volatile LONG    done;      /* progress counter — Interlocked */
    size_t           next_idx;  /* next file to claim — protected by cs */
    size_t           errors;    /* hash errors      — protected by cs */
    CRITICAL_SECTION cs;
} HashWork;

static DWORD WINAPI hash_worker_thread(LPVOID arg) {
    HashWork *w = (HashWork *)arg;
    for (;;) {
        EnterCriticalSection(&w->cs);
        size_t idx = w->next_idx;
        if (idx >= w->nfiles) { LeaveCriticalSection(&w->cs); break; }
        w->next_idx++;
        LeaveCriticalSection(&w->cs);

        File *f = w->files[idx];
        if (sha256_file(f->path, f->hash, w->max_bytes) != 0) {
            snprintf(f->hash, SHA256_HEX, "ERR%zu", idx);
            EnterCriticalSection(&w->cs);
            fprintf(stderr, "wlint: warning: cannot hash: %s\n", f->path);
            w->errors++;
            LeaveCriticalSection(&w->cs);
        }
        InterlockedIncrement(&w->done);
    }
    return 0;
}

/*
 * Hash files[0..n-1] using up to nthreads threads.
 * max_bytes: 0 = full file, >0 = partial sample.
 * Returns error count; caller adds to g_warnings.
 */
static size_t hash_parallel(File **files, size_t n,
                             int64_t max_bytes, int nthreads,
                             const char *label) {
    if (n == 0) return 0;
    if (nthreads < 1) nthreads = 1;
    if ((size_t)nthreads > n) nthreads = (int)n;
    if (nthreads > 64) nthreads = 64; /* WaitForMultipleObjects limit */

    HashWork w;
    w.files    = files;
    w.nfiles   = n;
    w.max_bytes = max_bytes;
    w.done     = 0;
    w.next_idx = 0;
    w.errors   = 0;
    InitializeCriticalSection(&w.cs);

    HANDLE *ths = (HANDLE *)malloc((size_t)nthreads * sizeof(HANDLE));
    if (!ths) {
        DeleteCriticalSection(&w.cs);
        /* fallback: single-threaded */
        for (size_t k = 0; k < n; k++) {
            File *f = files[k];
            if (sha256_file(f->path, f->hash, max_bytes) != 0) {
                snprintf(f->hash, SHA256_HEX, "ERR%zu", k);
                fprintf(stderr, "wlint: warning: cannot hash: %s\n", f->path);
                w.errors++;
            }
            if (g_opts.verbose)
                fprintf(stderr, "\r  %s... %zu   \r", label, k + 1);
        }
        return w.errors;
    }

    for (int t = 0; t < nthreads; t++)
        ths[t] = CreateThread(NULL, 0, hash_worker_thread, &w, 0, NULL);

    /* Main thread drives the verbose ticker while workers run */
    if (g_opts.verbose) {
        LONG last = -1;
        while (w.done < (LONG)n) {
            LONG cur = w.done;
            if (cur != last) {
                fprintf(stderr, "\r  %s... %ld/%zu   \r", label, cur, n);
                last = cur;
            }
            Sleep(50);
        }
        fprintf(stderr, "\r  %s... %zu/%zu   \r", label, n, n);
    }

    WaitForMultipleObjects(nthreads, ths, TRUE, INFINITE);
    for (int t = 0; t < nthreads; t++) CloseHandle(ths[t]);
    free(ths);
    DeleteCriticalSection(&w.cs);
    return w.errors;
}

static void find_duplicates(void) {
    if (g_files.count == 0) return;

    /* Phase 1: sort by size — O(n log n), no I/O */
    qsort(g_files.items, g_files.count, sizeof(File *), cmp_size);

    size_t i = 0;
    while (i < g_files.count) {
        size_t j = i + 1;
        while (j < g_files.count &&
               g_files.items[j]->size == g_files.items[i]->size)
            j++;

        size_t  run       = j - i;
        int64_t file_size = g_files.items[i]->size;
        if (run < 2) { i = j; continue; }

        g_same_size_groups++;

        /*
         * Phase 2a: quick-hash (first SAMPLE_SIZE bytes) for large files;
         * full hash for files that already fit in the sample window.
         * Files that differ at byte 0..SAMPLE_SIZE are eliminated here —
         * no need to read the remaining hundreds of MB.
         */
        int use_partial = (file_size > SAMPLE_SIZE);

        {
            size_t errs = hash_parallel(
                g_files.items + i, run,
                use_partial ? SAMPLE_SIZE : 0,
                g_opts.threads,
                use_partial ? "Quick hash" : "Hashing");
            g_warnings += errs;
            if (use_partial) g_partial_ops += run;
            else { g_sha256_ops += run; g_hashed += run; }
        }

        /* sort same-size group by quick/full hash */
        qsort(g_files.items + i, run, sizeof(File *), cmp_hash);

        /*
         * Phase 2b: walk quick-hash runs.
         * For large files: only the subset that matched on the sample gets a
         * full read — typically a tiny fraction of the group.
         * For small files: hash is already final; go straight to dup detection.
         */
        size_t m = i;
        while (m < j) {
            size_t n = m + 1;
            while (n < j && strcmp(g_files.items[n]->hash,
                                   g_files.items[m]->hash) == 0)
                n++;

            size_t subrun = n - m;
            if (subrun > 1) {
                if (use_partial) {
                    /* Full hash only for files that survived the quick-hash filter */
                    size_t errs2 = hash_parallel(
                        g_files.items + m, subrun, 0,
                        g_opts.threads, "Hashing");
                    g_warnings   += errs2;
                    g_sha256_ops += subrun;
                    g_hashed     += subrun;
                    qsort(g_files.items + m, subrun, sizeof(File *), cmp_hash);
                }

                /* Find confirmed duplicate runs */
                size_t p = m;
                while (p < n) {
                    size_t q = p + 1;
                    while (q < n && strcmp(g_files.items[q]->hash,
                                           g_files.items[p]->hash) == 0)
                        q++;

                    if (q - p > 1) {
                        DupSet ds;
                        memset(&ds, 0, sizeof(ds));
                        ds.size  = g_files.items[p]->size;
                        memcpy(ds.hash, g_files.items[p]->hash, SHA256_HEX);
                        ds.count = q - p;
                        ds.files = (File **)malloc(ds.count * sizeof(File *));

                        for (size_t k = 0; k < ds.count; k++)
                            ds.files[k] = g_files.items[p + k];

                        if (g_opts.do_verify && ds.count >= 2) {
                            g_verify_ops++;
                            ds.verified = files_identical(ds.files[0]->path,
                                                          ds.files[1]->path);
                        }

                        apply_keep_policy(&ds);
                        qsort(ds.files + 1, ds.count - 1, sizeof(File *), cmp_path);
                        dvec_push(&g_dupsets, ds);
                    }
                    p = q;
                }
            }
            m = n;
        }
        i = j;
    }

    if (g_opts.verbose) fprintf(stderr, "\n");
}

/* ── Sort dupsets by reclaimable bytes descending ────────────── */

static int cmp_reclaimable_desc(const void *a, const void *b) {
    const DupSet *da = (const DupSet *)a;
    const DupSet *db = (const DupSet *)b;
    int64_t ra = (int64_t)(da->count - 1) * da->size;
    int64_t rb = (int64_t)(db->count - 1) * db->size;
    if (ra > rb) return -1;
    if (ra < rb) return  1;
    return 0;
}

static void sort_dupsets(void) {
    if (g_dupsets.count > 1)
        qsort(g_dupsets.items, g_dupsets.count, sizeof(DupSet), cmp_reclaimable_desc);
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

        fmt_size(ds->size,    sz,  sizeof(sz));
        fmt_size(reclaimable, rec, sizeof(rec));

        printf("%s[DUPLICATE GROUP]%s  %zu files | %s each | %s reclaimable%s\n",
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

    if (g_opts.do_temp && g_temp_files.count > 0) {
        printf("%s[TEMP / JUNK FILES]%s\n", cc(C_YEL), cr());
        for (size_t i = 0; i < g_temp_files.count; i++) {
            char tsz[64], tmt[32];
            fmt_size(g_temp_files.items[i]->size, tsz, sizeof(tsz));
            fmt_mtime(g_temp_files.items[i]->mtime, tmt, sizeof(tmt));
            printf("  %s  %s  %s\n", tsz, tmt, g_temp_files.items[i]->path);
        }
        printf("\n");
    }

    if (g_opts.age_days > 0 && g_stale_files.count > 0) {
        printf("%s[STALE FILES]%s  (not modified in %d+ days)\n",
               cc(C_YEL), cr(), g_opts.age_days);
        for (size_t i = 0; i < g_stale_files.count; i++) {
            char tsz[64], tmt[32];
            fmt_size(g_stale_files.items[i]->size, tsz, sizeof(tsz));
            fmt_mtime(g_stale_files.items[i]->mtime, tmt, sizeof(tmt));
            printf("  %s  %s  %s\n", tsz, tmt, g_stale_files.items[i]->path);
        }
        printf("\n");
    }

    int lint_found = (g_dupsets.count > 0) ||
                     (g_opts.do_empty &&
                      (g_empty_files.count > 0 || g_empty_dirs.count > 0)) ||
                     (g_opts.do_temp && g_temp_files.count > 0) ||
                     (g_opts.age_days > 0 && g_stale_files.count > 0);

    if (!lint_found) {
        printf("%s[CLEAN]%s  No lint found.\n", cc(C_GRN), cr());
    }

    fmt_size(total_reclaimable, sz, sizeof(sz));
    printf("%s[SUMMARY]%s\n", cc(C_BOLD), cr());
    printf("  Files scanned:    %zu\n",  g_files_scanned);
    printf("  Duplicate groups: %zu\n",  g_dupsets.count);
    printf("  Duplicate files:  %zu\n",  total_dup_files);
    printf("  Reclaimable:      %s\n",   sz);
    if (g_opts.do_empty) {
        printf("  Empty files:      %zu\n", g_empty_files.count);
        printf("  Empty dirs:       %zu\n", g_empty_dirs.count);
    }
    if (g_opts.do_temp)
        printf("  Temp/junk files:  %zu\n", g_temp_files.count);
    if (g_opts.age_days > 0)
        printf("  Stale files:      %zu  (>= %d days)\n", g_stale_files.count, g_opts.age_days);
    if (g_warnings > 0)
        printf("  Warnings:         %zu (unreadable files, see stderr)\n", g_warnings);

    if (g_opts.do_stats) {
        char pool_sz[64];
        fmt_size(g_bytes_in_pool, pool_sz, sizeof(pool_sz));
        printf("%s[STATS]%s\n", cc(C_BOLD), cr());
        printf("  Elapsed:           %llu ms\n",  (unsigned long long)g_elapsed_ms);
        printf("  Bytes in pool:     %s\n",        pool_sz);
        printf("  Hash threads:      %d\n",         g_opts.threads);
        printf("  Same-size groups:  %zu\n",        g_same_size_groups);
        if (g_partial_ops > 0) {
            size_t skipped = g_partial_ops > g_sha256_ops
                           ? g_partial_ops - g_sha256_ops : 0;
            printf("  Quick hash ops:    %zu  (1 MiB sample)\n", g_partial_ops);
            printf("  Full SHA-256 ops:  %zu  (%zu large-file reads skipped)\n",
                   g_sha256_ops, skipped);
        } else {
            printf("  SHA-256 ops:       %zu\n",   g_sha256_ops);
        }
        printf("  Byte-verify ops:   %zu\n",       g_verify_ops);
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
    fprintf(fp, "  \"schema_version\": \"%s\",\n", SCHEMA_VERSION);
    fprintf(fp, "  \"wlint_version\": \"%s\",\n", WLINT_VERSION);
    fprintf(fp, "  \"generated\": \"%s\",\n", generated);

    fprintf(fp, "  \"scan_paths\": [");
    for (int i = 0; i < g_opts.nscan; i++) {
        json_esc(g_opts.scan_paths[i], esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"summary\": {\n");
    fprintf(fp, "    \"files_scanned\": %zu,\n",      g_files_scanned);
    fprintf(fp, "    \"duplicate_groups\": %zu,\n",   g_dupsets.count);
    fprintf(fp, "    \"duplicate_files\": %zu,\n",    total_dups);
    fprintf(fp, "    \"reclaimable_bytes\": %lld,\n", (long long)total_rec);
    fprintf(fp, "    \"empty_files\": %zu,\n",        g_empty_files.count);
    fprintf(fp, "    \"empty_dirs\": %zu,\n",         g_empty_dirs.count);
    fprintf(fp, "    \"temp_files\": %zu,\n",         g_temp_files.count);
    fprintf(fp, "    \"warnings\": %zu\n",             g_warnings);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"stats\": {\n");
    fprintf(fp, "    \"elapsed_ms\": %llu,\n",      (unsigned long long)g_elapsed_ms);
    fprintf(fp, "    \"bytes_in_pool\": %lld,\n",   (long long)g_bytes_in_pool);
    fprintf(fp, "    \"same_size_groups\": %zu,\n", g_same_size_groups);
    fprintf(fp, "    \"quick_hash_ops\": %zu,\n",   g_partial_ops);
    fprintf(fp, "    \"sha256_ops\": %zu,\n",       g_sha256_ops);
    fprintf(fp, "    \"verify_ops\": %zu\n",        g_verify_ops);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"duplicate_groups\": [\n");
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

    fprintf(fp, "  \"empty_directories\": [");
    for (size_t i = 0; i < g_empty_dirs.count; i++) {
        json_esc(g_empty_dirs.items[i]->path, esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"temp_files\": [");
    for (size_t i = 0; i < g_temp_files.count; i++) {
        json_esc(g_temp_files.items[i]->path, esc, sizeof(esc));
        fmt_mtime(g_temp_files.items[i]->mtime, mt, sizeof(mt));
        fprintf(fp, "%s{\"path\": \"%s\", \"size\": %lld, \"mtime\": \"%s\"}",
                i ? ", " : "", esc,
                (long long)g_temp_files.items[i]->size, mt);
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
        fprintf(fp, "empty_directory,,0,,\"%s\",\n", g_empty_dirs.items[i]->path);

    for (size_t i = 0; i < g_temp_files.count; i++) {
        fmt_mtime(g_temp_files.items[i]->mtime, mt, sizeof(mt));
        fprintf(fp, "temp_file,,0,,\"%s\",%s\n",
                g_temp_files.items[i]->path, mt);
    }
}

/* ── Output: Scan JSON (wsim candidate inventory) ────────────── */

static void output_scan_json(FILE *fp) {
    char esc[PATH_BUF * 2];
    char esc_base[512];
    char esc_ext[64];
    char mt[32];

    /* timestamp */
    SYSTEMTIME now;
    GetSystemTime(&now);
    char ts[32];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"schema_version\": \"1.0\",\n");
    fprintf(fp, "  \"wlint_version\": \"%s\",\n", WLINT_VERSION);
    fprintf(fp, "  \"generated\": \"%s\",\n", ts);

    /* scan_paths array */
    fprintf(fp, "  \"scan_paths\": [");
    for (int i = 0; i < g_opts.nscan; i++) {
        json_esc(g_opts.scan_paths[i], esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "],\n");

    /* filters */
    fprintf(fp, "  \"filters\": {\n");
    fprintf(fp, "    \"min_size\": %lld,\n", (long long)g_opts.min_size);
    fprintf(fp, "    \"max_size\": %lld,\n", (long long)g_opts.max_size);
    fprintf(fp, "    \"include_pats\": [");
    for (int i = 0; i < g_opts.ninclude; i++) {
        json_esc(g_opts.include_pats[i], esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "],\n");
    fprintf(fp, "    \"exclude_pats\": [");
    for (int i = 0; i < g_opts.nexclude; i++) {
        json_esc(g_opts.exclude_pats[i], esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "]\n");
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"file_count\": %zu,\n", g_files.count);
    fprintf(fp, "  \"files\": [\n");

    for (size_t i = 0; i < g_files.count; i++) {
        File *f = g_files.items[i];
        json_esc(f->path, esc, sizeof(esc));
        fmt_mtime(f->mtime, mt, sizeof(mt));
        const char *base = path_basename(f->path);
        const char *ext  = path_ext(base);
        json_esc(base, esc_base, sizeof(esc_base));
        json_esc(ext,  esc_ext,  sizeof(esc_ext));
        fprintf(fp, "    {\"path\": \"%s\", \"size\": %lld, \"mtime\": \"%s\","
                    " \"ext\": \"%s\", \"basename\": \"%s\"}%s\n",
                esc, (long long)f->size, mt, esc_ext, esc_base,
                i + 1 < g_files.count ? "," : "");
    }

    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
}

/* ── Output: operational log ─────────────────────────────────── */

static void output_log(FILE *fp) {
    SYSTEMTIME now;
    GetSystemTime(&now);
    char ts[32];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond);

    char esc[PATH_BUF * 2];

    /* compute summary stats */
    int64_t total_reclaimable = 0;
    size_t  total_dup_files   = 0;
    for (size_t i = 0; i < g_dupsets.count; i++) {
        DupSet *ds = &g_dupsets.items[i];
        total_reclaimable += (int64_t)(ds->count - 1) * ds->size;
        total_dup_files   += ds->count;
    }

    const char *keep_str = (g_opts.keep == KEEP_OLDEST) ? "oldest"
                         : (g_opts.keep == KEEP_FIRST)  ? "first" : "newest";

    fprintf(fp, "{\n");
    fprintf(fp, "  \"schema_version\": \"%s\",\n", SCHEMA_VERSION);
    fprintf(fp, "  \"wlint_version\": \"%s\",\n",  WLINT_VERSION);
    fprintf(fp, "  \"run_at\": \"%s\",\n", ts);
    fprintf(fp, "  \"scan_paths\": [");
    for (int i = 0; i < g_opts.nscan; i++) {
        json_esc(g_opts.scan_paths[i], esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "],\n");
    fprintf(fp, "  \"summary\": {\n");
    fprintf(fp, "    \"files_scanned\":     %zu,\n", g_files_scanned);
    fprintf(fp, "    \"dirs_scanned\":      %zu,\n", g_dirs_scanned);
    fprintf(fp, "    \"duplicate_groups\":  %zu,\n", g_dupsets.count);
    fprintf(fp, "    \"duplicate_files\":   %zu,\n", total_dup_files);
    fprintf(fp, "    \"bytes_reclaimable\": %lld,\n", (long long)total_reclaimable);
    fprintf(fp, "    \"empty_files\":       %zu,\n", g_empty_files.count);
    fprintf(fp, "    \"empty_dirs\":        %zu,\n", g_empty_dirs.count);
    fprintf(fp, "    \"temp_files\":        %zu,\n", g_temp_files.count);
    fprintf(fp, "    \"warnings\":          %zu,\n", g_warnings);
    fprintf(fp, "    \"elapsed_ms\":        %llu\n",
            (unsigned long long)g_elapsed_ms);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"options\": {\n");
    fprintf(fp, "    \"keep\":     \"%s\",\n", keep_str);
    fprintf(fp, "    \"min_size\": %lld,\n",  (long long)g_opts.min_size);
    fprintf(fp, "    \"max_size\": %lld,\n",  (long long)g_opts.max_size);
    fprintf(fp, "    \"verify\":   %s,\n", g_opts.do_verify ? "true" : "false");
    fprintf(fp, "    \"empty\":    %s,\n", g_opts.do_empty  ? "true" : "false");
    fprintf(fp, "    \"temp\":     %s,\n", g_opts.do_temp   ? "true" : "false");
    fprintf(fp, "    \"include_pats\": [");
    for (int i = 0; i < g_opts.ninclude; i++) {
        json_esc(g_opts.include_pats[i], esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "],\n");
    fprintf(fp, "    \"exclude_pats\": [");
    for (int i = 0; i < g_opts.nexclude; i++) {
        json_esc(g_opts.exclude_pats[i], esc, sizeof(esc));
        fprintf(fp, "%s\"%s\"", i ? ", " : "", esc);
    }
    fprintf(fp, "]\n");
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");
}

/* ── Subtree guard ───────────────────────────────────────────── */

/* Return 1 if qdir is inside or equal to any scan root (case-insensitive). */
static int qdir_inside_scan(const char *qdir) {
    wchar_t *wq_tmp = wide_from_utf8(qdir);
    if (!wq_tmp) return 0;
    wchar_t wq_buf[PATH_BUF];
    DWORD n = GetFullPathNameW(wq_tmp, PATH_BUF, wq_buf, NULL);
    free(wq_tmp);
    if (n == 0 || n >= PATH_BUF) return 0;
    normalize_wpath(wq_buf);

    for (int i = 0; i < g_opts.nscan; i++) {
        wchar_t *ws = wide_from_utf8(g_opts.scan_paths[i]);
        if (!ws) continue;
        wchar_t ws_buf[PATH_BUF];
        DWORD m = GetFullPathNameW(ws, PATH_BUF, ws_buf, NULL);
        free(ws);
        if (m == 0 || m >= PATH_BUF) continue;
        normalize_wpath(ws_buf);

        size_t slen = wcslen(ws_buf);
        size_t qlen = wcslen(wq_buf);
        /* qdir starts with scan_root\ or equals scan_root */
        if (qlen >= slen && _wcsnicmp(wq_buf, ws_buf, slen) == 0) {
            if (qlen == slen || wq_buf[slen] == L'\\')
                return 1;
        }
    }
    return 0;
}

/* ── Quarantine: move non-kept duplicates ────────────────────── */

static int do_quarantine(const char *qdir) {
    /* ── dry-run: just print what would happen ── */
    if (g_opts.dry_run) {
        int count = 0;
        printf("[DRY RUN]  Quarantine plan (no files will be moved):\n");
        for (size_t i = 0; i < g_dupsets.count; i++) {
            DupSet *ds = &g_dupsets.items[i];
            for (size_t k = 0; k < ds->count; k++) {
                if (k == ds->kept_idx) continue;
                File       *f     = ds->files[k];
                const char *fname = strrchr(f->path, '\\');
                if (!fname) fname = strrchr(f->path, '/');
                fname = fname ? fname + 1 : f->path;
                char dst[PATH_BUF];
                snprintf(dst, sizeof(dst), "%s\\%.8s_%s", qdir, ds->hash, fname);
                printf("  WOULD MOVE  %s\n             -> %s\n", f->path, dst);
                count++;
            }
        }
        printf("Dry run: %d file(s) would be moved.\n", count);
        return 0;
    }

    /* ── live path ── */
    wchar_t *wq = wide_from_utf8(qdir);
    if (wq) { CreateDirectoryW(wq, NULL); free(wq); }

    char logpath[PATH_BUF];
    snprintf(logpath, sizeof(logpath), "%s\\wlint_moves.json", qdir);
    FILE *log = fopen(logpath, "w");
    int   moved = 0, failed = 0, first = 1;

    if (log) {
        SYSTEMTIME st;
        GetSystemTime(&st);
        char ts[32];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        char esc[PATH_BUF * 2];
        fprintf(log, "{\n");
        fprintf(log, "  \"schema_version\": \"%s\",\n", SCHEMA_VERSION);
        fprintf(log, "  \"wlint_version\": \"%s\",\n",  WLINT_VERSION);
        fprintf(log, "  \"quarantine_date\": \"%s\",\n", ts);
        fprintf(log, "  \"scan_paths\": [");
        for (int i = 0; i < g_opts.nscan; i++) {
            json_esc(g_opts.scan_paths[i], esc, sizeof(esc));
            fprintf(log, "%s\"%s\"", i ? ", " : "", esc);
        }
        fprintf(log, "],\n");
        json_esc(qdir, esc, sizeof(esc));
        fprintf(log, "  \"quarantine_dir\": \"%s\",\n", esc);
        fprintf(log, "  \"dry_run\": false,\n");
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
                    snprintf(dst, sizeof(dst), "%s\\%.8s_%s",    qdir, ds->hash, fname);
                else
                    snprintf(dst, sizeof(dst), "%s\\%.8s_%s_%d", qdir, ds->hash, fname, tries);
                tries++;
                wchar_t *wdst = wide_from_utf8(dst);
                int occupied  = wdst && (GetFileAttributesW(wdst) != INVALID_FILE_ATTRIBUTES);
                free(wdst);
                if (!occupied) break;
            } while (tries < 1000);

            wchar_t *wsrc = wide_from_utf8(f->path);
            wchar_t *wdst = wide_from_utf8(dst);

            if (wsrc && wdst && MoveFileW(wsrc, wdst)) {
                moved++;
                if (log) {
                    char esrc[PATH_BUF * 2], edst[PATH_BUF * 2];
                    char mt[32];
                    json_esc(f->path, esrc, sizeof(esrc));
                    json_esc(dst,     edst, sizeof(edst));
                    fmt_mtime(f->mtime, mt, sizeof(mt));
                    /* moved_at timestamp */
                    SYSTEMTIME now;
                    GetSystemTime(&now);
                    char moved_at[32];
                    snprintf(moved_at, sizeof(moved_at),
                             "%04d-%02d-%02dT%02d:%02d:%02dZ",
                             now.wYear, now.wMonth, now.wDay,
                             now.wHour, now.wMinute, now.wSecond);
                    fprintf(log, "%s    {\n", first ? "" : ",\n");
                    fprintf(log, "      \"original_path\":   \"%s\",\n", esrc);
                    fprintf(log, "      \"quarantine_path\": \"%s\",\n", edst);
                    fprintf(log, "      \"sha256\": \"%s\",\n",          ds->hash);
                    fprintf(log, "      \"size\": %lld,\n",              (long long)ds->size);
                    fprintf(log, "      \"mtime\": \"%s\",\n",           mt);
                    fprintf(log, "      \"moved_at\": \"%s\"\n",         moved_at);
                    fprintf(log, "    }");
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

/* ── Undo: restore quarantined files from manifest ───────────── */

static int do_undo(const char *manifest_path) {
    FILE *fp = fopen(manifest_path, "rb");
    if (!fp) {
        fprintf(stderr, "wlint: cannot open manifest: %s\n", manifest_path);
        return 1;
    }

    /* read entire manifest into heap (refuse > 10 MB) */
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    rewind(fp);
    if (fsz > 10 * 1024 * 1024) {
        fprintf(stderr, "wlint: manifest too large (>10 MB)\n");
        fclose(fp); return 1;
    }
    char *buf = (char *)malloc((size_t)fsz + 1);
    if (!buf) { fclose(fp); return 1; }
    fread(buf, 1, (size_t)fsz, fp);
    buf[fsz] = '\0';
    fclose(fp);

    /* display version from manifest */
    char ver[64] = "(unknown)";
    json_str_val(buf, "wlint_version", ver, sizeof(ver));
    printf("Restoring from manifest (created by wlint %s)\n", ver);

    /* locate "moves" array */
    const char *moves_start = strstr(buf, "\"moves\"");
    if (!moves_start) {
        fprintf(stderr, "wlint: manifest has no 'moves' key\n");
        free(buf); return 1;
    }
    /* skip past "moves": [ */
    moves_start = strchr(moves_start, '[');
    if (!moves_start) { free(buf); return 1; }
    moves_start++;  /* past '[' */

    int restored = 0, failed = 0, skipped = 0;
    size_t pos = (size_t)(moves_start - buf);
    char block[PATH_BUF * 8];

    while (next_json_obj(buf, (size_t)fsz, &pos, block, sizeof(block))) {
        char orig[PATH_BUF]     = "";
        char qpath[PATH_BUF]    = "";
        char sha256[SHA256_HEX] = "";
        int64_t size_val        = 0;

        json_str_val(block,   "original_path",   orig,   sizeof(orig));
        json_str_val(block,   "quarantine_path",  qpath,  sizeof(qpath));
        json_str_val(block,   "sha256",           sha256, sizeof(sha256));
        json_int64_val(block, "size",             &size_val);

        if (!orig[0] || !qpath[0]) continue;

        /* a) quarantine file missing */
        wchar_t *wq = wide_from_utf8(qpath);
        int q_exists = wq && (GetFileAttributesW(wq) != INVALID_FILE_ATTRIBUTES);
        free(wq);
        if (!q_exists) {
            fprintf(stderr, "wlint: undo: quarantine file missing (skipped): %s\n", qpath);
            skipped++;
            continue;
        }

        /* b) hash mismatch */
        if (sha256[0]) {
            char actual[SHA256_HEX] = "";
            if (sha256_file(qpath, actual, 0) == 0 && strcmp(actual, sha256) != 0) {
                fprintf(stderr, "wlint: undo: hash mismatch (failed): %s\n", qpath);
                failed++;
                continue;
            }
        }

        /* c) original path already exists */
        wchar_t *wo = wide_from_utf8(orig);
        int o_exists = wo && (GetFileAttributesW(wo) != INVALID_FILE_ATTRIBUTES);
        if (o_exists) {
            fprintf(stderr, "wlint: undo: destination already exists (failed): %s\n", orig);
            free(wo);
            failed++;
            continue;
        }

        /* d) move quarantine -> original */
        wchar_t *wq2 = wide_from_utf8(qpath);
        if (wo && wq2 && MoveFileW(wq2, wo)) {
            printf("  RESTORED  %s\n", orig);
            restored++;
        } else {
            fprintf(stderr, "wlint: undo: cannot restore (error %lu): %s\n",
                    GetLastError(), orig);
            failed++;
        }
        free(wo);
        free(wq2);
    }

    printf("Undo summary: %d restored, %d failed, %d skipped\n",
           restored, failed, skipped);
    free(buf);
    return failed > 0 ? 1 : 0;
}

/* ── Help / version ──────────────────────────────────────────── */

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] <path> [path ...]\n\n"
        "Find duplicate files and filesystem lint.\n\n"
        "Options:\n"
        "  -e  --empty           report empty files and directories\n"
        "  -t  --temp            report temp/junk files (.tmp .bak .swp ~$ etc.)\n"
        "  -V  --verify          byte-for-byte verify after SHA-256 match\n"
        "  -k  --keep POLICY     newest|oldest|first  (default: newest)\n"
        "      --min-size BYTES  skip files smaller than BYTES (default: 1)\n"
        "      --json FILE       write JSON report to FILE\n"
        "      --csv  FILE       write CSV  report to FILE\n"
        "      --scan-json FILE  write full file inventory JSON for wsim\n"
        "      --log FILE        write operational log JSON to FILE\n"
        "      --quarantine DIR  move non-kept duplicates to DIR\n"
        "      --dry-run         show quarantine plan without moving files\n"
        "      --undo MANIFEST   restore quarantined files from move manifest\n"
        "      --include PAT     include only files matching glob (repeatable)\n"
        "      --exclude PAT     exclude files matching glob (repeatable)\n"
        "      --max-size BYTES  skip files larger than BYTES\n"
        "      --ext .EXT,...    include only these extensions (e.g. .jpg,.pdf)\n"
        "      --threads N       parallel hash threads (default: 2, max: 64)\n"
        "      --age N           flag files not modified in N or more days\n"
        "      --stats           show performance statistics\n"
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

    g_start_ms = GetTickCount64();
    memset(&g_opts, 0, sizeof(g_opts));
    g_opts.keep     = KEEP_NEWEST;
    g_opts.min_size = 1;
    g_opts.threads  = 2;
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
        else if (!strcmp(a, "-t") || !strcmp(a, "--temp"))    { g_opts.do_temp   = 1; }
        else if (!strcmp(a, "-V") || !strcmp(a, "--verify"))  { g_opts.do_verify = 1; }
        else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) { g_opts.verbose   = 1; }
        else if (!strcmp(a, "--no-color"))                    { g_color           = 0; }
        else if (!strcmp(a, "--dry-run"))                     { g_opts.dry_run    = 1; }
        else if (!strcmp(a, "--undo") && i + 1 < argc)        { g_opts.undo_manifest = argv[++i]; }
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
        else if (!strcmp(a, "--json") && i + 1 < argc)       { g_opts.json_out   = argv[++i]; }
        else if (!strcmp(a, "--csv")  && i + 1 < argc)       { g_opts.csv_out    = argv[++i]; }
        else if (!strcmp(a, "--scan-json") && i + 1 < argc) { g_opts.scan_json_out = argv[++i]; }
        else if (!strcmp(a, "--log")       && i + 1 < argc) { g_opts.log_out        = argv[++i]; }
        else if (!strcmp(a, "--quarantine") && i + 1 < argc)  { g_opts.quarantine = argv[++i]; }
        else if (!strcmp(a, "--include") && i + 1 < argc)     { push_include(argv[++i]); }
        else if (!strcmp(a, "--exclude") && i + 1 < argc)     { push_exclude(argv[++i]); }
        else if (!strcmp(a, "--max-size") && i + 1 < argc)    { g_opts.max_size = (int64_t)atoll(argv[++i]); }
        else if (!strcmp(a, "--ext") && i + 1 < argc)         { parse_ext(argv[++i]); }
        else if (!strcmp(a, "--threads") && i + 1 < argc) {
            g_opts.threads = atoi(argv[++i]);
            if (g_opts.threads < 1)  g_opts.threads = 1;
            if (g_opts.threads > 64) g_opts.threads = 64;
        }
        else if (!strcmp(a, "--stats"))                        { g_opts.do_stats = 1; }
        else if (!strcmp(a, "--age") && i + 1 < argc) {
            g_opts.age_days = atoi(argv[++i]);
            if (g_opts.age_days < 1) g_opts.age_days = 1;
        }
        else if (a[0] == '-') {
            fprintf(stderr, "wlint: unknown option: %s\n", a);
            usage(argv[0]); free(paths); return 2;
        }
        else {
            paths[npaths++] = a;
        }
    }

    /* undo mode: no scan paths required */
    if (g_opts.undo_manifest) {
        if (bcrypt_init() != 0) {
            fprintf(stderr, "wlint: failed to initialize SHA-256 (BCrypt)\n");
            free(paths); return 2;
        }
        ret = do_undo(g_opts.undo_manifest);
        bcrypt_fini();
        free(paths);
        return ret;
    }

    if (npaths == 0) {
        fprintf(stderr, "wlint: no paths specified\n");
        usage(argv[0]); free(paths); return 2;
    }

    g_opts.scan_paths = paths;
    g_opts.nscan      = npaths;

    /* subtree guard: refuse quarantine inside scan tree */
    if (g_opts.quarantine && qdir_inside_scan(g_opts.quarantine)) {
        fprintf(stderr,
                "wlint: error: quarantine dir '%s' is inside a scan root\n"
                "  This would corrupt the next run. Use a path outside the scan tree.\n",
                g_opts.quarantine);
        free(paths); return 2;
    }

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

    if (g_opts.scan_json_out) {
        FILE *fp = fopen(g_opts.scan_json_out, "w");
        if (!fp) {
            fprintf(stderr, "wlint: cannot write: %s\n", g_opts.scan_json_out);
            ret = 2;
        } else {
            output_scan_json(fp);
            fclose(fp);
            if (g_opts.verbose)
                fprintf(stderr, "Scan JSON: %s\n", g_opts.scan_json_out);
        }
    }

    find_duplicates();
    sort_dupsets();
    g_elapsed_ms = GetTickCount64() - g_start_ms;

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

    if (g_opts.log_out) {
        FILE *fp = fopen(g_opts.log_out, "w");
        if (!fp) {
            fprintf(stderr, "wlint: cannot write: %s\n", g_opts.log_out);
            ret = 2;
        } else {
            output_log(fp);
            fclose(fp);
            if (g_opts.verbose)
                fprintf(stderr, "Log: %s\n", g_opts.log_out);
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
    fvec_free(&g_temp_files);
    fvec_free(&g_stale_files);
    for (size_t i = 0; i < g_dupsets.count; i++) free(g_dupsets.items[i].files);
    free(g_dupsets.items);
    for (int i = 0; i < g_opts.ninclude; i++) free(g_opts.include_pats[i]);
    free(g_opts.include_pats);
    for (int i = 0; i < g_opts.nexclude; i++) free(g_opts.exclude_pats[i]);
    free(g_opts.exclude_pats);
    free(paths);

    return ret;
}
