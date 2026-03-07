/*
 * strings — extract printable character sequences from files
 *
 * Usage: strings [-a] [-n MIN] [-t FORMAT] [--version] [--help] [FILE ...]
 *   -a        scan entire file (default; accepted for compatibility)
 *   -n MIN    minimum string length (default: 4)
 *   -t d|o|x  prefix each string with its file offset (decimal/octal/hex)
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define VERSION "1.0"
#define BUF_SZ  65536

static int   g_min  = 4;
static char  g_fmt  = '\0'; /* 'd', 'o', 'x', or 0 for no offset */

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [-a] [-n MIN] [-t d|o|x] [FILE ...]\n\n"
        "Extract printable strings from FILE(s) (or stdin).\n\n"
        "  -a        scan whole file (default)\n"
        "  -n MIN    minimum run length (default: 4)\n"
        "  -t d      prefix offset as decimal\n"
        "  -t o      prefix offset as octal\n"
        "  -t x      prefix offset as hex\n"
        "      --version\n"
        "      --help\n",
        prog);
}

static int is_printable(unsigned char c) {
    return (c >= 0x20 && c <= 0x7e) || c == '\t';
}

static void scan(FILE *fp, const char *label, int multi) {
    unsigned char buf[BUF_SZ];
    char          run[BUF_SZ + 1];
    int           rlen    = 0;
    long long     run_off = 0;
    long long     off     = 0;
    size_t        n;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        for (size_t i = 0; i < n; i++, off++) {
            if (is_printable(buf[i])) {
                if (rlen == 0) run_off = off;
                if (rlen < (int)sizeof(run) - 1)
                    run[rlen++] = (char)buf[i];
            } else {
                if (rlen >= g_min) {
                    run[rlen] = '\0';
                    if (multi)          printf("%s: ", label);
                    if (g_fmt == 'd')   printf("%7lld  ", run_off);
                    else if (g_fmt=='o') printf("%7llo  ", run_off);
                    else if (g_fmt=='x') printf("%7llx  ", run_off);
                    puts(run);
                }
                rlen = 0;
            }
        }
    }
    /* flush any trailing run */
    if (rlen >= g_min) {
        run[rlen] = '\0';
        if (multi)          printf("%s: ", label);
        if (g_fmt == 'd')   printf("%7lld  ", run_off);
        else if (g_fmt=='o') printf("%7llo  ", run_off);
        else if (g_fmt=='x') printf("%7llx  ", run_off);
        puts(run);
    }
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("strings %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help"))    { usage(argv[0]); return 0; }
        if (!strcmp(a, "--"))        { argi++; break; }

        /* -n MIN  or  -nMIN */
        if (a[1] == 'n') {
            const char *val = a[2] ? a + 2 : argv[++argi];
            if (!val) { fprintf(stderr, "strings: -n requires argument\n"); return 1; }
            g_min = atoi(val);
            if (g_min < 1) g_min = 1;
            continue;
        }
        /* -t FORMAT  or  -tFORMAT */
        if (a[1] == 't') {
            const char *val = a[2] ? a + 2 : argv[++argi];
            if (!val || (val[0]!='d'&&val[0]!='o'&&val[0]!='x')) {
                fprintf(stderr, "strings: -t requires d, o, or x\n"); return 1;
            }
            g_fmt = val[0];
            continue;
        }
        /* -a: scan all (default) */
        if (a[1] == 'a' && !a[2]) continue;

        fprintf(stderr, "strings: invalid option -- '%s'\n", a);
        return 1;
    }

    int ret   = 0;
    int nargs = argc - argi;

    if (nargs == 0) {
        scan(stdin, "(stdin)", 0);
    } else {
        for (int i = argi; i < argc; i++) {
            FILE *fp = fopen(argv[i], "rb");
            if (!fp) { perror(argv[i]); ret = 1; continue; }
            scan(fp, argv[i], nargs > 1);
            fclose(fp);
        }
    }
    return ret;
}
