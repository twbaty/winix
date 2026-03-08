/*
 * gzip — compress or expand files (RFC 1951/1952, zlib backend)
 *
 * Usage: gzip [OPTIONS] [FILE ...]
 *        gunzip [OPTIONS] [FILE ...]
 *
 *   -d          decompress (default when called as gunzip)
 *   -k          keep original file (default: remove after success)
 *   -c          write to stdout, keep original
 *   -f          force overwrite of existing output file
 *   -v          verbose (show filename and ratio)
 *   -l          list compressed file info
 *   -t          test integrity
 *   -1 .. -9    compression level (default -6)
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error, 2 = warning
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "zlib.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#define VERSION     "1.0"
#define CHUNK       65536
#define GZ_EXT      ".gz"

/* ── options ─────────────────────────────────────────────── */
static int opt_decompress = 0;
static int opt_keep       = 0;
static int opt_stdout     = 0;
static int opt_force      = 0;
static int opt_verbose    = 0;
static int opt_list       = 0;
static int opt_test       = 0;
static int opt_level      = Z_DEFAULT_COMPRESSION; /* -6 */

/* ── helpers ─────────────────────────────────────────────── */
static int has_gz_suffix(const char *path) {
    size_t n = strlen(path);
    return n > 3 && strcmp(path + n - 3, GZ_EXT) == 0;
}

/* Build output path: compress → append .gz, decompress → strip .gz */
static int build_outpath(const char *in, char *out, size_t outsz) {
    if (opt_decompress) {
        if (!has_gz_suffix(in)) {
            fprintf(stderr, "gzip: %s: unknown suffix -- ignored\n", in);
            return -1;
        }
        size_t n = strlen(in);
        if (n - 3 >= outsz) { fprintf(stderr, "gzip: path too long\n"); return -1; }
        memcpy(out, in, n - 3);
        out[n - 3] = '\0';
    } else {
        if (strlen(in) + 3 >= outsz) { fprintf(stderr, "gzip: path too long\n"); return -1; }
        snprintf(out, outsz, "%s%s", in, GZ_EXT);
    }
    return 0;
}

/* ── compress one file ───────────────────────────────────── */
static int do_compress(const char *inpath) {
    char outpath[4096];
    const char *dest;

    if (!opt_stdout) {
        if (build_outpath(inpath, outpath, sizeof(outpath)) != 0) return 1;
        dest = outpath;
        if (!opt_force) {
            struct stat st;
            if (stat(dest, &st) == 0) {
                fprintf(stderr, "gzip: %s already exists; not overwritten\n", dest);
                return 1;
            }
        }
    }

    FILE *in = fopen(inpath, "rb");
    if (!in) { fprintf(stderr, "gzip: %s: %s\n", inpath, strerror(errno)); return 1; }

    gzFile gz;
    if (opt_stdout) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        char mode[8];
        snprintf(mode, sizeof(mode), "wb%d", opt_level == Z_DEFAULT_COMPRESSION ? 6 : opt_level);
        gz = gzdopen(_dup(_fileno(stdout)), mode);
    } else {
        char mode[8];
        snprintf(mode, sizeof(mode), "wb%d", opt_level == Z_DEFAULT_COMPRESSION ? 6 : opt_level);
        gz = gzopen(dest, mode);
    }
    if (!gz) {
        fprintf(stderr, "gzip: cannot open output\n");
        fclose(in);
        return 1;
    }

    unsigned char buf[CHUNK];
    size_t n;
    long long in_bytes = 0, out_bytes = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (gzwrite(gz, buf, (unsigned)n) != (int)n) {
            fprintf(stderr, "gzip: write error: %s\n", gzerror(gz, NULL));
            gzclose(gz); fclose(in); return 1;
        }
        in_bytes += (long long)n;
    }
    gzclose(gz);
    fclose(in);

    if (opt_verbose && !opt_stdout) {
        struct stat st;
        if (stat(dest, &st) == 0) out_bytes = st.st_size;
        double ratio = in_bytes > 0 ? 100.0 * (in_bytes - out_bytes) / in_bytes : 0.0;
        fprintf(stderr, "%s:\t%.1f%% -- replaced with %s\n", inpath, ratio, dest);
    }

    if (!opt_stdout && !opt_keep)
        remove(inpath);

    return 0;
}

/* ── decompress one file ─────────────────────────────────── */
static int do_decompress(const char *inpath) {
    if (!has_gz_suffix(inpath) && !opt_force) {
        fprintf(stderr, "gzip: %s: unknown suffix -- ignored\n", inpath);
        return 1;
    }

    char outpath[4096];
    FILE *out = NULL;

    if (opt_stdout || opt_test) {
#ifdef _WIN32
        if (opt_stdout) _setmode(_fileno(stdout), _O_BINARY);
#endif
        out = opt_test ? NULL : stdout;
    } else {
        if (build_outpath(inpath, outpath, sizeof(outpath)) != 0) return 1;
        if (!opt_force) {
            struct stat st;
            if (stat(outpath, &st) == 0) {
                fprintf(stderr, "gzip: %s already exists; not overwritten\n", outpath);
                return 1;
            }
        }
        out = fopen(outpath, "wb");
        if (!out) {
            fprintf(stderr, "gzip: %s: %s\n", outpath, strerror(errno));
            return 1;
        }
    }

    gzFile gz = gzopen(inpath, "rb");
    if (!gz) {
        fprintf(stderr, "gzip: %s: %s\n", inpath, strerror(errno));
        if (out && out != stdout) fclose(out);
        return 1;
    }

    unsigned char buf[CHUNK];
    int n;
    long long out_bytes = 0;
    int err = 0;
    while ((n = gzread(gz, buf, sizeof(buf))) > 0) {
        if (out && fwrite(buf, 1, (size_t)n, out) != (size_t)n) {
            fprintf(stderr, "gzip: write error: %s\n", strerror(errno));
            err = 1; break;
        }
        out_bytes += n;
    }
    if (n < 0) {
        fprintf(stderr, "gzip: %s: %s\n", inpath, gzerror(gz, NULL));
        err = 1;
    }
    gzclose(gz);
    if (out && out != stdout) fclose(out);

    if (err) {
        if (!opt_stdout && !opt_test) remove(outpath);
        return 1;
    }

    if (opt_verbose && !opt_stdout && !opt_test) {
        struct stat st;
        long long in_bytes = 0;
        if (stat(inpath, &st) == 0) in_bytes = st.st_size;
        double ratio = in_bytes > 0 ? 100.0 * (in_bytes - out_bytes) / in_bytes : 0.0;
        fprintf(stderr, "%s:\t%.1f%% -- replaced with %s\n", inpath, ratio, outpath);
    }

    if (opt_test) {
        if (opt_verbose) fprintf(stderr, "%s:\tOK\n", inpath);
        return 0;
    }

    if (!opt_stdout && !opt_keep)
        remove(inpath);

    return 0;
}

/* ── list mode ───────────────────────────────────────────── */
static int do_list(const char *inpath) {
    gzFile gz = gzopen(inpath, "rb");
    if (!gz) { fprintf(stderr, "gzip: %s: %s\n", inpath, strerror(errno)); return 1; }
    unsigned char buf[CHUNK];
    long long uncompressed = 0;
    int n;
    while ((n = gzread(gz, buf, sizeof(buf))) > 0) uncompressed += n;
    gzclose(gz);
    struct stat st;
    long long compressed = 0;
    if (stat(inpath, &st) == 0) compressed = st.st_size;
    double ratio = compressed > 0 ? 100.0 * (compressed - uncompressed) / uncompressed : 0.0;
    printf("%10lld %10lld %6.1f%% %s\n", compressed, uncompressed, -ratio, inpath);
    return 0;
}

/* ── stdin mode ──────────────────────────────────────────── */
static int do_stdin(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (opt_decompress) {
        gzFile gz = gzdopen(_dup(_fileno(stdin)), "rb");
        if (!gz) { fprintf(stderr, "gzip: cannot read stdin\n"); return 1; }
        unsigned char buf[CHUNK]; int n;
        while ((n = gzread(gz, buf, sizeof(buf))) > 0)
            fwrite(buf, 1, (size_t)n, stdout);
        gzclose(gz);
    } else {
        char mode[8];
        snprintf(mode, sizeof(mode), "wb%d", opt_level == Z_DEFAULT_COMPRESSION ? 6 : opt_level);
        gzFile gz = gzdopen(_dup(_fileno(stdout)), mode);
        if (!gz) { fprintf(stderr, "gzip: cannot write stdout\n"); return 1; }
        unsigned char buf[CHUNK]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0)
            gzwrite(gz, buf, (unsigned)n);
        gzclose(gz);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    /* Detect if called as gunzip */
    const char *prog = argv[0];
    const char *base = strrchr(prog, '/');
    if (!base) base = strrchr(prog, '\\');
    if (!base) base = prog; else base++;
    if (strncmp(base, "gunzip", 6) == 0) opt_decompress = 1;

    int argi = 1;
    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) {
            printf("gzip %s (Winix, zlib %s)\n", VERSION, ZLIB_VERSION);
            return 0;
        }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: gzip [OPTIONS] [FILE ...]\n\n"
                "  -d        decompress\n"
                "  -k        keep original file\n"
                "  -c        write to stdout\n"
                "  -f        force overwrite\n"
                "  -v        verbose\n"
                "  -l        list compressed file info\n"
                "  -t        test integrity\n"
                "  -1..-9    compression level\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 'd': opt_decompress = 1; break;
                case 'k': opt_keep       = 1; break;
                case 'c': opt_stdout     = 1; break;
                case 'f': opt_force      = 1; break;
                case 'v': opt_verbose    = 1; break;
                case 'l': opt_list       = 1; break;
                case 't': opt_test       = 1; opt_decompress = 1; break;
                case '1': case '2': case '3': case '4': case '5':
                case '6': case '7': case '8': case '9':
                    opt_level = *p - '0'; break;
                default:
                    fprintf(stderr, "gzip: invalid option -- '%c'\n", *p);
                    return 1;
            }
        }
    }

    /* No files — operate on stdin/stdout */
    if (argi >= argc) return do_stdin();

    if (opt_list)
        printf("%10s %10s  ratio  name\n", "compressed", "uncompressed");

    int ret = 0;
    for (int i = argi; i < argc; i++) {
        if (opt_list)        ret |= do_list(argv[i]);
        else if (opt_decompress) ret |= do_decompress(argv[i]);
        else                 ret |= do_compress(argv[i]);
    }
    return ret;
}
