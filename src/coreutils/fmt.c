/*
 * fmt — reformat paragraph text
 *
 * Usage: fmt [-w WIDTH] [-s] [-u] [FILE ...]
 *   -w WIDTH   target line width (default 75)
 *   -s         split long lines only, never join
 *   -u         uniform spacing: one space between words, two after sentence-end
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define VERSION "1.0"
#define MAX_WORD 4096

static int g_width   = 75;
static int g_split   = 0;
static int g_uniform = 0;

/* ── Sentence-end detection ──────────────────────────────────── */

static int is_sentence_end(const char *word) {
    int len = (int)strlen(word);
    if (len < 1) return 0;
    char last = word[len - 1];
    if (last == '.' || last == '!' || last == '?') return 1;
    /* ending with ." or .) etc. */
    if ((last == '"' || last == ')' || last == '\'') && len >= 2) {
        char prev = word[len - 2];
        if (prev == '.' || prev == '!' || prev == '?') return 1;
    }
    return 0;
}

/* ── Format one paragraph ────────────────────────────────────── */

static char  g_words[65536][MAX_WORD];
static int   g_nwords = 0;
static char  g_indent[MAX_WORD];  /* leading whitespace of first line */

static void flush_paragraph(void) {
    if (g_nwords == 0) return;

    int col = 0;
    int first = 1;

    /* Emit indent before first word */
    int indent_len = (int)strlen(g_indent);

    for (int i = 0; i < g_nwords; i++) {
        const char *w = g_words[i];
        int wlen = (int)strlen(w);

        /* Determine spacing before this word */
        int space = 0;
        if (!first) {
            space = 1;
            if (g_uniform && i > 0 && is_sentence_end(g_words[i-1])) space = 2;
        }

        if (first) {
            /* First word: emit indent + word */
            fputs(g_indent, stdout);
            fputs(w, stdout);
            col = indent_len + wlen;
            first = 0;
        } else if (g_split) {
            /* -s mode: never join lines, just wrap at width */
            if (col + space + wlen > g_width) {
                putchar('\n');
                fputs(g_indent, stdout);
                fputs(w, stdout);
                col = indent_len + wlen;
            } else {
                for (int s = 0; s < space; s++) putchar(' ');
                fputs(w, stdout);
                col += space + wlen;
            }
        } else {
            /* Normal: wrap if needed */
            if (col + space + wlen > g_width && col > indent_len) {
                putchar('\n');
                fputs(g_indent, stdout);
                fputs(w, stdout);
                col = indent_len + wlen;
            } else {
                for (int s = 0; s < space; s++) putchar(' ');
                fputs(w, stdout);
                col += space + wlen;
            }
        }
    }
    putchar('\n');
    g_nwords = 0;
    g_indent[0] = '\0';
}

static void process_file(FILE *fp) {
    char line[65536];
    int in_para = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Blank line = paragraph break */
        int all_blank = 1;
        for (int i = 0; i < len; i++) {
            if (!isspace((unsigned char)line[i])) { all_blank = 0; break; }
        }
        if (all_blank) {
            flush_paragraph();
            putchar('\n');
            in_para = 0;
            continue;
        }

        /* Detect indentation for first line of paragraph */
        if (!in_para) {
            int ii = 0;
            while (line[ii] && isspace((unsigned char)line[ii])) ii++;
            strncpy(g_indent, line, (size_t)ii);
            g_indent[ii] = '\0';
            in_para = 1;
        }

        /* Tokenize words */
        char *p = line;
        /* skip leading whitespace */
        while (*p && isspace((unsigned char)*p)) p++;

        while (*p) {
            char *start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            int wlen = (int)(p - start);
            if (wlen > 0 && g_nwords < (int)(sizeof(g_words)/sizeof(g_words[0]))) {
                strncpy(g_words[g_nwords], start, (size_t)(wlen < MAX_WORD-1 ? wlen : MAX_WORD-1));
                g_words[g_nwords][wlen < MAX_WORD-1 ? wlen : MAX_WORD-1] = '\0';
                g_nwords++;
            }
            while (*p && isspace((unsigned char)*p)) p++;
        }
    }
    flush_paragraph();
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("fmt %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: fmt [-w WIDTH] [-s] [-u] [FILE ...]\n\n"
                "Reformat paragraph text to fit within WIDTH columns.\n\n"
                "  -w N   target line width (default 75)\n"
                "  -s     split long lines only, don't join short ones\n"
                "  -u     uniform spacing (2 spaces after sentence end)\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }
        if (!strncmp(a, "-w", 2)) {
            const char *val = a[2] ? a+2 : (++argi < argc ? argv[argi] : NULL);
            if (!val) { fprintf(stderr, "fmt: option requires an argument -- 'w'\n"); return 1; }
            g_width = atoi(val);
            if (g_width < 1) g_width = 75;
            continue;
        }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 's': g_split   = 1; break;
                case 'u': g_uniform = 1; break;
                case 'w': {
                    const char *val = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!val) { fprintf(stderr, "fmt: option requires an argument -- 'w'\n"); return 1; }
                    g_width = atoi(val);
                    if (g_width < 1) g_width = 75;
                    p = val + strlen(val) - 1; /* skip rest */
                    break;
                }
                default:
                    fprintf(stderr, "fmt: invalid option -- '%c'\n", *p); return 1;
            }
        }
    }

    if (argi >= argc) {
        process_file(stdin);
    } else {
        for (int i = argi; i < argc; i++) {
            if (!strcmp(argv[i], "-")) {
                process_file(stdin);
            } else {
                FILE *fp = fopen(argv[i], "r");
                if (!fp) { perror(argv[i]); return 1; }
                process_file(fp);
                fclose(fp);
            }
        }
    }
    return 0;
}
