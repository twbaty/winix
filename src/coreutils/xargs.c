/*
 * xargs.c — Winix coreutil
 *
 * Usage: xargs [OPTIONS] [COMMAND [INITIAL-ARGS...]]
 *
 * Options:
 *   -0, --null              Input items terminated by NUL
 *   -d C, --delimiter=C     Use C as input delimiter (\\n \\t \\0 recognised)
 *   -n N, --max-args=N      At most N arguments per invocation
 *   -L N, --max-lines=N     At most N input lines per invocation
 *   -s N, --max-chars=N     Limit command line to N characters
 *   -I STR, --replace=STR   Replace STR in COMMAND args with each input item
 *   -i                      Same as -I {}
 *   -t, --verbose           Print command to stderr before running
 *   -p, --interactive       Prompt before each invocation
 *   -r, --no-run-if-empty   Do not run if stdin is empty
 *   --help                  Print usage and exit 0
 *   --version               Print version and exit 0
 *   --                      End of options
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define MAX_ARGS       65536
#define MAX_ARG_LEN    8192
#define MAX_CMD_LEN    32768

/* ------------------------------------------------------------------ */
/* Global options                                                       */
/* ------------------------------------------------------------------ */

static bool   opt_null        = false;
static int    opt_delim       = -1;     /* -1 = not set; else delimiter char */
static int    opt_max_args    = 0;      /* 0 = unlimited */
static int    opt_max_lines   = 0;      /* 0 = unlimited */
static int    opt_max_chars   = 0;      /* 0 = unlimited */
static char   opt_replace[MAX_ARG_LEN] = "";
static bool   opt_verbose     = false;
static bool   opt_interactive = false;
static bool   opt_no_run_empty= false;

/* ------------------------------------------------------------------ */
/* Token storage                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    char *items[MAX_ARGS];
    int   line_nums[MAX_ARGS]; /* input line number each token came from */
    int   count;
} TokenList;

/* ------------------------------------------------------------------ */
/* Utility: parse a delimiter escape sequence from a string            */
/* e.g. "\\n" -> '\n', "\\t" -> '\t', "\\0" -> '\0', else first char  */
/* ------------------------------------------------------------------ */

static int parse_delim(const char *s)
{
    if (s[0] == '\\') {
        switch (s[1]) {
            case 'n': return '\n';
            case 't': return '\t';
            case '0': return '\0';
            case '\\': return '\\';
            default:  return (unsigned char)s[1];
        }
    }
    return (unsigned char)s[0];
}

/* ------------------------------------------------------------------ */
/* Input tokenization                                                   */
/* ------------------------------------------------------------------ */

/* Read all input and split into tokens.
 * Mode A (opt_null):  split on NUL bytes.
 * Mode B (opt_delim): split on the delimiter character.
 * Mode C (default):   whitespace split with quote/backslash handling.
 *
 * Returns false on allocation failure.
 */
static bool read_tokens(FILE *fp, TokenList *tl)
{
    tl->count = 0;

    /* Read entire stdin into a dynamic buffer */
    size_t buf_cap = 65536;
    size_t buf_len = 0;
    char  *buf = malloc(buf_cap);
    if (!buf) {
        fprintf(stderr, "xargs: out of memory\n");
        return false;
    }

    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (buf_len + 1 >= buf_cap) {
            buf_cap *= 2;
            char *nb = realloc(buf, buf_cap);
            if (!nb) {
                fprintf(stderr, "xargs: out of memory\n");
                free(buf);
                return false;
            }
            buf = nb;
        }
        buf[buf_len++] = (char)ch;
    }
    buf[buf_len] = '\0';

    if (opt_null) {
        /* NUL-delimited: each NUL terminates a token */
        size_t start = 0;
        int lineno = 1;
        for (size_t i = 0; i <= buf_len; i++) {
            if (buf[i] == '\0') {
                if (tl->count >= MAX_ARGS) {
                    fprintf(stderr, "xargs: too many arguments\n");
                    free(buf);
                    return false;
                }
                size_t len = i - start;
                char *tok = malloc(len + 1);
                if (!tok) {
                    fprintf(stderr, "xargs: out of memory\n");
                    free(buf);
                    return false;
                }
                memcpy(tok, buf + start, len);
                tok[len] = '\0';
                tl->items[tl->count] = tok;
                tl->line_nums[tl->count] = lineno;
                tl->count++;
                start = i + 1;
                if (start > buf_len) break;
            }
        }
        free(buf);
        return true;
    }

    if (opt_delim >= 0) {
        /* Single-char delimiter mode */
        int delim = opt_delim;
        size_t start = 0;
        int lineno = 1;
        for (size_t i = 0; i <= buf_len; i++) {
            int c = (i < buf_len) ? (unsigned char)buf[i] : delim;
            if ((unsigned char)c == (unsigned char)delim || i == buf_len) {
                size_t len = i - start;
                /* Strip trailing newline from token if delimiter is not \n */
                char *raw = buf + start;
                if (len > 0 && raw[len - 1] == '\n' && delim != '\n')
                    len--;
                if (tl->count >= MAX_ARGS) {
                    fprintf(stderr, "xargs: too many arguments\n");
                    free(buf);
                    return false;
                }
                char *tok = malloc(len + 1);
                if (!tok) {
                    fprintf(stderr, "xargs: out of memory\n");
                    free(buf);
                    return false;
                }
                memcpy(tok, raw, len);
                tok[len] = '\0';
                tl->items[tl->count] = tok;
                tl->line_nums[tl->count] = lineno;
                tl->count++;
                if (buf[i] == '\n') lineno++;
                start = i + 1;
            } else if (buf[i] == '\n') {
                lineno++;
            }
        }
        free(buf);
        return true;
    }

    /* Default whitespace tokenization with quote/backslash handling */
    size_t i = 0;
    int lineno = 1;
    char tok[MAX_ARG_LEN];

    while (i < buf_len) {
        /* Skip whitespace */
        while (i < buf_len && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r')) {
            if (buf[i] == '\n') lineno++;
            i++;
        }
        if (i >= buf_len) break;

        /* Collect one token */
        size_t tok_len = 0;
        bool in_single = false;
        bool in_double = false;

        while (i < buf_len) {
            char c = buf[i];

            if (in_single) {
                if (c == '\'') { in_single = false; i++; continue; }
                if (tok_len + 1 < MAX_ARG_LEN) tok[tok_len++] = c;
                i++;
                continue;
            }

            if (in_double) {
                if (c == '"') { in_double = false; i++; continue; }
                if (c == '\\' && i + 1 < buf_len) {
                    char next = buf[i + 1];
                    if (next == '"' || next == '\\') {
                        if (tok_len + 1 < MAX_ARG_LEN) tok[tok_len++] = next;
                        i += 2; continue;
                    }
                }
                if (tok_len + 1 < MAX_ARG_LEN) tok[tok_len++] = c;
                i++;
                continue;
            }

            /* Unquoted */
            if (c == '\'') { in_single = true; i++; continue; }
            if (c == '"')  { in_double = true; i++; continue; }
            if (c == '\\' && i + 1 < buf_len) {
                char next = buf[i + 1];
                char decoded;
                if      (next == 'n')  decoded = '\n';
                else if (next == 't')  decoded = '\t';
                else if (next == '\\') decoded = '\\';
                else                   decoded = next;
                if (tok_len + 1 < MAX_ARG_LEN) tok[tok_len++] = decoded;
                i += 2;
                continue;
            }
            /* Whitespace terminates the token */
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
            if (tok_len + 1 < MAX_ARG_LEN) tok[tok_len++] = c;
            i++;
        }

        tok[tok_len] = '\0';

        /* Ignore blank tokens (e.g. empty quoted strings are still tokens;
         * truly empty ones from consecutive whitespace were skipped above) */
        if (tok_len == 0) continue;

        if (tl->count >= MAX_ARGS) {
            fprintf(stderr, "xargs: too many arguments\n");
            free(buf);
            return false;
        }
        tl->items[tl->count] = strdup(tok);
        if (!tl->items[tl->count]) {
            fprintf(stderr, "xargs: out of memory\n");
            free(buf);
            return false;
        }
        tl->line_nums[tl->count] = lineno;
        tl->count++;
    }

    free(buf);
    return true;
}

/* ------------------------------------------------------------------ */
/* Command string building                                              */
/* ------------------------------------------------------------------ */

/* Quote a single argument for use in a system() command string.
 * If the arg contains spaces, double-quotes, or backslashes, wrap it.
 * Result written into out (caller ensures at least MAX_ARG_LEN * 2 + 3 bytes).
 * Returns number of characters written.
 */
static int quote_arg(const char *arg, char *out)
{
    /* Check if quoting is needed */
    bool needs_quote = false;
    for (const char *p = arg; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '"' || *p == '\\' ||
            *p == '&' || *p == '|' || *p == '<' || *p == '>' ||
            *p == '(' || *p == ')' || *p == '^' || *p == '%' ||
            *p == '!' || *p == '\'' || *p == ';') {
            needs_quote = true;
            break;
        }
    }
    if (!needs_quote) {
        int len = (int)strlen(arg);
        memcpy(out, arg, (size_t)len);
        return len;
    }
    int pos = 0;
    out[pos++] = '"';
    for (const char *p = arg; *p; p++) {
        if (*p == '"') {
            out[pos++] = '\\';
            out[pos++] = '"';
        } else if (*p == '\\') {
            out[pos++] = '\\';
            out[pos++] = '\\';
        } else {
            out[pos++] = *p;
        }
    }
    out[pos++] = '"';
    return pos;
}

/* Build a command string from base_argv[0..base_argc-1] plus
 * extra[0..extra_count-1].  If replace_str is non-empty, substitute
 * replace_str inside each base_argv element with the single item in extra[0].
 * Writes into cmd_buf (size MAX_CMD_LEN).  Returns false if too long.
 */
static bool build_cmd(char **base_argv, int base_argc,
                      char **extra, int extra_count,
                      const char *replace_str,
                      char *cmd_buf)
{
    int pos = 0;
    char piece[MAX_ARG_LEN * 2 + 4];

    for (int i = 0; i < base_argc; i++) {
        if (i > 0) {
            if (pos + 1 >= MAX_CMD_LEN) return false;
            cmd_buf[pos++] = ' ';
        }

        const char *arg = base_argv[i];

        if (replace_str && replace_str[0] && extra_count > 0) {
            /* Substitute replace_str with extra[0] inside this arg */
            char expanded[MAX_ARG_LEN * 2];
            int ep = 0;
            size_t rlen = strlen(replace_str);
            const char *p = arg;
            while (*p) {
                if (strncmp(p, replace_str, rlen) == 0) {
                    const char *rep = extra[0];
                    size_t replen = strlen(rep);
                    if (ep + (int)replen >= (int)sizeof(expanded) - 1) break;
                    memcpy(expanded + ep, rep, replen);
                    ep += (int)replen;
                    p  += rlen;
                } else {
                    if (ep + 1 >= (int)sizeof(expanded) - 1) break;
                    expanded[ep++] = *p++;
                }
            }
            expanded[ep] = '\0';
            int n = quote_arg(expanded, piece);
            if (pos + n >= MAX_CMD_LEN) return false;
            memcpy(cmd_buf + pos, piece, (size_t)n);
            pos += n;
        } else {
            int n = quote_arg(arg, piece);
            if (pos + n >= MAX_CMD_LEN) return false;
            memcpy(cmd_buf + pos, piece, (size_t)n);
            pos += n;
        }
    }

    /* Append extra args (only in non-replace mode) */
    if (!replace_str || !replace_str[0]) {
        for (int i = 0; i < extra_count; i++) {
            if (pos + 1 >= MAX_CMD_LEN) return false;
            cmd_buf[pos++] = ' ';
            int n = quote_arg(extra[i], piece);
            if (pos + n >= MAX_CMD_LEN) return false;
            memcpy(cmd_buf + pos, piece, (size_t)n);
            pos += n;
        }
    }

    cmd_buf[pos] = '\0';
    return true;
}

/* ------------------------------------------------------------------ */
/* Interactive prompt                                                   */
/* ------------------------------------------------------------------ */

static bool prompt_user(const char *cmd_str)
{
    fprintf(stderr, "%s ?", cmd_str);
    fflush(stderr);

    /* Try to open the console directly on Windows */
    FILE *tty = fopen("CON", "r");
    if (!tty) tty = stdin;

    char line[64];
    if (!fgets(line, sizeof(line), tty)) {
        if (tty != stdin) fclose(tty);
        return false;
    }
    if (tty != stdin) fclose(tty);
    return (line[0] == 'y' || line[0] == 'Y');
}

/* ------------------------------------------------------------------ */
/* Run one command string                                               */
/* ------------------------------------------------------------------ */

static int run_cmd(const char *cmd_str)
{
    if (opt_verbose) {
        fprintf(stderr, "%s\n", cmd_str);
        fflush(stderr);
    }
    if (opt_interactive) {
        if (!prompt_user(cmd_str)) return 0; /* skipped */
    }
    int ret = system(cmd_str);
    if (ret == -1) {
        fprintf(stderr, "xargs: system() failed: %s\n", strerror(errno));
        return 1;
    }
#ifdef _WIN32
    /* On Windows, system() returns the exit code directly */
    return (ret != 0) ? 1 : 0;
#else
    return (WEXITSTATUS(ret) != 0) ? 1 : 0;
#endif
}

/* ------------------------------------------------------------------ */
/* Help / version                                                       */
/* ------------------------------------------------------------------ */

static void print_usage(void)
{
    puts("Usage: xargs [OPTIONS] [COMMAND [INITIAL-ARGS...]]\n"
         "\n"
         "Execute COMMAND with arguments read from stdin.\n"
         "\n"
         "Options:\n"
         "  -0, --null              Input items terminated by NUL\n"
         "  -d C, --delimiter=C     Use C as input delimiter (\\n \\t \\0 recognised)\n"
         "  -n N, --max-args=N      At most N arguments per invocation\n"
         "  -L N, --max-lines=N     At most N input lines per invocation\n"
         "  -s N, --max-chars=N     Limit command line to N characters\n"
         "  -I STR, --replace=STR   Replace STR in args with each input item\n"
         "  -i                      Same as -I {}\n"
         "  -t, --verbose           Print command to stderr before running\n"
         "  -p, --interactive       Prompt before each invocation\n"
         "  -r, --no-run-if-empty   Do not run if stdin is empty\n"
         "  --help                  Show this help and exit\n"
         "  --version               Show version and exit\n"
         "  --                      End of options\n"
         "\n"
         "If COMMAND is omitted, 'echo' is used.\n"
         "\n"
         "Examples:\n"
         "  find . -name '*.c' | xargs grep foo\n"
         "  find . -name '*.c' | xargs -I{} grep foo {}\n"
         "  find . -name '*.txt' -print0 | xargs -0 rm");
}

static void print_version(void)
{
    puts("xargs 1.0 (Winix 1.0)");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int argi = 1;

    /* ---- Parse options -------------------------------------------- */
    for (; argi < argc; argi++) {
        const char *a = argv[argi];

        if (strcmp(a, "--") == 0) { argi++; break; }
        if (strcmp(a, "--help") == 0) { print_usage(); return 0; }
        if (strcmp(a, "--version") == 0) { print_version(); return 0; }
        if (strcmp(a, "--null") == 0)         { opt_null = true; continue; }
        if (strcmp(a, "--verbose") == 0)      { opt_verbose = true; continue; }
        if (strcmp(a, "--interactive") == 0)  { opt_interactive = true; continue; }
        if (strcmp(a, "--no-run-if-empty") == 0) { opt_no_run_empty = true; continue; }

        if (strncmp(a, "--delimiter=", 12) == 0) {
            opt_delim = parse_delim(a + 12);
            continue;
        }
        if (strncmp(a, "--max-args=", 11) == 0) {
            opt_max_args = atoi(a + 11);
            if (opt_max_args <= 0) {
                fprintf(stderr, "xargs: invalid max-args value '%s'\n", a + 11);
                return 1;
            }
            continue;
        }
        if (strncmp(a, "--max-lines=", 12) == 0) {
            opt_max_lines = atoi(a + 12);
            if (opt_max_lines <= 0) {
                fprintf(stderr, "xargs: invalid max-lines value '%s'\n", a + 12);
                return 1;
            }
            continue;
        }
        if (strncmp(a, "--max-chars=", 12) == 0) {
            opt_max_chars = atoi(a + 12);
            if (opt_max_chars <= 0) {
                fprintf(stderr, "xargs: invalid max-chars value '%s'\n", a + 12);
                return 1;
            }
            continue;
        }
        if (strncmp(a, "--replace=", 10) == 0) {
            strncpy(opt_replace, a + 10, sizeof(opt_replace) - 1);
            opt_replace[sizeof(opt_replace) - 1] = '\0';
            continue;
        }

        if (a[0] != '-' || a[1] == '\0') break; /* not a flag */

        /* Short flags (combinable where applicable) */
        bool stop = false;
        for (int fi = 1; a[fi] && !stop; fi++) {
            char f = a[fi];
            switch (f) {
                case '0': opt_null = true; break;
                case 't': opt_verbose = true; break;
                case 'p': opt_interactive = true; break;
                case 'r': opt_no_run_empty = true; break;
                case 'i':
                    strncpy(opt_replace, "{}", sizeof(opt_replace) - 1);
                    break;
                case 'd':
                    /* -d takes the next character or argument */
                    if (a[fi + 1]) {
                        opt_delim = parse_delim(a + fi + 1);
                        stop = true; /* consumed rest of flag string */
                    } else if (argi + 1 < argc) {
                        opt_delim = parse_delim(argv[++argi]);
                        stop = true;
                    } else {
                        fprintf(stderr, "xargs: option requires an argument -- 'd'\n");
                        return 1;
                    }
                    break;
                case 'n':
                    if (a[fi + 1]) {
                        opt_max_args = atoi(a + fi + 1);
                        stop = true;
                    } else if (argi + 1 < argc) {
                        opt_max_args = atoi(argv[++argi]);
                        stop = true;
                    } else {
                        fprintf(stderr, "xargs: option requires an argument -- 'n'\n");
                        return 1;
                    }
                    if (opt_max_args <= 0) {
                        fprintf(stderr, "xargs: invalid max-args\n");
                        return 1;
                    }
                    break;
                case 'L':
                    if (a[fi + 1]) {
                        opt_max_lines = atoi(a + fi + 1);
                        stop = true;
                    } else if (argi + 1 < argc) {
                        opt_max_lines = atoi(argv[++argi]);
                        stop = true;
                    } else {
                        fprintf(stderr, "xargs: option requires an argument -- 'L'\n");
                        return 1;
                    }
                    if (opt_max_lines <= 0) {
                        fprintf(stderr, "xargs: invalid max-lines\n");
                        return 1;
                    }
                    break;
                case 's':
                    if (a[fi + 1]) {
                        opt_max_chars = atoi(a + fi + 1);
                        stop = true;
                    } else if (argi + 1 < argc) {
                        opt_max_chars = atoi(argv[++argi]);
                        stop = true;
                    } else {
                        fprintf(stderr, "xargs: option requires an argument -- 's'\n");
                        return 1;
                    }
                    if (opt_max_chars <= 0) {
                        fprintf(stderr, "xargs: invalid max-chars\n");
                        return 1;
                    }
                    break;
                case 'I':
                    /* -I takes the rest of the flag string or next arg */
                    if (a[fi + 1]) {
                        strncpy(opt_replace, a + fi + 1, sizeof(opt_replace) - 1);
                        opt_replace[sizeof(opt_replace) - 1] = '\0';
                        stop = true;
                    } else if (argi + 1 < argc) {
                        strncpy(opt_replace, argv[++argi], sizeof(opt_replace) - 1);
                        opt_replace[sizeof(opt_replace) - 1] = '\0';
                        stop = true;
                    } else {
                        fprintf(stderr, "xargs: option requires an argument -- 'I'\n");
                        return 1;
                    }
                    break;
                default:
                    fprintf(stderr, "xargs: invalid option -- '%c'\n", f);
                    return 1;
            }
        }
    }

    /* -I implies -n 1 */
    if (opt_replace[0]) {
        opt_max_args = 1;
    }

    /* ---- Collect COMMAND and its initial args --------------------- */
    /* Everything remaining after options is: [COMMAND [INITIAL-ARGS]] */
    int    base_argc = 0;
    char **base_argv = NULL;

    if (argi < argc) {
        base_argc = argc - argi;
        base_argv = argv + argi;
    } else {
        /* Default command: echo */
        static char *echo_argv[] = { "echo", NULL };
        base_argc = 1;
        base_argv = echo_argv;
    }

    /* ---- Read tokens from stdin ----------------------------------- */
    TokenList tl;
    if (!read_tokens(stdin, &tl)) return 1;

    /* -r: do not run if stdin is empty */
    if (opt_no_run_empty && tl.count == 0) return 0;

    /* If no tokens and no -r, still run once with no extra args
     * (matches GNU xargs behaviour: echo with no args prints blank line).
     * But only do that if there are no tokens and no -r flag. */
    if (tl.count == 0 && !opt_no_run_empty) {
        char cmd_buf[MAX_CMD_LEN];
        if (!build_cmd(base_argv, base_argc, NULL, 0, NULL, cmd_buf)) {
            fprintf(stderr, "xargs: command too long\n");
            return 1;
        }
        return run_cmd(cmd_buf);
    }

    /* ---- Execute -------------------------------------------------- */
    int any_failed = 0;
    char cmd_buf[MAX_CMD_LEN];

    if (opt_replace[0]) {
        /* Replace mode: one invocation per token */
        for (int i = 0; i < tl.count; i++) {
            char *item[1];
            item[0] = tl.items[i];
            if (!build_cmd(base_argv, base_argc, item, 1, opt_replace, cmd_buf)) {
                fprintf(stderr, "xargs: command too long\n");
                any_failed = 1;
                continue;
            }
            if (run_cmd(cmd_buf) != 0) any_failed = 1;
        }
    } else {
        /* Batch mode */
        int batch_start = 0; /* index into tl.items of current batch start */

        while (batch_start < tl.count) {
            int batch_end = batch_start; /* exclusive end */

            /* Determine base command length for -s accounting */
            int base_len = 0;
            for (int b = 0; b < base_argc; b++) {
                if (b > 0) base_len++;
                base_len += (int)strlen(base_argv[b]) + 2; /* +2 for possible quotes */
            }

            int lines_seen = -1; /* -1 means: first token not yet seen */
            int cur_line = -1;

            while (batch_end < tl.count) {
                /* Check -n limit */
                if (opt_max_args > 0 && (batch_end - batch_start) >= opt_max_args)
                    break;

                /* Check -L limit */
                if (opt_max_lines > 0) {
                    int tok_line = tl.line_nums[batch_end];
                    if (lines_seen < 0) {
                        /* First token in this batch */
                        cur_line = tok_line;
                        lines_seen = 1;
                    } else if (tok_line != cur_line) {
                        lines_seen++;
                        cur_line = tok_line;
                    }
                    if (lines_seen > opt_max_lines) break;
                }

                /* Check -s limit (rough estimate) */
                if (opt_max_chars > 0) {
                    int arg_len = (int)strlen(tl.items[batch_end]) + 3; /* space + possible quotes */
                    if (base_len + arg_len >= opt_max_chars) {
                        if (batch_end == batch_start) {
                            /* Single arg already too long; include it anyway
                             * to avoid infinite loop */
                            batch_end++;
                        }
                        break;
                    }
                    base_len += arg_len;
                }

                batch_end++;
            }

            if (batch_end == batch_start) {
                /* Safety: should not happen, but avoid infinite loop */
                batch_end++;
            }

            int extra_count = batch_end - batch_start;
            char **extra    = tl.items + batch_start;

            if (!build_cmd(base_argv, base_argc, extra, extra_count, NULL, cmd_buf)) {
                fprintf(stderr, "xargs: command too long\n");
                any_failed = 1;
                batch_start = batch_end;
                continue;
            }

            if (run_cmd(cmd_buf) != 0) any_failed = 1;

            batch_start = batch_end;
        }
    }

    /* Free tokens */
    for (int i = 0; i < tl.count; i++) free(tl.items[i]);

    return any_failed ? 1 : 0;
}
