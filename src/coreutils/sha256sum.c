/*
 * sha256sum.c — Winix coreutil
 * Compute and verify SHA-256 checksums (FIPS 180-4).
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
/* SHA-256 implementation (FIPS 180-4)                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t  buf[64];
    uint32_t buflen;
} SHA256_CTX;

/* Rotate right */
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* SHA-256 functions */
#define CH(e,f,g)   (((e) & (f)) ^ (~(e) & (g)))
#define MAJ(a,b,c)  (((a) & (b)) ^ ((a) & (c)) ^ ((b) & (c)))
#define SIG0(a)     (ROR32(a, 2)  ^ ROR32(a, 13) ^ ROR32(a, 22))
#define SIG1(e)     (ROR32(e, 6)  ^ ROR32(e, 11) ^ ROR32(e, 25))
#define sig0(x)     (ROR32(x, 7)  ^ ROR32(x, 18) ^ ((x) >> 3))
#define sig1(x)     (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

/* Initial hash values H0..H7 (first 32 bits of fractional parts of
   square roots of first 8 primes) */
static const uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/* Round constants K0..K63 (first 32 bits of fractional parts of
   cube roots of first 64 primes) */
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i*4]     << 24)
             | ((uint32_t)block[i*4 + 1] << 16)
             | ((uint32_t)block[i*4 + 2] <<  8)
             | ((uint32_t)block[i*4 + 3]);
    }
    for (i = 16; i < 64; i++)
        W[i] = sig1(W[i-2]) + W[i-7] + sig0(W[i-15]) + W[i-16];

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; i++) {
        uint32_t T1 = h + SIG1(e) + CH(e,f,g) + K[i] + W[i];
        uint32_t T2 = SIG0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    memcpy(ctx->state, H0, sizeof(H0));
    ctx->bitcount = 0;
    ctx->buflen   = 0;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len)
{
    while (len > 0) {
        uint32_t space = 64 - ctx->buflen;
        uint32_t take  = (len < space) ? (uint32_t)len : space;
        memcpy(ctx->buf + ctx->buflen, data, take);
        ctx->buflen += take;
        data        += take;
        len         -= take;
        ctx->bitcount += (uint64_t)take * 8;
        if (ctx->buflen == 64) {
            sha256_transform(ctx->state, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

static void sha256_final(uint8_t digest[32], SHA256_CTX *ctx)
{
    /* Append bit '1' (0x80 byte), then zero padding, then 64-bit big-endian
       bit count. Total padded message length must be ≡ 0 (mod 512 bits). */
    uint64_t bc = ctx->bitcount;
    uint8_t  pad[64];
    uint32_t padlen;
    int i;

    /* How many bytes of padding do we need? */
    /* We need buflen + 1 (the 0x80) + padding_zeros + 8 = 64 or 128 */
    uint32_t used = ctx->buflen;
    padlen = (used < 56) ? (56 - used) : (120 - used);

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    sha256_update(ctx, pad, padlen);

    /* Append big-endian 64-bit bit count */
    uint8_t bc_bytes[8];
    for (i = 7; i >= 0; i--) {
        bc_bytes[i] = (uint8_t)(bc & 0xff);
        bc >>= 8;
    }
    sha256_update(ctx, bc_bytes, 8);

    /* Encode state as big-endian bytes */
    for (i = 0; i < 8; i++) {
        digest[i*4]     = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4 + 2] = (uint8_t)(ctx->state[i] >>  8);
        digest[i*4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Utility                                                             */
/* ------------------------------------------------------------------ */

#define CHUNK (65536)

static int hash_stream(FILE *f, uint8_t digest[32])
{
    SHA256_CTX ctx;
    uint8_t buf[CHUNK];
    size_t n;

    sha256_init(&ctx);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        sha256_update(&ctx, buf, n);
    if (ferror(f)) return -1;
    sha256_final(digest, &ctx);
    return 0;
}

static void sprint_hex(char out[65], const uint8_t digest[32])
{
    int i;
    for (i = 0; i < 32; i++)
        sprintf(out + i*2, "%02x", digest[i]);
    out[64] = '\0';
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
            fprintf(stderr, "sha256sum: %s: %s\n", checkfile, strerror(errno));
            return 1;
        }
        opened = true;
    }

    char line[2048];
    int lineno = 0;

    while (fgets(line, sizeof(line), cf)) {
        lineno++;
        size_t ln = strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r'))
            line[--ln] = '\0';
        if (ln == 0) continue;

        /* Format: 64 hex chars, two-char separator, filename */
        if (ln < 67) {
            fprintf(stderr, "sha256sum: %s: %d: improperly formatted SHA256 checksum line\n",
                    checkfile, lineno);
            continue;
        }
        char expected_hex[65];
        memcpy(expected_hex, line, 64);
        expected_hex[64] = '\0';
        bool valid = true;
        for (int i = 0; i < 64; i++) {
            if (!isxdigit((unsigned char)expected_hex[i])) { valid = false; break; }
        }
        if (!valid || (line[64] != ' ' && line[64] != '*')) {
            fprintf(stderr, "sha256sum: %s: %d: improperly formatted SHA256 checksum line\n",
                    checkfile, lineno);
            continue;
        }
        const char *fname = line + 66;

        FILE *f;
        bool f_opened = false;
        if (strcmp(fname, "-") == 0) {
            f = stdin;
        } else {
            const char *mode = text_mode ? "r" : "rb";
            f = fopen(fname, mode);
            if (!f) {
                fprintf(stderr, "sha256sum: %s: %s\n", fname, strerror(errno));
                failures++;
                if (!status) printf("%s: FAILED open or read\n", fname);
                continue;
            }
            f_opened = true;
        }

        uint8_t digest[32];
        int rc = hash_stream(f, digest);
        if (f_opened) fclose(f);

        if (rc != 0) {
            fprintf(stderr, "sha256sum: %s: read error\n", fname);
            failures++;
            if (!status) printf("%s: FAILED open or read\n", fname);
            continue;
        }

        char got_hex[65];
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
    puts("Usage: sha256sum [OPTION]... [FILE]...");
    puts("Print or check SHA-256 checksums.");
    puts("");
    puts("With no FILE, or when FILE is -, read standard input.");
    puts("");
    puts("  -b, --binary   read in binary mode");
    puts("  -c, --check    read SHA256 sums from the FILEs and check them");
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

    int argi;
    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];
        if (strcmp(a, "--") == 0) { argi++; break; }
        if (a[0] != '-' || a[1] == '\0') break;

        if (strcmp(a, "--help") == 0)    { usage(); return 0; }
        if (strcmp(a, "--version") == 0) { puts("sha256sum 1.0 (Winix 1.0)"); return 0; }
        if (strcmp(a, "--check")   == 0) { check     = true; continue; }
        if (strcmp(a, "--binary")  == 0) { continue; }
        if (strcmp(a, "--text")    == 0) { text_mode = true; continue; }
        if (strcmp(a, "--quiet")   == 0) { quiet     = true; continue; }
        if (strcmp(a, "--status")  == 0) { status    = true; continue; }

        const char *p = a + 1;
        bool unknown = false;
        while (*p) {
            switch (*p) {
                case 'c': check     = true; break;
                case 'b': break;
                case 't': text_mode = true; break;
                default:
                    fprintf(stderr, "sha256sum: invalid option -- '%c'\n", *p);
                    unknown = true;
            }
            p++;
        }
        if (unknown) return 1;
    }

    int ret = 0;

    if (check) {
        if (argi >= argc) {
            ret = do_check("-", quiet, status, text_mode);
        } else {
            for (; argi < argc; argi++) {
                int r = do_check(argv[argi], quiet, status, text_mode);
                if (r) ret = r;
            }
        }
        return ret;
    }

    const char *open_mode = text_mode ? "r" : "rb";

    if (argi >= argc) {
        uint8_t digest[32];
        if (hash_stream(stdin, digest) != 0) {
            fprintf(stderr, "sha256sum: (stdin): read error\n");
            return 1;
        }
        char hex[65];
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
                    fprintf(stderr, "sha256sum: %s: %s\n", fname, strerror(errno));
                    ret = 1;
                    continue;
                }
                opened = true;
            }

            uint8_t digest[32];
            int rc = hash_stream(f, digest);
            if (opened) fclose(f);

            if (rc != 0) {
                fprintf(stderr, "sha256sum: %s: read error\n", fname);
                ret = 1;
                continue;
            }

            char hex[65];
            sprint_hex(hex, digest);
            printf("%s  %s\n", hex, fname);
        }
    }

    return ret;
}
