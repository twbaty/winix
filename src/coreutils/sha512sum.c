/*
 * sha512sum — compute and verify SHA-512 checksums
 *
 * Usage: sha512sum [-c] [--tag] [FILE ...]
 *   -c  check checksums from FILE
 *   --tag  BSD-style output: SHA512 (file) = hash
 *   -b  binary mode (default on Windows)
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

/* ── SHA-512 (FIPS 180-4) ──────────────────────────────────────── */

typedef struct {
    uint64_t state[8];
    uint64_t bitcount[2]; /* 128-bit bit count: [0]=low, [1]=high */
    uint8_t  buf[128];
    uint32_t buflen;
} SHA512_CTX;

#define ROR64(x,n) (((x)>>(n))|((x)<<(64-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define SIG0(x) (ROR64(x,28)^ROR64(x,34)^ROR64(x,39))
#define SIG1(x) (ROR64(x,14)^ROR64(x,18)^ROR64(x,41))
#define sig0(x) (ROR64(x,1)^ROR64(x,8)^((x)>>7))
#define sig1(x) (ROR64(x,19)^ROR64(x,61)^((x)>>6))

static const uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static void sha512_block(SHA512_CTX *ctx, const uint8_t *blk) {
    uint64_t w[80], a, b, c, d, e, f, g, h, t1, t2;
    for (int i = 0; i < 16; i++) {
        w[i] = 0;
        for (int j = 0; j < 8; j++)
            w[i] = (w[i] << 8) | blk[i*8+j];
    }
    for (int i = 16; i < 80; i++)
        w[i] = sig1(w[i-2]) + w[i-7] + sig0(w[i-15]) + w[i-16];
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (int i = 0; i < 80; i++) {
        t1 = h + SIG1(e) + CH(e,f,g) + K[i] + w[i];
        t2 = SIG0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void sha512_init(SHA512_CTX *ctx) {
    ctx->state[0]=0x6a09e667f3bcc908ULL; ctx->state[1]=0xbb67ae8584caa73bULL;
    ctx->state[2]=0x3c6ef372fe94f82bULL; ctx->state[3]=0xa54ff53a5f1d36f1ULL;
    ctx->state[4]=0x510e527fade682d1ULL; ctx->state[5]=0x9b05688c2b3e6c1fULL;
    ctx->state[6]=0x1f83d9abfb41bd6bULL; ctx->state[7]=0x5be0cd19137e2179ULL;
    ctx->bitcount[0]=0; ctx->bitcount[1]=0; ctx->buflen=0;
}

static void sha512_update(SHA512_CTX *ctx, const uint8_t *data, size_t len) {
    uint64_t prev = ctx->bitcount[0];
    ctx->bitcount[0] += (uint64_t)len * 8;
    if (ctx->bitcount[0] < prev) ctx->bitcount[1]++;
    while (len > 0) {
        uint32_t room = 128 - ctx->buflen;
        uint32_t take = (uint32_t)len < room ? (uint32_t)len : room;
        memcpy(ctx->buf + ctx->buflen, data, take);
        ctx->buflen += take; data += take; len -= take;
        if (ctx->buflen == 128) { sha512_block(ctx, ctx->buf); ctx->buflen = 0; }
    }
}

static void sha512_final(SHA512_CTX *ctx, uint8_t out[64]) {
    ctx->buf[ctx->buflen++] = 0x80;
    if (ctx->buflen > 112) {
        while (ctx->buflen < 128) ctx->buf[ctx->buflen++] = 0;
        sha512_block(ctx, ctx->buf); ctx->buflen = 0;
    }
    while (ctx->buflen < 112) ctx->buf[ctx->buflen++] = 0;
    /* Append 128-bit bit count big-endian */
    uint64_t hi = ctx->bitcount[1], lo = ctx->bitcount[0];
    for (int i = 7; i >= 0; i--) { ctx->buf[112+i] = (uint8_t)(hi&0xFF); hi>>=8; }
    for (int i = 7; i >= 0; i--) { ctx->buf[120+i] = (uint8_t)(lo&0xFF); lo>>=8; }
    sha512_block(ctx, ctx->buf);
    for (int i = 0; i < 8; i++) {
        out[i*8]  =(uint8_t)(ctx->state[i]>>56); out[i*8+1]=(uint8_t)(ctx->state[i]>>48);
        out[i*8+2]=(uint8_t)(ctx->state[i]>>40); out[i*8+3]=(uint8_t)(ctx->state[i]>>32);
        out[i*8+4]=(uint8_t)(ctx->state[i]>>24); out[i*8+5]=(uint8_t)(ctx->state[i]>>16);
        out[i*8+6]=(uint8_t)(ctx->state[i]>>8);  out[i*8+7]=(uint8_t)(ctx->state[i]);
    }
}

/* ── File hashing ─────────────────────────────────────────────── */

static int hash_file(FILE *fp, uint8_t out[64]) {
    SHA512_CTX ctx; sha512_init(&ctx);
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        sha512_update(&ctx, buf, n);
    if (ferror(fp)) return 0;
    sha512_final(&ctx, out);
    return 1;
}

static void hex(const uint8_t *d, int n, char *s) {
    for (int i = 0; i < n; i++) { s[i*2]=("0123456789abcdef")[d[i]>>4]; s[i*2+1]=("0123456789abcdef")[d[i]&15]; }
    s[n*2]='\0';
}

static int g_tag = 0;

static int do_file(const char *path) {
    FILE *fp = path ? fopen(path, "rb") : stdin;
    if (!fp) { perror(path); return 1; }
    uint8_t dig[64]; char hex128[129];
    int ok = hash_file(fp, dig);
    if (path) fclose(fp);
    if (!ok) { fprintf(stderr, "sha512sum: %s: read error\n", path ? path : "-"); return 1; }
    hex(dig, 64, hex128);
    if (g_tag)  printf("SHA512 (%s) = %s\n", path ? path : "-", hex128);
    else        printf("%s  %s\n", hex128, path ? path : "-");
    return 0;
}

static int do_check(const char *listfile) {
    FILE *fp = listfile ? fopen(listfile, "r") : stdin;
    if (!fp) { perror(listfile); return 1; }
    char line[8192]; int bad=0, ok_cnt=0;
    while (fgets(line, sizeof(line), fp)) {
        char *p = line; while (*p==' '||*p=='\t') p++;
        if (!*p || *p=='#') continue;
        /* BSD tag format: SHA512 (file) = hash */
        if (!strncmp(p,"SHA512 (",8)) {
            char *fn=p+8, *cp=strchr(fn,')'); if (!cp) continue;
            *cp='\0';
            char *hp=strstr(cp+1,"= "); if (!hp) continue; hp+=2;
            char expected[129]; strncpy(expected,hp,128); expected[128]='\0';
            /* strip trailing whitespace */
            int el=(int)strlen(expected);
            while(el>0&&(expected[el-1]=='\r'||expected[el-1]=='\n'||expected[el-1]==' '))expected[--el]='\0';
            FILE *f=fopen(fn,"rb"); if(!f){fprintf(stderr,"sha512sum: %s: FAILED open\n",fn);bad++;continue;}
            uint8_t dig[64]; char got[129]; hash_file(f,dig); fclose(f); hex(dig,64,got);
            if(!strncasecmp(got,expected,128)){ok_cnt++;printf("%s: OK\n",fn);}
            else{bad++;printf("%s: FAILED\n",fn);}
            continue;
        }
        /* GNU format: hash  file */
        char hx[129]; if (strlen(p)<130) continue;
        strncpy(hx,p,128); hx[128]='\0'; char *fn=p+130;
        fn[strcspn(fn,"\r\n")]='\0';
        FILE *f=fopen(fn,"rb"); if(!f){fprintf(stderr,"sha512sum: %s: FAILED open\n",fn);bad++;continue;}
        uint8_t dig[64]; char got[129]; hash_file(f,dig); fclose(f); hex(dig,64,got);
        if(!strncasecmp(got,hx,128)){ok_cnt++;printf("%s: OK\n",fn);}
        else{bad++;printf("%s: FAILED\n",fn);}
    }
    if (fp!=stdin) fclose(fp);
    if (bad) fprintf(stderr,"sha512sum: WARNING: %d computed checksum(s) did NOT match\n",bad);
    return bad ? 1 : 0;
}

int main(int argc, char *argv[]) {
    int check=0, argi=1;
    for (; argi<argc && argv[argi][0]=='-' && argv[argi][1]; argi++) {
        const char *a=argv[argi];
        if (!strcmp(a,"--version")){printf("sha512sum %s (Winix)\n",VERSION);return 0;}
        if (!strcmp(a,"--help")){fprintf(stderr,"usage: sha512sum [-c] [--tag] [FILE ...]\n");return 0;}
        if (!strcmp(a,"--tag")){g_tag=1;continue;}
        if (!strcmp(a,"--")){argi++;break;}
        for (const char *p=a+1;*p;p++){
            if (*p=='c') check=1;
            else if (*p=='b'||*p=='t') { /* mode flags, no-op */ }
            else{fprintf(stderr,"sha512sum: invalid option -- '%c'\n",*p);return 1;}
        }
    }
    if (check) {
        if (argi>=argc) return do_check(NULL);
        int ret=0; for (int i=argi;i<argc;i++) ret|=do_check(argv[i]); return ret;
    }
    if (argi>=argc) return do_file(NULL);
    int ret=0; for (int i=argi;i<argc;i++) ret|=do_file(argv[i]); return ret;
}
