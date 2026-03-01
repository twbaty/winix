/*
 * test.c — Winix coreutil
 *
 * Usage: test EXPR
 *        [ EXPR ]
 *
 * Evaluate a conditional expression. Exit 0 if true, 1 if false, 2 on error.
 *
 * File tests (unary):
 *   -e FILE   file exists
 *   -f FILE   regular file
 *   -d FILE   directory
 *   -r FILE   readable
 *   -w FILE   writable
 *   -x FILE   executable
 *   -s FILE   file exists and size > 0
 *   -z STRING string is empty
 *   -n STRING string is non-empty
 *   -L FILE   is a symbolic link  (also -h)
 *   -h FILE   is a symbolic link
 *
 * String comparisons:
 *   STRING1 = STRING2
 *   STRING1 != STRING2
 *   STRING1 < STRING2
 *   STRING1 > STRING2
 *
 * Numeric comparisons:
 *   INT1 -eq INT2
 *   INT1 -ne INT2
 *   INT1 -lt INT2
 *   INT1 -le INT2
 *   INT1 -gt INT2
 *   INT1 -ge INT2
 *
 * Boolean operators:
 *   ! EXPR
 *   EXPR1 -a EXPR2
 *   EXPR1 -o EXPR2
 *   ( EXPR )
 *
 * Options:
 *   --help     Print usage and exit 0
 *   --version  Print version and exit 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  define access _access
#  define F_OK 0
#  define R_OK 4
#  define W_OK 2
#  define X_OK 1
#else
#  include <unistd.h>
#endif

static void usage(void) {
    puts("Usage: test EXPR");
    puts("       [ EXPR ]");
    puts("Evaluate conditional expression EXPR.");
    puts("Exit status: 0 if true, 1 if false, 2 if an error occurred.");
    puts("");
    puts("File tests:");
    puts("  -e FILE   FILE exists");
    puts("  -f FILE   FILE is a regular file");
    puts("  -d FILE   FILE is a directory");
    puts("  -r FILE   FILE is readable");
    puts("  -w FILE   FILE is writable");
    puts("  -x FILE   FILE is executable");
    puts("  -s FILE   FILE exists and has size > 0");
    puts("  -L FILE   FILE is a symbolic link (also -h)");
    puts("");
    puts("String tests:");
    puts("  -z STRING      STRING is empty");
    puts("  -n STRING      STRING is non-empty");
    puts("  STRING1 = STRING2   equal");
    puts("  STRING1 != STRING2  not equal");
    puts("  STRING1 < STRING2   less than (lexicographic)");
    puts("  STRING1 > STRING2   greater than (lexicographic)");
    puts("");
    puts("Numeric comparisons:");
    puts("  INT1 -eq INT2  equal");
    puts("  INT1 -ne INT2  not equal");
    puts("  INT1 -lt INT2  less than");
    puts("  INT1 -le INT2  less or equal");
    puts("  INT1 -gt INT2  greater than");
    puts("  INT1 -ge INT2  greater or equal");
    puts("");
    puts("Boolean operators: ! EXPR   EXPR1 -a EXPR2   EXPR1 -o EXPR2   ( EXPR )");
    puts("");
    puts("  --help     display this help and exit");
    puts("  --version  output version information and exit");
}

/* ---------- argument cursor ---------- */
static char **g_argv = NULL;
static int    g_argc = 0;
static int    g_pos  = 0;  /* current index into g_argv */

static const char *peek(void) {
    if (g_pos < g_argc) return g_argv[g_pos];
    return NULL;
}

static const char *consume(void) {
    if (g_pos < g_argc) return g_argv[g_pos++];
    return NULL;
}

/* ---------- error handling ---------- */
static int g_error = 0;  /* set to 1 on parse/eval error → exit 2 */

/* ---------- file attribute helpers (Windows-aware) ---------- */
static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int file_is_regular(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode);
}

static int file_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int file_is_readable(const char *path) {
    return access(path, R_OK) == 0;
}

static int file_is_writable(const char *path) {
    return access(path, W_OK) == 0;
}

static int file_is_executable(const char *path) {
#ifdef _WIN32
    /* On Windows, check extension for .exe/.bat/.cmd, or check file attr */
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    /* Check extension */
    const char *dot = strrchr(path, '.');
    if (dot) {
        if (_stricmp(dot, ".exe") == 0) return 1;
        if (_stricmp(dot, ".bat") == 0) return 1;
        if (_stricmp(dot, ".cmd") == 0) return 1;
        if (_stricmp(dot, ".com") == 0) return 1;
    }
    /* Fall back to access X_OK which may not mean much on Windows */
    return access(path, X_OK) == 0;
#else
    return access(path, X_OK) == 0;
#endif
}

static int file_nonempty(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_size > 0;
}

static int file_is_symlink(const char *path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    return S_ISLNK(st.st_mode);
#endif
}

/* ---------- forward declarations ---------- */
static int parse_or(void);
static int parse_and(void);
static int parse_not(void);
static int parse_primary(void);

/* ---------- parse_or: lowest precedence (handles -o) ---------- */
static int parse_or(void) {
    int left = parse_and();
    while (1) {
        const char *t = peek();
        if (t && strcmp(t, "-o") == 0) {
            consume();
            int right = parse_and();
            left = left || right;
        } else {
            break;
        }
    }
    return left;
}

/* ---------- parse_and: handles -a ---------- */
static int parse_and(void) {
    int left = parse_not();
    while (1) {
        const char *t = peek();
        if (t && strcmp(t, "-a") == 0) {
            consume();
            int right = parse_not();
            left = left && right;
        } else {
            break;
        }
    }
    return left;
}

/* ---------- parse_not: handles ! ---------- */
static int parse_not(void) {
    const char *t = peek();
    if (t && strcmp(t, "!") == 0) {
        consume();
        int val = parse_not();  /* right-associative */
        return !val;
    }
    return parse_primary();
}

/* ---------- parse_primary: unary tests, binary ops, parens ---------- */
static int parse_primary(void) {
    const char *t = peek();
    if (!t) {
        /* empty expression → false (POSIX: zero arguments is false) */
        return 0;
    }

    /* Parenthesised sub-expression */
    if (strcmp(t, "(") == 0) {
        consume();
        int val = parse_or();
        const char *close = consume();
        if (!close || strcmp(close, ")") != 0) {
            fprintf(stderr, "test: missing ')'\n");
            g_error = 1;
            return 0;
        }
        return val;
    }

    /* Unary file / string operators */
    if (t[0] == '-' && t[1] != '\0' && t[2] == '\0') {
        char op = t[1];
        switch (op) {
            case 'e': case 'f': case 'd': case 'r': case 'w':
            case 'x': case 's': case 'L': case 'h': {
                consume();
                const char *file = consume();
                if (!file) {
                    fprintf(stderr, "test: missing argument after '-%c'\n", op);
                    g_error = 1;
                    return 0;
                }
                switch (op) {
                    case 'e': return file_exists(file);
                    case 'f': return file_is_regular(file);
                    case 'd': return file_is_dir(file);
                    case 'r': return file_is_readable(file);
                    case 'w': return file_is_writable(file);
                    case 'x': return file_is_executable(file);
                    case 's': return file_exists(file) && file_nonempty(file);
                    case 'L': /* fall through */
                    case 'h': return file_is_symlink(file);
                }
                break;
            }
            case 'z': {
                consume();
                const char *s = consume();
                if (!s) {
                    fprintf(stderr, "test: missing argument after '-z'\n");
                    g_error = 1;
                    return 0;
                }
                return strlen(s) == 0;
            }
            case 'n': {
                consume();
                const char *s = consume();
                if (!s) {
                    fprintf(stderr, "test: missing argument after '-n'\n");
                    g_error = 1;
                    return 0;
                }
                return strlen(s) > 0;
            }
            default:
                break;
        }
    }

    /* At this point we have a string/integer that might be left of a binary op.
     * Consume it and check what follows. */
    const char *lhs = consume();

    const char *op = peek();
    if (!op) {
        /* Single argument: true if non-empty string */
        return strlen(lhs) > 0;
    }

    /* String binary ops */
    if (strcmp(op, "=") == 0) {
        consume();
        const char *rhs = consume();
        if (!rhs) { fprintf(stderr, "test: missing argument after '='\n"); g_error = 1; return 0; }
        return strcmp(lhs, rhs) == 0;
    }
    if (strcmp(op, "!=") == 0) {
        consume();
        const char *rhs = consume();
        if (!rhs) { fprintf(stderr, "test: missing argument after '!='\n"); g_error = 1; return 0; }
        return strcmp(lhs, rhs) != 0;
    }
    if (strcmp(op, "<") == 0) {
        consume();
        const char *rhs = consume();
        if (!rhs) { fprintf(stderr, "test: missing argument after '<'\n"); g_error = 1; return 0; }
        return strcmp(lhs, rhs) < 0;
    }
    if (strcmp(op, ">") == 0) {
        consume();
        const char *rhs = consume();
        if (!rhs) { fprintf(stderr, "test: missing argument after '>'\n"); g_error = 1; return 0; }
        return strcmp(lhs, rhs) > 0;
    }

    /* Numeric binary ops */
    if (op[0] == '-' && op[1] != '\0') {
        int is_num_op = (strcmp(op, "-eq") == 0 || strcmp(op, "-ne") == 0 ||
                         strcmp(op, "-lt") == 0 || strcmp(op, "-le") == 0 ||
                         strcmp(op, "-gt") == 0 || strcmp(op, "-ge") == 0);
        if (is_num_op) {
            consume();
            const char *rhs = consume();
            if (!rhs) {
                fprintf(stderr, "test: missing argument after '%s'\n", op);
                g_error = 1;
                return 0;
            }
            char *end1, *end2;
            long l = strtol(lhs, &end1, 10);
            long r = strtol(rhs, &end2, 10);
            if (*end1 != '\0') {
                fprintf(stderr, "test: '%s': integer expression expected\n", lhs);
                g_error = 1;
                return 0;
            }
            if (*end2 != '\0') {
                fprintf(stderr, "test: '%s': integer expression expected\n", rhs);
                g_error = 1;
                return 0;
            }
            if (strcmp(op, "-eq") == 0) return l == r;
            if (strcmp(op, "-ne") == 0) return l != r;
            if (strcmp(op, "-lt") == 0) return l < r;
            if (strcmp(op, "-le") == 0) return l <= r;
            if (strcmp(op, "-gt") == 0) return l > r;
            if (strcmp(op, "-ge") == 0) return l >= r;
        }
    }

    /* No recognized operator — treat lhs as the only value: non-empty = true */
    /* Push op back (by not consuming) — it will be consumed by caller or cause
     * a parse error at a higher level. We already consumed lhs, so just return. */
    return strlen(lhs) > 0;
}

int main(int argc, char *argv[]) {
    /* Detect invocation as "[" */
    const char *prog = argv[0];
    /* basename — handle both / and \ */
    const char *base = strrchr(prog, '/');
    if (!base) base = strrchr(prog, '\\');
    if (base) base++; else base = prog;

    /* Strip .exe if present */
    char progname[64];
    strncpy(progname, base, sizeof(progname) - 1);
    progname[sizeof(progname) - 1] = '\0';
    char *ext = strrchr(progname, '.');
    if (ext && _stricmp(ext, ".exe") == 0) *ext = '\0';

    int bracket_mode = (strcmp(progname, "[") == 0);

    /* Handle --help and --version before bracket stripping */
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(); return 0; }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("test 1.0 (Winix 1.0)");
        return 0;
    }

    /* For "[" mode, last argument must be "]" */
    int expr_argc = argc - 1; /* skip argv[0] */
    char **expr_argv = argv + 1;

    if (bracket_mode) {
        if (expr_argc < 1 || strcmp(expr_argv[expr_argc - 1], "]") != 0) {
            fprintf(stderr, "[: missing ']'\n");
            return 2;
        }
        expr_argc--; /* strip trailing "]" */
    }

    /* Zero-expression: false */
    if (expr_argc == 0) return 1;

    /* Set up global parse state */
    g_argv = expr_argv;
    g_argc = expr_argc;
    g_pos  = 0;
    g_error = 0;

    int result = parse_or();

    if (g_error) return 2;

    /* If we didn't consume all tokens, that's an error */
    if (g_pos < g_argc) {
        fprintf(stderr, "test: too many arguments\n");
        return 2;
    }

    return result ? 0 : 1;
}
