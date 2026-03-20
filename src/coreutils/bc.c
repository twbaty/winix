/*
 * bc.c — Winix coreutil
 *
 * Usage: bc [-l] [FILE...]
 *
 * An interactive arbitrary-precision calculator.
 * Uses IEEE-754 double (up to ~15 significant digits).
 *
 * Language subset:
 *   Arithmetic   : + - * / % ^  (unary -)
 *   Comparison   : == != < > <= >=
 *   Logic        : && || !
 *   Variables    : multi-char lowercase names
 *   Special vars : scale  ibase  obase
 *   Builtins     : sqrt() length() scale()
 *   With -l      : s() c() a() e() l()  (sin/cos/atan/exp/ln)
 *   Control      : if/else  while  for(;;)  break  quit
 *   Output       : print "str"  print expr  bare expr auto-prints
 *   Comments     : slash-star ... star-slash  or  # to end of line
 *
 * Exit codes: 0 success, 1 error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>

/* ── Tokens ──────────────────────────────────────────────────────────────── */

typedef enum {
    T_EOF, T_NUM, T_NAME, T_STR, T_NEWLINE, T_SEMI,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PCT, T_CARET,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_COMMA,
    T_ASSIGN,                       /* =  */
    T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
    T_AND, T_OR, T_NOT,
    T_IF, T_ELSE, T_WHILE, T_FOR, T_PRINT, T_QUIT, T_BREAK,
} Tok;

/* ── Parser state ─────────────────────────────────────────────────────────── */

typedef struct {
    const char *src;
    int         pos;
    Tok         tok;
    double      num;
    char        id[256];
    char        str[1024];
} P;

/* ── Global interpreter state ────────────────────────────────────────────── */

#define MAX_VARS 128
static struct { char name[64]; double val; } g_vars[MAX_VARS];
static int  g_nvars   = 0;
static int  g_scale   = 0;
static int  g_ibase   = 10;
static int  g_obase   = 10;
static bool g_math    = false;
static bool g_quit    = false;
static bool g_break   = false;
static bool g_assign  = false; /* last top-level expr was an assignment */

static double get_var(const char *n) {
    if (!strcmp(n,"scale")) return g_scale;
    if (!strcmp(n,"ibase")) return g_ibase;
    if (!strcmp(n,"obase")) return g_obase;
    for (int i=0; i<g_nvars; i++)
        if (!strcmp(g_vars[i].name, n)) return g_vars[i].val;
    return 0.0;
}
static void set_var(const char *n, double v) {
    if (!strcmp(n,"scale")) { g_scale = (int)v; return; }
    if (!strcmp(n,"ibase")) { g_ibase = (int)v; return; }
    if (!strcmp(n,"obase")) { g_obase = (int)v; return; }
    for (int i=0; i<g_nvars; i++)
        if (!strcmp(g_vars[i].name, n)) { g_vars[i].val = v; return; }
    if (g_nvars < MAX_VARS) {
        strncpy(g_vars[g_nvars].name, n, 63);
        g_vars[g_nvars++].val = v;
    }
}

/* ── Output ───────────────────────────────────────────────────────────────── */

static void print_num(double v) {
    if (g_obase == 16) {
        if (v == (long long)v) { printf("%llX\n", (long long)v); return; }
    }
    if (g_obase == 8) {
        if (v == (long long)v) { printf("%llo\n", (long long)v); return; }
    }
    if (g_obase == 2) {
        if (v == (long long)v) {
            long long n = (long long)v; char buf[128]; int bi=0;
            if (n==0) { puts("0"); return; }
            bool neg = n<0; if (neg) n=-n;
            while (n) { buf[bi++]='0'+(int)(n&1); n>>=1; }
            if (neg) buf[bi++]='-';
            for (int i=bi-1;i>=0;i--) putchar(buf[i]);
            putchar('\n'); return;
        }
    }
    if (g_scale == 0)
        printf("%.0f\n", v);
    else
        printf("%.*f\n", g_scale, v);
}

/* ── Lexer ────────────────────────────────────────────────────────────────── */

static void next(P *p) {
    /* Skip spaces and tabs; line continuation \<newline> */
    for (;;) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t') { p->pos++; continue; }
        if (c == '\\' && p->src[p->pos+1] == '\n') { p->pos += 2; continue; }
        break;
    }

    char c = p->src[p->pos];

    if (c == '\0') { p->tok = T_EOF;     return; }
    if (c == '\n') { p->pos++; p->tok = T_NEWLINE; return; }
    if (c == ';')  { p->pos++; p->tok = T_SEMI;    return; }

    /* Block comments */
    if (c == '/' && p->src[p->pos+1] == '*') {
        p->pos += 2;
        while (p->src[p->pos] && !(p->src[p->pos]=='*' && p->src[p->pos+1]=='/'))
            p->pos++;
        if (p->src[p->pos]) p->pos += 2;
        next(p); return;
    }
    /* Line comments */
    if (c == '#') {
        while (p->src[p->pos] && p->src[p->pos] != '\n') p->pos++;
        next(p); return;
    }

    /* Number */
    if (isdigit((unsigned char)c) ||
        (c == '.' && isdigit((unsigned char)p->src[p->pos+1]))) {
        char buf[64]; int bi = 0;
        while (isdigit((unsigned char)p->src[p->pos]) || p->src[p->pos] == '.')
            buf[bi++] = p->src[p->pos++];
        buf[bi] = '\0';
        p->num = atof(buf); p->tok = T_NUM; return;
    }

    /* Identifier / keyword */
    if (isalpha((unsigned char)c) || c == '_') {
        int bi = 0;
        while (isalnum((unsigned char)p->src[p->pos]) || p->src[p->pos] == '_')
            p->id[bi++] = p->src[p->pos++];
        p->id[bi] = '\0';
        if (!strcmp(p->id,"if"))    { p->tok=T_IF;    return; }
        if (!strcmp(p->id,"else"))  { p->tok=T_ELSE;  return; }
        if (!strcmp(p->id,"while")) { p->tok=T_WHILE; return; }
        if (!strcmp(p->id,"for"))   { p->tok=T_FOR;   return; }
        if (!strcmp(p->id,"print")) { p->tok=T_PRINT; return; }
        if (!strcmp(p->id,"break")) { p->tok=T_BREAK; return; }
        if (!strcmp(p->id,"quit") || !strcmp(p->id,"q")) { p->tok=T_QUIT; return; }
        p->tok = T_NAME; return;
    }

    /* String literal */
    if (c == '"') {
        p->pos++; int bi=0;
        while (p->src[p->pos] && p->src[p->pos] != '"') {
            if (p->src[p->pos] == '\\') {
                p->pos++;
                switch (p->src[p->pos]) {
                    case 'n': p->str[bi++]='\n'; break;
                    case 't': p->str[bi++]='\t'; break;
                    default:  p->str[bi++]=p->src[p->pos]; break;
                }
            } else { p->str[bi++] = p->src[p->pos]; }
            p->pos++;
        }
        if (p->src[p->pos]=='"') p->pos++;
        p->str[bi]='\0'; p->tok=T_STR; return;
    }

    /* Two-char operators */
    char d = p->src[p->pos+1];
    if (c=='=' && d=='=') { p->pos+=2; p->tok=T_EQ;  return; }
    if (c=='!' && d=='=') { p->pos+=2; p->tok=T_NE;  return; }
    if (c=='<' && d=='=') { p->pos+=2; p->tok=T_LE;  return; }
    if (c=='>' && d=='=') { p->pos+=2; p->tok=T_GE;  return; }
    if (c=='&' && d=='&') { p->pos+=2; p->tok=T_AND; return; }
    if (c=='|' && d=='|') { p->pos+=2; p->tok=T_OR;  return; }

    /* Single-char operators */
    p->pos++;
    switch (c) {
        case '+': p->tok=T_PLUS;   return;
        case '-': p->tok=T_MINUS;  return;
        case '*': p->tok=T_STAR;   return;
        case '/': p->tok=T_SLASH;  return;
        case '%': p->tok=T_PCT;    return;
        case '^': p->tok=T_CARET;  return;
        case '(': p->tok=T_LPAREN; return;
        case ')': p->tok=T_RPAREN; return;
        case '{': p->tok=T_LBRACE; return;
        case '}': p->tok=T_RBRACE; return;
        case ',': p->tok=T_COMMA;  return;
        case '=': p->tok=T_ASSIGN; return;
        case '<': p->tok=T_LT;     return;
        case '>': p->tok=T_GT;     return;
        case '!': p->tok=T_NOT;    return;
        default:
            fprintf(stderr, "bc: unexpected character '%c'\n", c);
            p->tok = T_EOF;
    }
}

/* ── Expression parser (recursive descent) ───────────────────────────────── */

static double parse_expr(P *p);
static void   parse_stmts(P *p);

static double parse_primary(P *p) {
    /* Unary minus */
    if (p->tok == T_MINUS) { next(p); return -parse_primary(p); }
    /* Logical NOT */
    if (p->tok == T_NOT)   { next(p); return parse_primary(p)==0.0 ? 1.0 : 0.0; }

    /* Parenthesised expression */
    if (p->tok == T_LPAREN) {
        next(p);
        double v = parse_expr(p);
        if (p->tok == T_RPAREN) next(p);
        return v;
    }

    /* Number literal */
    if (p->tok == T_NUM) {
        double v = p->num; next(p); return v;
    }

    /* String literal (print inline) */
    if (p->tok == T_STR) {
        printf("%s", p->str); fflush(stdout);
        next(p); return 0.0;
    }

    /* Name: assignment, function call, or variable read */
    if (p->tok == T_NAME) {
        char name[256]; strcpy(name, p->id);
        next(p);

        /* Assignment: name = expr */
        if (p->tok == T_ASSIGN) {
            next(p);
            double v = parse_expr(p);
            set_var(name, v);
            g_assign = true;
            return v;
        }

        /* Function call: name(args) */
        if (p->tok == T_LPAREN) {
            next(p);
            /* Collect up to 2 args */
            double a1 = 0.0;
            if (p->tok != T_RPAREN) {
                a1 = parse_expr(p);
                if (p->tok == T_COMMA) { next(p); parse_expr(p); }
            }
            if (p->tok == T_RPAREN) next(p);

            if (!strcmp(name,"sqrt")) {
                if (a1 < 0) { fputs("bc: sqrt of negative\n", stderr); return 0; }
                return sqrt(a1);
            }
            if (!strcmp(name,"length")) {
                /* number of significant digits in integer part */
                long long n = (long long)fabs(a1);
                if (n == 0) return 1;
                int d = 0; while (n) { d++; n/=10; }
                return d;
            }
            if (!strcmp(name,"scale")) {
                /* count fractional digits of argument */
                if (a1 == (long long)a1) return 0;
                char buf[64]; snprintf(buf, sizeof buf, "%.15f", a1);
                char *dot = strchr(buf, '.'); if (!dot) return 0;
                int n = (int)strlen(dot+1);
                while (n > 0 && dot[n] == '0') n--;
                return n;
            }
            if (g_math) {
                if (!strcmp(name,"s")) return sin(a1);
                if (!strcmp(name,"c")) return cos(a1);
                if (!strcmp(name,"a")) return atan(a1);
                if (!strcmp(name,"e")) return exp(a1);
                if (!strcmp(name,"l")) {
                    if (a1 <= 0) { fputs("bc: log of non-positive\n", stderr); return 0; }
                    return log(a1);
                }
                if (!strcmp(name,"j")) return 0.0; /* Bessel — stub */
            }
            fprintf(stderr, "bc: undefined function %s()\n", name);
            return 0.0;
        }

        /* Plain variable read */
        return get_var(name);
    }

    return 0.0;
}

/* Power: right-associative */
static double parse_power(P *p) {
    double v = parse_primary(p);
    if (p->tok == T_CARET) { next(p); return pow(v, parse_power(p)); }
    return v;
}

/* Multiply / divide / modulo */
static double parse_term(P *p) {
    double v = parse_power(p);
    while (p->tok==T_STAR || p->tok==T_SLASH || p->tok==T_PCT) {
        Tok op = p->tok; next(p);
        double r = parse_power(p);
        if (op == T_STAR) {
            v *= r;
        } else if (op == T_SLASH) {
            if (r == 0.0) { fputs("bc: division by zero\n", stderr); v = 0.0; }
            else {
                double sf = pow(10.0, g_scale);
                v = trunc(v / r * sf) / sf;
            }
        } else {
            if (r == 0.0) { fputs("bc: division by zero\n", stderr); v = 0.0; }
            else v = fmod(v, r);
        }
    }
    return v;
}

/* Add / subtract */
static double parse_add(P *p) {
    double v = parse_term(p);
    while (p->tok==T_PLUS || p->tok==T_MINUS) {
        Tok op = p->tok; next(p);
        double r = parse_term(p);
        v = (op==T_PLUS) ? v+r : v-r;
    }
    return v;
}

/* Comparisons */
static double parse_cmp(P *p) {
    double v = parse_add(p);
    while (p->tok==T_LT || p->tok==T_GT || p->tok==T_LE ||
           p->tok==T_GE || p->tok==T_EQ || p->tok==T_NE) {
        Tok op = p->tok; next(p); double r = parse_add(p);
        switch (op) {
            case T_LT: v = v < r  ? 1:0; break; case T_GT: v = v > r  ? 1:0; break;
            case T_LE: v = v <= r ? 1:0; break; case T_GE: v = v >= r ? 1:0; break;
            case T_EQ: v = v == r ? 1:0; break; case T_NE: v = v != r ? 1:0; break;
            default: break;
        }
    }
    return v;
}

/* Logical AND / OR */
static double parse_expr(P *p) {
    double v = parse_cmp(p);
    while (p->tok==T_AND || p->tok==T_OR) {
        Tok op = p->tok; next(p); double r = parse_cmp(p);
        v = (op==T_AND) ? ((v!=0&&r!=0)?1:0) : ((v!=0||r!=0)?1:0);
    }
    return v;
}

/* ── Skip newlines / semicolons ──────────────────────────────────────────── */

static void skip_sep(P *p) {
    while (p->tok==T_NEWLINE || p->tok==T_SEMI) next(p);
}

/* ── Statement block: { stmts } or single stmt ──────────────────────────── */

static void parse_block(P *p) {
    if (p->tok == T_LBRACE) {
        next(p); skip_sep(p);
        while (p->tok!=T_RBRACE && p->tok!=T_EOF && !g_quit && !g_break)
            parse_stmts(p);
        if (p->tok == T_RBRACE) next(p);
    } else {
        parse_stmts(p);
    }
}

/* ── Statement parser ────────────────────────────────────────────────────── */

static void parse_stmts(P *p) {
    skip_sep(p);
    if (p->tok==T_EOF || p->tok==T_RBRACE || g_quit) return;

    /* quit */
    if (p->tok == T_QUIT) { g_quit = true; return; }

    /* break */
    if (p->tok == T_BREAK) { next(p); g_break = true; return; }

    /* print */
    if (p->tok == T_PRINT) {
        next(p);
        for (;;) {
            if (p->tok == T_STR) { printf("%s", p->str); next(p); }
            else                 { print_num(parse_expr(p)); }
            if (p->tok != T_COMMA) break;
            next(p);
        }
        skip_sep(p);
        return;
    }

    /* if / else */
    if (p->tok == T_IF) {
        next(p);
        if (p->tok==T_LPAREN) next(p);
        double cond = parse_expr(p);
        if (p->tok==T_RPAREN) next(p);
        skip_sep(p);
        if (cond != 0.0) {
            parse_block(p);
            skip_sep(p);
            if (p->tok == T_ELSE) { next(p); skip_sep(p); /* skip else branch */
                if (p->tok == T_LBRACE) {
                    int depth = 1; next(p);
                    while (depth>0 && p->tok!=T_EOF) {
                        if (p->tok==T_LBRACE) depth++;
                        else if (p->tok==T_RBRACE) depth--;
                        next(p);
                    }
                } else {
                    while (p->tok!=T_NEWLINE && p->tok!=T_SEMI && p->tok!=T_EOF)
                        next(p);
                }
            }
        } else {
            /* skip then-branch */
            if (p->tok == T_LBRACE) {
                int depth = 1; next(p);
                while (depth>0 && p->tok!=T_EOF) {
                    if (p->tok==T_LBRACE) depth++;
                    else if (p->tok==T_RBRACE) depth--;
                    next(p);
                }
            } else {
                while (p->tok!=T_NEWLINE && p->tok!=T_SEMI && p->tok!=T_EOF)
                    next(p);
            }
            skip_sep(p);
            if (p->tok == T_ELSE) { next(p); skip_sep(p); parse_block(p); }
        }
        skip_sep(p);
        return;
    }

    /* while (cond) { body } */
    if (p->tok == T_WHILE) {
        next(p);
        /* Save cond_pos before advancing past '(' — p->pos already points to
           the first byte of the condition when tok == T_LPAREN. */
        int cond_pos = p->pos;
        if (p->tok==T_LPAREN) next(p);
        double cond = parse_expr(p);
        if (p->tok==T_RPAREN) next(p);
        skip_sep(p);

        /* Parse body once to find its extent.
         * When tok==T_LBRACE, next() already advanced past '{', so
         * p->pos-1 is the '{' itself — include it so parse_block sees
         * the opening brace and handles multi-statement bodies correctly. */
        int body_start = (p->tok == T_LBRACE) ? p->pos - 1 : p->pos;
        if (p->tok == T_LBRACE) {
            int d=1; next(p);
            while (d>0 && p->tok!=T_EOF) {
                if (p->tok==T_LBRACE) d++;
                else if (p->tok==T_RBRACE) d--;
                next(p);
            }
        } else {
            while (p->tok!=T_NEWLINE && p->tok!=T_SEMI && p->tok!=T_EOF) next(p);
        }
        int body_end = p->pos;

        /* Now loop */
        while (cond != 0.0 && !g_quit) {
            char saved[body_end - body_start + 1];
            memcpy(saved, p->src + body_start, body_end - body_start);
            saved[body_end - body_start] = '\0';
            P bp; bp.src = saved; bp.pos = 0; next(&bp); skip_sep(&bp);
            g_break = false;
            parse_block(&bp);
            if (g_break) { g_break = false; break; }
            /* Re-evaluate condition */
            P cp; cp.src = p->src; cp.pos = cond_pos; next(&cp);
            cond = parse_expr(&cp);
        }
        skip_sep(p);
        return;
    }

    /* for (init; cond; update) { body } */
    if (p->tok == T_FOR) {
        next(p);
        if (p->tok==T_LPAREN) next(p);

        /* init */
        if (p->tok!=T_SEMI) { g_assign=false; parse_expr(p); }
        /* Save cond_pos before advancing — when tok==T_SEMI, p->pos already
           points to the first byte of the condition expression. */
        int cond_pos = p->pos;
        if (p->tok==T_SEMI) next(p);

        /* cond */
        double cond = 1.0;
        if (p->tok!=T_SEMI) cond = parse_expr(p);
        /* Save update_pos before advancing — same reasoning as cond_pos. */
        int update_pos = p->pos;
        if (p->tok==T_SEMI) next(p);

        /* update — scan to ')' to find its extent */
        while (p->tok!=T_RPAREN && p->tok!=T_EOF) next(p);
        int update_end = p->pos;
        if (p->tok==T_RPAREN) next(p);
        skip_sep(p);

        /* body — scan to find extent (include '{' so parse_block sees it) */
        int body_start = (p->tok == T_LBRACE) ? p->pos - 1 : p->pos;
        if (p->tok == T_LBRACE) {
            int d=1; next(p);
            while (d>0 && p->tok!=T_EOF) {
                if (p->tok==T_LBRACE) d++;
                else if (p->tok==T_RBRACE) d--;
                next(p);
            }
        } else {
            while (p->tok!=T_NEWLINE && p->tok!=T_SEMI && p->tok!=T_EOF) next(p);
        }
        int body_end = p->pos;

        /* Loop */
        while (cond != 0.0 && !g_quit) {
            /* Execute body */
            int blen = body_end - body_start;
            char *body_src = (char *)malloc(blen + 1);
            if (!body_src) break;
            memcpy(body_src, p->src + body_start, blen);
            body_src[blen] = '\0';
            P bp; bp.src = body_src; bp.pos = 0; next(&bp); skip_sep(&bp);
            g_break = false;
            parse_block(&bp);
            free(body_src);
            if (g_break) { g_break = false; break; }

            /* Execute update */
            int ulen = update_end - update_pos;
            char *upd_src = (char *)malloc(ulen + 1);
            if (!upd_src) break;
            memcpy(upd_src, p->src + update_pos, ulen);
            upd_src[ulen] = '\0';
            P up; up.src = upd_src; up.pos = 0; next(&up);
            g_assign = false; parse_expr(&up);
            free(upd_src);

            /* Re-evaluate condition */
            P cp; cp.src = p->src; cp.pos = cond_pos; next(&cp);
            if (p->src[cond_pos] == ';' || update_pos == cond_pos)
                cond = 1.0; /* empty condition = always true */
            else
                cond = parse_expr(&cp);
        }
        skip_sep(p);
        return;
    }

    /* Expression statement (may auto-print) */
    g_assign = false;
    double v = parse_expr(p);
    if (!g_assign) print_num(v);
    skip_sep(p);
}

/* ── Run a source string ──────────────────────────────────────────────────── */

static void run_source(const char *src) {
    P p; p.src = src; p.pos = 0;
    next(&p);
    while (p.tok != T_EOF && !g_quit)
        parse_stmts(&p);
}

/* ── Interactive REPL ────────────────────────────────────────────────────── */

static void repl(void) {
    char line[4096];
    /* Buffer for multi-line input (e.g. while loops) */
    char buf[65536]; buf[0] = '\0';
    int  depth = 0;   /* open brace depth */

    for (;;) {
        fputs(depth > 0 ? "  " : "", stdout);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* Count brace depth to detect multi-line blocks */
        for (int i = 0; line[i]; i++) {
            if (line[i] == '{') depth++;
            else if (line[i] == '}') depth--;
        }

        if (strlen(buf) + strlen(line) < sizeof(buf) - 1)
            strcat(buf, line);

        if (depth <= 0) {
            depth = 0;
            run_source(buf);
            buf[0] = '\0';
            if (g_quit) break;
        }
    }
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

static void print_usage(void) {
    puts("Usage: bc [-l] [FILE...]");
    puts("An interactive calculator.");
    puts("");
    puts("  -l   load math library (s c a e l functions; scale=20)");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
    puts("");
    puts("Special variables: scale  ibase  obase");
    puts("Builtins:          sqrt(x)  length(x)  scale(x)");
    puts("With -l:           s(x) c(x) a(x) e(x) l(x)");
    puts("Control:           if(c){} else{}  while(c){}  for(;;){}  quit");
}

int main(int argc, char *argv[]) {
    int   first_file = argc;
    bool  have_files = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help"))    { print_usage(); return 0; }
        if (!strcmp(argv[i], "--version")) { puts("bc 1.0 (Winix 1.4)"); return 0; }
        if (!strcmp(argv[i], "-l")) {
            g_math  = true;
            g_scale = 20;
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "bc: invalid option -- '%s'\n", argv[i]);
            return 1;
        }
        if (!have_files) { first_file = i; have_files = true; }
    }

    if (have_files) {
        for (int i = first_file; i < argc; i++) {
            if (argv[i][0] == '-') continue;
            FILE *f = fopen(argv[i], "r");
            if (!f) { fprintf(stderr, "bc: %s: No such file or directory\n", argv[i]); continue; }
            fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
            char *src = (char *)malloc(sz + 1);
            if (!src) { fclose(f); continue; }
            sz = (long)fread(src, 1, sz, f); src[sz] = '\0';
            fclose(f);
            run_source(src);
            free(src);
            if (g_quit) break;
        }
    } else {
        repl();
    }
    return 0;
}
