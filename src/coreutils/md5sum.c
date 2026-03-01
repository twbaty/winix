/*
 * md5sum.c — Winix coreutil
 * Compute and verify MD5 checksums (RFC 1321).
 * No external crypto libraries; algorithm implemented inline.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* MD5 implementation (RFC 1321)                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t state[4];   /* A, B, C, D */
    uint32_t count[2];   /* bit count low, high */
    uint8_t  buf[64];
} MD5_CTX;

/* Rotate left */
#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* The four MD5 auxiliary functions */
#define F(b,c,d)  (((b) & (c)) | (~(b) & (d)))
#define G(b,c,d)  (((b) & (d)) | ((c) & ~(d)))
#define H(b,c,d)  ((b) ^ (c) ^ (d))
#define I(b,c,d)  ((c) ^ ((b) | ~(d)))

/* Per-round shift amounts */
static const uint32_t S[64] = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
};

/* Precomputed T[i] = floor(abs(sin(i+1)) * 2^32) */
static const uint32_t T[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,
    0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,
    0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,
    0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,
    0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,
    0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,
    0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,
    0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,
    0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t M[16];
    int i;

    /* Decode block as little-endian 32-bit words */
    for (i = 0; i < 16; i++) {
        M[i] = ((uint32_t)block[i*4])
             | ((uint32_t)block[i*4+1] << 8)
             | ((uint32_t)block[i*4+2] << 16)
             | ((uint32_t)block[i*4+3] << 24);
    }

    for (i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16)      { f = F(b,c,d); g = (uint32_t)i; }
        else if (i < 32) { f = G(b,c,d); g = (5*(uint32_t)i + 1) % 16; }
        else if (i < 48) { f = H(b,c,d); g = (3*(uint32_t)i + 5) % 16; }
        else             { f = I(b,c,d); g = (7*(uint32_t)i)     % 16; }

        f = f + a + T[i] + M[g];
        a = d;
        d = c;
        c = b;
        b = b + ROL32(f, S[i]);
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_init(MD5_CTX *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

static void md5_update(MD5_CTX *ctx, const uint8_t *data, size_t len)
{
    uint32_t index = (ctx->count[0] >> 3) & 0x3f;
    uint32_t part;

    /* Update bit count */
    ctx->count[0] += (uint32_t)(len << 3);
    if (ctx->count[0] < (uint32_t)(len << 3))
        ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);

    part = 64 - index;

    if (len >= part) {
        memcpy(&ctx->buf[index], data, part);
        md5_transform(ctx->state, ctx->buf);
        size_t i;
        for (i = part; i + 63 < len; i += 64)
            md5_transform(ctx->state, data + i);
        index = 0;
        data += i;
        len  -= i;
    }

    memcpy(&ctx->buf[index], data, len);
}

static void md5_final(uint8_t digest[16], MD5_CTX *ctx)
{
    static const uint8_t PADDING[64] = { 0x80 };
    uint8_t bits[8];
    uint32_t index, pad_len;
    int i;

    /* Encode bit count as little-endian 64-bit */
    for (i = 0; i < 4; i++) {
        bits[i]   = (uint8_t)(ctx->count[0] >> (i * 8));
        bits[i+4] = (uint8_t)(ctx->count[1] >> (i * 8));
    }

    index   = (ctx->count[0] >> 3) & 0x3f;
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, PADDING, pad_len);
    md5_update(ctx, bits, 8);

    for (i = 0; i < 4; i++) {
        digest[i*4]   = (uint8_t)(ctx->state[i]);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+3] = (uint8_t)(ctx->state[i] >> 24);
    }
}

/* ------------------------------------------------------------------ */
/* Utility                                                             */
/* ------------------------------------------------------------------ */

#define CHUNK (65536)

static int hash_stream(FILE *f, uint8_t digest[16])
{
    MD5_CTX ctx;
    uint8_t buf[CHUNK];
    size_t n;

    md5_init(&ctx);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        md5_update(&ctx, buf, n);
    if (ferror(f)) return -1;
    md5_final(digest, &ctx);
    return 0;
}

static void sprint_hex(char out[33], const uint8_t digest[16])
{
    int i;
    for (i = 0; i < 16; i++)
        sprintf(out + i*2, "%02x", digest[i]);
    out[32] = '\0';
}

/* ------------------------------------------------------------------ */
/* Check mode                                                          */
/* ------------------------------------------------------------------ */

static int do_check(const char *checkfile, bool quiet, bool status,
                    bool text_mode)
{
    FILE *cf;
    bool opened = false;
    int failures = 0;

    if (strcmp(checkfile, "-") == 0) {
        cf = stdin;
    } else {
        cf = fopen(checkfile, "r");
        if (!cf) {
            fprintf(stderr, "md5sum: %s: %s\n", checkfile, strerror(errno));
            return 1;
        }
        opened = true;
    }

    char line[1024];
    int lineno = 0;

    while (fgets(line, sizeof(line), cf)) {
        lineno++;
        /* Strip newline */
        size_t ln = strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r'))
            line[--ln] = '\0';
        if (ln == 0) continue;

        /* Format: 32 hex chars, two spaces (or space+*), then filename */
        if (ln < 35) {
            fprintf(stderr, "md5sum: %s: %d: improperly formatted MD5 checksum line\n",
                    checkfile, lineno);
            continue;
        }
        char expected_hex[33];
        memcpy(expected_hex, line, 32);
        expected_hex[32] = '\0';
        /* Validate hex */
        bool valid = true;
        for (int i = 0; i < 32; i++) {
            if (!isxdigit((unsigned char)expected_hex[i])) { valid = false; break; }
        }
        if (!valid || (line[32] != ' ' && line[32] != '*')) {
            fprintf(stderr, "md5sum: %s: %d: improperly formatted MD5 checksum line\n",
                    checkfile, lineno);
            continue;
        }
        const char *fname = line + 34; /* skip two-char separator */

        FILE *f;
        bool f_opened = false;
        if (strcmp(fname, "-") == 0) {
            f = stdin;
        } else {
            const char *mode = text_mode ? "r" : "rb";
            f = fopen(fname, mode);
            if (!f) {
                fprintf(stderr, "md5sum: %s: %s\n", fname, strerror(errno));
                failures++;
                if (!status) printf("%s: FAILED open or read\n", fname);
                continue;
            }
            f_opened = true;
        }

        uint8_t digest[16];
        int rc = hash_stream(f, digest);
        if (f_opened) fclose(f);

        if (rc != 0) {
            fprintf(stderr, "md5sum: %s: read error\n", fname);
            failures++;
            if (!status) printf("%s: FAILED open or read\n", fname);
            continue;
        }

        char got_hex[33];
        sprint_hex(got_hex, digest);

        bool match = (strcmp(got_hex, expected_hex) == 0);
        if (!match) failures++;

        if (!status) {
            if (match && !quiet)
                printf("%s: OK\n", fname);
            else if (!match)
                printf("%s: FAILED\n", fname);
        }
    }

    if (opened) fclose(cf);
    return (failures > 0) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void usage(void)
{
    puts("Usage: md5sum [OPTION]... [FILE]...");
    puts("Print or check MD5 checksums.");
    puts("");
    puts("With no FILE, or when FILE is -, read standard input.");
    puts("");
    puts("  -b, --binary   read in binary mode");
    puts("  -c, --check    read MD5 sums from the FILEs and check them");
    puts("  -t, --text     read in text mode");
    puts("      --quiet    (with -c) don't print OK for each verified file");
    puts("      --status   (with -c) don't output anything, status code shows success");
    puts("      --help     display this help and exit");
    puts("      --version  output version information and exit");
}

int main(int argc, char *argv[])
{
    bool check     = false;
    bool quiet     = false;
    bool status    = false;
    bool text_mode = false;
    /* binary is the default on Windows; flag accepted but has no extra effect */

    int argi;
    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];
        if (strcmp(a, "--") == 0) { argi++; break; }
        if (a[0] != '-' || a[1] == '\0') break;

        if (strcmp(a, "--help") == 0)    { usage(); return 0; }
        if (strcmp(a, "--version") == 0) { puts("md5sum 1.0 (Winix 1.0)"); return 0; }
        if (strcmp(a, "--check")   == 0) { check     = true; continue; }
        if (strcmp(a, "--binary")  == 0) { continue; }
        if (strcmp(a, "--text")    == 0) { text_mode = true; continue; }
        if (strcmp(a, "--quiet")   == 0) { quiet     = true; continue; }
        if (strcmp(a, "--status")  == 0) { status    = true; continue; }

        /* Short flags (may be combined) */
        const char *p = a + 1;
        bool unknown = false;
        while (*p) {
            switch (*p) {
                case 'c': check     = true; break;
                case 'b': break; /* binary: no-op */
                case 't': text_mode = true; break;
                default:
                    fprintf(stderr, "md5sum: invalid option -- '%c'\n", *p);
                    unknown = true;
            }
            p++;
        }
        if (unknown) return 1;
    }

    int ret = 0;

    if (check) {
        if (argi >= argc) {
            /* check from stdin */
            ret = do_check("-", quiet, status, text_mode);
        } else {
            for (; argi < argc; argi++) {
                int r = do_check(argv[argi], quiet, status, text_mode);
                if (r) ret = r;
            }
        }
        return ret;
    }

    /* Normal hash mode */
    const char *open_mode = text_mode ? "r" : "rb";

    if (argi >= argc) {
        /* stdin */
        uint8_t digest[16];
        if (hash_stream(stdin, digest) != 0) {
            fprintf(stderr, "md5sum: (stdin): read error\n");
            return 1;
        }
        char hex[33];
        sprint_hex(hex, digest);
        printf("%s  -\n", hex);
    } else {
        for (; argi < argc; argi++) {
            const char *fname = argv[argi];
            FILE *f;
            bool opened = false;

            if (strcmp(fname, "-") == 0) {
                f = stdin;
            } else {
                f = fopen(fname, open_mode);
                if (!f) {
                    fprintf(stderr, "md5sum: %s: %s\n", fname, strerror(errno));
                    ret = 1;
                    continue;
                }
                opened = true;
            }

            uint8_t digest[16];
            int rc = hash_stream(f, digest);
            if (opened) fclose(f);

            if (rc != 0) {
                fprintf(stderr, "md5sum: %s: read error\n", fname);
                ret = 1;
                continue;
            }

            char hex[33];
            sprint_hex(hex, digest);
            printf("%s  %s\n", hex, fname);
        }
    }

    return ret;
}
