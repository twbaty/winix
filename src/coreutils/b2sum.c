/*
 * b2sum — compute and verify BLAKE2b checksums
 *
 * Usage: b2sum [-c] [-l BITS] [--tag] [FILE ...]
 *   -c        check checksums from FILE
 *   -l BITS   digest length in bits (default 512, must be 8..512, multiple of 8)
 *   --tag     BSD-style output: BLAKE2b (file) = hash
 *   --version / --help
 *
 * Exit: 0 = success, 1 = mismatch/error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define VERSION "1.0"

/* ── BLAKE2b (RFC 7693) ─────────────────────────────────────── */

static const uint64_t BLAKE2B_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

static const uint8_t SIGMA[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3},
    {11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4},
    { 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8},
    { 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13},
    { 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9},
    {12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11},
    {13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10},
    { 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0},
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3},
};

#define ROR64(x,n) (((x)>>(n))|((x)<<(64-(n))))

#define G(r,i,a,b,c,d) \
    a = a + b + m[SIGMA[r][2*i+0]]; \
    d = ROR64(d^a, 32); \
    c = c + d; \
    b = ROR64(b^c, 24); \
    a = a + b + m[SIGMA[r][2*i+1]]; \
    d = ROR64(d^a, 16); \
    c = c + d; \
    b = ROR64(b^c, 63);

typedef struct {
    uint64_t h[8];
    uint64_t t[2];    /* counter */
    uint64_t f[2];    /* flags */
    uint8_t  buf[128];
    uint32_t buflen;
    uint8_t  outlen;  /* digest bytes */
} BLAKE2B_CTX;

static void blake2b_compress(BLAKE2B_CTX *ctx, const uint8_t *block) {
    uint64_t m[16], v[16];
    for (int i = 0; i < 16; i++) {
        m[i] = 0;
        for (int j = 0; j < 8; j++) m[i] |= ((uint64_t)block[i*8+j]) << (j*8);
    }
    for (int i = 0; i < 8; i++) v[i]   = ctx->h[i];
    for (int i = 0; i < 8; i++) v[8+i] = BLAKE2B_IV[i];
    v[12] ^= ctx->t[0];
    v[13] ^= ctx->t[1];
    v[14] ^= ctx->f[0];
    v[15] ^= ctx->f[1];

    for (int r = 0; r < 12; r++) {
        G(r,0,v[0],v[4],v[ 8],v[12]);
        G(r,1,v[1],v[5],v[ 9],v[13]);
        G(r,2,v[2],v[6],v[10],v[14]);
        G(r,3,v[3],v[7],v[11],v[15]);
        G(r,4,v[0],v[5],v[10],v[15]);
        G(r,5,v[1],v[6],v[11],v[12]);
        G(r,6,v[2],v[7],v[ 8],v[13]);
        G(r,7,v[3],v[4],v[ 9],v[14]);
    }
    for (int i = 0; i < 8; i++) ctx->h[i] ^= v[i] ^ v[i+8];
}

static void blake2b_init(BLAKE2B_CTX *ctx, int outlen) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->outlen = (uint8_t)outlen;
    for (int i = 0; i < 8; i++) ctx->h[i] = BLAKE2B_IV[i];
    ctx->h[0] ^= 0x01010000 ^ (uint64_t)outlen;
}

static void blake2b_update(BLAKE2B_CTX *ctx, const uint8_t *in, size_t inlen) {
    while (inlen > 0) {
        uint32_t room = 128 - ctx->buflen;
        uint32_t take = (uint32_t)inlen < room ? (uint32_t)inlen : room;
        memcpy(ctx->buf + ctx->buflen, in, take);
        ctx->buflen += take; in += take; inlen -= take;
        if (ctx->buflen == 128 && inlen > 0) {
            ctx->t[0] += 128;
            if (ctx->t[0] < 128) ctx->t[1]++;
            blake2b_compress(ctx, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

static void blake2b_final(BLAKE2B_CTX *ctx, uint8_t *out) {
    ctx->t[0] += ctx->buflen;
    if (ctx->t[0] < ctx->buflen) ctx->t[1]++;
    ctx->f[0] = (uint64_t)-1; /* last block flag */
    memset(ctx->buf + ctx->buflen, 0, 128 - ctx->buflen);
    blake2b_compress(ctx, ctx->buf);
    /* Extract output */
    uint8_t tmp[64];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) tmp[i*8+j] = (uint8_t)(ctx->h[i] >> (j*8));
    }
    memcpy(out, tmp, ctx->outlen);
}

/* ── Helpers ─────────────────────────────────────────────────── */

static int   g_tag    = 0;
static int   g_outlen = 64; /* bytes, default 512 bits */

static int hash_file(FILE *fp, uint8_t *out) {
    BLAKE2B_CTX ctx; blake2b_init(&ctx, g_outlen);
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        blake2b_update(&ctx, buf, n);
    if (ferror(fp)) return 0;
    blake2b_final(&ctx, out);
    return 1;
}

static void to_hex(const uint8_t *d, int n, char *s) {
    for (int i = 0; i < n; i++) {
        s[i*2]   = "0123456789abcdef"[d[i] >> 4];
        s[i*2+1] = "0123456789abcdef"[d[i] & 15];
    }
    s[n*2] = '\0';
}

static int do_file(const char *path) {
    FILE *fp = path ? fopen(path, "rb") : stdin;
    if (!fp) { perror(path); return 1; }
    uint8_t dig[64]; char hexstr[129];
    int ok = hash_file(fp, dig);
    if (path) fclose(fp);
    if (!ok) { fprintf(stderr, "b2sum: %s: read error\n", path ? path : "-"); return 1; }
    to_hex(dig, g_outlen, hexstr);
    if (g_tag)
        printf("BLAKE2b-%d (%s) = %s\n", g_outlen * 8, path ? path : "-", hexstr);
    else
        printf("%s  %s\n", hexstr, path ? path : "-");
    return 0;
}

static int do_check(const char *listfile) {
    FILE *fp = listfile ? fopen(listfile, "r") : stdin;
    if (!fp) { perror(listfile); return 1; }
    char line[1024]; int bad = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;
        int hlen = g_outlen * 2;
        if ((int)strlen(p) < hlen + 2) continue;
        char hx[129]; strncpy(hx, p, (size_t)hlen); hx[hlen] = '\0';
        char *fn = p + hlen + 2;
        fn[strcspn(fn, "\r\n")] = '\0';
        FILE *f = fopen(fn, "rb");
        if (!f) { fprintf(stderr, "b2sum: %s: FAILED open\n", fn); bad++; continue; }
        uint8_t dig[64]; char got[129];
        hash_file(f, dig); fclose(f); to_hex(dig, g_outlen, got);
        if (!strncasecmp(got, hx, (size_t)hlen)) printf("%s: OK\n", fn);
        else { bad++; printf("%s: FAILED\n", fn); }
    }
    if (fp != stdin) fclose(fp);
    if (bad) fprintf(stderr, "b2sum: WARNING: %d computed checksum(s) did NOT match\n", bad);
    return bad ? 1 : 0;
}

int main(int argc, char *argv[]) {
    int check = 0, argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("b2sum %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: b2sum [-c] [-l BITS] [--tag] [FILE ...]\n\n"
                "Compute or check BLAKE2b checksums.\n\n"
                "  -c        check checksums\n"
                "  -l BITS   digest length in bits (8..512, multiple of 8; default 512)\n"
                "  --tag     BSD-style output\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--tag")) { g_tag = 1; continue; }
        if (!strcmp(a, "--"))   { argi++; break; }
        for (const char *p = a + 1; *p; p++) {
            if (*p == 'c') { check = 1; }
            else if (*p == 'l') {
                const char *val = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                if (!val) { fprintf(stderr, "b2sum: option requires argument -- 'l'\n"); return 1; }
                int bits = atoi(val);
                if (bits < 8 || bits > 512 || bits % 8 != 0) {
                    fprintf(stderr, "b2sum: invalid digest length: %d\n", bits); return 1;
                }
                g_outlen = bits / 8;
                p = val + strlen(val) - 1;
            } else {
                fprintf(stderr, "b2sum: invalid option -- '%c'\n", *p); return 1;
            }
        }
    }

    if (check) {
        if (argi >= argc) return do_check(NULL);
        int r = 0; for (int i = argi; i < argc; i++) r |= do_check(argv[i]); return r;
    }
    if (argi >= argc) return do_file(NULL);
    int r = 0; for (int i = argi; i < argc; i++) r |= do_file(argv[i]); return r;
}
