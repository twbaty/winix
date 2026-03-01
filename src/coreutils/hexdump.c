/*
 * hexdump.c — Winix coreutil
 * Display file contents in hexadecimal (BSD hexdump / xxd -C style).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Output modes                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    MODE_DEFAULT,   /* two-byte hex words, 8/line (classic BSD)    */
    MODE_CANONICAL, /* -C: hex + ASCII sidebar                     */
    MODE_HEX2,      /* -x: two-byte hex words                      */
    MODE_DEC2,      /* -d: two-byte decimal                        */
    MODE_OCT2       /* -o: two-byte octal                          */
} DumpMode;

/* ------------------------------------------------------------------ */
/* Canonical (-C) display                                             */
/* ------------------------------------------------------------------ */

/*
 * Format (exactly matching GNU hexdump -C / xxd -C):
 *
 * 00000000  48 65 6c 6c 6f 20 57 6f  72 6c 64 0a              |Hello World.|
 * ^8hex^  ^^8 bytes + spaces      ^^8 bytes + spaces        ^^|ascii|
 *
 * Field layout (fixed columns):
 *   col  0.. 7  : offset (8 hex digits)
 *   col  8.. 9  : two spaces
 *   col 10..34  : first group of 8 bytes "xx " x8 (24 chars, last has trailing space)
 *   col 34      : extra space (separator between groups)
 *   col 35..58  : second group of 8 bytes "xx " x8
 *   col 59..60  : padding to column 60 if line not full (handled below)
 *   col 61      : |
 *   col 62..nn  : ASCII chars
 *                 |
 */

static void print_canonical_line(uint64_t offset,
                                  const uint8_t *buf, int len)
{
    char line[100];
    int pos = 0;

    /* Offset */
    pos += sprintf(line + pos, "%08llx  ", (unsigned long long)offset);

    /* Hex bytes: two groups of 8, space-separated, gap between groups */
    for (int i = 0; i < 16; i++) {
        if (i > 0 && i % 8 == 0)
            line[pos++] = ' '; /* extra space between the two groups */
        if (i < len)
            pos += sprintf(line + pos, "%02x ", (unsigned)buf[i]);
        else
            pos += sprintf(line + pos, "   ");
    }

    /* Two spaces before ASCII sidebar */
    line[pos++] = ' ';

    /* ASCII sidebar */
    line[pos++] = '|';
    for (int i = 0; i < len; i++) {
        line[pos++] = isprint((unsigned char)buf[i]) ? (char)buf[i] : '.';
    }
    line[pos++] = '|';
    line[pos++] = '\n';
    line[pos]   = '\0';

    fputs(line, stdout);
}

/* ------------------------------------------------------------------ */
/* Default / -x two-byte hex word display                             */
/* ------------------------------------------------------------------ */

/*
 * Classic BSD hexdump default:
 *   0000000 6548 6c6c 206f 6f57 6c72 0a64
 *   000000c
 *
 * 8 two-byte words per line (16 bytes), offset in 7-digit octal.
 * If the last block is short, pad with spaces.
 *
 * NOTE: bytes are displayed as little-endian 16-bit words on x86 (the
 * low byte comes first in the pair).  That matches real BSD hexdump.
 */

static void print_word_line_hex(uint64_t offset,
                                 const uint8_t *buf, int len,
                                 int words_per_line,
                                 bool is_default_mode)
{
    /* offset: 7-char octal for default, 7-char hex for -x */
    if (is_default_mode)
        printf("%07llo", (unsigned long long)offset);
    else
        printf("%07llx", (unsigned long long)offset);

    for (int w = 0; w < words_per_line; w++) {
        int base = w * 2;
        if (base >= len) break;
        uint8_t lo = buf[base];
        uint8_t hi = (base + 1 < len) ? buf[base + 1] : 0;
        /* little-endian word: hi byte is more significant in display */
        printf(" %02x%02x", (unsigned)hi, (unsigned)lo);
    }
    putchar('\n');
}

static void print_word_line_dec(uint64_t offset,
                                 const uint8_t *buf, int len,
                                 int words_per_line)
{
    printf("%07llx", (unsigned long long)offset);
    for (int w = 0; w < words_per_line; w++) {
        int base = w * 2;
        if (base >= len) break;
        uint8_t lo = buf[base];
        uint8_t hi = (base + 1 < len) ? buf[base + 1] : 0;
        uint16_t val = (uint16_t)((hi << 8) | lo);
        printf(" %05u", (unsigned)val);
    }
    putchar('\n');
}

static void print_word_line_oct(uint64_t offset,
                                 const uint8_t *buf, int len,
                                 int words_per_line)
{
    printf("%07llx", (unsigned long long)offset);
    for (int w = 0; w < words_per_line; w++) {
        int base = w * 2;
        if (base >= len) break;
        uint8_t lo = buf[base];
        uint8_t hi = (base + 1 < len) ? buf[base + 1] : 0;
        uint16_t val = (uint16_t)((hi << 8) | lo);
        printf(" %06o", (unsigned)val);
    }
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/* Duplicate-line suppression (the '*' feature)                       */
/* ------------------------------------------------------------------ */

#define LINE_BYTES 16

static bool lines_equal(const uint8_t *a, const uint8_t *b, int len)
{
    return memcmp(a, b, (size_t)len) == 0;
}

/* ------------------------------------------------------------------ */
/* Core dump routine                                                   */
/* ------------------------------------------------------------------ */

static int dump_stream(FILE *f, DumpMode mode, long long skip_n,
                       long long limit_n, bool no_collapse)
{
    /* Seek / skip */
    if (skip_n > 0) {
        if (fseek(f, (long)skip_n, SEEK_SET) != 0) {
            /* fseek failed (stdin?), manually drain bytes */
            long long remaining = skip_n;
            while (remaining > 0) {
                int c = fgetc(f);
                if (c == EOF) break;
                remaining--;
            }
        }
    }

    uint8_t buf[LINE_BYTES];
    uint8_t prev[LINE_BYTES];
    memset(prev, 0, LINE_BYTES);
    bool prev_valid    = false;
    bool star_printed  = false;
    uint64_t offset    = (uint64_t)skip_n;
    long long bytes_left = (limit_n >= 0) ? limit_n : -1;
    int words_per_line = 8;

    while (1) {
        int want = LINE_BYTES;
        if (bytes_left >= 0 && (long long)want > bytes_left)
            want = (int)bytes_left;
        if (want == 0) break;

        int got = (int)fread(buf, 1, (size_t)want, f);
        if (got <= 0) break;

        if (bytes_left >= 0) bytes_left -= got;

        /* Collapse duplicate lines with '*' unless -v */
        if (!no_collapse && prev_valid && got == LINE_BYTES &&
            lines_equal(buf, prev, LINE_BYTES)) {
            if (!star_printed) {
                puts("*");
                star_printed = true;
            }
            offset += (uint64_t)got;
            continue;
        }
        star_printed = false;

        switch (mode) {
            case MODE_CANONICAL:
                print_canonical_line(offset, buf, got);
                break;
            case MODE_HEX2:
                print_word_line_hex(offset, buf, got, words_per_line, false);
                break;
            case MODE_DEC2:
                print_word_line_dec(offset, buf, got, words_per_line);
                break;
            case MODE_OCT2:
                print_word_line_oct(offset, buf, got, words_per_line);
                break;
            case MODE_DEFAULT:
            default:
                print_word_line_hex(offset, buf, got, words_per_line, true);
                break;
        }

        memcpy(prev, buf, (size_t)got);
        prev_valid = true;
        offset += (uint64_t)got;
    }

    if (ferror(f)) return -1;

    /* Final offset line */
    if (mode == MODE_CANONICAL) {
        printf("%08llx\n", (unsigned long long)offset);
    } else {
        printf("%07llo\n", (unsigned long long)offset);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Parse -n / -s numeric argument (supports k, m suffixes)           */
/* ------------------------------------------------------------------ */

static long long parse_count(const char *s)
{
    char *end;
    long long v = strtoll(s, &end, 0);
    if (end == s) return -2; /* parse error */
    if (*end == 'k' || *end == 'K') v *= 1024;
    else if (*end == 'm' || *end == 'M') v *= 1024*1024;
    return v;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void usage(void)
{
    puts("Usage: hexdump [OPTION]... [FILE]...");
    puts("Display file contents in hexadecimal.");
    puts("");
    puts("With no FILE, or when FILE is -, read standard input.");
    puts("");
    puts("  -C           canonical hex+ASCII display");
    puts("  -x           two-byte hexadecimal display");
    puts("  -d           two-byte decimal display");
    puts("  -o           two-byte octal display");
    puts("  -n N         interpret only N input bytes");
    puts("  -s N         skip N bytes from the beginning");
    puts("  -v           display all input data (no duplicate-line collapsing)");
    puts("      --help     display this help and exit");
    puts("      --version  output version information and exit");
    puts("");
    puts("Default (no format flag): two-byte hex words, 8 per line, octal offset.");
}

int main(int argc, char *argv[])
{
    DumpMode mode     = MODE_DEFAULT;
    long long limit_n = -1;   /* -1 = no limit */
    long long skip_n  =  0;
    bool no_collapse  = false;

    int argi;
    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];
        if (strcmp(a, "--") == 0) { argi++; break; }
        if (strcmp(a, "--help")    == 0) { usage(); return 0; }
        if (strcmp(a, "--version") == 0) { puts("hexdump 1.0 (Winix 1.0)"); return 0; }
        if (a[0] != '-' || a[1] == '\0') break;

        /* Options may be combined: -Cv, -Cn 32, etc. */
        const char *p = a + 1;
        bool done = false;
        while (*p && !done) {
            switch (*p) {
                case 'C': mode = MODE_CANONICAL; break;
                case 'x': mode = MODE_HEX2;      break;
                case 'd': mode = MODE_DEC2;       break;
                case 'o': mode = MODE_OCT2;       break;
                case 'v': no_collapse = true;     break;
                case 'n': {
                    /* value follows immediately or as next arg */
                    const char *val = p + 1;
                    if (*val == '\0') {
                        if (++argi >= argc) {
                            fprintf(stderr, "hexdump: option requires an argument -- 'n'\n");
                            return 1;
                        }
                        val = argv[argi];
                    } else {
                        p = val + strlen(val) - 1; /* advance to end */
                    }
                    limit_n = parse_count(val);
                    if (limit_n < 0) {
                        fprintf(stderr, "hexdump: invalid number of bytes '%s'\n", val);
                        return 1;
                    }
                    done = true; /* consumed rest of this arg */
                    break;
                }
                case 's': {
                    const char *val = p + 1;
                    if (*val == '\0') {
                        if (++argi >= argc) {
                            fprintf(stderr, "hexdump: option requires an argument -- 's'\n");
                            return 1;
                        }
                        val = argv[argi];
                    } else {
                        p = val + strlen(val) - 1;
                    }
                    skip_n = parse_count(val);
                    if (skip_n < 0) {
                        fprintf(stderr, "hexdump: invalid offset '%s'\n", val);
                        return 1;
                    }
                    done = true;
                    break;
                }
                default:
                    fprintf(stderr, "hexdump: invalid option -- '%c'\n", *p);
                    return 1;
            }
            p++;
        }
    }

    int ret = 0;

    if (argi >= argc) {
        /* stdin */
        if (dump_stream(stdin, mode, skip_n, limit_n, no_collapse) != 0) {
            fprintf(stderr, "hexdump: (stdin): read error\n");
            ret = 1;
        }
    } else {
        for (; argi < argc; argi++) {
            const char *fname = argv[argi];
            FILE *f;
            bool opened = false;

            if (strcmp(fname, "-") == 0) {
                f = stdin;
            } else {
                f = fopen(fname, "rb");
                if (!f) {
                    fprintf(stderr, "hexdump: %s: %s\n", fname, strerror(errno));
                    ret = 1;
                    continue;
                }
                opened = true;
            }

            /* For multi-file dumps, skip and limit apply per-file but offset
               resets to 0 (or skip_n) for each file, matching BSD behaviour. */
            if (dump_stream(f, mode, skip_n, limit_n, no_collapse) != 0) {
                fprintf(stderr, "hexdump: %s: read error\n", fname);
                ret = 1;
            }

            if (opened) fclose(f);
        }
    }

    return ret;
}
