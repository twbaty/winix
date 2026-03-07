/*
 * xxd — make a hex dump or reverse a hex dump
 *
 * Usage: xxd [OPTIONS] [FILE [OUTFILE]]
 *   -n LEN    stop after LEN bytes
 *   -s OFFSET skip OFFSET bytes before dumping
 *   -c COLS   bytes per output row (default 16)
 *   -g GROUP  bytes per group (default 2; 0 = no grouping)
 *   -u        use uppercase hex digits
 *   -p        plain hex dump (no address or ASCII sidebar)
 *   -r        reverse: convert hex dump back to binary
 *   -v / --version
 *       --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define VERSION "1.0"

static long long g_seek   = 0;
static long long g_len    = -1;    /* -1 = unlimited */
static int       g_cols   = 16;
static int       g_group  = 2;
static int       g_upper  = 0;
static int       g_plain  = 0;
static int       g_reverse = 0;

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] [infile [outfile]]\n\n"
        "Make a hex dump or reverse a hex dump to binary.\n\n"
        "  -n LEN    stop after LEN bytes\n"
        "  -s OFF    skip OFF bytes from start\n"
        "  -c COLS   bytes per row (default 16)\n"
        "  -g GROUP  bytes per group (default 2; 0 = none)\n"
        "  -u        uppercase hex\n"
        "  -p        plain hex (no offsets or ASCII)\n"
        "  -r        reverse: hex dump -> binary\n"
        "  -v        show version\n"
        "      --help\n",
        prog);
}

/* ── Forward dump ────────────────────────────────────────────── */

static void dump(FILE *in, FILE *out) {
    const char *hfmt = g_upper ? "%02X" : "%02x";
    const char *afmt = g_upper ? "%08llX: " : "%08llx: ";

    if (g_seek > 0) fseek(in, (long)g_seek, SEEK_SET);

    long long off  = g_seek;
    long long done = 0;

    unsigned char row[256];
    int cols = g_cols > 256 ? 256 : (g_cols < 1 ? 1 : g_cols);

    for (;;) {
        int want = cols;
        if (g_len >= 0 && done + want > g_len) want = (int)(g_len - done);
        if (want <= 0) break;

        int n = (int)fread(row, 1, (size_t)want, in);
        if (n <= 0) break;

        if (g_plain) {
            for (int i = 0; i < n; i++) {
                fprintf(out, hfmt, row[i]);
                if (g_group > 0 && (i + 1) % g_group == 0 && i + 1 < n)
                    fputc(' ', out);
            }
            fputc('\n', out);
        } else {
            /* offset */
            fprintf(out, afmt, (unsigned long long)off);

            /* hex section */
            int grp = g_group > 0 ? g_group : cols + 1;
            for (int i = 0; i < cols; i++) {
                if (i < n) fprintf(out, hfmt, row[i]);
                else       fputs("  ", out);
                if ((i + 1) % grp == 0) fputc(' ', out);
                else                    fputc(' ', out);
            }

            /* ASCII sidebar */
            fputs(" ", out);
            for (int i = 0; i < n; i++)
                fputc((row[i] >= 0x20 && row[i] <= 0x7e) ? row[i] : '.', out);
            fputc('\n', out);
        }

        off  += n;
        done += n;
    }
}

/* ── Reverse dump ────────────────────────────────────────────── */

static void reverse(FILE *in, FILE *out) {
    char line[4096];
    while (fgets(line, sizeof(line), in)) {
        const char *p = line;
        /* skip optional "XXXXXXXX: " address prefix */
        while (isxdigit((unsigned char)*p)) p++;
        if (*p == ':') { p++; while (*p == ' ') p++; }
        /* consume hex pairs */
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (!*p) break;
            if (!isxdigit((unsigned char)*p)) break;   /* hit ASCII sidebar */
            char hi = *p++;
            if (!isxdigit((unsigned char)*p)) break;
            char lo = *p++;
            unsigned char byte = (unsigned char)(
                (strchr("0123456789abcdefABCDEF", hi) - "0123456789abcdefABCDEF" > 9
                    ? (hi & 0x5f) - 'A' + 10 : hi - '0') * 16 +
                (strchr("0123456789abcdefABCDEF", lo) - "0123456789abcdefABCDEF" > 9
                    ? (lo & 0x5f) - 'A' + 10 : lo - '0'));
            /* simpler: use strtol for each pair */
            char pair[3] = { hi, lo, '\0' };
            byte = (unsigned char)strtol(pair, NULL, 16);
            fputc(byte, out);
        }
    }
}

/* ── Argument parsing helpers ────────────────────────────────── */

static long long parse_ll(const char *s) {
    char *end;
    long long v = strtoll(s, &end, 0);
    if (*end == 'k' || *end == 'K') v *= 1024;
    else if (*end == 'm' || *end == 'M') v *= 1024 * 1024;
    return v;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version") || !strcmp(a, "-v")) {
            printf("xxd %s (Winix)\n", VERSION); return 0;
        }
        if (!strcmp(a, "--help"))  { usage(argv[0]); return 0; }
        if (!strcmp(a, "--"))      { argi++; break; }
        if (!strcmp(a, "-r"))      { g_reverse = 1; continue; }
        if (!strcmp(a, "-u"))      { g_upper   = 1; continue; }
        if (!strcmp(a, "-p"))      { g_plain   = 1; continue; }

        /* flags that take a value: -X VAL or -XVAL */
        char flag = a[1];
        if (flag == 'n' || flag == 's' || flag == 'c' || flag == 'g') {
            const char *val = a[2] ? a + 2 : argv[++argi];
            if (!val) { fprintf(stderr, "xxd: -%c requires argument\n", flag); return 1; }
            long long v = parse_ll(val);
            if      (flag == 'n') g_len   = v;
            else if (flag == 's') g_seek  = v;
            else if (flag == 'c') g_cols  = (int)v;
            else if (flag == 'g') g_group = (int)v;
            continue;
        }

        fprintf(stderr, "xxd: invalid option -- '%s'\n", a);
        return 1;
    }

    FILE *in  = stdin;
    FILE *out = stdout;

    if (argi < argc && strcmp(argv[argi], "-") != 0) {
        in = fopen(argv[argi], g_reverse ? "r" : "rb");
        if (!in) { perror(argv[argi]); return 1; }
        argi++;
    }
    if (argi < argc) {
        out = fopen(argv[argi], g_reverse ? "wb" : "w");
        if (!out) { perror(argv[argi]); if (in != stdin) fclose(in); return 1; }
    }

    if (g_reverse) reverse(in, out);
    else           dump(in, out);

    if (in  != stdin)  fclose(in);
    if (out != stdout) fclose(out);
    return 0;
}
