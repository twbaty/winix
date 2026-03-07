/*
 * pr — convert text files for printing
 *
 * Usage: pr [OPTIONS] [FILE ...]
 *   +PAGE        begin output at page PAGE
 *   -N           produce N-column output
 *   -a           across: fill columns across before going down
 *   -d           double-space output
 *   -e[CHAR[N]]  expand input tabs to char (default spaces, width N, default 8)
 *   -F           use form feed for new pages instead of newlines
 *   -h STRING    use STRING as header (default: filename)
 *   -l N         page length in lines (default 66)
 *   -m           merge files side by side
 *   -n[CHAR[N]]  number lines with separator CHAR and N digits
 *   -o N         indent each line N spaces
 *   -r           skip non-existent files
 *   -s[CHAR]     use CHAR as column separator (default: tab)
 *   -t           no header or trailer
 *   -w N         page width (default 72 for multi-column)
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define VERSION "1.0"
#define DFLT_PAGE_LEN  66
#define DFLT_PAGE_WID  72
#define HEADER_LINES   5   /* 3 header + 1 blank = 4, but we count body lines */
#define TRAILER_LINES  5

static int   g_first_page = 1;
static int   g_cols       = 1;
static int   g_across     = 0;
static int   g_double     = 0;
static int   g_ff         = 0;
static char *g_header     = NULL;
static int   g_page_len   = DFLT_PAGE_LEN;
static int   g_merge      = 0;
static int   g_num        = 0;
static char  g_num_sep    = '\t';
static int   g_num_wid    = 5;
static int   g_offset     = 0;
static int   g_skip       = 0;  /* -r */
static char  g_col_sep    = '\t';
static int   g_col_sep_set= 0;
static int   g_no_header  = 0;
static int   g_page_wid   = 0;  /* 0 = use default */

static int g_page_num = 0;

static char g_datebuf[64];

static void get_date(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(g_datebuf, sizeof(g_datebuf), "%Y-%m-%d %H:%M", tm);
}

static void print_header(const char *fname) {
    if (g_no_header) return;
    printf("\n\n");
    printf("%-20s %s Page %d\n", g_datebuf, g_header ? g_header : (fname ? fname : ""), g_page_num);
    printf("\n\n");
}

static void print_trailer(void) {
    if (g_no_header) return;
    printf("\n\n\n\n\n");
}

/* ── Single-file, single-column ──────────────────────────────── */

static int pr_simple(FILE *fp, const char *fname) {
    char line[4096];
    int body_lines = g_page_len - (g_no_header ? 0 : 10); /* header+trailer = 10 */
    if (body_lines < 1) body_lines = 1;
    int lcount = 0;

    g_page_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (lcount == 0) {
            g_page_num++;
            if (g_page_num < g_first_page) {
                lcount++;
                if (lcount >= body_lines) lcount = 0;
                continue;
            }
            print_header(fname);
        }

        /* offset */
        for (int i = 0; i < g_offset; i++) putchar(' ');

        /* line number */
        if (g_num) printf("%*d%c", g_num_wid, lcount + 1, g_num_sep);

        /* strip trailing newline for double-space control */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        printf("%s\n", line);
        if (g_double) putchar('\n');

        lcount++;
        if (lcount >= body_lines) {
            print_trailer();
            if (g_ff) putchar('\f');
            lcount = 0;
        }
    }

    /* Final trailer */
    if (lcount > 0 && g_page_num >= g_first_page) {
        /* Pad remaining lines */
        if (!g_no_header) {
            while (lcount < body_lines) { putchar('\n'); lcount++; }
            print_trailer();
        }
    }
    return 0;
}

/* ── Multi-column ────────────────────────────────────────────── */

static int pr_columns(FILE *fp, const char *fname, int ncols) {
    int page_wid = g_page_wid ? g_page_wid : DFLT_PAGE_WID;
    int col_wid  = (page_wid - (ncols - 1)) / ncols; /* approx */
    int body_len = g_page_len - (g_no_header ? 0 : 10);
    if (body_len < 1) body_len = 1;
    int rows_per_page = body_len;

    /* Read all lines */
    char **lines = NULL;
    int nlines = 0, lcap = 0;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) {
        if (nlines >= lcap) {
            lcap = lcap ? lcap * 2 : 256;
            char **tmp = realloc(lines, (size_t)lcap * sizeof(char *));
            if (!tmp) { perror("pr"); exit(1); }
            lines = tmp;
        }
        int len = (int)strlen(buf);
        while (len > 0 && (buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len] = '\0';
        lines[nlines++] = strdup(buf);
    }

    g_page_num = 0;
    int li = 0;

    while (li < nlines) {
        g_page_num++;
        if (g_page_num >= g_first_page) print_header(fname);

        for (int row = 0; row < rows_per_page; row++) {
            int any = 0;
            for (int col = 0; col < ncols; col++) {
                int idx;
                if (g_across) idx = li + row * ncols + col;
                else          idx = li + col * rows_per_page + row;
                if (idx >= li + (nlines - li < rows_per_page * ncols ? nlines - li : rows_per_page * ncols)) continue;
                if (idx >= nlines) continue;

                if (any) putchar(g_col_sep_set ? g_col_sep : '\t');
                for (int i = 0; i < g_offset; i++) putchar(' ');
                if (g_num) printf("%*d%c", g_num_wid, idx + 1, g_num_sep);
                printf("%-*s", col_wid, lines[idx]);
                any = 1;
            }
            if (any) { putchar('\n'); if (g_double) putchar('\n'); }
        }

        int page_lines = (nlines - li < rows_per_page * ncols) ? nlines - li : rows_per_page * ncols;
        li += page_lines;
        if (g_page_num >= g_first_page) print_trailer();
        if (g_ff) putchar('\f');
    }

    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    get_date();

    for (; argi < argc && (argv[argi][0] == '-' || argv[argi][0] == '+') && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("pr %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: pr [OPTIONS] [FILE ...]\n\n"
                "Paginate or columnate FILE(s) for printing.\n\n"
                "  +PAGE     begin at page PAGE\n"
                "  -N        N-column output\n"
                "  -a        fill columns across then down\n"
                "  -d        double-space\n"
                "  -F        form feed between pages\n"
                "  -h STR    page header\n"
                "  -l N      page length (default 66)\n"
                "  -m        merge files side by side\n"
                "  -n        number lines\n"
                "  -o N      indent N spaces\n"
                "  -r        skip missing files\n"
                "  -s C      column separator char\n"
                "  -t        no header/trailer\n"
                "  -w N      page width (default 72)\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }

        if (*a == '+') { g_first_page = atoi(a + 1); continue; }

        /* numeric flag: -N sets column count */
        if (a[1] >= '2' && a[1] <= '9' && !a[2]) { g_cols = atoi(a + 1); continue; }

        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 'a': g_across   = 1; break;
                case 'd': g_double   = 1; break;
                case 'F': g_ff       = 1; break;
                case 't': g_no_header= 1; break;
                case 'r': g_skip     = 1; break;
                case 'm': g_merge    = 1; break;
                case 'n': g_num      = 1;
                    if (*(p+1) && !isdigit((unsigned char)*(p+1))) { g_num_sep = *(p+1); p++; }
                    if (*(p+1) && isdigit((unsigned char)*(p+1))) { g_num_wid = atoi(p+1); while(isdigit((unsigned char)*(p+1)))p++; }
                    break;
                case 'h': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "pr: option requires argument -- 'h'\n"); return 1; }
                    g_header = (char *)v; p = v + strlen(v) - 1; break;
                }
                case 'l': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "pr: option requires argument -- 'l'\n"); return 1; }
                    g_page_len = atoi(v); p = v + strlen(v) - 1; break;
                }
                case 'o': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "pr: option requires argument -- 'o'\n"); return 1; }
                    g_offset = atoi(v); p = v + strlen(v) - 1; break;
                }
                case 'w': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "pr: option requires argument -- 'w'\n"); return 1; }
                    g_page_wid = atoi(v); p = v + strlen(v) - 1; break;
                }
                case 's':
                    g_col_sep_set = 1;
                    g_col_sep = p[1] ? *(p+1) : '\t';
                    if (p[1]) p++;
                    break;
                default:
                    if (isdigit((unsigned char)*p)) { g_cols = atoi(p); while(isdigit((unsigned char)*(p+1)))p++; break; }
                    fprintf(stderr, "pr: invalid option -- '%c'\n", *p); return 1;
            }
        }
    }

    int ret = 0;

    if (argi >= argc) {
        ret = (g_cols > 1) ? pr_columns(stdin, NULL, g_cols) : pr_simple(stdin, NULL);
    } else {
        for (int i = argi; i < argc; i++) {
            FILE *fp;
            if (!strcmp(argv[i], "-")) {
                fp = stdin;
            } else {
                fp = fopen(argv[i], "r");
                if (!fp) {
                    if (g_skip) continue;
                    perror(argv[i]); ret = 1; continue;
                }
            }
            int r = (g_cols > 1) ? pr_columns(fp, argv[i], g_cols) : pr_simple(fp, argv[i]);
            ret |= r;
            if (fp != stdin) fclose(fp);
        }
    }
    return ret;
}
