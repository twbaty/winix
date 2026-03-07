/*
 * sha224sum — compute and verify SHA-224 checksums
 *
 * SHA-224 is SHA-256 with different initial values, output truncated to 224 bits.
 *
 * Usage: sha224sum [-c] [--tag] [FILE ...]
 *   -c  check checksums from FILE
 *   --tag  BSD-style output: SHA224 (file) = hash
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
#define HASH_BYTES 28   /* 224 bits */
#define HEX_LEN    57   /* 56 hex chars + NUL */

/* ── SHA-224/256 shared core ────────────────────────────────── */

typedef struct { uint32_t state[8]; uint64_t bitcount; uint8_t buf[64]; uint32_t buflen; } SHA256_CTX;

#define ROR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (ROR32(x,2)^ROR32(x,13)^ROR32(x,22))
#define EP1(x) (ROR32(x,6)^ROR32(x,11)^ROR32(x,25))
#define SG0(x) (ROR32(x,7)^ROR32(x,18)^((x)>>3))
#define SG1(x) (ROR32(x,17)^ROR32(x,19)^((x)>>10))

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

static void sha256_block(SHA256_CTX *ctx, const uint8_t *blk) {
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2;
    for (int i=0;i<16;i++) w[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|((uint32_t)blk[i*4+2]<<8)|(uint32_t)blk[i*4+3];
    for (int i=16;i<64;i++) w[i]=SG1(w[i-2])+w[i-7]+SG0(w[i-15])+w[i-16];
    a=ctx->state[0];b=ctx->state[1];c=ctx->state[2];d=ctx->state[3];
    e=ctx->state[4];f=ctx->state[5];g=ctx->state[6];h=ctx->state[7];
    for (int i=0;i<64;i++){t1=h+EP1(e)+CH(e,f,g)+K256[i]+w[i];t2=EP0(a)+MAJ(a,b,c);h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
    ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}

static void sha224_init(SHA256_CTX *ctx) {
    /* SHA-224 initial hash values (second 32 bits of fractional parts of sqrt of 9th-16th primes) */
    ctx->state[0]=0xc1059ed8; ctx->state[1]=0x367cd507; ctx->state[2]=0x3070dd17; ctx->state[3]=0xf70e5939;
    ctx->state[4]=0xffc00b31; ctx->state[5]=0x68581511; ctx->state[6]=0x64f98fa7; ctx->state[7]=0xbefa4fa4;
    ctx->bitcount=0; ctx->buflen=0;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len) {
    ctx->bitcount += (uint64_t)len * 8;
    while (len > 0) {
        uint32_t room=64-ctx->buflen, take=(uint32_t)len<room?(uint32_t)len:room;
        memcpy(ctx->buf+ctx->buflen,data,take); ctx->buflen+=take; data+=take; len-=take;
        if (ctx->buflen==64){sha256_block(ctx,ctx->buf);ctx->buflen=0;}
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t *out, int outbytes) {
    ctx->buf[ctx->buflen++]=0x80;
    if (ctx->buflen>56){while(ctx->buflen<64)ctx->buf[ctx->buflen++]=0;sha256_block(ctx,ctx->buf);ctx->buflen=0;}
    while(ctx->buflen<56)ctx->buf[ctx->buflen++]=0;
    uint64_t bc=ctx->bitcount;
    for(int i=7;i>=0;i--){ctx->buf[56+i]=(uint8_t)(bc&0xFF);bc>>=8;}
    sha256_block(ctx,ctx->buf);
    for(int i=0;i<outbytes/4;i++){out[i*4]=(uint8_t)(ctx->state[i]>>24);out[i*4+1]=(uint8_t)(ctx->state[i]>>16);out[i*4+2]=(uint8_t)(ctx->state[i]>>8);out[i*4+3]=(uint8_t)(ctx->state[i]);}
}

static int hash_file(FILE *fp, uint8_t out[HASH_BYTES]) {
    SHA256_CTX ctx; sha224_init(&ctx);
    uint8_t buf[65536]; size_t n;
    while ((n=fread(buf,1,sizeof(buf),fp))>0) sha256_update(&ctx,buf,n);
    if (ferror(fp)) return 0;
    sha256_final(&ctx,out,HASH_BYTES);
    return 1;
}

static void hex(const uint8_t *d, int n, char *s) {
    for(int i=0;i<n;i++){s[i*2]=("0123456789abcdef")[d[i]>>4];s[i*2+1]=("0123456789abcdef")[d[i]&15];}
    s[n*2]='\0';
}

static int g_tag = 0;

static int do_file(const char *path) {
    FILE *fp = path ? fopen(path,"rb") : stdin;
    if (!fp){perror(path);return 1;}
    uint8_t dig[HASH_BYTES]; char hexstr[HEX_LEN];
    int ok = hash_file(fp,dig);
    if (path) fclose(fp);
    if (!ok){fprintf(stderr,"sha224sum: %s: read error\n",path?path:"-");return 1;}
    hex(dig,HASH_BYTES,hexstr);
    if (g_tag) printf("SHA224 (%s) = %s\n",path?path:"-",hexstr);
    else       printf("%s  %s\n",hexstr,path?path:"-");
    return 0;
}

static int do_check(const char *listfile) {
    FILE *fp = listfile ? fopen(listfile,"r") : stdin;
    if (!fp){perror(listfile);return 1;}
    char line[4096]; int bad=0;
    while (fgets(line,sizeof(line),fp)) {
        char *p=line; while(*p==' '||*p=='\t')p++;
        if (!*p||*p=='#') continue;
        if (!strncmp(p,"SHA224 (",8)){
            char *fn=p+8,*cp=strchr(fn,')'); if(!cp)continue; *cp='\0';
            char *hp=strstr(cp+1,"= "); if(!hp)continue; hp+=2;
            char ex[HEX_LEN]; strncpy(ex,hp,HEX_LEN-1); ex[HEX_LEN-1]='\0';
            ex[strcspn(ex,"\r\n ")]='\0';
            FILE *f=fopen(fn,"rb"); if(!f){fprintf(stderr,"sha224sum: %s: FAILED open\n",fn);bad++;continue;}
            uint8_t dig[HASH_BYTES]; char got[HEX_LEN]; hash_file(f,dig);fclose(f);hex(dig,HASH_BYTES,got);
            if(!strncasecmp(got,ex,HASH_BYTES*2)){printf("%s: OK\n",fn);}else{bad++;printf("%s: FAILED\n",fn);}
            continue;
        }
        int hlen=HASH_BYTES*2;
        char hx[HEX_LEN]; if((int)strlen(p)<hlen+2)continue;
        strncpy(hx,p,hlen);hx[hlen]='\0'; char *fn=p+hlen+2;
        fn[strcspn(fn,"\r\n")]='\0';
        FILE *f=fopen(fn,"rb"); if(!f){fprintf(stderr,"sha224sum: %s: FAILED open\n",fn);bad++;continue;}
        uint8_t dig[HASH_BYTES]; char got[HEX_LEN]; hash_file(f,dig);fclose(f);hex(dig,HASH_BYTES,got);
        if(!strncasecmp(got,hx,hlen)){printf("%s: OK\n",fn);}else{bad++;printf("%s: FAILED\n",fn);}
    }
    if(fp!=stdin)fclose(fp);
    if(bad)fprintf(stderr,"sha224sum: WARNING: %d computed checksum(s) did NOT match\n",bad);
    return bad?1:0;
}

int main(int argc, char *argv[]) {
    int check=0,argi=1;
    for(;argi<argc&&argv[argi][0]=='-'&&argv[argi][1];argi++){
        const char *a=argv[argi];
        if(!strcmp(a,"--version")){printf("sha224sum %s (Winix)\n",VERSION);return 0;}
        if(!strcmp(a,"--help")){fprintf(stderr,"usage: sha224sum [-c] [--tag] [FILE ...]\n");return 0;}
        if(!strcmp(a,"--tag")){g_tag=1;continue;}
        if(!strcmp(a,"--")){argi++;break;}
        for(const char *p=a+1;*p;p++){
            if(*p=='c')check=1;
            else if(*p=='b'||*p=='t'){}
            else{fprintf(stderr,"sha224sum: invalid option -- '%c'\n",*p);return 1;}
        }
    }
    if(check){if(argi>=argc)return do_check(NULL);int r=0;for(int i=argi;i<argc;i++)r|=do_check(argv[i]);return r;}
    if(argi>=argc)return do_file(NULL);
    int r=0;for(int i=argi;i<argc;i++)r|=do_file(argv[i]);return r;
}
