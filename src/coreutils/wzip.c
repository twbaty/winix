/*
 * wzip.c — Winix native compression tool (zstd backend)
 *
 * Usage: wzip   [OPTIONS] [FILE ...]
 *        wunzip [OPTIONS] [FILE ...]
 *
 *   -d / --decompress   decompress (default when called as wunzip)
 *   -k / --keep         keep original file (default: remove after success)
 *   -c / --stdout       write to stdout, keep original
 *   -f / --force        force overwrite of existing output file
 *   -v / --verbose      verbose (filename + ratio)
 *   -t / --test         test integrity (decompress to /dev/null)
 *   -r / --recursive    recurse into directories
 *   -1 .. -19           compression level (default 3)
 *   --version / --help
 *
 * .wz file format (all fields little-endian):
 *   [0..3]   magic   "WZ\x01\x00"
 *   [4..5]   version uint16  (currently 1)
 *   [6..9]   mtime   uint32  (Unix timestamp, 0 if unknown)
 *   [10]     fnlen   uint8   (original filename length, 0 = stdin)
 *   [11..]   fname   bytes   (original filename, no NUL)
 *   [..]     zstd frame(s)   (standard zstd format)
 *
 * Exit: 0 = success, 1 = error
 */

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#  include <direct.h>
#  define stat  _stat
#  ifndef S_ISREG
#    define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#  endif
#  ifndef S_ISDIR
#    define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#  endif
/* Recursive directory listing on Windows */
#  include <windows.h>
#endif

/* ── magic ───────────────────────────────────────────────── */
#define WZ_MAGIC0  'W'
#define WZ_MAGIC1  'Z'
#define WZ_MAGIC2  '\x01'
#define WZ_MAGIC3  '\x00'
#define WZ_VERSION  1
#define WZ_EXT      ".wz"
#define WZ_EXT_LEN  3

#define CHUNK  (1 << 17)  /* 128 KiB I/O buffer */

/* ── options ─────────────────────────────────────────────── */
static int opt_decompress = 0;
static int opt_keep       = 0;
static int opt_stdout     = 0;
static int opt_force      = 0;
static int opt_verbose    = 0;
static int opt_test       = 0;
static int opt_recursive  = 0;
static int opt_level      = 3;   /* zstd default fast level */

/* ── return codes ────────────────────────────────────────── */
static int g_rc = 0;  /* 0 = success, 1 = at least one error */

/* ── helpers ─────────────────────────────────────────────── */
static int has_wz_suffix(const char *path) {
    size_t n = strlen(path);
    return n > WZ_EXT_LEN &&
           strcmp(path + n - WZ_EXT_LEN, WZ_EXT) == 0;
}

static void write_le16(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)(v & 0xFF);
    buf[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void write_le32(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v & 0xFF);
    buf[1] = (uint8_t)((v >>  8) & 0xFF);
    buf[2] = (uint8_t)((v >> 16) & 0xFF);
    buf[3] = (uint8_t)((v >> 24) & 0xFF);
}
static uint16_t read_le16(const uint8_t *buf) {
    return (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}
static uint32_t read_le32(const uint8_t *buf) {
    return (uint32_t)(buf[0] | ((uint32_t)buf[1] << 8) |
                      ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24));
}

/* Extract just the base filename from a path */
static const char *basename_of(const char *path) {
    const char *p = path + strlen(path);
    while (p > path && p[-1] != '/' && p[-1] != '\\') --p;
    return p;
}

/* ── compress one FILE* → FILE* ──────────────────────────── */
static int compress_stream(FILE *fin, FILE *fout,
                            const char *orig_name, uint32_t mtime,
                            size_t *in_bytes, size_t *out_bytes)
{
    /* Write .wz header */
    uint8_t fnlen = orig_name ? (uint8_t)strlen(orig_name) : 0;
    /* cap at 255 */
    const char *fn = orig_name ? orig_name : "";
    if (fnlen > 255) { fnlen = 255; }

    uint8_t hdr[11];
    hdr[0] = WZ_MAGIC0; hdr[1] = WZ_MAGIC1;
    hdr[2] = WZ_MAGIC2; hdr[3] = WZ_MAGIC3;
    write_le16(hdr + 4, WZ_VERSION);
    write_le32(hdr + 6, mtime);
    hdr[10] = fnlen;
    if (fwrite(hdr, 1, 11, fout) != 11) return -1;
    if (fnlen && fwrite(fn, 1, fnlen, fout) != fnlen) return -1;

    *out_bytes = 11 + fnlen;

    /* Streaming compression */
    ZSTD_CStream *cstream = ZSTD_createCStream();
    if (!cstream) { fprintf(stderr, "wzip: out of memory\n"); return -1; }
    size_t r = ZSTD_initCStream(cstream, opt_level);
    if (ZSTD_isError(r)) {
        fprintf(stderr, "wzip: zstd init error: %s\n", ZSTD_getErrorName(r));
        ZSTD_freeCStream(cstream);
        return -1;
    }

    uint8_t *ibuf = malloc(CHUNK);
    uint8_t *obuf = malloc(ZSTD_CStreamOutSize());
    if (!ibuf || !obuf) {
        free(ibuf); free(obuf); ZSTD_freeCStream(cstream);
        fprintf(stderr, "wzip: out of memory\n");
        return -1;
    }
    size_t obufsz = ZSTD_CStreamOutSize();

    *in_bytes = 0;
    int rc = 0;
    size_t nr;
    while ((nr = fread(ibuf, 1, CHUNK, fin)) > 0) {
        *in_bytes += nr;
        ZSTD_inBuffer in_buf  = { ibuf, nr, 0 };
        while (in_buf.pos < in_buf.size) {
            ZSTD_outBuffer out_buf = { obuf, obufsz, 0 };
            r = ZSTD_compressStream(cstream, &out_buf, &in_buf);
            if (ZSTD_isError(r)) {
                fprintf(stderr, "wzip: compress error: %s\n", ZSTD_getErrorName(r));
                rc = -1; goto done;
            }
            if (out_buf.pos && fwrite(obuf, 1, out_buf.pos, fout) != out_buf.pos) {
                fprintf(stderr, "wzip: write error: %s\n", strerror(errno));
                rc = -1; goto done;
            }
            *out_bytes += out_buf.pos;
        }
    }
    if (ferror(fin)) {
        fprintf(stderr, "wzip: read error: %s\n", strerror(errno));
        rc = -1; goto done;
    }

    /* Flush / end frame */
    {
        ZSTD_outBuffer out_buf = { obuf, obufsz, 0 };
        r = ZSTD_endStream(cstream, &out_buf);
        if (ZSTD_isError(r)) {
            fprintf(stderr, "wzip: flush error: %s\n", ZSTD_getErrorName(r));
            rc = -1; goto done;
        }
        if (out_buf.pos && fwrite(obuf, 1, out_buf.pos, fout) != out_buf.pos) {
            fprintf(stderr, "wzip: write error: %s\n", strerror(errno));
            rc = -1; goto done;
        }
        *out_bytes += out_buf.pos;
    }

done:
    free(ibuf); free(obuf);
    ZSTD_freeCStream(cstream);
    return rc;
}

/* ── decompress one FILE* → FILE* ───────────────────────── */
static int decompress_stream(FILE *fin, FILE *fout,
                              char *orig_name_out, size_t name_out_sz,
                              uint32_t *mtime_out,
                              size_t *in_bytes, size_t *out_bytes)
{
    /* Read and validate .wz header */
    uint8_t hdr[11];
    if (fread(hdr, 1, 11, fin) != 11) {
        fprintf(stderr, "wunzip: not a .wz file (too short)\n");
        return -1;
    }
    if (hdr[0] != WZ_MAGIC0 || hdr[1] != WZ_MAGIC1 ||
        hdr[2] != WZ_MAGIC2 || hdr[3] != WZ_MAGIC3) {
        fprintf(stderr, "wunzip: not a .wz file (bad magic)\n");
        return -1;
    }
    uint16_t ver   = read_le16(hdr + 4);
    uint32_t mtime = read_le32(hdr + 6);
    uint8_t  fnlen = hdr[10];
    (void)ver;

    if (mtime_out) *mtime_out = mtime;

    char fname[256] = "";
    if (fnlen > 0) {
        if (fread(fname, 1, fnlen, fin) != fnlen) {
            fprintf(stderr, "wunzip: truncated header\n");
            return -1;
        }
        fname[fnlen] = '\0';
    }
    if (orig_name_out && name_out_sz > 0) {
        strncpy(orig_name_out, fname, name_out_sz - 1);
        orig_name_out[name_out_sz - 1] = '\0';
    }

    *in_bytes  = 11 + fnlen;
    *out_bytes = 0;

    ZSTD_DStream *dstream = ZSTD_createDStream();
    if (!dstream) { fprintf(stderr, "wunzip: out of memory\n"); return -1; }
    ZSTD_initDStream(dstream);

    uint8_t *ibuf = malloc(CHUNK);
    uint8_t *obuf = malloc(ZSTD_DStreamOutSize());
    if (!ibuf || !obuf) {
        free(ibuf); free(obuf); ZSTD_freeDStream(dstream);
        fprintf(stderr, "wunzip: out of memory\n");
        return -1;
    }
    size_t obufsz = ZSTD_DStreamOutSize();

    int rc = 0;
    size_t nr;
    while ((nr = fread(ibuf, 1, CHUNK, fin)) > 0) {
        *in_bytes += nr;
        ZSTD_inBuffer in_buf = { ibuf, nr, 0 };
        while (in_buf.pos < in_buf.size) {
            ZSTD_outBuffer out_buf = { obuf, obufsz, 0 };
            size_t r = ZSTD_decompressStream(dstream, &out_buf, &in_buf);
            if (ZSTD_isError(r)) {
                fprintf(stderr, "wunzip: decompress error: %s\n", ZSTD_getErrorName(r));
                rc = -1; goto done;
            }
            if (out_buf.pos) {
                if (fout && fwrite(obuf, 1, out_buf.pos, fout) != out_buf.pos) {
                    fprintf(stderr, "wunzip: write error: %s\n", strerror(errno));
                    rc = -1; goto done;
                }
                *out_bytes += out_buf.pos;
            }
        }
    }
    if (ferror(fin)) {
        fprintf(stderr, "wunzip: read error: %s\n", strerror(errno));
        rc = -1;
    }

done:
    free(ibuf); free(obuf);
    ZSTD_freeDStream(dstream);
    return rc;
}

/* ── compress a named file ───────────────────────────────── */
static void compress_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "wzip: %s: %s\n", path, strerror(errno));
        g_rc = 1; return;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!opt_recursive)
            fprintf(stderr, "wzip: %s: is a directory (use -r)\n", path);
        /* recursive case handled by caller */
        return;
    }

    /* Build output path */
    char outpath[4096];
    if (opt_stdout) {
        /* will write to stdout */
        outpath[0] = '\0';
    } else {
        if (snprintf(outpath, sizeof(outpath), "%s%s", path, WZ_EXT)
                >= (int)sizeof(outpath)) {
            fprintf(stderr, "wzip: %s: path too long\n", path);
            g_rc = 1; return;
        }
        if (!opt_force && access(outpath, 0) == 0) {
            fprintf(stderr, "wzip: %s: already exists\n", outpath);
            g_rc = 1; return;
        }
    }

    FILE *fin = fopen(path, "rb");
    if (!fin) {
        fprintf(stderr, "wzip: %s: %s\n", path, strerror(errno));
        g_rc = 1; return;
    }

    FILE *fout;
    if (opt_stdout) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        fout = stdout;
    } else {
        fout = fopen(outpath, "wb");
        if (!fout) {
            fprintf(stderr, "wzip: %s: %s\n", outpath, strerror(errno));
            fclose(fin); g_rc = 1; return;
        }
    }

    const char *bname = basename_of(path);
    uint32_t mtime = (uint32_t)st.st_mtime;
    size_t in_b = 0, out_b = 0;

    int r = compress_stream(fin, fout, bname, mtime, &in_b, &out_b);
    fclose(fin);
    if (!opt_stdout) fclose(fout);

    if (r != 0) {
        if (!opt_stdout) remove(outpath);
        g_rc = 1; return;
    }

    if (opt_verbose) {
        double pct = in_b > 0 ? (1.0 - (double)out_b / in_b) * 100.0 : 0.0;
        fprintf(stderr, "%s: %.1f%% (%.0f -> %.0f bytes)\n",
                path, pct, (double)in_b, (double)out_b);
    }

    if (!opt_keep && !opt_stdout)
        remove(path);
}

/* ── decompress a named file ─────────────────────────────── */
static void decompress_file(const char *path)
{
    if (!has_wz_suffix(path) && !opt_force) {
        fprintf(stderr, "wunzip: %s: unknown suffix -- ignored\n", path);
        g_rc = 1; return;
    }

    FILE *fin = fopen(path, "rb");
    if (!fin) {
        fprintf(stderr, "wunzip: %s: %s\n", path, strerror(errno));
        g_rc = 1; return;
    }

    /* Peek at header to get original filename */
    char orig_name[256] = "";
    uint32_t mtime = 0;
    size_t in_b = 0, out_b = 0;

    /* Determine output path before we start streaming */
    FILE *fout = NULL;
    char outpath[4096] = "";

    if (opt_test) {
        fout = NULL;  /* discard output */
    } else if (opt_stdout) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        fout = stdout;
    } else {
        /* We need to read the header first to get the original name.
         * Open a temp file or read header then seek back? Instead,
         * read header, build outpath, then stream rest. */
        /* peek-read the header to get orig_name */
        uint8_t hdr[11];
        if (fread(hdr, 1, 11, fin) != 11 ||
            hdr[0] != WZ_MAGIC0 || hdr[1] != WZ_MAGIC1 ||
            hdr[2] != WZ_MAGIC2 || hdr[3] != WZ_MAGIC3) {
            fprintf(stderr, "wunzip: %s: not a .wz file\n", path);
            fclose(fin); g_rc = 1; return;
        }
        uint8_t fnlen = hdr[10];
        mtime = read_le32(hdr + 6);
        if (fnlen > 0) {
            if (fread(orig_name, 1, fnlen, fin) != (size_t)fnlen) {
                fprintf(stderr, "wunzip: %s: truncated header\n", path);
                fclose(fin); g_rc = 1; return;
            }
            orig_name[fnlen] = '\0';
        }
        in_b = 11 + fnlen;

        /* Build output path from original name or strip .wz */
        if (orig_name[0]) {
            snprintf(outpath, sizeof(outpath), "%s", orig_name);
        } else {
            /* strip .wz */
            size_t n = strlen(path);
            if (n - WZ_EXT_LEN >= sizeof(outpath)) {
                fprintf(stderr, "wunzip: path too long\n");
                fclose(fin); g_rc = 1; return;
            }
            strncpy(outpath, path, n - WZ_EXT_LEN);
            outpath[n - WZ_EXT_LEN] = '\0';
        }

        if (!opt_force && access(outpath, 0) == 0) {
            fprintf(stderr, "wunzip: %s: already exists\n", outpath);
            fclose(fin); g_rc = 1; return;
        }
        fout = fopen(outpath, "wb");
        if (!fout) {
            fprintf(stderr, "wunzip: %s: %s\n", outpath, strerror(errno));
            fclose(fin); g_rc = 1; return;
        }

        /* Now decompress the remaining stream (header already consumed) */
        ZSTD_DStream *dstream = ZSTD_createDStream();
        if (!dstream) { fclose(fin); fclose(fout); g_rc = 1; return; }
        ZSTD_initDStream(dstream);

        uint8_t *ibuf = malloc(CHUNK);
        uint8_t *obuf = malloc(ZSTD_DStreamOutSize());
        if (!ibuf || !obuf) {
            free(ibuf); free(obuf);
            ZSTD_freeDStream(dstream);
            fclose(fin); fclose(fout);
            fprintf(stderr, "wunzip: out of memory\n");
            g_rc = 1; return;
        }
        size_t obufsz = ZSTD_DStreamOutSize();
        int rc_d = 0;
        size_t nr;
        while ((nr = fread(ibuf, 1, CHUNK, fin)) > 0) {
            in_b += nr;
            ZSTD_inBuffer ib = { ibuf, nr, 0 };
            while (ib.pos < ib.size) {
                ZSTD_outBuffer ob = { obuf, obufsz, 0 };
                size_t r2 = ZSTD_decompressStream(dstream, &ob, &ib);
                if (ZSTD_isError(r2)) {
                    fprintf(stderr, "wunzip: %s: decompress error: %s\n",
                            path, ZSTD_getErrorName(r2));
                    rc_d = -1; goto done_file;
                }
                if (ob.pos) {
                    if (fwrite(obuf, 1, ob.pos, fout) != ob.pos) {
                        fprintf(stderr, "wunzip: write error: %s\n", strerror(errno));
                        rc_d = -1; goto done_file;
                    }
                    out_b += ob.pos;
                }
            }
        }
        done_file:
        free(ibuf); free(obuf);
        ZSTD_freeDStream(dstream);
        fclose(fin);
        fclose(fout);

        if (rc_d != 0) { remove(outpath); g_rc = 1; return; }

        if (opt_verbose) {
            double pct = out_b > 0 ? (1.0 - (double)in_b / out_b) * 100.0 : 0.0;
            fprintf(stderr, "%s: %.1f%% (%.0f -> %.0f bytes)\n",
                    path, pct, (double)in_b, (double)out_b);
        }
        if (!opt_keep) remove(path);
        return;
    }

    /* test or stdout path: use decompress_stream which handles header internally */
    rewind(fin);
    int r = decompress_stream(fin, fout, orig_name, sizeof(orig_name),
                               &mtime, &in_b, &out_b);
    fclose(fin);

    if (r != 0) { g_rc = 1; return; }

    if (opt_verbose) {
        if (opt_test)
            fprintf(stderr, "%s: OK\n", path);
        else {
            double pct = out_b > 0 ? (1.0 - (double)in_b / out_b) * 100.0 : 0.0;
            fprintf(stderr, "%s: %.1f%% (%.0f -> %.0f bytes)\n",
                    path, pct, (double)in_b, (double)out_b);
        }
    }
    if (!opt_keep && !opt_stdout && !opt_test)
        remove(path);
}

/* ── stdin → stdout ──────────────────────────────────────── */
static void compress_stdin(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    size_t in_b = 0, out_b = 0;
    if (compress_stream(stdin, stdout, NULL, 0, &in_b, &out_b) != 0)
        g_rc = 1;
}

static void decompress_stdin(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    char orig_name[256];
    uint32_t mtime = 0;
    size_t in_b = 0, out_b = 0;
    if (decompress_stream(stdin, stdout, orig_name, sizeof(orig_name),
                           &mtime, &in_b, &out_b) != 0)
        g_rc = 1;
}

/* ── recursive directory walk ────────────────────────────── */
#ifdef _WIN32
static void walk_dir(const char *dir)
{
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char full[4096];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            walk_dir(full);
        } else {
            if (opt_decompress) {
                if (has_wz_suffix(fd.cFileName) || opt_force)
                    decompress_file(full);
            } else {
                if (!has_wz_suffix(fd.cFileName))
                    compress_file(full);
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#endif

/* ── usage / version ─────────────────────────────────────── */
static void print_usage(int decomp)
{
    if (decomp) {
        puts("Usage: wunzip [OPTION]... [FILE.wz ...]");
        puts("Decompress .wz files (zstd format).");
    } else {
        puts("Usage: wzip [OPTION]... [FILE ...]");
        puts("Compress files to .wz format (zstd backend).");
    }
    puts("");
    puts("  -d, --decompress    decompress");
    puts("  -k, --keep          keep original file");
    puts("  -c, --stdout        write to stdout");
    puts("  -f, --force         overwrite existing output");
    puts("  -v, --verbose       show filename and ratio");
    puts("  -t, --test          test integrity (decompress to null)");
    puts("  -r, --recursive     recurse into directories");
    puts("  -1 .. -19           compression level (default 3)");
    puts("  --help              show this help");
    puts("  --version           show version");
}

static void print_version(void)
{
    puts("wzip 1.0 (Winix, zstd 1.5.7)");
}

/* ── main ────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    /* argv[0] detection: called as wunzip → decompress mode */
    {
        const char *prog = argv[0];
        const char *base = prog + strlen(prog);
        while (base > prog && base[-1] != '/' && base[-1] != '\\') --base;
        if (strncmp(base, "wunzip", 6) == 0)
            opt_decompress = 1;
    }

    int file_start = argc; /* index where non-option args begin */
    int i;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help")       == 0) { print_usage(opt_decompress); return 0; }
        if (strcmp(a, "--version")    == 0) { print_version(); return 0; }
        if (strcmp(a, "--decompress") == 0) { opt_decompress = 1; continue; }
        if (strcmp(a, "--keep")       == 0) { opt_keep       = 1; continue; }
        if (strcmp(a, "--stdout")     == 0) { opt_stdout     = 1; continue; }
        if (strcmp(a, "--force")      == 0) { opt_force      = 1; continue; }
        if (strcmp(a, "--verbose")    == 0) { opt_verbose    = 1; continue; }
        if (strcmp(a, "--test")       == 0) { opt_test       = 1; continue; }
        if (strcmp(a, "--recursive")  == 0) { opt_recursive  = 1; continue; }
        if (strcmp(a, "--")           == 0) { file_start = i + 1; break; }

        if (a[0] == '-' && a[1] != '\0') {
            /* Check for numeric level: -1 .. -19 */
            if (a[1] >= '1' && a[1] <= '9') {
                char *end;
                long lvl = strtol(a + 1, &end, 10);
                if (*end == '\0' && lvl >= 1 && lvl <= 19) {
                    opt_level = (int)lvl;
                    continue;
                }
            }
            /* Combined short flags */
            const char *p = a + 1;
            int bad = 0;
            while (*p && !bad) {
                switch (*p) {
                    case 'd': opt_decompress = 1; break;
                    case 'k': opt_keep       = 1; break;
                    case 'c': opt_stdout     = 1; break;
                    case 'f': opt_force      = 1; break;
                    case 'v': opt_verbose    = 1; break;
                    case 't': opt_test       = 1; break;
                    case 'r': opt_recursive  = 1; break;
                    default:
                        fprintf(stderr, "wzip: invalid option -- '%c'\n", *p);
                        bad = 1; g_rc = 1;
                }
                p++;
            }
            if (bad) continue;
            continue;
        }

        /* First non-option arg */
        file_start = i;
        break;
    }

    if (g_rc) return g_rc;

    /* stdin / stdout pipe mode */
    if (file_start >= argc) {
        if (opt_decompress || opt_test)
            decompress_stdin();
        else
            compress_stdin();
        return g_rc;
    }

    /* Process each file argument */
    for (i = file_start; i < argc; i++) {
        const char *path = argv[i];
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
#ifdef _WIN32
            if (opt_recursive)
                walk_dir(path);
            else
#endif
                fprintf(stderr, "wzip: %s: is a directory (use -r)\n", path);
            continue;
        }
        if (opt_decompress || opt_test)
            decompress_file(path);
        else
            compress_file(path);
    }

    return g_rc;
}
