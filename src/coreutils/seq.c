/*
 * seq.c — Winix coreutil
 *
 * Usage: seq [OPTION]... LAST
 *        seq [OPTION]... FIRST LAST
 *        seq [OPTION]... FIRST INCREMENT LAST
 *
 * Print numbers from FIRST to LAST (inclusive) with step INCREMENT.
 * Default FIRST=1, INCREMENT=1.
 *
 * Options:
 *   -s STRING / --separator=STRING  separator between numbers (default \n)
 *   -w        / --equal-width       pad with leading zeros to equal width
 *   -f FORMAT / --format=FORMAT     printf format string (default %g or %d)
 *   --help     Print usage and exit 0
 *   --version  Print version and exit 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>

static const char *separator = "\n";
static int equal_width = 0;
static const char *user_format = NULL;

static void usage(void) {
    puts("Usage: seq [OPTION]... LAST");
    puts("       seq [OPTION]... FIRST LAST");
    puts("       seq [OPTION]... FIRST INCREMENT LAST");
    puts("Print numbers from FIRST to LAST by INCREMENT.");
    puts("");
    puts("  -s STRING, --separator=STRING  separator between numbers (default newline)");
    puts("  -w, --equal-width              pad numbers with leading zeros to equal width");
    puts("  -f FORMAT, --format=FORMAT     use printf FORMAT for each number");
    puts("  --help                         display this help and exit");
    puts("  --version                      output version information and exit");
}

/* Return 1 if string looks like an integer (optional sign, then digits only) */
static int is_integer_str(const char *s) {
    if (*s == '+' || *s == '-') s++;
    if (*s == '\0') return 0;
    while (*s) {
        if (!isdigit((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

/* Count decimal places in a numeric string */
static int decimal_places(const char *s) {
    const char *dot = strchr(s, '.');
    if (!dot) return 0;
    dot++; /* skip '.' */
    int count = 0;
    while (isdigit((unsigned char)*dot)) { count++; dot++; }
    return count;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    /* Parse options */
    while (argi < argc) {
        if (strcmp(argv[argi], "--help") == 0) { usage(); return 0; }
        if (strcmp(argv[argi], "--version") == 0) { puts("seq 1.0 (Winix 1.0)"); return 0; }
        if (strcmp(argv[argi], "--") == 0) { argi++; break; }

        /* --separator=STRING */
        if (strncmp(argv[argi], "--separator=", 12) == 0) {
            separator = argv[argi] + 12;
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--separator") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "seq: option requires an argument -- '--separator'\n");
                return 1;
            }
            separator = argv[++argi];
            argi++;
            continue;
        }

        /* --equal-width */
        if (strcmp(argv[argi], "--equal-width") == 0) {
            equal_width = 1;
            argi++;
            continue;
        }

        /* --format=FORMAT */
        if (strncmp(argv[argi], "--format=", 9) == 0) {
            user_format = argv[argi] + 9;
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--format") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "seq: option requires an argument -- '--format'\n");
                return 1;
            }
            user_format = argv[++argi];
            argi++;
            continue;
        }

        if (argv[argi][0] == '-' && argv[argi][1] != '\0') {
            /* Short options — may be combined or have argument stuck on */
            char *p = argv[argi] + 1;
            int done = 0;
            while (*p && !done) {
                switch (*p) {
                    case 'w':
                        equal_width = 1;
                        p++;
                        break;
                    case 's':
                        p++;
                        if (*p != '\0') {
                            separator = p;
                        } else if (argi + 1 < argc) {
                            separator = argv[++argi];
                        } else {
                            fprintf(stderr, "seq: option requires an argument -- 's'\n");
                            return 1;
                        }
                        done = 1;
                        break;
                    case 'f':
                        p++;
                        if (*p != '\0') {
                            user_format = p;
                        } else if (argi + 1 < argc) {
                            user_format = argv[++argi];
                        } else {
                            fprintf(stderr, "seq: option requires an argument -- 'f'\n");
                            return 1;
                        }
                        done = 1;
                        break;
                    default:
                        fprintf(stderr, "seq: invalid option -- '%c'\n", *p);
                        return 1;
                }
            }
            argi++;
            continue;
        }

        break; /* non-option argument */
    }

    int nargs = argc - argi;
    if (nargs < 1 || nargs > 3) {
        fprintf(stderr, "seq: invalid number of arguments\n");
        fprintf(stderr, "Try 'seq --help' for more information.\n");
        return 1;
    }

    const char *s_first = (nargs >= 2) ? argv[argi]     : "1";
    const char *s_incr  = (nargs == 3) ? argv[argi + 1] : "1";
    const char *s_last  = argv[argi + nargs - 1];

    double first = strtod(s_first, NULL);
    double incr  = strtod(s_incr,  NULL);
    double last  = strtod(s_last,  NULL);

    if (incr == 0.0) {
        fprintf(stderr, "seq: increment must not be zero\n");
        return 1;
    }

    /* Determine if all inputs are integers (no decimal points) */
    int all_int = is_integer_str(s_first) && is_integer_str(s_incr) && is_integer_str(s_last);

    /* Determine decimal precision needed */
    int prec = 0;
    if (!all_int) {
        int p1 = decimal_places(s_first);
        int p2 = decimal_places(s_incr);
        int p3 = decimal_places(s_last);
        prec = p1 > p2 ? p1 : p2;
        if (p3 > prec) prec = p3;
    }

    /* Choose format string */
    char auto_format[32];
    if (user_format) {
        /* use as-is */
    } else if (all_int) {
        strcpy(auto_format, "%ld");
    } else if (prec > 0) {
        snprintf(auto_format, sizeof(auto_format), "%%.%df", prec);
    } else {
        strcpy(auto_format, "%g");
    }
    const char *fmt = user_format ? user_format : auto_format;

    /* Collect all values to determine equal-width padding */
    /* We need the widest formatted value; do a dry run if -w is set */
    int pad_width = 0;
    if (equal_width && !user_format) {
        /* Format first, last, and a sample to find widest */
        char tmp[128];
        double v;
        /* Check all values — but for large sequences just check endpoints
         * and the value closest to zero (which may have fewer digits) */
        double candidates[4];
        int nc = 0;
        candidates[nc++] = first;
        candidates[nc++] = last;
        /* Also the nearest value to zero inside the range */
        if (incr > 0 && first < 0 && last >= 0) {
            candidates[nc++] = 0.0;
        } else if (incr < 0 && first > 0 && last <= 0) {
            candidates[nc++] = 0.0;
        }
        for (int ci = 0; ci < nc; ci++) {
            v = candidates[ci];
            if (all_int)
                snprintf(tmp, sizeof(tmp), "%ld", (long)v);
            else
                snprintf(tmp, sizeof(tmp), fmt, v);
            int w = (int)strlen(tmp);
            if (w > pad_width) pad_width = w;
        }
    }

    /* Build zero-padded format for equal-width integer mode */
    char zpad_format[64];
    if (equal_width && all_int && pad_width > 0 && !user_format) {
        /* Count leading '-' for negative numbers */
        int sign_width = (first < 0 || last < 0) ? 1 : 0;
        snprintf(zpad_format, sizeof(zpad_format), "%%0%dld", pad_width);
        (void)sign_width;
    }

    /* Generate and print the sequence */
    int first_val = 1; /* flag for separator logic */

    /* Use a loop counter to avoid float accumulation error */
    /* Compute count: how many values to emit */
    /* For positive incr: while val <= last; for negative: while val >= last */
    long count = 0;
    if (incr > 0) {
        if (first > last) count = 0;
        else count = (long)floor((last - first) / incr + 1.0 + 1e-10);
    } else {
        if (first < last) count = 0;
        else count = (long)floor((first - last) / (-incr) + 1.0 + 1e-10);
    }

    for (long i = 0; i < count; i++) {
        double val = first + (double)i * incr;

        /* Print separator before each value except the first */
        if (!first_val) {
            fputs(separator, stdout);
        }
        first_val = 0;

        char buf[256];
        if (equal_width && all_int && pad_width > 0 && !user_format) {
            long ival = (long)round(val);
            snprintf(buf, sizeof(buf), zpad_format, ival);
        } else if (all_int && !user_format) {
            long ival = (long)round(val);
            snprintf(buf, sizeof(buf), "%ld", ival);
        } else {
            snprintf(buf, sizeof(buf), fmt, val);
            if (equal_width && pad_width > 0) {
                /* For float equal-width: right-pad or left-pad with zeros is
                 * ambiguous; pad on the left with spaces to match GNU behavior */
                int len = (int)strlen(buf);
                if (len < pad_width) {
                    /* Print leading spaces */
                    for (int sp = 0; sp < pad_width - len; sp++) putchar(' ');
                }
            }
        }
        fputs(buf, stdout);
    }

    /* Final newline (only if we printed something) */
    if (!first_val) {
        putchar('\n');
    }

    return 0;
}
