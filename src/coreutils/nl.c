/*
 * nl.c — Winix coreutil
 *
 * Usage: nl [OPTION]... [FILE]...
 *
 * Number lines of each FILE (or stdin) and write to stdout.
 *
 * Options:
 *   -b STYLE   body numbering style: a=all, t=non-empty (default), n=none
 *   -n FORMAT  number format: ln=left-justified, rn=right-justified (default),
 *              rz=right-justified with leading zeros
 *   -w N       number width (default 6)
 *   -s STRING  separator between number and text (default TAB)
 *   -v N       starting line number (default 1)
 *   -i N       line number increment (default 1)
 *   --help     Print usage and exit 0
 *   --version  Print version and exit 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

/* Numbering body style */
typedef enum { STYLE_ALL, STYLE_NONEMPTY, STYLE_NONE } BodyStyle;

/* Number format */
typedef enum { FMT_LN, FMT_RN, FMT_RZ } NumFmt;

static BodyStyle body_style = STYLE_NONEMPTY;
static NumFmt    num_fmt    = FMT_RN;
static int       num_width  = 6;
static char      separator[64] = "\t";
static long      start_num  = 1;
static long      increment  = 1;

static void usage(void) {
    puts("Usage: nl [OPTION]... [FILE]...");
    puts("Number lines of each FILE and write to standard output.");
    puts("With no FILE, or when FILE is -, read standard input.");
    puts("");
    puts("  -b STYLE   body numbering: a=all, t=non-empty lines (default), n=none");
    puts("  -n FORMAT  number format: ln, rn (default), rz");
    puts("  -w N       width of line numbers (default 6)");
    puts("  -s STRING  separator after number (default TAB)");
    puts("  -v N       first line number (default 1)");
    puts("  -i N       line number increment (default 1)");
    puts("  --help     display this help and exit");
    puts("  --version  output version information and exit");
}

static void print_number(long n) {
    char nbuf[64];
    switch (num_fmt) {
        case FMT_LN:
            /* Left-justified, no leading zeros: print number then pad with spaces */
            snprintf(nbuf, sizeof(nbuf), "%ld", n);
            fputs(nbuf, stdout);
            /* Pad to width with trailing spaces */
            {
                int printed = (int)strlen(nbuf);
                for (int k = printed; k < num_width; k++) putchar(' ');
            }
            break;
        case FMT_RZ:
            /* Right-justified with leading zeros */
            {
                char fmt[32];
                snprintf(fmt, sizeof(fmt), "%%0%dld", num_width);
                snprintf(nbuf, sizeof(nbuf), fmt, n);
                fputs(nbuf, stdout);
            }
            break;
        case FMT_RN:
        default:
            /* Right-justified, no leading zeros */
            {
                char fmt[32];
                snprintf(fmt, sizeof(fmt), "%%%dld", num_width);
                snprintf(nbuf, sizeof(nbuf), fmt, n);
                fputs(nbuf, stdout);
            }
            break;
    }
    fputs(separator, stdout);
}

static int nl_stream(FILE *f, long *line_num) {
    char buf[65536];
    while (fgets(buf, sizeof(buf), f)) {
        /* Determine if line is "non-empty" (has content beyond the newline) */
        int nonempty = 0;
        for (size_t j = 0; buf[j] && buf[j] != '\n'; j++) {
            if (!isspace((unsigned char)buf[j])) { nonempty = 1; break; }
        }
        /* GNU nl considers a line non-empty if it has any char before \n,
         * including whitespace. Match that behaviour: check length > 1 or
         * buf[0] != '\n'. */
        size_t len = strlen(buf);
        int has_content = (len > 1 || (len == 1 && buf[0] != '\n'));
        (void)nonempty; /* use has_content as GNU nl does */

        int should_number = 0;
        switch (body_style) {
            case STYLE_ALL:      should_number = 1;           break;
            case STYLE_NONEMPTY: should_number = has_content; break;
            case STYLE_NONE:     should_number = 0;           break;
        }

        if (should_number) {
            print_number(*line_num);
            *line_num += increment;
        } else {
            /* Blank line with no number: emit leading spaces equal to
             * num_width + strlen(separator) to preserve column alignment. */
            int pad = num_width + (int)strlen(separator);
            for (int k = 0; k < pad; k++) putchar(' ');
        }
        fputs(buf, stdout);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[argi], "--version") == 0) { puts("nl 1.0 (Winix 1.0)"); return 0; }
        if (strcmp(argv[argi], "--") == 0)        { argi++; break; }

        if (argv[argi][0] != '-' || argv[argi][1] == '\0') break;

        /* Single-char flags — some take an argument */
        char *p = argv[argi] + 1;
        char  flag = *p++;

        /* Argument is either remainder of this token or next argv */
        char *optarg_val = (*p != '\0') ? p : NULL;

        switch (flag) {
            case 'b':
                if (!optarg_val) {
                    if (argi + 1 >= argc) {
                        fprintf(stderr, "nl: option requires an argument -- 'b'\n");
                        return 1;
                    }
                    optarg_val = argv[++argi];
                }
                if (strcmp(optarg_val, "a") == 0)      body_style = STYLE_ALL;
                else if (strcmp(optarg_val, "t") == 0) body_style = STYLE_NONEMPTY;
                else if (strcmp(optarg_val, "n") == 0) body_style = STYLE_NONE;
                else {
                    fprintf(stderr, "nl: invalid body numbering style '%s'\n", optarg_val);
                    return 1;
                }
                break;

            case 'n':
                if (!optarg_val) {
                    if (argi + 1 >= argc) {
                        fprintf(stderr, "nl: option requires an argument -- 'n'\n");
                        return 1;
                    }
                    optarg_val = argv[++argi];
                }
                if (strcmp(optarg_val, "ln") == 0)      num_fmt = FMT_LN;
                else if (strcmp(optarg_val, "rn") == 0) num_fmt = FMT_RN;
                else if (strcmp(optarg_val, "rz") == 0) num_fmt = FMT_RZ;
                else {
                    fprintf(stderr, "nl: invalid number format '%s'\n", optarg_val);
                    return 1;
                }
                break;

            case 'w':
                if (!optarg_val) {
                    if (argi + 1 >= argc) {
                        fprintf(stderr, "nl: option requires an argument -- 'w'\n");
                        return 1;
                    }
                    optarg_val = argv[++argi];
                }
                num_width = atoi(optarg_val);
                if (num_width < 1) {
                    fprintf(stderr, "nl: invalid number width '%s'\n", optarg_val);
                    return 1;
                }
                break;

            case 's':
                if (!optarg_val) {
                    if (argi + 1 >= argc) {
                        fprintf(stderr, "nl: option requires an argument -- 's'\n");
                        return 1;
                    }
                    optarg_val = argv[++argi];
                }
                strncpy(separator, optarg_val, sizeof(separator) - 1);
                separator[sizeof(separator) - 1] = '\0';
                break;

            case 'v':
                if (!optarg_val) {
                    if (argi + 1 >= argc) {
                        fprintf(stderr, "nl: option requires an argument -- 'v'\n");
                        return 1;
                    }
                    optarg_val = argv[++argi];
                }
                start_num = atol(optarg_val);
                break;

            case 'i':
                if (!optarg_val) {
                    if (argi + 1 >= argc) {
                        fprintf(stderr, "nl: option requires an argument -- 'i'\n");
                        return 1;
                    }
                    optarg_val = argv[++argi];
                }
                increment = atol(optarg_val);
                if (increment < 1) {
                    fprintf(stderr, "nl: invalid increment '%s'\n", optarg_val);
                    return 1;
                }
                break;

            default:
                fprintf(stderr, "nl: invalid option -- '%c'\n", flag);
                return 1;
        }
    }

    long line_num = start_num;
    int  ret      = 0;

    if (argi >= argc) {
        ret = nl_stream(stdin, &line_num);
    } else {
        for (int i = argi; i < argc; i++) {
            if (strcmp(argv[i], "-") == 0) {
                if (nl_stream(stdin, &line_num) != 0) ret = 1;
                continue;
            }
            FILE *f = fopen(argv[i], "r");
            if (!f) {
                fprintf(stderr, "nl: %s: %s\n", argv[i], strerror(errno));
                ret = 1;
                continue;
            }
            if (nl_stream(f, &line_num) != 0) ret = 1;
            fclose(f);
        }
    }

    return ret;
}
