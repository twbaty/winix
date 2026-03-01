/*
 * diff.c — line-by-line file comparison for Winix
 *
 * Usage: diff [OPTION]... FILE1 FILE2
 *
 * Exit codes: 0 = files identical, 1 = files differ, 2 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define MAX_LINES   4000
#define LINE_BUF    8192

/* Edit op codes stored in the backtracked script */
#define OP_KEEP     0
#define OP_DELETE   1
#define OP_INSERT   2

/* ------------------------------------------------------------------ */
/* Global options                                                       */
/* ------------------------------------------------------------------ */

static bool opt_unified   = false;   /* -u / -U N  */
static int  ctx_lines     = 3;       /* context lines for unified */
static bool opt_ignore_case       = false;   /* -i */
static bool opt_ignore_all_space  = false;   /* -w */
static bool opt_ignore_space_change = false; /* -b */
static bool opt_brief     = false;   /* -q */

/* ------------------------------------------------------------------ */
/* Line comparison helpers                                              */
/* ------------------------------------------------------------------ */

/* Copy src into dst, collapsing/stripping whitespace according to flags.
 * dst must be at least strlen(src)+1 bytes.  Returns dst. */
static char *normalize_ws(char *dst, const char *src) {
    const char *s = src;
    char *d = dst;

    if (opt_ignore_all_space) {
        /* Remove every whitespace character */
        while (*s) {
            if (!isspace((unsigned char)*s))
                *d++ = *s;
            s++;
        }
    } else if (opt_ignore_space_change) {
        /* Collapse runs of whitespace to a single space; trim leading/trailing */
        bool in_space = true;   /* start true to trim leading */
        while (*s) {
            if (isspace((unsigned char)*s)) {
                if (!in_space) {
                    *d++ = ' ';
                    in_space = true;
                }
            } else {
                *d++ = *s;
                in_space = false;
            }
            s++;
        }
        /* trim trailing space we may have added */
        if (d > dst && *(d - 1) == ' ')
            d--;
    } else {
        /* No whitespace normalization — plain copy */
        while (*s) *d++ = *s++;
    }
    *d = '\0';
    return dst;
}

/* Case-insensitive strcmp (portable, no _stricmp dependency) */
static int ci_strcmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* Compare two lines according to current flags.  Uses original strings but
 * builds normalized scratch copies on the stack when needed. */
static bool lines_equal(const char *a, const char *b) {
    if (!opt_ignore_case && !opt_ignore_all_space && !opt_ignore_space_change)
        return strcmp(a, b) == 0;

    /* Build normalized copies */
    char na[LINE_BUF], nb[LINE_BUF];
    normalize_ws(na, a);
    normalize_ws(nb, b);

    if (opt_ignore_case)
        return ci_strcmp(na, nb) == 0;
    return strcmp(na, nb) == 0;
}

/* ------------------------------------------------------------------ */
/* File reading                                                         */
/* ------------------------------------------------------------------ */

static char **read_file(const char *path, int *count) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "diff: cannot open '%s': ", path);
        perror(NULL);
        return NULL;
    }

    char **lines = malloc((MAX_LINES + 1) * sizeof(char *));
    if (!lines) {
        fprintf(stderr, "diff: out of memory\n");
        fclose(fp);
        return NULL;
    }

    char buf[LINE_BUF];
    int n = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        if (n >= MAX_LINES) {
            fprintf(stderr, "diff: '%s': file exceeds %d line limit\n", path, MAX_LINES);
            for (int i = 0; i < n; i++) free(lines[i]);
            free(lines);
            fclose(fp);
            return NULL;
        }
        /* Strip trailing newline */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
        if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';

        lines[n] = malloc(len + 1);
        if (!lines[n]) {
            fprintf(stderr, "diff: out of memory\n");
            for (int i = 0; i < n; i++) free(lines[i]);
            free(lines);
            fclose(fp);
            return NULL;
        }
        memcpy(lines[n], buf, len + 1);
        n++;
    }
    fclose(fp);
    *count = n;
    return lines;
}

static void free_lines(char **lines, int n) {
    if (!lines) return;
    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);
}

/* ------------------------------------------------------------------ */
/* LCS dynamic programming                                              */
/* ------------------------------------------------------------------ */

/* dp[i][j] = LCS length of a[0..i-1] vs b[0..j-1].
 * Stored row-major: index = i*(n+1)+j */
static unsigned short *build_dp(char **a, int m, char **b, int n) {
    unsigned short *dp = calloc((size_t)(m + 1) * (n + 1), sizeof(unsigned short));
    if (!dp) {
        fprintf(stderr, "diff: out of memory for DP table\n");
        return NULL;
    }
    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            if (lines_equal(a[i - 1], b[j - 1]))
                dp[i * (n + 1) + j] = dp[(i - 1) * (n + 1) + (j - 1)] + 1;
            else {
                unsigned short up   = dp[(i - 1) * (n + 1) + j];
                unsigned short left = dp[i * (n + 1) + (j - 1)];
                dp[i * (n + 1) + j] = up > left ? up : left;
            }
        }
    }
    return dp;
}

/* ------------------------------------------------------------------ */
/* Edit script                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    int  op;    /* OP_KEEP, OP_DELETE, OP_INSERT */
    int  ai;    /* 0-based index into a (valid for KEEP/DELETE) */
    int  bi;    /* 0-based index into b (valid for KEEP/INSERT) */
} Edit;

/* Backtrack dp to produce edit list.  Returns malloc'd array; *nops set. */
static Edit *backtrack(unsigned short *dp, char **a, int m, char **b, int n, int *nops) {
    /* Worst case: m+n ops */
    Edit *ops = malloc((size_t)(m + n + 1) * sizeof(Edit));
    if (!ops) {
        fprintf(stderr, "diff: out of memory for edit script\n");
        return NULL;
    }
    int cnt = 0;
    int i = m, j = n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && lines_equal(a[i - 1], b[j - 1])) {
            ops[cnt++] = (Edit){OP_KEEP, i - 1, j - 1};
            i--; j--;
        } else if (j > 0 && (i == 0 || dp[i * (n + 1) + (j - 1)] >= dp[(i - 1) * (n + 1) + j])) {
            ops[cnt++] = (Edit){OP_INSERT, -1, j - 1};
            j--;
        } else {
            ops[cnt++] = (Edit){OP_DELETE, i - 1, -1};
            i--;
        }
    }
    /* Reverse so ops are in forward order */
    for (int lo = 0, hi = cnt - 1; lo < hi; lo++, hi--) {
        Edit tmp = ops[lo]; ops[lo] = ops[hi]; ops[hi] = tmp;
    }
    *nops = cnt;
    return ops;
}

/* ------------------------------------------------------------------ */
/* Timestamp helper for unified header                                  */
/* ------------------------------------------------------------------ */

static void format_mtime(const char *path, char *buf, size_t bufsz) {
    struct stat st;
    if (stat(path, &st) != 0) {
        strncpy(buf, "unknown", bufsz - 1);
        buf[bufsz - 1] = '\0';
        return;
    }
    time_t t = st.st_mtime;
    struct tm *tm = localtime(&t);
    if (!tm) {
        strncpy(buf, "unknown", bufsz - 1);
        buf[bufsz - 1] = '\0';
        return;
    }
    /* Format: YYYY-MM-DD HH:MM:SS.000000000 +0000 */
    strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S.000000000 +0000", tm);
}

/* ------------------------------------------------------------------ */
/* Normal diff output                                                   */
/* ------------------------------------------------------------------ */

/* Print range: if start==end print single number, else "start,end" */
static void print_range(int start, int end) {
    if (start == end)
        printf("%d", start);
    else
        printf("%d,%d", start, end);
}

/* Emit one normal-format hunk.
 *   del_start/del_end : 1-based line numbers in file1 (0 if no deletions)
 *   ins_start/ins_end : 1-based line numbers in file2 (0 if no insertions)
 *   kind              : 'c', 'd', or 'a'
 */
static void emit_normal_hunk(char **a, char **b,
                              int del_start, int del_end,
                              int ins_start, int ins_end,
                              char kind) {
    /* Header line */
    if (kind == 'd') {
        print_range(del_start, del_end);
        printf("d%d\n", ins_start);          /* lines deleted: after line ins_start in b */
    } else if (kind == 'a') {
        printf("%d", del_start);             /* after line del_start in a */
        printf("a");
        print_range(ins_start, ins_end);
        printf("\n");
    } else {
        /* 'c' */
        print_range(del_start, del_end);
        printf("c");
        print_range(ins_start, ins_end);
        printf("\n");
    }

    /* Deleted lines */
    if (kind == 'd' || kind == 'c') {
        for (int i = del_start - 1; i < del_end; i++)
            printf("< %s\n", a[i]);
    }

    /* Separator for change hunks */
    if (kind == 'c')
        printf("---\n");

    /* Inserted lines */
    if (kind == 'a' || kind == 'c') {
        for (int j = ins_start - 1; j < ins_end; j++)
            printf("> %s\n", b[j]);
    }
}

/* Produce normal diff output from the edit script */
static void output_normal(Edit *ops, int nops, char **a, char **b) {
    /* Scan for runs of DELETE/INSERT (contiguous, ignoring KEEPs between
     * them only when both sides have edits — classic diff groups adjacent
     * changes separated by no KEEPs into one hunk). */

    int i = 0;
    while (i < nops) {
        if (ops[i].op == OP_KEEP) { i++; continue; }

        /* Start of a change region */
        int start = i;

        /* Collect all consecutive non-KEEP ops.  In a standard diff edit
         * script from LCS backtracking there are no KEEPs interleaved inside
         * a single change cluster, but we guard anyway. */
        while (i < nops && ops[i].op != OP_KEEP) i++;
        int end = i; /* exclusive */

        /* Gather file1 deleted range and file2 inserted range */
        int del_start = 0, del_end = 0, ins_start = 0, ins_end = 0;
        bool has_del = false, has_ins = false;

        for (int k = start; k < end; k++) {
            if (ops[k].op == OP_DELETE) {
                int ln = ops[k].ai + 1; /* 1-based */
                if (!has_del) { del_start = del_end = ln; has_del = true; }
                else { if (ln < del_start) del_start = ln; if (ln > del_end) del_end = ln; }
            } else if (ops[k].op == OP_INSERT) {
                int ln = ops[k].bi + 1;
                if (!has_ins) { ins_start = ins_end = ln; has_ins = true; }
                else { if (ln < ins_start) ins_start = ln; if (ln > ins_end) ins_end = ln; }
            }
        }

        /* Determine the "after" position for pure adds/deletes */
        char kind;
        if (has_del && has_ins) {
            kind = 'c';
        } else if (has_del) {
            kind = 'd';
            /* ins_start = last b line before the deletion */
            ins_start = (start > 0) ? ops[start - 1].bi + 1 : 0;
        } else {
            kind = 'a';
            /* del_start = last a line before the insertion */
            del_start = (start > 0) ? ops[start - 1].ai + 1 : 0;
        }

        emit_normal_hunk(a, b, del_start, del_end, ins_start, ins_end, kind);
    }
}

/* ------------------------------------------------------------------ */
/* Unified diff output                                                  */
/* ------------------------------------------------------------------ */

static void output_unified(Edit *ops, int nops, char **a, char **b,
                            const char *file1, const char *file2) {
    /* Print header */
    char ts1[64], ts2[64];
    format_mtime(file1, ts1, sizeof(ts1));
    format_mtime(file2, ts2, sizeof(ts2));
    printf("--- %s\t%s\n", file1, ts1);
    printf("+++ %s\t%s\n", file2, ts2);

    /* Walk ops, grouping into hunks with ctx_lines context on each side */
    int i = 0;
    while (i < nops) {
        /* Skip KEEPs until we find a change */
        if (ops[i].op == OP_KEEP) { i++; continue; }

        /* Found the start of a change.  Hunk begins ctx_lines before it. */
        int hunk_start = i;

        /* Find the end of this hunk: keep extending as long as there is a
         * change within ctx_lines of the current end. */
        int hunk_end = i; /* exclusive, updated as we scan */
        {
            int j = i;
            while (j < nops) {
                if (ops[j].op != OP_KEEP) {
                    hunk_end = j + 1;
                    j++;
                } else {
                    /* Count consecutive KEEPs */
                    int kstart = j;
                    while (j < nops && ops[j].op == OP_KEEP) j++;
                    int klen = j - kstart;
                    if (klen > 2 * ctx_lines) {
                        /* Gap is large enough to split into separate hunks */
                        break;
                    }
                    /* Not large enough — absorb into current hunk */
                    hunk_end = j;
                }
            }
        }

        /* Determine the actual slice we will print:
         * ctx_lines of KEEP before the first change, ctx_lines after the last. */
        int print_start = hunk_start;
        int keep_before = 0;
        while (print_start > 0 && keep_before < ctx_lines
               && ops[print_start - 1].op == OP_KEEP) {
            print_start--;
            keep_before++;
        }

        int print_end = hunk_end;
        int keep_after = 0;
        while (print_end < nops && keep_after < ctx_lines
               && ops[print_end].op == OP_KEEP) {
            print_end++;
            keep_after++;
        }

        /* Compute file1 (a) and file2 (b) start lines and lengths */
        int a_start = -1, a_len = 0;
        int b_start = -1, b_len = 0;
        for (int k = print_start; k < print_end; k++) {
            if (ops[k].op == OP_KEEP || ops[k].op == OP_DELETE) {
                if (a_start < 0) a_start = ops[k].ai;
                a_len++;
            }
            if (ops[k].op == OP_KEEP || ops[k].op == OP_INSERT) {
                if (b_start < 0) b_start = ops[k].bi;
                b_len++;
            }
        }
        if (a_start < 0) {
            /* No a lines at all — find position just before */
            for (int k = print_start - 1; k >= 0; k--) {
                if (ops[k].ai >= 0) { a_start = ops[k].ai + 1; break; }
            }
            if (a_start < 0) a_start = 0;
        }
        if (b_start < 0) {
            for (int k = print_start - 1; k >= 0; k--) {
                if (ops[k].bi >= 0) { b_start = ops[k].bi + 1; break; }
            }
            if (b_start < 0) b_start = 0;
        }

        /* Print hunk header: @@ -a_start+1,a_len +b_start+1,b_len @@ */
        printf("@@ -%d", a_start + 1);
        if (a_len != 1) printf(",%d", a_len);
        printf(" +%d", b_start + 1);
        if (b_len != 1) printf(",%d", b_len);
        printf(" @@\n");

        /* Print hunk body */
        for (int k = print_start; k < print_end; k++) {
            if (ops[k].op == OP_KEEP)
                printf(" %s\n", a[ops[k].ai]);
            else if (ops[k].op == OP_DELETE)
                printf("-%s\n", a[ops[k].ai]);
            else
                printf("+%s\n", b[ops[k].bi]);
        }

        i = print_end;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    int argi = 1;

    /* Parse options */
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (strcmp(argv[argi], "--") == 0) { argi++; break; }

        if (strcmp(argv[argi], "--version") == 0) {
            printf("diff 1.0 (Winix 1.0)\n");
            return 0;
        }
        if (strcmp(argv[argi], "--help") == 0) {
            printf("Usage: diff [OPTION]... FILE1 FILE2\n"
                   "Compare files line by line.\n\n"
                   "  -q          report only whether files differ\n"
                   "  -u          output unified diff (3 lines context)\n"
                   "  -U N        output unified diff with N lines context\n"
                   "  -i          ignore case differences\n"
                   "  -w          ignore all whitespace\n"
                   "  -b          ignore changes in whitespace amount\n"
                   "  --help      display this help and exit\n"
                   "  --version   output version information and exit\n\n"
                   "Exit status: 0 if identical, 1 if different, 2 if trouble.\n");
            return 0;
        }

        /* -U N: unified with explicit context count */
        if (strcmp(argv[argi], "-U") == 0) {
            opt_unified = true;
            argi++;
            if (argi >= argc) {
                fprintf(stderr, "diff: option '-U' requires an argument\n");
                return 2;
            }
            ctx_lines = atoi(argv[argi]);
            if (ctx_lines < 0) ctx_lines = 0;
            argi++;
            continue;
        }
        /* -UN shorthand e.g. -U5 */
        if (argv[argi][1] == 'U' && argv[argi][2] != '\0') {
            opt_unified = true;
            ctx_lines = atoi(argv[argi] + 2);
            if (ctx_lines < 0) ctx_lines = 0;
            argi++;
            continue;
        }

        /* Combined single-char flags */
        bool bad = false;
        for (const char *p = argv[argi] + 1; *p && !bad; p++) {
            switch (*p) {
                case 'u': opt_unified   = true; break;
                case 'i': opt_ignore_case          = true; break;
                case 'w': opt_ignore_all_space      = true; break;
                case 'b': opt_ignore_space_change   = true; break;
                case 'q': opt_brief     = true; break;
                default:
                    fprintf(stderr, "diff: invalid option -- '%c'\n", *p);
                    bad = true;
                    break;
            }
        }
        if (bad) return 2;
        argi++;
    }

    /* Need exactly two file arguments */
    if (argc - argi < 2) {
        fprintf(stderr, "diff: missing operand\n"
                        "Usage: diff [OPTION]... FILE1 FILE2\n");
        return 2;
    }
    if (argc - argi > 2) {
        fprintf(stderr, "diff: extra operand '%s'\n", argv[argi + 2]);
        return 2;
    }

    const char *file1 = argv[argi];
    const char *file2 = argv[argi + 1];

    /* Read files */
    int m = 0, n = 0;
    char **a = read_file(file1, &m);
    if (!a) return 2;

    char **b = read_file(file2, &n);
    if (!b) { free_lines(a, m); return 2; }

    /* Build LCS DP table */
    unsigned short *dp = build_dp(a, m, b, n);
    if (!dp) { free_lines(a, m); free_lines(b, n); return 2; }

    /* Backtrack to get edit script */
    int nops = 0;
    Edit *ops = backtrack(dp, a, m, b, n, &nops);
    free(dp);
    if (!ops) { free_lines(a, m); free_lines(b, n); return 2; }

    /* Check whether files actually differ */
    bool differ = false;
    for (int k = 0; k < nops; k++) {
        if (ops[k].op != OP_KEEP) { differ = true; break; }
    }

    if (!differ) {
        /* Files are identical */
        free(ops);
        free_lines(a, m);
        free_lines(b, n);
        return 0;
    }

    /* -q: just announce and exit */
    if (opt_brief) {
        printf("Files %s and %s differ\n", file1, file2);
        free(ops);
        free_lines(a, m);
        free_lines(b, n);
        return 1;
    }

    /* Output diff */
    if (opt_unified)
        output_unified(ops, nops, a, b, file1, file2);
    else
        output_normal(ops, nops, a, b);

    free(ops);
    free_lines(a, m);
    free_lines(b, n);
    return 1;
}
