/*
 * split — split a file into pieces
 *
 * Usage: split [OPTIONS] [FILE [PREFIX]]
 *   -b SIZE   split by bytes  (suffix: k/m/g)
 *   -l LINES  split by line count (default: 1000)
 *   -n N      split into exactly N equal-size pieces
 *   -d        use numeric suffixes (00, 01, ...) instead of alpha (aa, ab, ...)
 *   -a LEN    suffix length (default: 2)
 *   --verbose print a message for each output file
 *   --version / --help
 *
 * Output files are named PREFIX + suffix (default PREFIX = "x").
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "1.0"
#define BUFSZ   65536

static long long g_bytes  = 0;
static long long g_lines  = 1000;
static int       g_nchunk = 0;
static int       g_numeric = 0;
static int       g_suflen  = 2;
static int       g_verbose = 0;

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] [FILE [PREFIX]]\n\n"
        "Split FILE (or stdin) into pieces.\n\n"
        "  -b SIZE   split by byte count (k/m/g suffix ok)\n"
        "  -l LINES  split by line count (default: 1000)\n"
        "  -n N      split into N equal pieces\n"
        "  -d        numeric suffixes (00, 01, ...)\n"
        "  -a LEN    suffix length (default: 2)\n"
        "      --verbose  announce each output file\n"
        "      --version\n"
        "      --help\n",
        prog);
}

static long long parse_size(const char *s) {
    char *end;
    long long v = strtoll(s, &end, 10);
    if (*end == 'k' || *end == 'K') v *= 1024LL;
    else if (*end == 'm' || *end == 'M') v *= 1024LL * 1024;
    else if (*end == 'g' || *end == 'G') v *= 1024LL * 1024 * 1024;
    return v;
}

/* Build next suffix string into buf.  seq is 0-based. */
static int next_suffix(char *buf, int seq) {
    if (g_numeric) {
        /* numeric: 00, 01, ... */
        int cap = 1;
        for (int i = 0; i < g_suflen; i++) cap *= 10;
        if (seq >= cap) return 0;
        char fmt[16];
        snprintf(fmt, sizeof(fmt), "%%0%dd", g_suflen);
        snprintf(buf, (size_t)(g_suflen + 1), fmt, seq);
    } else {
        /* alpha: aa, ab, ..., az, ba, ... */
        long long cap = 1;
        for (int i = 0; i < g_suflen; i++) cap *= 26;
        if ((long long)seq >= cap) return 0;
        int rem = seq;
        for (int i = g_suflen - 1; i >= 0; i--) {
            buf[i] = (char)('a' + rem % 26);
            rem /= 26;
        }
        buf[g_suflen] = '\0';
    }
    return 1;
}

static FILE *open_out(const char *prefix, int seq) {
    char suf[64];
    if (!next_suffix(suf, seq)) {
        fprintf(stderr, "split: too many output files\n");
        return NULL;
    }
    char path[4096];
    snprintf(path, sizeof(path), "%s%s", prefix, suf);
    if (g_verbose) fprintf(stderr, "creating file '%s'\n", path);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); }
    return f;
}

static int split_by_bytes(FILE *in, const char *prefix) {
    unsigned char *buf = (unsigned char *)malloc(BUFSZ);
    if (!buf) { fprintf(stderr, "split: out of memory\n"); return 1; }

    int    seq   = 0;
    FILE  *out   = NULL;
    long long written = 0;
    int    ret   = 0;
    size_t n;

    while ((n = fread(buf, 1, BUFSZ, in)) > 0) {
        size_t i = 0;
        while (i < n) {
            if (!out || written >= g_bytes) {
                if (out) fclose(out);
                out = open_out(prefix, seq++);
                if (!out) { ret = 1; goto done; }
                written = 0;
            }
            size_t chunk = (size_t)(g_bytes - written);
            if (chunk > n - i) chunk = n - i;
            fwrite(buf + i, 1, chunk, out);
            written += (long long)chunk;
            i += chunk;
        }
    }
done:
    if (out) fclose(out);
    free(buf);
    return ret;
}

static int split_by_lines(FILE *in, const char *prefix) {
    char *buf = (char *)malloc(BUFSZ);
    if (!buf) { fprintf(stderr, "split: out of memory\n"); return 1; }

    int    seq   = 0;
    FILE  *out   = NULL;
    long long lcount = 0;
    int    ret   = 0;
    size_t n;

    while ((n = fread(buf, 1, BUFSZ, in)) > 0) {
        size_t i = 0;
        while (i < n) {
            if (!out) {
                out = open_out(prefix, seq++);
                if (!out) { ret = 1; goto done; }
                lcount = 0;
            }
            /* find next newline */
            size_t j = i;
            while (j < n && buf[j] != '\n') j++;
            if (j < n) j++; /* include the newline */
            fwrite(buf + i, 1, j - i, out);
            if (j > i && buf[j - 1] == '\n') {
                lcount++;
                if (lcount >= g_lines) { fclose(out); out = NULL; }
            }
            i = j;
        }
    }
done:
    if (out) fclose(out);
    free(buf);
    return ret;
}

static int split_by_chunks(FILE *in, const char *prefix) {
    /* Determine total size */
    long start = ftell(in);
    fseek(in, 0, SEEK_END);
    long total = ftell(in) - start;
    fseek(in, start, SEEK_SET);

    if (total <= 0) {
        /* Empty file — create empty first chunk */
        FILE *f = open_out(prefix, 0);
        if (f) fclose(f);
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc(BUFSZ);
    if (!buf) { fprintf(stderr, "split: out of memory\n"); return 1; }

    int ret = 0;
    for (int i = 0; i < g_nchunk; i++) {
        long long off_start = (long long)total * i / g_nchunk;
        long long off_end   = (long long)total * (i + 1) / g_nchunk;
        long long chunk_sz  = off_end - off_start;

        FILE *out = open_out(prefix, i);
        if (!out) { ret = 1; break; }

        long long remaining = chunk_sz;
        while (remaining > 0) {
            size_t want = BUFSZ < remaining ? BUFSZ : (size_t)remaining;
            size_t n = fread(buf, 1, want, in);
            if (n == 0) break;
            fwrite(buf, 1, n, out);
            remaining -= (long long)n;
        }
        fclose(out);
    }
    free(buf);
    return ret;
}

int main(int argc, char *argv[]) {
    int argi = 1;
    int mode = 0; /* 0=lines, 1=bytes, 2=chunks */

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("split %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help"))    { usage(argv[0]); return 0; }
        if (!strcmp(a, "--verbose")) { g_verbose = 1; continue; }
        if (!strcmp(a, "--"))        { argi++; break; }
        if (!strcmp(a, "-d"))        { g_numeric = 1; continue; }

        char flag = a[1];
        if (flag == 'b' || flag == 'l' || flag == 'n' || flag == 'a') {
            const char *val = a[2] ? a + 2 : argv[++argi];
            if (!val) { fprintf(stderr, "split: -%c requires argument\n", flag); return 1; }
            if      (flag == 'b') { g_bytes  = parse_size(val); mode = 1; }
            else if (flag == 'l') { g_lines  = atoll(val);      mode = 0; }
            else if (flag == 'n') { g_nchunk = atoi(val);       mode = 2; }
            else if (flag == 'a') { g_suflen = atoi(val); }
            continue;
        }

        fprintf(stderr, "split: invalid option -- '%s'\n", a);
        return 1;
    }

    const char *infile = NULL;
    const char *prefix = "x";

    if (argi < argc) infile = argv[argi++];
    if (argi < argc) prefix = argv[argi++];

    FILE *in = stdin;
    if (infile && strcmp(infile, "-") != 0) {
        in = fopen(infile, "rb");
        if (!in) { perror(infile); return 1; }
    }

    int ret;
    if      (mode == 1) ret = split_by_bytes(in, prefix);
    else if (mode == 2) ret = split_by_chunks(in, prefix);
    else                ret = split_by_lines(in, prefix);

    if (in != stdin) fclose(in);
    return ret;
}
