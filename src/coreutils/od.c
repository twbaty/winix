/*
 * od — octal, decimal, hex, or ASCII dump of files
 *
 * Usage: od [OPTIONS] [FILE ...]
 *   -t TYPE   output type: o (octal, default), x (hex), d (decimal),
 *             u (unsigned decimal), c (chars/escapes), a (named chars)
 *             Append a size suffix: 1 2 4 8  (default: 4)
 *   -A RADIX  address format: o (octal, default), x (hex), d (decimal), n (none)
 *   -N COUNT  read at most COUNT bytes
 *   -j OFFSET skip OFFSET bytes from start
 *   -w [N]    bytes per output row (default 16 for non-char, 16 for char)
 *   -v        don't suppress duplicate lines
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define VERSION "1.0"

typedef enum { FMT_OCT, FMT_HEX, FMT_DEC, FMT_UDEC, FMT_CHAR, FMT_NAMED } FmtType;

static FmtType  g_fmt    = FMT_OCT;
static int      g_size   = 4;       /* element size in bytes */
static char     g_addr   = 'o';     /* o/x/d/n */
static long long g_skip  = 0;
static long long g_count = -1;
static int      g_width  = 16;
static int      g_verbose = 0;

static const char *NAMED[] = {
    "nul","soh","stx","etx","eot","enq","ack","bel",
    "bs", "ht", "nl", "vt", "ff", "cr", "so", "si",
    "dle","dc1","dc2","dc3","dc4","nak","syn","etb",
    "can","em", "sub","esc","fs", "gs", "rs", "us",
    "sp"
};

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] [FILE ...]\n\n"
        "Dump file contents in various formats.\n\n"
        "  -t TYPE   o=octal, x=hex, d=signed-dec, u=unsigned-dec,\n"
        "            c=chars, a=named  (append 1/2/4/8 for element size)\n"
        "  -A RADIX  address radix: o=octal, x=hex, d=decimal, n=none\n"
        "  -N COUNT  read at most COUNT bytes\n"
        "  -j SKIP   skip SKIP bytes from start\n"
        "  -w N      bytes per row (default 16)\n"
        "  -v        do not suppress duplicate rows\n"
        "      --version\n"
        "      --help\n",
        prog);
}

static void print_addr(long long off) {
    if      (g_addr == 'o') printf("%07llo ", (unsigned long long)off);
    else if (g_addr == 'x') printf("%06llx ", (unsigned long long)off);
    else if (g_addr == 'd') printf("%07lld ", off);
    /* 'n' = no address */
}

static void print_elem(const unsigned char *data, int n) {
    /* Pad with zeros if partial element at end */
    unsigned char buf[8] = {0};
    memcpy(buf, data, (size_t)n);

    switch (g_fmt) {
        case FMT_OCT: {
            unsigned long long v = 0;
            for (int i = g_size - 1; i >= 0; i--) v = (v << 8) | buf[i];
            int w = g_size == 1 ? 4 : g_size == 2 ? 7 : g_size == 4 ? 12 : 23;
            printf(" %0*llo", w, v);
            break;
        }
        case FMT_HEX: {
            unsigned long long v = 0;
            for (int i = g_size - 1; i >= 0; i--) v = (v << 8) | buf[i];
            int w = g_size * 2;
            printf(" %0*llx", w + 1, v);
            break;
        }
        case FMT_DEC: {
            long long v = 0;
            for (int i = g_size - 1; i >= 0; i--) v = (v << 8) | buf[i];
            /* sign-extend */
            if (g_size < 8) {
                int shift = (8 - g_size) * 8;
                v = (v << shift) >> shift;
            }
            int w = g_size == 1 ? 5 : g_size == 2 ? 7 : g_size == 4 ? 12 : 21;
            printf(" %*lld", w, v);
            break;
        }
        case FMT_UDEC: {
            unsigned long long v = 0;
            for (int i = g_size - 1; i >= 0; i--) v = (v << 8) | buf[i];
            int w = g_size == 1 ? 4 : g_size == 2 ? 6 : g_size == 4 ? 11 : 21;
            printf(" %*llu", w, v);
            break;
        }
        case FMT_CHAR: {
            for (int i = 0; i < n; i++) {
                unsigned char c = buf[i];
                switch (c) {
                    case '\0': printf("  \\0"); break;
                    case '\a': printf("  \\a"); break;
                    case '\b': printf("  \\b"); break;
                    case '\f': printf("  \\f"); break;
                    case '\n': printf("  \\n"); break;
                    case '\r': printf("  \\r"); break;
                    case '\t': printf("  \\t"); break;
                    case '\v': printf("  \\v"); break;
                    default:
                        if (c >= 0x20 && c <= 0x7e) printf("   %c", c);
                        else printf(" %03o", c);
                }
            }
            break;
        }
        case FMT_NAMED: {
            for (int i = 0; i < n; i++) {
                unsigned char c = buf[i];
                if (c <= 0x20)      printf(" %3s", NAMED[c]);
                else if (c == 0x7f) printf(" del");
                else if (c < 0x7f)  printf("   %c", c);
                else                printf(" %03o", c);
            }
            break;
        }
    }
}

static void dump(FILE *fp) {
    unsigned char cur[256], prev[256];
    int width = g_width < 1 ? 1 : (g_width > 256 ? 256 : g_width);
    long long off  = g_skip;
    long long done = 0;
    int suppress = 0, prev_len = 0;

    if (g_skip > 0) fseek(fp, (long)g_skip, SEEK_SET);

    for (;;) {
        int want = width;
        if (g_count >= 0 && done + want > g_count) want = (int)(g_count - done);
        if (want <= 0) break;

        int n = (int)fread(cur, 1, (size_t)want, fp);
        if (n <= 0) break;

        int dup = !g_verbose && prev_len == n && memcmp(cur, prev, (size_t)n) == 0;
        if (dup) {
            if (!suppress) { printf("*\n"); suppress = 1; }
        } else {
            suppress = 0;
            print_addr(off);
            /* print elements */
            int es = (g_fmt == FMT_CHAR || g_fmt == FMT_NAMED) ? 1 : g_size;
            for (int i = 0; i < n; i += es)
                print_elem(cur + i, (i + es <= n) ? es : n - i);
            putchar('\n');
            memcpy(prev, cur, (size_t)n);
            prev_len = n;
        }

        off  += n;
        done += n;
    }
    print_addr(off);
    putchar('\n');
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("od %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help"))    { usage(argv[0]); return 0; }
        if (!strcmp(a, "--"))        { argi++; break; }
        if (!strcmp(a, "-v"))        { g_verbose = 1; continue; }

        if (!strcmp(a, "-t") || (a[1]=='t' && a[2])) {
            const char *val = a[2] ? a + 2 : argv[++argi];
            if (!val) { fprintf(stderr, "od: -t requires argument\n"); return 1; }
            switch (val[0]) {
                case 'o': g_fmt = FMT_OCT;   break;
                case 'x': g_fmt = FMT_HEX;   break;
                case 'd': g_fmt = FMT_DEC;   break;
                case 'u': g_fmt = FMT_UDEC;  break;
                case 'c': g_fmt = FMT_CHAR;  break;
                case 'a': g_fmt = FMT_NAMED; break;
                default:
                    fprintf(stderr, "od: invalid type '%c'\n", val[0]); return 1;
            }
            if (val[1] >= '1' && val[1] <= '8') g_size = val[1] - '0';
            continue;
        }
        if (!strcmp(a, "-A") || (a[1]=='A' && a[2])) {
            const char *val = a[2] ? a + 2 : argv[++argi];
            if (!val) { fprintf(stderr, "od: -A requires argument\n"); return 1; }
            g_addr = val[0];
            continue;
        }
        if (!strcmp(a, "-N") || (a[1]=='N' && a[2])) {
            const char *val = a[2] ? a + 2 : argv[++argi];
            if (!val) { fprintf(stderr, "od: -N requires argument\n"); return 1; }
            g_count = strtoll(val, NULL, 0);
            continue;
        }
        if (!strcmp(a, "-j") || (a[1]=='j' && a[2])) {
            const char *val = a[2] ? a + 2 : argv[++argi];
            if (!val) { fprintf(stderr, "od: -j requires argument\n"); return 1; }
            g_skip = strtoll(val, NULL, 0);
            continue;
        }
        if (a[1] == 'w') {
            const char *val = a[2] ? a + 2 : NULL;
            if (val) g_width = atoi(val);
            else     g_width = 32;
            continue;
        }

        fprintf(stderr, "od: invalid option -- '%s'\n", a);
        return 1;
    }

    int ret = 0;
    if (argi == argc) {
        dump(stdin);
    } else {
        for (int i = argi; i < argc; i++) {
            FILE *fp = fopen(argv[i], "rb");
            if (!fp) { perror(argv[i]); ret = 1; continue; }
            dump(fp);
            fclose(fp);
        }
    }
    return ret;
}
