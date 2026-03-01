/*
 * comm.c — Winix coreutil
 *
 * Compare two sorted files line by line.
 *
 * Usage: comm [OPTION]... FILE1 FILE2
 *
 * Output three columns:
 *   Col 1: lines only in FILE1   (no prefix)
 *   Col 2: lines only in FILE2   (one TAB prefix)
 *   Col 3: lines in both         (two TAB prefix)
 *
 * Options:
 *   -1            suppress column 1
 *   -2            suppress column 2
 *   -3            suppress column 3
 *   -i, --ignore-case     case-insensitive comparison
 *   --output-delimiter=STR  use STR instead of TAB between active columns
 *
 * '-' as filename reads stdin (only one may be stdin).
 *
 * Compile: C99, no dependencies beyond the C standard library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Limits                                                               */
/* ------------------------------------------------------------------ */

#define LINE_MAX_COMM 65536

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */

static bool suppress1    = false;  /* -1 */
static bool suppress2    = false;  /* -2 */
static bool suppress3    = false;  /* -3 */
static bool ignore_case  = false;  /* -i */
static const char *out_delim = "\t";

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/* Strip trailing \r\n in-place. */
static void strip_crlf(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r'))
        s[--len] = '\0';
}

/* Compare two lines respecting ignore_case setting. */
static int cmp_lines(const char *a, const char *b)
{
    if (ignore_case) {
        /* Case-insensitive byte-by-byte comparison */
        for (;;) {
            unsigned char ca = (unsigned char)tolower((unsigned char)*a);
            unsigned char cb = (unsigned char)tolower((unsigned char)*b);
            if (ca != cb) return (int)ca - (int)cb;
            if (ca == '\0') return 0;
            a++; b++;
        }
    }
    return strcmp(a, b);
}

/*
 * Print a line in the appropriate column, prefixed by the right number
 * of delimiters depending on which columns are active before it.
 *
 * col: 0 = col1 (FILE1 only), 1 = col2 (FILE2 only), 2 = col3 (both)
 */
static void print_col(int col, const char *line)
{
    /* Count how many active columns appear before this column. */
    int prefix = 0;
    if (col >= 1 && !suppress1) prefix++;
    if (col >= 2 && !suppress2) prefix++;

    for (int i = 0; i < prefix; i++)
        fputs(out_delim, stdout);

    puts(line);
}

/* ------------------------------------------------------------------ */
/* Usage / version                                                      */
/* ------------------------------------------------------------------ */

static void print_usage(void)
{
    puts("Usage: comm [OPTION]... FILE1 FILE2");
    puts("Compare two sorted files line by line.");
    puts("");
    puts("Output is three columns: lines only in FILE1, lines only in FILE2,");
    puts("and lines in both files.");
    puts("");
    puts("  -1                    suppress lines unique to FILE1");
    puts("  -2                    suppress lines unique to FILE2");
    puts("  -3                    suppress lines that appear in both files");
    puts("  -i, --ignore-case     case-insensitive line comparison");
    puts("  --output-delimiter=STR  separate columns with STR (default: TAB)");
    puts("  --help                display this help and exit");
    puts("  --version             output version information and exit");
    puts("");
    puts("With FILE as -, read standard input (only one file may be -).");
    puts("Both files should be sorted.");
}

static void print_version(void)
{
    puts("comm 1.0 (Winix 1.0)");
}

/* ------------------------------------------------------------------ */
/* Main merge loop                                                       */
/* ------------------------------------------------------------------ */

static int do_comm(FILE *f1, FILE *f2)
{
    char line1[LINE_MAX_COMM];
    char line2[LINE_MAX_COMM];
    bool have1 = false, have2 = false;

    /* Prime the pump */
    have1 = fgets(line1, sizeof(line1), f1) != NULL;
    if (have1) strip_crlf(line1);
    have2 = fgets(line2, sizeof(line2), f2) != NULL;
    if (have2) strip_crlf(line2);

    while (have1 || have2) {
        int cmp;

        if (!have1)       cmp =  1;   /* f1 exhausted: f2 wins */
        else if (!have2)  cmp = -1;   /* f2 exhausted: f1 wins */
        else              cmp = cmp_lines(line1, line2);

        if (cmp < 0) {
            /* line1 < line2: unique to FILE1 */
            if (!suppress1)
                print_col(0, line1);
            have1 = fgets(line1, sizeof(line1), f1) != NULL;
            if (have1) strip_crlf(line1);
        } else if (cmp > 0) {
            /* line2 < line1: unique to FILE2 */
            if (!suppress2)
                print_col(1, line2);
            have2 = fgets(line2, sizeof(line2), f2) != NULL;
            if (have2) strip_crlf(line2);
        } else {
            /* equal: appears in both */
            if (!suppress3)
                print_col(2, line1);
            have1 = fgets(line1, sizeof(line1), f1) != NULL;
            if (have1) strip_crlf(line1);
            have2 = fgets(line2, sizeof(line2), f2) != NULL;
            if (have2) strip_crlf(line2);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
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
        if (strcmp(arg, "--ignore-case") == 0) {
            ignore_case = true;
            i++;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }
        if (strncmp(arg, "--output-delimiter=", 19) == 0) {
            out_delim = arg + 19;
            i++;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            const char *p = arg + 1;
            while (*p) {
                switch (*p) {
                    case '1': suppress1   = true; break;
                    case '2': suppress2   = true; break;
                    case '3': suppress3   = true; break;
                    case 'i': ignore_case = true; break;
                    default:
                        fprintf(stderr, "comm: invalid option -- '%c'\n", *p);
                        return 1;
                }
                p++;
            }
            i++;
            continue;
        }
        break; /* first non-option */
    }

    if (argc - i < 2) {
        fprintf(stderr, "comm: missing operand\n");
        fprintf(stderr, "Try 'comm --help' for more information.\n");
        return 1;
    }

    const char *path1 = argv[i];
    const char *path2 = argv[i + 1];

    FILE *f1, *f2;
    bool  close1 = false, close2 = false;
    bool  stdin_used = false;

    if (strcmp(path1, "-") == 0) {
        f1 = stdin;
        stdin_used = true;
    } else {
        f1 = fopen(path1, "r");
        if (!f1) {
            fprintf(stderr, "comm: %s: %s\n", path1, strerror(errno));
            return 1;
        }
        close1 = true;
    }

    if (strcmp(path2, "-") == 0) {
        if (stdin_used) {
            fprintf(stderr, "comm: both files cannot be standard input\n");
            if (close1) fclose(f1);
            return 1;
        }
        f2 = stdin;
    } else {
        f2 = fopen(path2, "r");
        if (!f2) {
            fprintf(stderr, "comm: %s: %s\n", path2, strerror(errno));
            if (close1) fclose(f1);
            return 1;
        }
        close2 = true;
    }

    int ret = do_comm(f1, f2);

    if (close1) fclose(f1);
    if (close2) fclose(f2);

    return ret;
}
