/*
 * dd — convert and copy a file
 *
 * Usage: dd [OPERAND ...]
 *   if=FILE    read from FILE (default stdin)
 *   of=FILE    write to FILE (default stdout)
 *   bs=N       read/write N bytes at a time (overrides ibs/obs)
 *   ibs=N      read N bytes at a time (default 512)
 *   obs=N      write N bytes at a time (default 512)
 *   count=N    copy only N input blocks
 *   skip=N     skip N input blocks before copying
 *   seek=N     skip N output blocks before writing
 *   conv=CONV  convert: nocreat,notrunc,noerror,sync,lcase,ucase,swab
 *   status=none|noxfer|progress  control output
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>

#define VERSION "1.0"

static const char *g_if     = NULL;
static const char *g_of     = NULL;
static size_t  g_ibs        = 512;
static size_t  g_obs        = 512;
static int64_t g_count      = -1;  /* -1 = unlimited */
static int64_t g_skip       = 0;
static int64_t g_seek       = 0;

/* conv flags */
static int g_nocreat  = 0;
static int g_notrunc  = 0;
static int g_noerror  = 0;
static int g_sync_pad = 0;
static int g_lcase    = 0;
static int g_ucase    = 0;
static int g_swab     = 0;

/* status flags */
#define STATUS_DEFAULT  0
#define STATUS_NONE     1
#define STATUS_NOXFER   2
#define STATUS_PROGRESS 3
static int g_status = STATUS_DEFAULT;

/* ── Size suffix parser ──────────────────────────────────────── */

static int64_t parse_size(const char *s) {
    char *end;
    int64_t n = strtoll(s, &end, 10);
    if (*end) {
        switch (toupper((unsigned char)*end)) {
            case 'K': n *=       1024LL; break;
            case 'M': n *=    1048576LL; break;
            case 'G': n *= 1073741824LL; break;
            case 'T': n *= 1099511627776LL; break;
            case 'B': /* block = 512 */ n *= 512; break;
            default: break;
        }
    }
    return n;
}

/* ── Main ────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    size_t bs = 0; /* if set, overrides ibs/obs */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("dd %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[i], "--help")) {
            fprintf(stderr,
                "usage: dd [OPERAND ...]\n\n"
                "Convert and copy a file.\n\n"
                "  if=FILE    input file (default stdin)\n"
                "  of=FILE    output file (default stdout)\n"
                "  bs=N       block size (overrides ibs/obs)\n"
                "  ibs=N      input block size (default 512)\n"
                "  obs=N      output block size (default 512)\n"
                "  count=N    copy only N input blocks\n"
                "  skip=N     skip N ibs-blocks in input\n"
                "  seek=N     skip N obs-blocks in output\n"
                "  conv=LIST  nocreat,notrunc,noerror,sync,lcase,ucase,swab\n"
                "  status=none|noxfer|progress\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        /* Parse key=value operands */
        char *eq = strchr(argv[i], '=');
        if (!eq) { fprintf(stderr, "dd: invalid operand '%s'\n", argv[i]); return 1; }
        char key[64]; int klen = (int)(eq - argv[i]);
        if (klen >= (int)sizeof(key)) klen = (int)sizeof(key) - 1;
        strncpy(key, argv[i], (size_t)klen); key[klen] = '\0';
        const char *val = eq + 1;

        if      (!strcmp(key, "if"))     g_if = val;
        else if (!strcmp(key, "of"))     g_of = val;
        else if (!strcmp(key, "bs"))     bs = (size_t)parse_size(val);
        else if (!strcmp(key, "ibs"))    g_ibs = (size_t)parse_size(val);
        else if (!strcmp(key, "obs"))    g_obs = (size_t)parse_size(val);
        else if (!strcmp(key, "count"))  g_count = parse_size(val);
        else if (!strcmp(key, "skip"))   g_skip  = parse_size(val);
        else if (!strcmp(key, "seek"))   g_seek  = parse_size(val);
        else if (!strcmp(key, "status")) {
            if (!strcmp(val, "none"))     g_status = STATUS_NONE;
            else if (!strcmp(val, "noxfer")) g_status = STATUS_NOXFER;
            else if (!strcmp(val, "progress")) g_status = STATUS_PROGRESS;
        }
        else if (!strcmp(key, "conv")) {
            char cvbuf[256]; strncpy(cvbuf, val, sizeof(cvbuf)-1); cvbuf[sizeof(cvbuf)-1]='\0';
            char *tok = strtok(cvbuf, ",");
            while (tok) {
                if      (!strcmp(tok, "nocreat")) g_nocreat  = 1;
                else if (!strcmp(tok, "notrunc")) g_notrunc  = 1;
                else if (!strcmp(tok, "noerror")) g_noerror  = 1;
                else if (!strcmp(tok, "sync"))    g_sync_pad = 1;
                else if (!strcmp(tok, "lcase"))   g_lcase    = 1;
                else if (!strcmp(tok, "ucase"))   g_ucase    = 1;
                else if (!strcmp(tok, "swab"))    g_swab     = 1;
                else { fprintf(stderr, "dd: invalid conversion '%s'\n", tok); return 1; }
                tok = strtok(NULL, ",");
            }
        }
        else { fprintf(stderr, "dd: invalid operand '%s'\n", argv[i]); return 1; }
    }

    if (bs) { g_ibs = g_obs = bs; }
    if (g_ibs < 1) g_ibs = 512;
    if (g_obs < 1) g_obs = 512;

    /* Open input */
    FILE *fin = stdin;
    if (g_if) {
        fin = fopen(g_if, "rb");
        if (!fin) { perror(g_if); return 1; }
    } else {
        _setmode(_fileno(stdin), 0x8000); /* _O_BINARY */
    }

    /* Open output */
    FILE *fout = stdout;
    if (g_of) {
        const char *mode = "wb";
        if (g_nocreat) {
            /* Open existing only */
            fout = fopen(g_of, g_notrunc ? "r+b" : "wb");
        } else {
            fout = fopen(g_of, g_notrunc ? "r+b" : "wb");
            if (!fout && !g_nocreat) fout = fopen(g_of, "wb");
        }
        if (!fout) { perror(g_of); if (fin != stdin) fclose(fin); return 1; }
        (void)mode;
    } else {
        _setmode(_fileno(stdout), 0x8000);
    }

    /* Skip input blocks */
    if (g_skip > 0) {
        int64_t to_skip = g_skip * (int64_t)g_ibs;
        if (fseek(fin, (long)to_skip, SEEK_SET) != 0) {
            /* fseek failed (stdin) — read and discard */
            unsigned char *tmp = malloc(g_ibs);
            if (!tmp) { fprintf(stderr, "dd: out of memory\n"); exit(1); }
            for (int64_t s = 0; s < g_skip; s++) {
                size_t r = fread(tmp, 1, g_ibs, fin);
                if (r == 0) break;
            }
            free(tmp);
        }
    }

    /* Seek output */
    if (g_seek > 0 && fout != stdout) {
        int64_t to_seek = g_seek * (int64_t)g_obs;
        fseek(fout, (long)to_seek, SEEK_SET);
    }

    /* Allocate I/O buffer */
    unsigned char *ibuf = malloc(g_ibs);
    if (!ibuf) { fprintf(stderr, "dd: out of memory\n"); return 1; }

    int64_t blocks_in = 0, blocks_out = 0;
    int64_t bytes_in  = 0, bytes_out  = 0;
    int ret = 0;
    DWORD t0 = GetTickCount();

    while (g_count < 0 || blocks_in < g_count) {
        size_t n = fread(ibuf, 1, g_ibs, fin);
        if (n == 0) {
            if (ferror(fin) && !g_noerror) {
                fprintf(stderr, "dd: read error\n"); ret = 1;
            }
            break;
        }
        bytes_in += (int64_t)n;
        blocks_in++;

        /* conv transformations */
        if (g_lcase) for (size_t i = 0; i < n; i++) ibuf[i] = (unsigned char)tolower(ibuf[i]);
        if (g_ucase) for (size_t i = 0; i < n; i++) ibuf[i] = (unsigned char)toupper(ibuf[i]);
        if (g_swab) {
            for (size_t i = 0; i + 1 < n; i += 2) {
                unsigned char t = ibuf[i]; ibuf[i] = ibuf[i+1]; ibuf[i+1] = t;
            }
        }
        /* sync: pad partial block with NUL */
        if (g_sync_pad && n < g_ibs) memset(ibuf + n, 0, g_ibs - n);

        size_t wn = fwrite(ibuf, 1, n, fout);
        bytes_out += (int64_t)wn;
        blocks_out++;

        if (g_status == STATUS_PROGRESS) {
            fprintf(stderr, "\r%lld bytes copied", (long long)bytes_out);
            fflush(stderr);
        }
    }

    if (g_status == STATUS_PROGRESS) fprintf(stderr, "\n");

    free(ibuf);
    if (fin  != stdin)  fclose(fin);
    if (fout != stdout) fclose(fout);

    if (g_status != STATUS_NONE) {
        DWORD elapsed = GetTickCount() - t0;
        double secs = elapsed / 1000.0;
        fprintf(stderr,
            "%lld+0 records in\n"
            "%lld+0 records out\n"
            "%lld bytes (%s) copied",
            (long long)blocks_in, (long long)blocks_out, (long long)bytes_out,
            bytes_out >= 1073741824LL ? "GB" :
            bytes_out >= 1048576LL    ? "MB" :
            bytes_out >= 1024LL       ? "kB" : "B");
        if (secs > 0)
            fprintf(stderr, ", %.3f s, %.1f kB/s", secs, (bytes_out / 1024.0) / secs);
        fprintf(stderr, "\n");
    }

    return ret;
}
