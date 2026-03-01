/*
 * base64.c — Winix coreutil
 *
 * Encode or decode base64.
 *
 * Usage: base64 [OPTION]... [FILE]
 *
 * Default: encode stdin (or FILE) to stdout.
 * -d / --decode         decode base64 input
 * -w N / --wrap=N       wrap encoded output at N chars per line
 *                       (default 76; 0 = no wrap)
 * -i / --ignore-garbage when decoding, ignore non-base64 characters
 *
 * Standard RFC 4648 base64 alphabet: A-Z a-z 0-9 + /
 * Padding: '=' character.
 *
 * Compile: C99, no dependencies beyond the C standard library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

/* ------------------------------------------------------------------ */
/* Base64 alphabet and tables                                           */
/* ------------------------------------------------------------------ */

static const char b64_chars[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * Decode table: b64_decode[c] gives the 6-bit value for base64 char c,
 * or 0xFF if c is not a valid base64 character.
 */
static unsigned char b64_decode[256];

static void init_decode_table(void)
{
    memset(b64_decode, 0xFF, sizeof(b64_decode));
    for (int i = 0; i < 64; i++)
        b64_decode[(unsigned char)b64_chars[i]] = (unsigned char)i;
}

/* ------------------------------------------------------------------ */
/* Encoding                                                             */
/* ------------------------------------------------------------------ */

static int do_encode(FILE *fin, int wrap)
{
    unsigned char in[3];
    int col = 0;   /* current output column */

    for (;;) {
        /* Read up to 3 bytes */
        int n = 0;
        int c;
        while (n < 3 && (c = fgetc(fin)) != EOF)
            in[n++] = (unsigned char)c;

        if (n == 0)
            break;  /* EOF with nothing to flush */

        /* Encode 3 (or fewer) bytes into 4 base64 chars */
        unsigned char b0 = in[0];
        unsigned char b1 = (n >= 2) ? in[1] : 0;
        unsigned char b2 = (n >= 3) ? in[2] : 0;

        char out[4];
        out[0] = b64_chars[(b0 >> 2) & 0x3F];
        out[1] = b64_chars[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
        out[2] = (n >= 2) ? b64_chars[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=';
        out[3] = (n >= 3) ? b64_chars[b2 & 0x3F] : '=';

        for (int i = 0; i < 4; i++) {
            if (wrap > 0 && col >= wrap) {
                putchar('\n');
                col = 0;
            }
            putchar(out[i]);
            col++;
        }
    }

    /* Final newline */
    putchar('\n');
    return 0;
}

/* ------------------------------------------------------------------ */
/* Decoding                                                             */
/* ------------------------------------------------------------------ */

static int do_decode(FILE *fin, bool ignore_garbage)
{
    /*
     * Accumulate 4 base64 characters at a time, then emit 3 bytes.
     * Whitespace is always silently skipped (standard behaviour).
     * '=' signals padding and ends the stream.
     */
    unsigned char group[4];
    int           gi        = 0;  /* index into group */
    bool          seen_pad  = false;

    int c;
    while ((c = fgetc(fin)) != EOF) {
        unsigned char uc = (unsigned char)c;

        /* Always skip whitespace */
        if (uc == ' ' || uc == '\t' || uc == '\n' || uc == '\r')
            continue;

        if (uc == '=') {
            seen_pad = true;
            /* Flush whatever we have collected */
            break;
        }

        unsigned char val = b64_decode[uc];
        if (val == 0xFF) {
            if (ignore_garbage)
                continue;
            fprintf(stderr, "base64: invalid input\n");
            return 1;
        }

        group[gi++] = val;

        if (gi == 4) {
            /* Emit 3 bytes */
            putchar((int)((group[0] << 2) | (group[1] >> 4)));
            putchar((int)(((group[1] & 0x0F) << 4) | (group[2] >> 2)));
            putchar((int)(((group[2] & 0x03) << 6) | group[3]));
            gi = 0;
        }
    }

    /* Handle partial group (with or without padding) */
    if (gi == 1) {
        /* Only 1 valid char — technically invalid; emit what we can */
        if (!ignore_garbage) {
            fprintf(stderr, "base64: invalid input (truncated stream)\n");
            return 1;
        }
    } else if (gi == 2) {
        /* 2 chars → 1 byte */
        putchar((int)((group[0] << 2) | (group[1] >> 4)));
    } else if (gi == 3) {
        /* 3 chars → 2 bytes */
        putchar((int)((group[0] << 2) | (group[1] >> 4)));
        putchar((int)(((group[1] & 0x0F) << 4) | (group[2] >> 2)));
    }

    (void)seen_pad;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Usage / version                                                      */
/* ------------------------------------------------------------------ */

static void print_usage(void)
{
    puts("Usage: base64 [OPTION]... [FILE]");
    puts("Base64 encode or decode FILE, or standard input, to standard output.");
    puts("");
    puts("  -d, --decode          decode data");
    puts("  -i, --ignore-garbage  when decoding, ignore non-alphabet characters");
    puts("  -w N, --wrap=N        wrap encoded lines after N characters (default 76)");
    puts("                        Use 0 to disable line wrapping");
    puts("  --help                display this help and exit");
    puts("  --version             output version information and exit");
    puts("");
    puts("The data are encoded as described for the base64 alphabet in RFC 4648.");
}

static void print_version(void)
{
    puts("base64 1.0 (Winix 1.0)");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    bool decode         = false;
    bool ignore_garbage = false;
    int  wrap           = 76;

    init_decode_table();

    int i = 1;
    while (i < argc) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "--decode") == 0) {
            decode = true;
            i++;
            continue;
        }
        if (strcmp(arg, "--ignore-garbage") == 0) {
            ignore_garbage = true;
            i++;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }
        /* --wrap=N */
        if (strncmp(arg, "--wrap=", 7) == 0) {
            char *end;
            long v = strtol(arg + 7, &end, 10);
            if (*end != '\0' || v < 0) {
                fprintf(stderr, "base64: invalid wrap width: '%s'\n", arg + 7);
                return 1;
            }
            wrap = (int)v;
            i++;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            const char *p = arg + 1;
            bool stop = false;
            while (*p && !stop) {
                char opt = *p;
                if (opt == 'd') {
                    decode = true;
                    p++;
                } else if (opt == 'i') {
                    ignore_garbage = true;
                    p++;
                } else if (opt == 'w') {
                    /* -w N — N may be attached or next token */
                    const char *nstr = p + 1;
                    if (*nstr == '\0') {
                        i++;
                        if (i >= argc) {
                            fprintf(stderr, "base64: option requires an argument -- 'w'\n");
                            return 1;
                        }
                        nstr = argv[i];
                    }
                    char *end;
                    long v = strtol(nstr, &end, 10);
                    if (*end != '\0' || v < 0) {
                        fprintf(stderr, "base64: invalid wrap width: '%s'\n", nstr);
                        return 1;
                    }
                    wrap = (int)v;
                    stop = true;
                } else {
                    fprintf(stderr, "base64: invalid option -- '%c'\n", opt);
                    return 1;
                }
            }
            i++;
            continue;
        }
        break; /* non-option: file argument */
    }

    /* Open input */
    FILE *fin;
    bool  close_fin = false;

    if (i >= argc) {
        fin = stdin;
    } else {
        const char *path = argv[i];
        if (strcmp(path, "-") == 0) {
            fin = stdin;
        } else {
            fin = fopen(path, "rb");
            if (!fin) {
                fprintf(stderr, "base64: %s: %s\n", path, strerror(errno));
                return 1;
            }
            close_fin = true;
        }
    }

    /* Switch stdin to binary mode on Windows to avoid CRLF translation */
#ifdef _WIN32
    if (fin == stdin) {
        _setmode(_fileno(stdin), 0x8000 /*_O_BINARY*/);
    }
    if (!decode) {
        _setmode(_fileno(stdout), 0x8000 /*_O_BINARY*/);
    }
#endif

    int ret;
    if (decode)
        ret = do_decode(fin, ignore_garbage);
    else
        ret = do_encode(fin, wrap);

    if (close_fin) fclose(fin);
    return ret;
}
