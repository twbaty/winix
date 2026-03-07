/*
 * expr — evaluate expressions
 *
 * Usage: expr EXPRESSION
 *
 * Arithmetic:   expr 3 + 4      expr 10 % 3
 * Comparison:   expr 3 \< 4     (returns 1 if true, 0 if false)
 * Logic:        expr A \| B     (B if A is 0 or empty, else A)
 *               expr A \& B     (A if neither is 0/empty, else 0)
 * Strings:      expr length STR
 *               expr substr STR POS LEN
 *               expr index  STR CHARS
 *               expr match  STR REGEX
 *
 * Exit: 0 = result is non-zero/non-empty
 *       1 = result is zero or empty
 *       2 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define VERSION "1.0"

static int  g_argc;
static char **g_argv;
static int  g_pos;

static const char *peek(void) {
    return g_pos < g_argc ? g_argv[g_pos] : NULL;
}
static const char *consume(void) {
    return g_pos < g_argc ? g_argv[g_pos++] : NULL;
}

static int is_integer(const char *s, long long *out) {
    if (!s || !*s) return 0;
    char *end;
    *out = strtoll(s, &end, 10);
    return *end == '\0' && (s[0] == '-' || isdigit((unsigned char)s[0]));
}

static int is_truthy(const char *s) {
    if (!s || !*s || !strcmp(s, "0")) return 0;
    return 1;
}

/* Forward declarations */
static char *eval_expr(void);

/* ── String operations ──────────────────────────────────────── */

static char *eval_string_op(const char *op) {
    char buf[256];
    if (!strcmp(op, "length")) {
        const char *s = consume();
        if (!s) { fprintf(stderr, "expr: missing operand\n"); exit(2); }
        snprintf(buf, sizeof(buf), "%d", (int)strlen(s));
        return strdup(buf);
    }
    if (!strcmp(op, "substr")) {
        const char *s   = consume();
        const char *ps  = consume();
        const char *ls  = consume();
        if (!s || !ps || !ls) { fprintf(stderr, "expr: missing operand\n"); exit(2); }
        int pos = atoi(ps) - 1;  /* 1-based */
        int len = atoi(ls);
        int slen = (int)strlen(s);
        if (pos < 0) pos = 0;
        if (pos >= slen || len <= 0) return strdup("");
        if (pos + len > slen) len = slen - pos;
        char *r = (char *)malloc((size_t)len + 1);
        memcpy(r, s + pos, (size_t)len);
        r[len] = '\0';
        return r;
    }
    if (!strcmp(op, "index")) {
        const char *s = consume();
        const char *c = consume();
        if (!s || !c) { fprintf(stderr, "expr: missing operand\n"); exit(2); }
        size_t pos = strcspn(s, c);
        snprintf(buf, sizeof(buf), "%d", (int)(pos < strlen(s) ? pos + 1 : 0));
        return strdup(buf);
    }
    if (!strcmp(op, "match")) {
        /* Basic: return length of match at start (anchored), else 0.
         * Supports: . * [class] ^ anchors only — enough for common use. */
        const char *s  = consume();
        const char *re = consume();
        if (!s || !re) { fprintf(stderr, "expr: missing operand\n"); exit(2); }
        /* Simple prefix match: just check literal prefix for now */
        int mlen = 0;
        const char *sp = s;
        const char *rp = (*re == '^') ? re + 1 : re;
        /* Walk regex manually — very basic */
        while (*rp && *sp) {
            if (rp[1] == '*') {
                char pat = rp[0];
                rp += 2;
                while (*sp && (pat == '.' || *sp == pat)) { sp++; mlen++; }
            } else if (*rp == '.') {
                rp++; sp++; mlen++;
            } else if (*rp == *sp) {
                rp++; sp++; mlen++;
            } else {
                mlen = 0; break;
            }
        }
        snprintf(buf, sizeof(buf), "%d", *rp ? 0 : mlen);
        return strdup(buf);
    }
    return NULL;
}

/* ── Grammar (recursive descent, loosely POSIX) ─────────────── */

/* or-expr: A | B */
static char *eval_or(void) {
    char *left = eval_expr();
    const char *op = peek();
    if (op && !strcmp(op, "|")) {
        consume();
        char *right = eval_or();
        char *res = is_truthy(left) ? left : right;
        if (res == left) free(right); else free(left);
        return res;
    }
    return left;
}

/* and-expr: A & B */
static char *eval_and(void) {
    char *left = eval_expr();
    const char *op = peek();
    if (op && !strcmp(op, "&")) {
        consume();
        char *right = eval_and();
        char *res;
        if (is_truthy(left) && is_truthy(right)) res = left;
        else { res = strdup("0"); free(left); }
        free(right);
        return res;
    }
    return left;
}

/* comparison-expr: A = B, A != B, A < B, A <= B, A > B, A >= B */
static char *eval_cmp(void) {
    char *left = eval_expr();
    const char *op = peek();
    if (op && (!strcmp(op,"=") || !strcmp(op,"!=") ||
               !strcmp(op,"<") || !strcmp(op,"<=") ||
               !strcmp(op,">") || !strcmp(op,">="))) {
        consume();
        char *right = eval_expr();
        long long li, ri;
        int result;
        if (is_integer(left, &li) && is_integer(right, &ri)) {
            if      (!strcmp(op,"="))  result = li == ri;
            else if (!strcmp(op,"!=")) result = li != ri;
            else if (!strcmp(op,"<"))  result = li <  ri;
            else if (!strcmp(op,"<=")) result = li <= ri;
            else if (!strcmp(op,">"))  result = li >  ri;
            else                       result = li >= ri;
        } else {
            int c = strcmp(left, right);
            if      (!strcmp(op,"="))  result = c == 0;
            else if (!strcmp(op,"!=")) result = c != 0;
            else if (!strcmp(op,"<"))  result = c <  0;
            else if (!strcmp(op,"<=")) result = c <= 0;
            else if (!strcmp(op,">"))  result = c >  0;
            else                       result = c >= 0;
        }
        free(left); free(right);
        return strdup(result ? "1" : "0");
    }
    return left;
}

/* add-expr: A + B, A - B */
static char *eval_add(void) {
    char *left = eval_expr();
    const char *op = peek();
    while (op && (!strcmp(op,"+") || !strcmp(op,"-"))) {
        consume();
        char *right = eval_expr();
        long long li, ri;
        if (!is_integer(left, &li) || !is_integer(right, &ri)) {
            fprintf(stderr, "expr: non-integer argument\n"); exit(2);
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld",
                 !strcmp(op,"+") ? li + ri : li - ri);
        free(left); free(right);
        left = strdup(buf);
        op = peek();
    }
    return left;
}

/* mul-expr: A * B, A / B, A % B */
static char *eval_mul(void) {
    char *left = eval_expr();
    const char *op = peek();
    while (op && (!strcmp(op,"*") || !strcmp(op,"/") || !strcmp(op,"%"))) {
        consume();
        char *right = eval_expr();
        long long li, ri;
        if (!is_integer(left, &li) || !is_integer(right, &ri)) {
            fprintf(stderr, "expr: non-integer argument\n"); exit(2);
        }
        if ((strcmp(op,"/")==0 || strcmp(op,"%")==0) && ri == 0) {
            fprintf(stderr, "expr: division by zero\n"); exit(2);
        }
        char buf[64];
        long long res = !strcmp(op,"*") ? li*ri :
                        !strcmp(op,"/") ? li/ri : li%ri;
        snprintf(buf, sizeof(buf), "%lld", res);
        free(left); free(right);
        left = strdup(buf);
        op = peek();
    }
    return left;
}

/* primary: number, string, string-op, or ( expr ) */
static char *eval_expr(void) {
    const char *t = peek();
    if (!t) { fprintf(stderr, "expr: missing operand\n"); exit(2); }

    /* Parentheses */
    if (!strcmp(t, "(")) {
        consume();
        char *val = eval_or();
        const char *close = consume();
        if (!close || strcmp(close, ")"))
            { fprintf(stderr, "expr: missing ')'\n"); exit(2); }
        return val;
    }

    /* String operations */
    if (!strcmp(t,"length") || !strcmp(t,"substr") ||
        !strcmp(t,"index")  || !strcmp(t,"match")) {
        consume();
        char *r = eval_string_op(t);
        if (r) return r;
    }

    /* Literal token */
    consume();
    return strdup(t);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "expr: missing operand\nTry 'expr --help'.\n");
        return 2;
    }
    if (!strcmp(argv[1], "--version")) { printf("expr %s (Winix)\n", VERSION); return 0; }
    if (!strcmp(argv[1], "--help")) {
        fprintf(stderr,
            "usage: expr EXPRESSION\n\n"
            "Evaluate EXPRESSION and print result.\n\n"
            "  Arithmetic:  expr NUM + NUM   - * / %%\n"
            "  Compare:     expr A = B       != < <= > >=\n"
            "  Logic:       expr A | B       A & B\n"
            "  Strings:     expr length STR\n"
            "               expr substr STR POS LEN\n"
            "               expr index  STR CHARS\n"
            "               expr match  STR REGEX\n\n"
            "Exit: 0=non-zero result  1=zero/empty result  2=error\n");
        return 0;
    }

    g_argc = argc - 1;
    g_argv = argv + 1;
    g_pos  = 0;

    char *result = eval_or();
    puts(result);

    int exit_code = is_truthy(result) ? 0 : 1;
    free(result);
    return exit_code;
}
