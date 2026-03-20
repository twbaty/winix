/*
 * sha1sum — compute and verify SHA-1 checksums
 *
 * Usage: sha1sum [-c] [--tag] [FILE ...]
 *   -c  check checksums from FILE
 *   --tag  BSD-style output: SHA1 (file) = hash
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

/* ── SHA-1 (FIPS 180-4) ──────────────────────────────────────── */

typedef struct {
    uint32_t state[5];
    uint64_t bitcount;
    uint8_t  buf[64];
    uint32_t buflen;
} SHA1_CTX;

#define ROL32(x,n) (((x)<<(n))|((x)>>(32-(n))))

static void sha1_block(SHA1_CTX *ctx, const uint8_t *blk) {
    uint32_t w[80], a, b, c, d, e, tmp;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|
               ((uint32_t)blk[i*4+2]<<8)|(uint32_t)blk[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = ROL32(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2];
    d=ctx->state[3]; e=ctx->state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if      (i < 20) { f=(b&c)|(~b&d); k=0x5A827999; }
        else if (i < 40) { f=b^c^d;        k=0x6ED9EBA1; }
        else if (i < 60) { f=(b&c)|(b&d)|(c&d); k=0x8F1BBCDC; }
        else             { f=b^c^d;        k=0xCA62C1D6; }
        tmp=ROL32(a,5)+f+e+k+w[i]; e=d; d=c; c=ROL32(b,30); b=a; a=tmp;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c;
    ctx->state[3]+=d; ctx->state[4]+=e;
}

static void sha1_init(SHA1_CTX *ctx) {
    ctx->state[0]=0x67452301; ctx->state[1]=0xEFCDAB89;
    ctx->state[2]=0x98BADCFE; ctx->state[3]=0x10325476;
    ctx->state[4]=0xC3D2E1F0;
    ctx->bitcount=0; ctx->buflen=0;
}

static void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len) {
    ctx->bitcount += (uint64_t)len * 8;
    while (len > 0) {
        uint32_t room = 64 - ctx->buflen;
        uint32_t take = (uint32_t)len < room ? (uint32_t)len : room;
        memcpy(ctx->buf + ctx->buflen, data, take);
        ctx->buflen += take; data += take; len -= take;
        if (ctx->buflen == 64) { sha1_block(ctx, ctx->buf); ctx->buflen = 0; }
    }
}

static void sha1_final(SHA1_CTX *ctx, uint8_t out[20]) {
    ctx->buf[ctx->buflen++] = 0x80;
    if (ctx->buflen > 56) {
        while (ctx->buflen < 64) ctx->buf[ctx->buflen++] = 0;
        sha1_block(ctx, ctx->buf); ctx->buflen = 0;
    }
    while (ctx->buflen < 56) ctx->buf[ctx->buflen++] = 0;
    uint64_t bc = ctx->bitcount;
    for (int i = 7; i >= 0; i--) { ctx->buf[56+i] = (uint8_t)(bc&0xFF); bc>>=8; }
    sha1_block(ctx, ctx->buf);
    for (int i = 0; i < 5; i++) {
        out[i*4]=(uint8_t)(ctx->state[i]>>24); out[i*4+1]=(uint8_t)(ctx->state[i]>>16);
        out[i*4+2]=(uint8_t)(ctx->state[i]>>8); out[i*4+3]=(uint8_t)(ctx->state[i]);
    }
}

/* ── File hashing ─────────────────────────────────────────────── */

static int hash_file(FILE *fp, uint8_t out[20]) {
    SHA1_CTX ctx; sha1_init(&ctx);
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        sha1_update(&ctx, buf, n);
    if (ferror(fp)) return 0;
    sha1_final(&ctx, out);
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
    uint8_t dig[20]; char hex40[41];
    int ok = hash_file(fp, dig);
    if (path) fclose(fp);
    if (!ok) { fprintf(stderr, "sha1sum: %s: read error\n", path ? path : "-"); return 1; }
    hex(dig, 20, hex40);
    if (g_tag)  printf("SHA1 (%s) = %s\n", path ? path : "-", hex40);
    else        printf("%s  %s\n", hex40, path ? path : "-");
    return 0;
}

static int do_check(const char *listfile) {
    FILE *fp = listfile ? fopen(listfile, "r") : stdin;
    if (!fp) { perror(listfile); return 1; }
    char line[4096]; int bad=0, ok_cnt=0;
    while (fgets(line, sizeof(line), fp)) {
        char *p = line; while (*p==' '||*p=='\t') p++;
        if (!*p || *p=='#') continue;
        /* BSD tag format: SHA1 (file) = hash */
        if (!strncmp(p,"SHA1 (",6)) {
            char *fn=p+6, *cp=strchr(fn,')'); if (!cp) continue;
            *cp='\0'; char *hp=strstr(cp+1,"= "); if (!hp) continue; hp+=2;
            char expected[41]; strncpy(expected,hp,40); expected[40]='\0';
            FILE *f=fopen(fn,"rb"); if (!f){fprintf(stderr,"sha1sum: %s: FAILED open\n",fn);bad++;continue;}
            uint8_t dig[20]; char got[41]; hash_file(f,dig); fclose(f); hex(dig,20,got);
            if (!strncasecmp(got,expected,40)){ok_cnt++;printf("%s: OK\n",fn);}
            else{bad++;printf("%s: FAILED\n",fn);}
            continue;
        }
        /* GNU format: hash  file */
        char hx[41]; if (strlen(p)<42) continue;
        strncpy(hx,p,40); hx[40]='\0'; char *fn=p+42;
        fn[strcspn(fn,"\r\n")]='\0';
        FILE *f=fopen(fn,"rb"); if (!f){fprintf(stderr,"sha1sum: %s: FAILED open\n",fn);bad++;continue;}
        uint8_t dig[20]; char got[41]; hash_file(f,dig); fclose(f); hex(dig,20,got);
        if (!strncasecmp(got,hx,40)){ok_cnt++;printf("%s: OK\n",fn);}
        else{bad++;printf("%s: FAILED\n",fn);}
    }
    if (fp!=stdin) fclose(fp);
    if (bad) fprintf(stderr,"sha1sum: WARNING: %d computed checksum(s) did NOT match\n",bad);
    return bad ? 1 : 0;
}

int main(int argc, char *argv[]) {
    int check=0, argi=1;
    for (; argi<argc && argv[argi][0]=='-' && argv[argi][1]; argi++) {
        const char *a=argv[argi];
        if (!strcmp(a,"--version")){printf("sha1sum %s (Winix)\n",VERSION);return 0;}
        if (!strcmp(a,"--help")){fprintf(stderr,"usage: sha1sum [-c] [--tag] [FILE ...]\n");return 0;}
        if (!strcmp(a,"--tag")){g_tag=1;continue;}
        if (!strcmp(a,"--")){argi++;break;}
        for (const char *p=a+1;*p;p++){
            if (*p=='c') check=1;
            else if (*p=='b'||*p=='t') { /* mode flags, no-op */ }
            else{fprintf(stderr,"sha1sum: invalid option -- '%c'\n",*p);return 1;}
        }
    }
    if (check) {
        if (argi>=argc) return do_check(NULL);
        int ret=0; for (int i=argi;i<argc;i++) ret|=do_check(argv[i]); return ret;
    }
    if (argi>=argc) return do_file(NULL);
    int ret=0; for (int i=argi;i<argc;i++) ret|=do_file(argv[i]); return ret;
}
