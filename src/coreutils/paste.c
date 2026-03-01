/*
 * paste.c — Winix coreutil
 *
 * Merge lines of files side by side.
 *
 * Usage: paste [OPTION]... [FILE]...
 *
 * Default: read files simultaneously, join one line from each with TAB,
 *          output a row per iteration.  Stops when ALL files are exhausted.
 * -s / --serial: all lines of each file on one output row, then the next file.
 * -d LIST / --delimiters=LIST: cycle through chars in LIST as delimiters.
 *   Escape sequences in LIST: \n  \t  \\  \0 (empty — no delimiter).
 * '-' as filename reads stdin.
 *
 * Compile: C99, no dependencies beyond the C standard library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Limits                                                               */
/* ------------------------------------------------------------------ */

#define MAX_FILES  32
#define LINE_MAX   65536

/* ------------------------------------------------------------------ */
/* Delimiter list handling                                              */
/* ------------------------------------------------------------------ */

/*
 * The delimiter list is stored as an array of "slots".  Each slot is
 * either a single byte (possibly '\0' for "empty string") or one of
 * the special escape sequences.  We represent "empty" by storing '\0'
 * in the byte and setting is_empty=true.
 */

#define MAX_DELIM  256

typedef struct {
    char ch;         /* the delimiter character (may be '\0') */
    bool is_empty;   /* true → output no delimiter at this position */
    bool is_newline; /* true → delimiter is '\n' (special output) */
} DelimSlot;

static DelimSlot delim_slots[MAX_DELIM];
static int       delim_count = 1;      /* default: one TAB */

/*
 * Parse the user-supplied delimiter list string into delim_slots[].
 * Returns true on success.
 */
static bool parse_delimiters(const char *s)
{
    delim_count = 0;
    const char *p = s;
    while (*p) {
        if (delim_count >= MAX_DELIM) {
            fprintf(stderr, "paste: delimiter list too long (max %d)\n", MAX_DELIM);
            return false;
        }
        DelimSlot slot;
        slot.is_empty   = false;
        slot.is_newline = false;
        slot.ch         = '\t';

        if (*p == '\\') {
            p++;
            switch (*p) {
                case 'n':  slot.ch = '\n'; slot.is_newline = true; p++; break;
                case 't':  slot.ch = '\t'; p++; break;
                case '\\': slot.ch = '\\'; p++; break;
                case '0':  slot.ch = '\0'; slot.is_empty = true; p++; break;
                case '\0':
                    /* trailing backslash — treat as literal backslash */
                    slot.ch = '\\';
                    break;
                default:
                    /* unknown escape — keep literal char after backslash */
                    slot.ch = *p;
                    p++;
                    break;
            }
        } else {
            slot.ch = *p;
            p++;
        }
        delim_slots[delim_count++] = slot;
    }

    if (delim_count == 0) {
        /* Empty list means no delimiters at all */
        delim_slots[0].ch = '\0';
        delim_slots[0].is_empty = true;
        delim_slots[0].is_newline = false;
        delim_count = 1;
    }
    return true;
}

/*
 * Output the delimiter at cyclic position idx % delim_count.
 */
static void put_delim(int idx)
{
    int i = idx % delim_count;
    if (delim_slots[i].is_empty)
        return;
    putchar((unsigned char)delim_slots[i].ch);
}

/* ------------------------------------------------------------------ */
/* Utility: read a line, strip \r\n, return false on EOF/error          */
/* ------------------------------------------------------------------ */

static bool read_line(FILE *f, char *buf, int bufsz)
{
    if (!fgets(buf, bufsz, f))
        return false;
    /* Strip trailing \r and \n */
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
        buf[--len] = '\0';
    return true;
}

/* ------------------------------------------------------------------ */
/* Normal (parallel) mode                                               */
/* ------------------------------------------------------------------ */

static int do_parallel(FILE *files[], int nfiles)
{
    char buf[LINE_MAX];
    bool any_open = true;
    bool file_open[MAX_FILES];
    for (int i = 0; i < nfiles; i++) file_open[i] = true;

    while (any_open) {
        /* Try to read one line from each file */
        bool got_any = false;
        for (int i = 0; i < nfiles; i++) {
            if (i > 0)
                put_delim(i - 1);   /* delimiter between columns */

            if (file_open[i] && read_line(files[i], buf, sizeof(buf))) {
                fputs(buf, stdout);
                got_any = true;
            } else {
                file_open[i] = false;
                /* output empty field */
            }
        }
        putchar('\n');

        /* Stop when all files are exhausted */
        any_open = false;
        for (int i = 0; i < nfiles; i++) {
            if (file_open[i]) { any_open = true; break; }
        }
        if (!got_any)
            break;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Serial mode                                                           */
/* ------------------------------------------------------------------ */

static int do_serial(FILE *files[], int nfiles)
{
    char buf[LINE_MAX];
    for (int i = 0; i < nfiles; i++) {
        bool first = true;
        int  delim_idx = 0;
        while (read_line(files[i], buf, sizeof(buf))) {
            if (!first)
                put_delim(delim_idx++);
            fputs(buf, stdout);
            first = false;
        }
        if (!first)
            putchar('\n');
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Usage / version                                                      */
/* ------------------------------------------------------------------ */

static void print_usage(void)
{
    puts("Usage: paste [OPTION]... [FILE]...");
    puts("Merge lines of files side by side.");
    puts("");
    puts("  -d LIST, --delimiters=LIST   use chars from LIST cyclically as delimiters");
    puts("                               (default: TAB)");
    puts("                               Escapes in LIST: \\n \\t \\\\ \\0 (empty)");
    puts("  -s, --serial                 paste one file at a time");
    puts("  --help                       display this help and exit");
    puts("  --version                    output version information and exit");
    puts("");
    puts("With no FILE, or when FILE is -, read standard input.");
}

static void print_version(void)
{
    puts("paste 1.0 (Winix 1.0)");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    bool serial      = false;
    bool delim_set   = false;

    /* Default delimiter: TAB */
    delim_slots[0].ch = '\t';
    delim_slots[0].is_empty = false;
    delim_slots[0].is_newline = false;
    delim_count = 1;

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
        if (strcmp(arg, "--serial") == 0) {
            serial = true;
            i++;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }
        /* --delimiters=LIST */
        if (strncmp(arg, "--delimiters=", 13) == 0) {
            if (!parse_delimiters(arg + 13)) return 1;
            delim_set = true;
            i++;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            const char *p = arg + 1;
            bool stop = false;
            while (*p && !stop) {
                char opt = *p;
                if (opt == 's') {
                    serial = true;
                    p++;
                } else if (opt == 'd') {
                    /* -d LIST — LIST may be attached or next arg */
                    const char *list = p + 1;
                    if (*list == '\0') {
                        i++;
                        if (i >= argc) {
                            fprintf(stderr, "paste: option requires an argument -- 'd'\n");
                            return 1;
                        }
                        list = argv[i];
                    }
                    if (!parse_delimiters(list)) return 1;
                    delim_set = true;
                    stop = true;  /* consumed rest of token */
                } else {
                    fprintf(stderr, "paste: invalid option -- '%c'\n", opt);
                    return 1;
                }
            }
            i++;
            continue;
        }
        /* Non-option: first file arg — stop option parsing */
        break;
    }
    (void)delim_set; /* currently only used for correctness, not further logic */

    /* Collect file arguments */
    int   nfiles = 0;
    FILE *files[MAX_FILES];
    bool  opened[MAX_FILES];   /* true if we fopen'd it (need fclose) */
    bool  stdin_used = false;

    if (i >= argc) {
        /* No file args: use stdin */
        files[0]  = stdin;
        opened[0] = false;
        nfiles    = 1;
    } else {
        for (int j = i; j < argc && nfiles < MAX_FILES; j++) {
            const char *path = argv[j];
            if (strcmp(path, "-") == 0) {
                if (stdin_used) {
                    fprintf(stderr, "paste: stdin (-) cannot be used more than once\n");
                    return 1;
                }
                files[nfiles]  = stdin;
                opened[nfiles] = false;
                stdin_used     = true;
            } else {
                files[nfiles] = fopen(path, "r");
                if (!files[nfiles]) {
                    fprintf(stderr, "paste: %s: %s\n", path, strerror(errno));
                    /* Close already-opened files */
                    for (int k = 0; k < nfiles; k++)
                        if (opened[k]) fclose(files[k]);
                    return 1;
                }
                opened[nfiles] = true;
            }
            nfiles++;
        }
        if (argc - i > MAX_FILES) {
            fprintf(stderr, "paste: too many files (max %d)\n", MAX_FILES);
            return 1;
        }
    }

    int ret;
    if (serial)
        ret = do_serial(files, nfiles);
    else
        ret = do_parallel(files, nfiles);

    for (int j = 0; j < nfiles; j++)
        if (opened[j]) fclose(files[j]);

    return ret;
}
