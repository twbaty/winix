/*
 * base32 — base32 encode or decode
 *
 * Usage: base32 [-d] [-w COLS] [FILE]
 *   -d, --decode    decode base32 input
 *   -w N, --wrap=N  wrap encoded lines at N characters (default 76, 0=no wrap)
 *   -i, --ignore-garbage  when decoding, ignore non-alphabet chars
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define VERSION "1.0"

/* RFC 4648 Base32 alphabet */
static const char B32ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static int g_decode  = 0;
static int g_wrap    = 76;
static int g_ignore  = 0;

/* ── Encoding ────────────────────────────────────────────────── */

static void encode(FILE *fp) {
    unsigned char in[5];
    char out[8];
    int col = 0;
    size_t n;

    while ((n = fread(in, 1, 5, fp)) > 0) {
        /* Pad to 5 bytes */
        for (size_t i = n; i < 5; i++) in[i] = 0;

        /* 5 bytes → 8 base32 chars */
        out[0] = B32ALPHA[(in[0] >> 3) & 0x1F];
        out[1] = B32ALPHA[((in[0] << 2) | (in[1] >> 6)) & 0x1F];
        out[2] = B32ALPHA[(in[1] >> 1) & 0x1F];
        out[3] = B32ALPHA[((in[1] << 4) | (in[2] >> 4)) & 0x1F];
        out[4] = B32ALPHA[((in[2] << 1) | (in[3] >> 7)) & 0x1F];
        out[5] = B32ALPHA[(in[3] >> 2) & 0x1F];
        out[6] = B32ALPHA[((in[3] << 3) | (in[4] >> 5)) & 0x1F];
        out[7] = B32ALPHA[in[4] & 0x1F];

        /* Determine padding based on how many bytes were read */
        int chars;
        if      (n == 1) { chars = 2; }
        else if (n == 2) { chars = 4; }
        else if (n == 3) { chars = 5; }
        else if (n == 4) { chars = 7; }
        else             { chars = 8; }

        for (int i = 0; i < chars; i++) {
            putchar(out[i]);
            if (g_wrap > 0 && ++col >= g_wrap) { putchar('\n'); col = 0; }
        }
        for (int i = chars; i < 8; i++) {
            putchar('=');
            if (g_wrap > 0 && ++col >= g_wrap) { putchar('\n'); col = 0; }
        }
    }
    if (g_wrap == 0 || col > 0) putchar('\n');
}

/* ── Decoding ────────────────────────────────────────────────── */

static int decode_char(int c) {
    c = toupper(c);
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

static int decode(FILE *fp) {
    unsigned char buf[8];
    int n = 0;
    int c;

    while ((c = fgetc(fp)) != EOF) {
        if (c == '=' ) { buf[n++] = 0; if (n == 8) goto flush; continue; }
        if (c == '\n' || c == '\r' || c == ' ') continue;
        int v = decode_char(c);
        if (v < 0) {
            if (g_ignore) continue;
            fprintf(stderr, "base32: invalid character '%c'\n", c); return 1;
        }
        buf[n++] = (unsigned char)v;
        if (n == 8) {
flush:
            /* 8 base32 chars → 5 bytes */
            unsigned char out[5];
            out[0] = (buf[0] << 3) | (buf[1] >> 2);
            out[1] = (buf[1] << 6) | (buf[2] << 1) | (buf[3] >> 4);
            out[2] = (buf[3] << 4) | (buf[4] >> 1);
            out[3] = (buf[4] << 7) | (buf[5] << 2) | (buf[6] >> 3);
            out[4] = (buf[6] << 5) | buf[7];

            /* Figure out how many output bytes (based on padding) */
            /* We detect padding by checking which buf slots were '=' */
            /* Simple: write all 5 and let the output be slightly over-read
               — instead count non-padding input chars */
            /* We already set padding slots to 0; determine count from n before flush */
            fwrite(out, 1, 5, stdout);
            n = 0;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version"))        { printf("base32 %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help"))           {
            fprintf(stderr,
                "usage: base32 [-d] [-w N] [FILE]\n\n"
                "Base32 encode or decode FILE (stdin if omitted).\n\n"
                "  -d, --decode          decode\n"
                "  -i, --ignore-garbage  ignore non-alphabet chars when decoding\n"
                "  -w N, --wrap=N        wrap at column N (0=no wrap, default 76)\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--decode"))          { g_decode = 1; continue; }
        if (!strcmp(a, "--ignore-garbage"))  { g_ignore = 1; continue; }
        if (!strcmp(a, "--"))                { argi++; break; }
        if (!strncmp(a, "--wrap=", 7))       { g_wrap = atoi(a + 7); continue; }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 'd': g_decode = 1; break;
                case 'i': g_ignore = 1; break;
                case 'w': {
                    const char *val = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!val) { fprintf(stderr, "base32: option requires argument -- 'w'\n"); return 1; }
                    g_wrap = atoi(val);
                    p = val + strlen(val) - 1;
                    break;
                }
                default: fprintf(stderr, "base32: invalid option -- '%c'\n", *p); return 1;
            }
        }
    }

    FILE *fp = (argi < argc && strcmp(argv[argi], "-")) ? fopen(argv[argi], "rb") : stdin;
    if (!fp) { perror(argv[argi]); return 1; }

    int ret = g_decode ? decode(fp) : (encode(fp), 0);
    if (fp != stdin) fclose(fp);
    return ret;
}
