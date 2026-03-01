/*
 * find.c — Winix coreutil
 *
 * Usage: find [PATH...] [EXPRESSION]
 *
 * Supported primaries:
 *   -name PATTERN   basename glob match (case-sensitive)
 *   -iname PATTERN  basename glob match (case-insensitive)
 *   -type f|d       regular file / directory
 *   -maxdepth N     don't recurse deeper than N
 *   -mindepth N     don't print entries shallower than N
 *   -newer FILE     modified more recently than FILE
 *   -size +N|-N     larger/smaller than N 512-byte blocks
 *   -print          print path (default action)
 *   -delete         delete matched file/empty dir
 *   -not / !        negate next primary
 *   -exec CMD {} \; run CMD with {} replaced by path
 *
 * Options:
 *   --help          print usage and exit 0
 *   --version       print version and exit 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Expression types                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    EXPR_NAME,
    EXPR_INAME,
    EXPR_TYPE,
    EXPR_MAXDEPTH,
    EXPR_MINDEPTH,
    EXPR_NEWER,
    EXPR_SIZE,
    EXPR_PRINT,
    EXPR_DELETE,
    EXPR_NOT,
    EXPR_EXEC
} ExprKind;

/* For -size: +N means larger, -N means smaller (N in 512-byte blocks) */
typedef enum { SIZE_GT, SIZE_LT } SizeCmp;

#define MAX_EXPRS      64
#define MAX_EXEC_ARGS  64
#define PATH_BUF_SIZE  4096

typedef struct {
    ExprKind kind;
    bool     negate;       /* preceded by -not or ! */

    /* EXPR_NAME / EXPR_INAME */
    char pattern[256];

    /* EXPR_TYPE */
    char type_char;        /* 'f' or 'd' */

    /* EXPR_MAXDEPTH / EXPR_MINDEPTH */
    int  depth_val;

    /* EXPR_NEWER */
    time_t newer_mtime;

    /* EXPR_SIZE */
    SizeCmp size_cmp;
    long long size_blocks; /* in 512-byte blocks */

    /* EXPR_EXEC */
    char *exec_argv[MAX_EXEC_ARGS];
    int   exec_argc;
} Expr;

static Expr  exprs[MAX_EXPRS];
static int   nexpr = 0;

static int   g_maxdepth = INT_MAX;   /* from -maxdepth (global for recursion guard) */
static int   g_mindepth = 0;         /* from -mindepth */
static bool  g_has_action = false;   /* any -print or -delete or -exec seen */
static int   g_ret = 0;              /* overall exit code */

/* ------------------------------------------------------------------ */
/* Wildcard / glob matching                                             */
/* ------------------------------------------------------------------ */

/*
 * wildmatch — match PATTERN against STR.
 * Supports * (any sequence including empty) and ? (any single char).
 * Both pointers must be non-NULL.
 */
static bool wildmatch(const char *pat, const char *str) {
    while (*pat) {
        if (*pat == '*') {
            /* Collapse consecutive stars */
            while (*pat == '*') pat++;
            /* Tail-match: try anchoring the rest of pat at every position */
            if (*pat == '\0') return true;  /* trailing * matches anything */
            for (; *str; str++) {
                if (wildmatch(pat, str)) return true;
            }
            return false;
        } else if (*pat == '?') {
            if (*str == '\0') return false;
            pat++;
            str++;
        } else {
            if (*pat != *str) return false;
            pat++;
            str++;
        }
    }
    return *str == '\0';
}

/* Case-insensitive version: lowercase both strings into temporaries */
static bool wildmatch_icase(const char *pat, const char *str) {
    char lpat[256], lstr[256];
    size_t i;

    for (i = 0; i < sizeof(lpat) - 1 && pat[i]; i++)
        lpat[i] = (char)tolower((unsigned char)pat[i]);
    lpat[i] = '\0';

    for (i = 0; i < sizeof(lstr) - 1 && str[i]; i++)
        lstr[i] = (char)tolower((unsigned char)str[i]);
    lstr[i] = '\0';

    return wildmatch(lpat, lstr);
}

/* ------------------------------------------------------------------ */
/* Path helpers                                                         */
/* ------------------------------------------------------------------ */

/* Return pointer to the basename component of path (no allocation). */
static const char *path_basename(const char *path) {
    const char *s = path;
    const char *last = path;
    for (; *s; s++) {
        if (*s == '/' || *s == '\\')
            last = s + 1;
    }
    return last;
}

/* Build child path: parent/name — avoids double slash. */
static void path_join(char *buf, size_t bufsz, const char *parent, const char *name) {
    size_t plen = strlen(parent);
    if (plen > 0 && (parent[plen - 1] == '/' || parent[plen - 1] == '\\'))
        snprintf(buf, bufsz, "%s%s", parent, name);
    else
        snprintf(buf, bufsz, "%s/%s", parent, name);
}

/* ------------------------------------------------------------------ */
/* Expression evaluation                                                */
/* ------------------------------------------------------------------ */

/*
 * eval_expr — return true if the path/stat pair matches expression e.
 * Does NOT handle the negate flag; that is applied by the caller.
 */
static bool eval_expr(const Expr *e, const char *path, const struct stat *st) {
    switch (e->kind) {
        case EXPR_NAME:
            return wildmatch(e->pattern, path_basename(path));

        case EXPR_INAME:
            return wildmatch_icase(e->pattern, path_basename(path));

        case EXPR_TYPE:
            if (e->type_char == 'f') return S_ISREG(st->st_mode);
            if (e->type_char == 'd') return S_ISDIR(st->st_mode);
            return false;

        case EXPR_MAXDEPTH:
        case EXPR_MINDEPTH:
            /* These control recursion/printing, not per-entry filtering here */
            return true;

        case EXPR_NEWER:
            return st->st_mtime > e->newer_mtime;

        case EXPR_SIZE: {
            /* Convert file size to 512-byte blocks (round up) */
            long long blocks = (st->st_size + 511) / 512;
            if (e->size_cmp == SIZE_GT) return blocks > e->size_blocks;
            else                         return blocks < e->size_blocks;
        }

        case EXPR_PRINT:
        case EXPR_DELETE:
        case EXPR_EXEC:
            /* Actions always match (used as side effects) */
            return true;

        case EXPR_NOT:
            /* Handled via the negate flag; shouldn't reach here */
            return true;
    }
    return true;
}

/*
 * run_actions — execute all action expressions (-print, -delete, -exec)
 * for a matched path.
 */
static void run_actions(const char *path, const struct stat *st) {
    bool any_action = false;

    for (int i = 0; i < nexpr; i++) {
        Expr *e = &exprs[i];

        if (e->kind == EXPR_PRINT) {
            printf("%s\n", path);
            any_action = true;
        } else if (e->kind == EXPR_DELETE) {
            any_action = true;
            if (S_ISDIR(st->st_mode)) {
                if (rmdir(path) != 0) {
                    fprintf(stderr, "find: cannot remove directory '%s': %s\n",
                            path, strerror(errno));
                    g_ret = 1;
                }
            } else {
                if (remove(path) != 0) {
                    fprintf(stderr, "find: cannot remove '%s': %s\n",
                            path, strerror(errno));
                    g_ret = 1;
                }
            }
        } else if (e->kind == EXPR_EXEC) {
            any_action = true;
            /* Build command string: replace {} with path */
            char cmd[PATH_BUF_SIZE * 2];
            cmd[0] = '\0';
            size_t pos = 0;
            for (int j = 0; j < e->exec_argc; j++) {
                const char *arg = e->exec_argv[j];
                const char *tok;
                if (j > 0) {
                    if (pos < sizeof(cmd) - 2) {
                        cmd[pos++] = ' ';
                        cmd[pos]   = '\0';
                    }
                }
                /* Replace {} with path */
                if (strcmp(arg, "{}") == 0) {
                    tok = path;
                } else {
                    tok = arg;
                }
                size_t tlen = strlen(tok);
                if (pos + tlen < sizeof(cmd) - 1) {
                    memcpy(cmd + pos, tok, tlen);
                    pos += tlen;
                    cmd[pos] = '\0';
                }
            }
            if (system(cmd) == -1) {
                fprintf(stderr, "find: exec failed: %s\n", strerror(errno));
                g_ret = 1;
            }
        }
    }

    /* Default action when no action expression was seen */
    if (!any_action && !g_has_action) {
        printf("%s\n", path);
    }
}

/* ------------------------------------------------------------------ */
/* Core recursive walk                                                  */
/* ------------------------------------------------------------------ */

static void find_in(const char *path, int depth);

static void process_entry(const char *path, int depth) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "find: cannot stat '%s': %s\n", path, strerror(errno));
        g_ret = 1;
        return;
    }

    /* Check all filter expressions (skip action-only exprs) */
    bool match = true;
    for (int i = 0; i < nexpr; i++) {
        Expr *e = &exprs[i];

        /* Skip action expressions from the filter pass */
        if (e->kind == EXPR_PRINT || e->kind == EXPR_DELETE || e->kind == EXPR_EXEC)
            continue;
        /* Skip depth control expressions from per-entry filtering */
        if (e->kind == EXPR_MAXDEPTH || e->kind == EXPR_MINDEPTH)
            continue;

        bool result = eval_expr(e, path, &st);
        if (e->negate) result = !result;

        if (!result) {
            match = false;
            break;
        }
    }

    /* Honour -mindepth: don't print/act if shallower than required */
    if (match && depth >= g_mindepth) {
        run_actions(path, &st);
    }

    /* Recurse into directories if within maxdepth */
    if (S_ISDIR(st.st_mode) && depth < g_maxdepth) {
        find_in(path, depth + 1);
    }
}

static void find_in(const char *path, int depth) {
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "find: cannot open directory '%s': %s\n", path, strerror(errno));
        g_ret = 1;
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char child[PATH_BUF_SIZE];
        path_join(child, sizeof(child), path, ent->d_name);

        process_entry(child, depth);
    }
    closedir(d);
}

/* ------------------------------------------------------------------ */
/* Argument parsing                                                     */
/* ------------------------------------------------------------------ */

static void usage(void) {
    fprintf(stderr,
        "Usage: find [PATH...] [EXPRESSION]\n"
        "\n"
        "Recursively search for files under each PATH (default: .).\n"
        "\n"
        "Primaries:\n"
        "  -name PATTERN   Match filename against glob (case-sensitive)\n"
        "  -iname PATTERN  Match filename against glob (case-insensitive)\n"
        "  -type f|d       Regular file (f) or directory (d)\n"
        "  -maxdepth N     Do not descend more than N levels\n"
        "  -mindepth N     Do not act on entries shallower than N levels\n"
        "  -newer FILE     Modified more recently than FILE\n"
        "  -size +N        Larger than N 512-byte blocks\n"
        "  -size -N        Smaller than N 512-byte blocks\n"
        "  -print          Print matching path (default if no action given)\n"
        "  -delete         Delete matching file or empty directory\n"
        "  -not / !        Negate the next primary\n"
        "  -exec CMD {} \\; Execute CMD, replacing {} with path\n"
        "\n"
        "Options:\n"
        "  --help          Show this help and exit\n"
        "  --version       Show version and exit\n"
    );
}

/*
 * parse_exprs — parse argv[argi..argc-1] into the global exprs[] array.
 * Returns 0 on success, 1 on error.
 */
static int parse_exprs(int argc, char *argv[], int argi) {
    bool next_negate = false;

    while (argi < argc) {
        const char *tok = argv[argi];

        /* Handle ! as a standalone token (same as -not) */
        if (strcmp(tok, "!") == 0) {
            next_negate = !next_negate;
            argi++;
            continue;
        }

        if (strcmp(tok, "-not") == 0) {
            next_negate = !next_negate;
            argi++;
            continue;
        }

        if (nexpr >= MAX_EXPRS) {
            fprintf(stderr, "find: too many expressions (max %d)\n", MAX_EXPRS);
            return 1;
        }

        Expr *e = &exprs[nexpr];
        memset(e, 0, sizeof(*e));
        e->negate = next_negate;
        next_negate = false;

        if (strcmp(tok, "-name") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "find: -name requires an argument\n");
                return 1;
            }
            e->kind = EXPR_NAME;
            strncpy(e->pattern, argv[argi + 1], sizeof(e->pattern) - 1);
            argi += 2;

        } else if (strcmp(tok, "-iname") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "find: -iname requires an argument\n");
                return 1;
            }
            e->kind = EXPR_INAME;
            strncpy(e->pattern, argv[argi + 1], sizeof(e->pattern) - 1);
            argi += 2;

        } else if (strcmp(tok, "-type") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "find: -type requires an argument\n");
                return 1;
            }
            const char *targ = argv[argi + 1];
            if (strcmp(targ, "f") != 0 && strcmp(targ, "d") != 0) {
                fprintf(stderr, "find: -type: unknown type '%s' (use f or d)\n", targ);
                return 1;
            }
            e->kind      = EXPR_TYPE;
            e->type_char = targ[0];
            argi += 2;

        } else if (strcmp(tok, "-maxdepth") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "find: -maxdepth requires an argument\n");
                return 1;
            }
            char *end;
            long val = strtol(argv[argi + 1], &end, 10);
            if (*end != '\0' || val < 0) {
                fprintf(stderr, "find: -maxdepth: invalid depth '%s'\n", argv[argi + 1]);
                return 1;
            }
            e->kind      = EXPR_MAXDEPTH;
            e->depth_val = (int)val;
            g_maxdepth   = (int)val;
            argi += 2;

        } else if (strcmp(tok, "-mindepth") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "find: -mindepth requires an argument\n");
                return 1;
            }
            char *end;
            long val = strtol(argv[argi + 1], &end, 10);
            if (*end != '\0' || val < 0) {
                fprintf(stderr, "find: -mindepth: invalid depth '%s'\n", argv[argi + 1]);
                return 1;
            }
            e->kind      = EXPR_MINDEPTH;
            e->depth_val = (int)val;
            g_mindepth   = (int)val;
            argi += 2;

        } else if (strcmp(tok, "-newer") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "find: -newer requires an argument\n");
                return 1;
            }
            struct stat ref;
            if (stat(argv[argi + 1], &ref) != 0) {
                fprintf(stderr, "find: -newer: cannot stat '%s': %s\n",
                        argv[argi + 1], strerror(errno));
                return 1;
            }
            e->kind        = EXPR_NEWER;
            e->newer_mtime = ref.st_mtime;
            argi += 2;

        } else if (strcmp(tok, "-size") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "find: -size requires an argument\n");
                return 1;
            }
            const char *sarg = argv[argi + 1];
            if (sarg[0] != '+' && sarg[0] != '-') {
                fprintf(stderr, "find: -size: argument must start with + or -\n");
                return 1;
            }
            char *end;
            long long val = strtoll(sarg + 1, &end, 10);
            if (*end != '\0' || val < 0) {
                fprintf(stderr, "find: -size: invalid value '%s'\n", sarg);
                return 1;
            }
            e->kind        = EXPR_SIZE;
            e->size_cmp    = (sarg[0] == '+') ? SIZE_GT : SIZE_LT;
            e->size_blocks = val;
            argi += 2;

        } else if (strcmp(tok, "-print") == 0) {
            e->kind      = EXPR_PRINT;
            g_has_action = true;
            argi++;

        } else if (strcmp(tok, "-delete") == 0) {
            e->kind      = EXPR_DELETE;
            g_has_action = true;
            argi++;

        } else if (strcmp(tok, "-exec") == 0) {
            argi++;
            if (argi >= argc) {
                fprintf(stderr, "find: -exec requires a command\n");
                return 1;
            }
            e->kind      = EXPR_EXEC;
            e->exec_argc = 0;
            g_has_action = true;

            /* Collect tokens until \; */
            bool found_semi = false;
            while (argi < argc) {
                if (strcmp(argv[argi], ";") == 0 || strcmp(argv[argi], "\\;") == 0) {
                    found_semi = true;
                    argi++;
                    break;
                }
                if (e->exec_argc >= MAX_EXEC_ARGS - 1) {
                    fprintf(stderr, "find: -exec: too many arguments\n");
                    return 1;
                }
                e->exec_argv[e->exec_argc++] = argv[argi];
                argi++;
            }
            if (!found_semi) {
                fprintf(stderr, "find: -exec: missing terminating \\;\n");
                return 1;
            }
            if (e->exec_argc == 0) {
                fprintf(stderr, "find: -exec: no command given\n");
                return 1;
            }

        } else {
            fprintf(stderr, "find: unknown expression: '%s'\n", tok);
            return 1;
        }

        nexpr++;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    /* --help / --version before anything else */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("find 1.0 (Winix 1.0)\n");
            return 0;
        }
    }

    /*
     * Split argv into PATH list and EXPRESSION list.
     * PATHs: leading non-option arguments (don't start with '-' and aren't '!').
     * Expressions: everything from the first '-' or '!' token onward.
     */
    char *paths[64];
    int   npaths = 0;
    int   argi   = 1;

    while (argi < argc) {
        const char *tok = argv[argi];
        /* An expression starts when we see '-something' or '!' */
        if (tok[0] == '-' || strcmp(tok, "!") == 0)
            break;
        if (npaths < 64)
            paths[npaths++] = argv[argi];
        argi++;
    }

    /* Default path is '.' if none given */
    if (npaths == 0) {
        paths[0] = ".";
        npaths   = 1;
    }

    /* Parse expressions */
    if (parse_exprs(argc, argv, argi) != 0)
        return 1;

    /*
     * For each starting path: print/act on it first (depth 0),
     * then recurse if it is a directory.
     */
    for (int i = 0; i < npaths; i++) {
        const char *root = paths[i];
        struct stat st;

        if (stat(root, &st) != 0) {
            fprintf(stderr, "find: '%s': %s\n", root, strerror(errno));
            g_ret = 1;
            continue;
        }

        /* Evaluate the root itself at depth 0 */
        bool match = true;
        for (int j = 0; j < nexpr; j++) {
            Expr *e = &exprs[j];
            if (e->kind == EXPR_PRINT || e->kind == EXPR_DELETE || e->kind == EXPR_EXEC)
                continue;
            if (e->kind == EXPR_MAXDEPTH || e->kind == EXPR_MINDEPTH)
                continue;
            bool result = eval_expr(e, root, &st);
            if (e->negate) result = !result;
            if (!result) { match = false; break; }
        }

        if (match && g_mindepth == 0) {
            run_actions(root, &st);
        }

        /* Descend if it is a directory and maxdepth allows */
        if (S_ISDIR(st.st_mode) && g_maxdepth > 0) {
            find_in(root, 1);
        }
    }

    return g_ret;
}
