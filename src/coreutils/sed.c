/*
 * sed.c — POSIX sed for Winix
 *
 * Supports:
 *   Options : -n -e SCRIPT -f FILE -E/-r -i --
 *   Commands: s d p q = a i y
 *   Addresses: line, $, /regex/, ranges (N,M  N,+M  /re/,/re/), negation (!)
 *   Replacement: & \1-\9 \n \\ in s command
 *
 * Uses a self-contained regex engine (no regex.h required).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

/* ================================================================== */
/* Minimal regex engine                                                 */
/* Supports: . * + ? ^ $ [...] \(...\) BRE groups / (...) ERE groups   */
/*           \1-\9 backrefs, | ERE alternation, \+ \? GNU extensions   */
/* ================================================================== */

#define REG_EXTENDED  0x01
#define REG_ICASE     0x02
#define REG_NOTBOL    0x04
#define REG_NOMATCH   1
#define RE_MAXCAP     10

typedef struct { int rm_so, rm_eo; } regmatch_t;
typedef struct { char *pat; int flags; } regex_t;

typedef struct {
    const char *s;
    int slen;
    int flags;  /* combined REG_ICASE | REG_NOTBOL */
    int icase;
    int ere;
    int gs[RE_MAXCAP], ge[RE_MAXCAP]; /* capture group start/end (-1 = unset) */
    int ncap;
} RCtx;

/* forward */
static int rmatch_here(RCtx *ctx, const char *p, int si);

static int re_eq(RCtx *ctx, unsigned char a, unsigned char b)
{
    if (ctx->icase) return tolower(a) == tolower(b);
    return a == b;
}

/* Match character class starting after '['. Sets *endp past ']'. */
static int match_class(RCtx *ctx, const char *p, unsigned char c, const char **endp)
{
    int negate = 0;
    if (*p == '^') { negate = 1; p++; }
    unsigned char lc = ctx->icase ? (unsigned char)tolower(c) : c;
    int matched = 0;
    /* first ] is literal */
    if (*p == ']') {
        if (tolower(']') == lc || ']' == c) matched = 1;
        p++;
    }
    while (*p && *p != ']') {
        if (*p == '[' && p[1] == ':') {
            const char *q = p + 2;
            while (*q && !(*q == ':' && q[1] == ']')) q++;
            char cn[16] = {0};
            int nl = (int)(q - (p + 2));
            if (nl < 15) { memcpy(cn, p + 2, nl); }
            if      (!strcmp(cn,"alpha"))  matched |= isalpha(lc);
            else if (!strcmp(cn,"digit"))  matched |= isdigit(lc);
            else if (!strcmp(cn,"alnum"))  matched |= isalnum(lc);
            else if (!strcmp(cn,"space"))  matched |= isspace(lc);
            else if (!strcmp(cn,"upper"))  matched |= (ctx->icase ? isalpha(lc) : isupper(c));
            else if (!strcmp(cn,"lower"))  matched |= (ctx->icase ? isalpha(lc) : islower(c));
            else if (!strcmp(cn,"print"))  matched |= isprint(lc);
            else if (!strcmp(cn,"punct"))  matched |= ispunct(lc);
            else if (!strcmp(cn,"blank"))  matched |= (c==' '||c=='\t');
            else if (!strcmp(cn,"cntrl"))  matched |= iscntrl(c);
            else if (!strcmp(cn,"xdigit")) matched |= isxdigit(c);
            p = (*q == ':') ? q + 2 : q;
        } else if (p[1] == '-' && p[2] != '\0' && p[2] != ']') {
            unsigned char lo = ctx->icase ? (unsigned char)tolower(p[0]) : (unsigned char)p[0];
            unsigned char hi = ctx->icase ? (unsigned char)tolower(p[2]) : (unsigned char)p[2];
            if (lc >= lo && lc <= hi) matched = 1;
            p += 3;
        } else {
            unsigned char cc = ctx->icase ? (unsigned char)tolower(*p) : (unsigned char)*p;
            if (cc == lc) matched = 1;
            p++;
        }
    }
    if (*p == ']') p++;
    if (endp) *endp = p;
    return negate ? !matched : matched;
}

/* Skip past one atom (not including quantifier) in pattern. */
static const char *skip_atom(const char *p, int ere)
{
    if (!*p) return p;
    if (*p == '\\') {
        if (!p[1]) return p + 1;
        if (!ere && (p[1] == '(' || p[1] == ')')) return p + 2;
        return p + 2;
    }
    if (ere && (*p == '(' || *p == ')')) return p + 1;
    if (*p == '[') {
        const char *q = p + 1;
        if (*q == '^') q++;
        if (*q == ']') q++;
        while (*q && *q != ']') {
            if (*q == '[' && q[1] == ':') {
                q += 2;
                while (*q && !(*q == ':' && q[1] == ']')) q++;
                if (*q) q += 2;
            } else q++;
        }
        return (*q == ']') ? q + 1 : q;
    }
    return p + 1;
}

/* Get quantifier at p (if any). Returns # of pattern chars consumed (0/1/2). */
static int get_quant(const char *p, int ere, int *rmin, int *rmax)
{
    if (ere) {
        if (*p == '*') { *rmin = 0; *rmax = -1; return 1; }
        if (*p == '+') { *rmin = 1; *rmax = -1; return 1; }
        if (*p == '?') { *rmin = 0; *rmax =  1; return 1; }
    } else {
        if (*p == '*')               { *rmin = 0; *rmax = -1; return 1; }
        if (*p == '\\' && p[1]=='+') { *rmin = 1; *rmax = -1; return 2; }
        if (*p == '\\' && p[1]=='?') { *rmin = 0; *rmax =  1; return 2; }
    }
    return 0;
}

/* Find closing group delimiter matching an open group at p (p points after open paren). */
static const char *find_group_end(const char *p, int ere)
{
    int depth = 1;
    while (*p && depth > 0) {
        if (*p == '\\' && p[1]) {
            if (!ere && p[1] == '(') depth++;
            else if (!ere && p[1] == ')') { if (--depth == 0) { p += 2; break; } }
            p += 2;
        } else if (ere) {
            if (*p == '[') { p = skip_atom(p, ere); continue; }
            if (*p == '(') depth++;
            else if (*p == ')') { if (--depth == 0) { p++; break; } }
            p++;
        } else {
            if (*p == '[') { p = skip_atom(p, ere); continue; }
            p++;
        }
    }
    return p;
}

/* Try to match atom at p against ctx->s[si].
 * On success: *new_si = new subject pos, *atom_end = end of atom in pattern. Returns 1.
 * On fail: returns 0. */
static int try_atom(RCtx *ctx, const char *p, int si, int *new_si, const char **atom_end)
{
    unsigned char c = (si < ctx->slen) ? (unsigned char)ctx->s[si] : 0;

    if (*p == '.') {
        *atom_end = p + 1;
        if (si < ctx->slen) { *new_si = si + 1; return 1; }
        return 0;
    }
    if (*p == '[') {
        const char *end;
        int m = match_class(ctx, p + 1, c, &end);
        *atom_end = end;
        if (m && si < ctx->slen) { *new_si = si + 1; return 1; }
        return 0;
    }
    /* backreference \1-\9 */
    if (*p == '\\' && p[1] >= '1' && p[1] <= '9') {
        *atom_end = p + 2;
        int gn = p[1] - '1';
        if (gn >= ctx->ncap || ctx->gs[gn] < 0) return 0;
        int glen = ctx->ge[gn] - ctx->gs[gn];
        if (glen < 0 || si + glen > ctx->slen) return 0;
        int ok;
        if (ctx->icase) {
            ok = 1;
            for (int i = 0; i < glen; i++)
                if (tolower((unsigned char)ctx->s[si+i]) != tolower((unsigned char)ctx->s[ctx->gs[gn]+i]))
                    { ok = 0; break; }
        } else {
            ok = (memcmp(ctx->s + si, ctx->s + ctx->gs[gn], (size_t)glen) == 0);
        }
        if (ok) { *new_si = si + glen; return 1; }
        return 0;
    }
    /* escaped literal */
    if (*p == '\\' && p[1]) {
        *atom_end = p + 2;
        unsigned char ec = (unsigned char)p[1];
        if (ec == 'n') ec = '\n';
        else if (ec == 't') ec = '\t';
        if (si < ctx->slen && re_eq(ctx, c, ec)) { *new_si = si + 1; return 1; }
        return 0;
    }
    /* literal */
    *atom_end = p + 1;
    if (si < ctx->slen && re_eq(ctx, c, (unsigned char)*p)) { *new_si = si + 1; return 1; }
    return 0;
}

/* Greedy quantifier: match atom 0..max times, then try rest. */
static int rmatch_quant(RCtx *ctx, const char *atom_p, const char *rest, int si, int mn, int mx)
{
    /* collect positions */
    int pos[8192];
    int cnt = 0;
    pos[cnt++] = si;
    int cur = si;
    while (mx < 0 || cnt - 1 < mx) {
        const char *ae;
        int ns;
        if (!try_atom(ctx, atom_p, cur, &ns, &ae)) break;
        if (ns == cur) break; /* zero-length: prevent infinite loop */
        pos[cnt++] = ns;
        cur = ns;
        if (cnt >= 8191) break;
    }
    /* try from longest to shortest */
    for (int i = cnt - 1; i >= mn; i--) {
        int r = rmatch_here(ctx, rest, pos[i]);
        if (r >= 0) return r;
    }
    return -1;
}

/* Recursively match alternation: try each branch of re|... at si. */
static int rmatch_alt(RCtx *ctx, const char *p, int si)
{
    /* Save and restore capture state for each alternative */
    int save_gs[RE_MAXCAP], save_ge[RE_MAXCAP], save_nc;
    while (1) {
        /* save state */
        memcpy(save_gs, ctx->gs, sizeof(ctx->gs));
        memcpy(save_ge, ctx->ge, sizeof(ctx->ge));
        save_nc = ctx->ncap;
        int r = rmatch_here(ctx, p, si);
        if (r >= 0) return r;
        /* restore state, find next | at depth 0 */
        memcpy(ctx->gs, save_gs, sizeof(ctx->gs));
        memcpy(ctx->ge, save_ge, sizeof(ctx->ge));
        ctx->ncap = save_nc;
        /* skip this branch */
        int depth = 0;
        while (*p) {
            if (*p == '\\' && p[1]) {
                if (!ctx->ere) {
                    if (p[1]=='(') depth++;
                    else if (p[1]==')') depth--;
                }
                p += 2; continue;
            }
            if (ctx->ere) {
                if (*p == '(') { depth++; p++; continue; }
                if (*p == ')') { depth--; if (depth < 0) break; p++; continue; }
                if (*p == '|' && depth == 0) { p++; break; }
            }
            if (*p == '[') { p = skip_atom(p, ctx->ere); continue; }
            p++;
        }
        if (!*p || (ctx->ere && *p == ')' && depth < 0)) return -1;
        /* p now points to start of next alternative or end */
    }
}

static int rmatch_here(RCtx *ctx, const char *p, int si)
{
tail:
    /* end of pattern */
    if (!*p) return si;

    /* ERE: hit | or ) at depth 0 means end of this alternative/group */
    if (ctx->ere && (*p == '|' || *p == ')')) return si;

    /* $ anchor */
    if (*p == '$') {
        const char *np = p + 1;
        if (!*np || (ctx->ere && (*np=='|'||*np==')'))) {
            return (si == ctx->slen) ? si : -1;
        }
    }

    /* group open */
    int is_open = (ctx->ere && *p == '(') ||
                  (!ctx->ere && *p == '\\' && p[1] == '(');
    if (is_open) {
        int gn = ctx->ncap++;
        if (gn >= RE_MAXCAP) gn = RE_MAXCAP - 1;
        const char *inner = ctx->ere ? p + 1 : p + 2;
        const char *gend  = find_group_end(inner, ctx->ere);
        /* gend points past closing paren */
        const char *rest  = gend;

        /* quantifier after group? */
        int mn, mx;
        int ql = get_quant(rest, ctx->ere, &mn, &mx);
        const char *after_quant = rest + ql;

        /* save group state */
        int old_gs = ctx->gs[gn], old_ge = ctx->ge[gn];

        if (ql == 0) {
            /* no quantifier: match inner with alternation, then continue */
            ctx->gs[gn] = si;
            int r = rmatch_alt(ctx, inner, si);
            if (r < 0) { ctx->ncap--; return -1; }
            ctx->ge[gn] = r;
            p = rest; si = r;
            goto tail;
        } else {
            /* quantifier on group: try greedily */
            /* collect possible end positions */
            int pos[8192]; int cnt = 0;
            pos[cnt++] = si;
            int cur = si;
            while (mx < 0 || cnt - 1 < mx) {
                ctx->gs[gn] = cur; ctx->ge[gn] = -1;
                int r = rmatch_alt(ctx, inner, cur);
                if (r < 0) break;
                if (r == cur) break; /* zero-length */
                ctx->ge[gn] = r;
                pos[cnt++] = r;
                cur = r;
                if (cnt >= 8191) break;
            }
            for (int i = cnt - 1; i >= mn; i--) {
                int r2 = rmatch_here(ctx, after_quant, pos[i]);
                if (r2 >= 0) return r2;
            }
            ctx->gs[gn] = old_gs; ctx->ge[gn] = old_ge; ctx->ncap--;
            return -1;
        }
    }

    /* group close (BRE \)) — shouldn't normally be reached here */
    if (!ctx->ere && *p == '\\' && p[1] == ')') return si;

    /* ERE alternation at depth 0 handled above; BRE has no | */
    /* Handle ERE | inside rmatch_alt, not here */

    /* regular atom */
    const char *atom_start = p;
    const char *atom_end   = skip_atom(p, ctx->ere);

    int mn, mx;
    int ql = get_quant(atom_end, ctx->ere, &mn, &mx);
    const char *rest = atom_end + ql;

    if (ql == 0) {
        /* no quantifier — must match once */
        const char *ae;
        int ns;
        if (!try_atom(ctx, atom_start, si, &ns, &ae)) return -1;
        p = rest; si = ns;
        goto tail;
    } else {
        return rmatch_quant(ctx, atom_start, rest, si, mn, mx);
    }
}

/* Search for pat anywhere in ctx->s starting at from. Returns match start or -1. */
static int rmatch_search(RCtx *ctx, const char *pat, int from, regmatch_t *pm, int npm)
{
    int notbol = ctx->flags & REG_NOTBOL;
    /* ^ at start only matches if we start at 0 and not NOTBOL */
    int anchored = (pat[0] == '^');

    int start = from;
    int end   = ctx->slen;
    for (int i = start; i <= end; i++) {
        if (anchored && (i > start || notbol)) break;
        /* init capture state */
        for (int g = 0; g < RE_MAXCAP; g++) { ctx->gs[g] = -1; ctx->ge[g] = -1; }
        ctx->ncap = 0;

        const char *p = pat;
        if (*p == '^') p++; /* skip ^ anchor — we already handled positioning */

        int r;
        if (ctx->ere)
            r = rmatch_alt(ctx, p, i);
        else
            r = rmatch_here(ctx, p, i);

        if (r >= 0) {
            if (pm && npm > 0) {
                pm[0].rm_so = i; pm[0].rm_eo = r;
                for (int g = 1; g < npm && g <= ctx->ncap; g++) {
                    pm[g].rm_so = ctx->gs[g-1];
                    pm[g].rm_eo = ctx->ge[g-1];
                }
                for (int g = ctx->ncap + 1; g < npm; g++) {
                    pm[g].rm_so = -1; pm[g].rm_eo = -1;
                }
            }
            return i;
        }
        if (i >= end) break;
    }
    return -1;
}

/* POSIX-like interface */
int regcomp(regex_t *re, const char *pat, int flags)
{
    re->pat   = strdup(pat);
    re->flags = flags;
    return re->pat ? 0 : 1;
}

int regexec(const regex_t *re, const char *str, size_t nmatch, regmatch_t pmatch[], int eflags)
{
    RCtx ctx;
    ctx.s     = str;
    ctx.slen  = (int)strlen(str);
    ctx.flags = re->flags | eflags;
    ctx.icase = (ctx.flags & REG_ICASE) != 0;
    ctx.ere   = (re->flags & REG_EXTENDED) != 0;
    ctx.ncap  = 0;
    for (int i = 0; i < RE_MAXCAP; i++) ctx.gs[i] = ctx.ge[i] = -1;

    int r = rmatch_search(&ctx, re->pat, 0, pmatch, (int)nmatch);
    return (r >= 0) ? 0 : REG_NOMATCH;
}

void regfree(regex_t *re)
{
    free(re->pat);
    re->pat = NULL;
}

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define MAX_CMDS      256
#define MAX_SCRIPTS   64
#define PAT_SIZE      65536
#define ADDR_PAT_SIZE 256

/* s-command flag bits */
#define S_GLOBAL  (1 << 0)
#define S_ICASE   (1 << 1)
#define S_PRINT   (1 << 2)
#define S_NTH     (1 << 3)

/* ------------------------------------------------------------------ */
/* Data structures                                                      */
/* ------------------------------------------------------------------ */

typedef enum { ADDR_NONE, ADDR_LINE, ADDR_LAST, ADDR_REGEX } AddrType;

typedef struct {
    AddrType type;
    int      line;
    char     pat[ADDR_PAT_SIZE];
    regex_t  re;
    int      compiled;
} Addr;

typedef struct {
    Addr   a1, a2;
    int    negate;
    char   cmd;
    /* s command */
    char  *s_pat;
    char  *s_repl;
    int    s_flags;
    int    s_nth;
    regex_t s_re;
    int    s_compiled;
    /* a / i command */
    char  *text;
    /* y command */
    unsigned char y_from[256];
    unsigned char y_to[256];
    int    y_len;
    /* range state */
    int    in_range;
} Command;

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */

static Command  g_cmds[MAX_CMDS];
static int      g_ncmds = 0;

static char    *g_scripts[MAX_SCRIPTS];
static int      g_nscripts = 0;

static int      g_suppress = 0;
static int      g_ere      = 0;
static int      g_inplace  = 0;

static int      g_lineno   = 0;
static int      g_is_last  = 0;

static char    *g_pending_a[MAX_CMDS];
static int      g_npending  = 0;

/* ------------------------------------------------------------------ */
/* Error helpers                                                        */
/* ------------------------------------------------------------------ */

static void die(const char *msg)
{
    fprintf(stderr, "sed: %s\n", msg);
    exit(1);
}

static void die2(const char *a, const char *b)
{
    fprintf(stderr, "sed: %s: %s\n", a, b);
    exit(1);
}

/* ------------------------------------------------------------------ */
/* Script buffer helpers                                                */
/* ------------------------------------------------------------------ */

static void add_script_str(const char *s)
{
    if (g_nscripts >= MAX_SCRIPTS) die("too many scripts");
    g_scripts[g_nscripts++] = strdup(s);
    if (!g_scripts[g_nscripts - 1]) die("out of memory");
}

static void add_script_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) die2(path, strerror(errno));
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);
    char *buf = malloc((size_t)len + 1);
    if (!buf) die("out of memory");
    size_t nr = fread(buf, 1, (size_t)len, fp);
    buf[nr] = '\0';
    fclose(fp);
    add_script_str(buf);
    free(buf);
}

static char *build_full_script(void)
{
    size_t total = 0;
    for (int i = 0; i < g_nscripts; i++)
        total += strlen(g_scripts[i]) + 1;
    char *out = malloc(total + 1);
    if (!out) die("out of memory");
    out[0] = '\0';
    for (int i = 0; i < g_nscripts; i++) {
        strcat(out, g_scripts[i]);
        strcat(out, "\n");
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Script parser                                                        */
/* ------------------------------------------------------------------ */

static void skip_blanks(const char **p)
{
    while (**p == ' ' || **p == '\t') (*p)++;
}

static char *read_delimited(const char **p, char delim)
{
    const char *start = *p;
    const char *q = start;
    while (*q && *q != delim) {
        if (*q == '\\' && *(q + 1)) q += 2;
        else q++;
    }
    size_t len = (size_t)(q - start);
    char *s = malloc(len + 1);
    if (!s) die("out of memory");
    memcpy(s, start, len);
    s[len] = '\0';
    if (*q == delim) q++;
    *p = q;
    return s;
}

static void compile_addr_re(Addr *addr)
{
    if (addr->compiled) return;
    int flags = g_ere ? REG_EXTENDED : 0;
    int rc = regcomp(&addr->re, addr->pat, flags);
    if (rc != 0) {
        fprintf(stderr, "sed: bad regex /%s/\n", addr->pat);
        exit(1);
    }
    addr->compiled = 1;
}

static int parse_addr(const char **p, Addr *addr)
{
    memset(addr, 0, sizeof(*addr));
    skip_blanks(p);

    if (**p == '$') {
        addr->type = ADDR_LAST;
        (*p)++;
        return 1;
    }
    if (isdigit((unsigned char)**p)) {
        addr->type = ADDR_LINE;
        addr->line = 0;
        while (isdigit((unsigned char)**p)) {
            addr->line = addr->line * 10 + (**p - '0');
            (*p)++;
        }
        return 1;
    }
    if (**p == '/') {
        (*p)++;
        char *pat = read_delimited(p, '/');
        addr->type = ADDR_REGEX;
        strncpy(addr->pat, pat, ADDR_PAT_SIZE - 1);
        addr->pat[ADDR_PAT_SIZE - 1] = '\0';
        free(pat);
        compile_addr_re(addr);
        return 1;
    }
    return 0;
}

static char *parse_text_arg(const char **p)
{
    skip_blanks(p);
    if (**p == '\\') {
        (*p)++;
        if (**p == '\n') (*p)++;
    }
    const char *start = *p;
    while (**p && **p != '\n' && **p != ';') (*p)++;
    size_t len = (size_t)(*p - start);
    char *t = malloc(len + 2);
    if (!t) die("out of memory");
    memcpy(t, start, len);
    t[len] = '\n';
    t[len + 1] = '\0';
    return t;
}

static void parse_y_cmd(const char **p, Command *cmd)
{
    if (!**p) die("y: missing delimiter");
    char delim = **p; (*p)++;
    char from_raw[512], to_raw[512];
    int fi = 0, ti = 0;
    while (**p && **p != delim) {
        if (**p == '\\') {
            (*p)++;
            char c = **p; (*p)++;
            if      (c == 'n')  from_raw[fi++] = '\n';
            else if (c == '\\') from_raw[fi++] = '\\';
            else                from_raw[fi++] = c;
        } else {
            from_raw[fi++] = **p; (*p)++;
        }
        if (fi >= 255) break;
    }
    if (**p == delim) (*p)++;
    while (**p && **p != delim) {
        if (**p == '\\') {
            (*p)++;
            char c = **p; (*p)++;
            if      (c == 'n')  to_raw[ti++] = '\n';
            else if (c == '\\') to_raw[ti++] = '\\';
            else                to_raw[ti++] = c;
        } else {
            to_raw[ti++] = **p; (*p)++;
        }
        if (ti >= 255) break;
    }
    if (**p == delim) (*p)++;
    if (fi != ti) die("y: unequal set lengths");
    cmd->y_len = fi;
    for (int i = 0; i < fi; i++) {
        cmd->y_from[i] = (unsigned char)from_raw[i];
        cmd->y_to[i]   = (unsigned char)to_raw[i];
    }
}

static void parse_s_cmd(const char **p, Command *cmd)
{
    if (!**p) die("s: missing delimiter");
    char delim = **p; (*p)++;
    char *pat  = read_delimited(p, delim);
    char *repl = read_delimited(p, delim);

    cmd->s_pat   = pat;
    cmd->s_repl  = repl;
    cmd->s_flags = 0;
    cmd->s_nth   = 1;

    while (**p && **p != '\n' && **p != ';' && **p != '}') {
        char f = **p; (*p)++;
        if      (f == 'g') cmd->s_flags |= S_GLOBAL;
        else if (f == 'i') cmd->s_flags |= S_ICASE;
        else if (f == 'p') cmd->s_flags |= S_PRINT;
        else if (f >= '1' && f <= '9') {
            cmd->s_nth = f - '0';
            cmd->s_flags |= S_NTH;
        } else { (*p)--; break; }
    }

    int rflags = g_ere ? REG_EXTENDED : 0;
    if (cmd->s_flags & S_ICASE) rflags |= REG_ICASE;
    int rc = regcomp(&cmd->s_re, cmd->s_pat, rflags);
    if (rc != 0) {
        fprintf(stderr, "sed: bad regex s/%s/\n", cmd->s_pat);
        exit(1);
    }
    cmd->s_compiled = 1;
}

static void parse_script(const char *script)
{
    const char *p = script;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ';' || *p == '\r')
            p++;
        if (!*p) break;
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }

        if (g_ncmds >= MAX_CMDS) die("too many commands");
        Command *cmd = &g_cmds[g_ncmds];
        memset(cmd, 0, sizeof(*cmd));

        int has_a1 = parse_addr(&p, &cmd->a1);
        skip_blanks(&p);

        if (has_a1 && *p == ',') {
            p++;
            skip_blanks(&p);
            if (*p == '+' && isdigit((unsigned char)*(p + 1))) {
                p++;
                int m = 0;
                while (isdigit((unsigned char)*p)) { m = m * 10 + (*p - '0'); p++; }
                cmd->a2.type = ADDR_LINE;
                cmd->a2.line = -m;
            } else {
                parse_addr(&p, &cmd->a2);
            }
        } else if (!has_a1) {
            cmd->a1.type = ADDR_NONE;
            cmd->a2.type = ADDR_NONE;
        }

        skip_blanks(&p);
        if (*p == '!') { cmd->negate = 1; p++; skip_blanks(&p); }

        if (!*p || *p == '\n') continue;
        cmd->cmd = *p++;

        switch (cmd->cmd) {
            case 's': parse_s_cmd(&p, cmd); break;
            case 'y': parse_y_cmd(&p, cmd); break;
            case 'a': case 'i': cmd->text = parse_text_arg(&p); break;
            case 'd': case 'p': case 'q': case '=': break;
            case '{': case '}': break;
            default:
                fprintf(stderr, "sed: unknown command '%c'\n", cmd->cmd);
                exit(1);
        }
        g_ncmds++;
    }
}

/* ------------------------------------------------------------------ */
/* Address matching                                                     */
/* ------------------------------------------------------------------ */

static int addr_matches(Addr *addr, const char *buf, int lineno, int is_last)
{
    switch (addr->type) {
        case ADDR_NONE:  return 1;
        case ADDR_LAST:  return is_last;
        case ADDR_LINE:  return (lineno == addr->line);
        case ADDR_REGEX: {
            compile_addr_re(addr);
            regmatch_t m;
            return (regexec(&addr->re, buf, 1, &m, 0) == 0);
        }
    }
    return 0;
}

static int cmd_active(Command *cmd, const char *buf, int lineno, int is_last)
{
    int active;

    if (cmd->a1.type == ADDR_NONE) {
        active = 1;
    } else if (cmd->a2.type == ADDR_NONE) {
        active = addr_matches(&cmd->a1, buf, lineno, is_last);
    } else {
        if (!cmd->in_range) {
            if (addr_matches(&cmd->a1, buf, lineno, is_last)) {
                cmd->in_range = 1;
                active = 1;
                if (cmd->a2.type == ADDR_LINE) {
                    int end_line = cmd->a2.line;
                    if (end_line >= 0 && lineno >= end_line)
                        cmd->in_range = 0;
                    else if (end_line < 0) {
                        cmd->a2.line = lineno + (-end_line);
                        if (lineno >= cmd->a2.line) cmd->in_range = 0;
                    }
                } else if (cmd->a2.type == ADDR_LAST && is_last) {
                    cmd->in_range = 0;
                }
            } else {
                active = 0;
            }
        } else {
            active = 1;
            if (cmd->a2.type == ADDR_LINE) {
                if (cmd->a2.line >= 0 && lineno >= cmd->a2.line)
                    cmd->in_range = 0;
                else if (cmd->a2.line < 0 && lineno >= (-cmd->a2.line))
                    cmd->in_range = 0;
            } else if (cmd->a2.type == ADDR_LAST) {
                if (is_last) cmd->in_range = 0;
            } else if (cmd->a2.type == ADDR_REGEX) {
                compile_addr_re(&cmd->a2);
                regmatch_t m;
                if (regexec(&cmd->a2.re, buf, 1, &m, 0) == 0)
                    cmd->in_range = 0;
            }
        }
    }

    if (cmd->negate) active = !active;
    return active;
}

/* ------------------------------------------------------------------ */
/* s command execution                                                  */
/* ------------------------------------------------------------------ */

static int build_replacement(const char *repl, const char *src,
                              regmatch_t *match, char *out, int outsz)
{
    int wi = 0;
    for (const char *r = repl; *r; r++) {
        if (*r == '\\') {
            r++;
            if (!*r) break;
            if (*r >= '1' && *r <= '9') {
                int gn = *r - '0';
                if (match[gn].rm_so >= 0) {
                    int glen = match[gn].rm_eo - match[gn].rm_so;
                    if (wi + glen < outsz) {
                        memcpy(out + wi, src + match[gn].rm_so, (size_t)glen);
                        wi += glen;
                    }
                }
            } else if (*r == '\\') {
                if (wi < outsz - 1) out[wi++] = '\\';
            } else if (*r == 'n') {
                if (wi < outsz - 1) out[wi++] = '\n';
            } else if (*r == 'u') {
                r++;
                if (*r && wi < outsz - 1)
                    out[wi++] = (char)toupper((unsigned char)*r);
            } else {
                if (wi < outsz - 1) out[wi++] = *r;
            }
        } else if (*r == '&') {
            int mlen = match[0].rm_eo - match[0].rm_so;
            if (wi + mlen < outsz) {
                memcpy(out + wi, src + match[0].rm_so, (size_t)mlen);
                wi += mlen;
            }
        } else {
            if (wi < outsz - 1) out[wi++] = *r;
        }
    }
    out[wi] = '\0';
    return wi;
}

static int exec_s(Command *cmd, char *buf, int *buflen)
{
    regmatch_t pmatch[10];
    char tmp[PAT_SIZE];
    char repl_buf[PAT_SIZE];

    int global = (cmd->s_flags & S_GLOBAL) != 0;
    int nth    = (cmd->s_flags & S_NTH)    != 0 ? cmd->s_nth : 1;
    int made   = 0;
    int occur  = 0;
    int wi     = 0;
    const char *src = buf;
    int src_len = *buflen;
    int pos = 0;

    while (pos <= src_len) {
        int eflags = (pos > 0) ? REG_NOTBOL : 0;
        int rc = regexec(&cmd->s_re, src + pos, 10, pmatch, eflags);
        if (rc != 0) {
            int rem = src_len - pos;
            if (wi + rem < PAT_SIZE) { memcpy(tmp + wi, src + pos, (size_t)rem); wi += rem; }
            break;
        }

        occur++;
        int mstart = pmatch[0].rm_so;
        int mend   = pmatch[0].rm_eo;

        /* copy pre-match */
        if (wi + mstart < PAT_SIZE) { memcpy(tmp + wi, src + pos, (size_t)mstart); wi += mstart; }

        if (occur == nth || (global && occur >= nth)) {
            /* fix up pmatch offsets relative to full src */
            regmatch_t abs_match[10];
            for (int g = 0; g < 10; g++) {
                abs_match[g].rm_so = (pmatch[g].rm_so >= 0) ? pmatch[g].rm_so + pos : -1;
                abs_match[g].rm_eo = (pmatch[g].rm_eo >= 0) ? pmatch[g].rm_eo + pos : -1;
            }
            int rlen = build_replacement(cmd->s_repl, src, abs_match, repl_buf, PAT_SIZE);
            if (wi + rlen < PAT_SIZE) { memcpy(tmp + wi, repl_buf, (size_t)rlen); wi += rlen; }
            made = 1;
        } else {
            int mlen = mend - mstart;
            if (wi + mlen < PAT_SIZE) { memcpy(tmp + wi, src + pos + mstart, (size_t)mlen); wi += mlen; }
        }

        pos += mend;

        if (mend == mstart) {
            if (pos < src_len && wi < PAT_SIZE - 1) tmp[wi++] = src[pos++];
            else break;
        }

        if (!global && occur >= nth) {
            int rem = src_len - pos;
            if (wi + rem < PAT_SIZE) { memcpy(tmp + wi, src + pos, (size_t)rem); wi += rem; }
            break;
        }
    }

    if (made) {
        tmp[wi] = '\0';
        memcpy(buf, tmp, (size_t)wi + 1);
        *buflen = wi;
    }
    return made;
}

/* ------------------------------------------------------------------ */
/* y command                                                            */
/* ------------------------------------------------------------------ */

static void exec_y(Command *cmd, char *buf, int *buflen)
{
    for (int i = 0; i < *buflen; i++) {
        unsigned char c = (unsigned char)buf[i];
        for (int j = 0; j < cmd->y_len; j++) {
            if (cmd->y_from[j] == c) { buf[i] = (char)cmd->y_to[j]; break; }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Process one line                                                     */
/* ------------------------------------------------------------------ */

static int process_line(char *buf, int *buflen, FILE *out)
{
    int deleted = 0;
    int quit    = 0;
    g_npending  = 0;

    for (int ci = 0; ci < g_ncmds && !deleted && !quit; ci++) {
        Command *cmd = &g_cmds[ci];
        if (!cmd_active(cmd, buf, g_lineno, g_is_last)) continue;

        switch (cmd->cmd) {
            case 'd': deleted = 1; break;
            case 'p': fwrite(buf, 1, (size_t)*buflen, out); fputc('\n', out); break;
            case 'q': quit = 1; break;
            case '=': fprintf(out, "%d\n", g_lineno); break;
            case 'a': if (g_npending < MAX_CMDS) g_pending_a[g_npending++] = cmd->text; break;
            case 'i': fputs(cmd->text, out); break;
            case 's': {
                int changed = exec_s(cmd, buf, buflen);
                if (changed && (cmd->s_flags & S_PRINT)) {
                    fwrite(buf, 1, (size_t)*buflen, out); fputc('\n', out);
                }
                break;
            }
            case 'y': exec_y(cmd, buf, buflen); break;
            default:  break;
        }
    }

    if (!deleted && !g_suppress) {
        fwrite(buf, 1, (size_t)*buflen, out);
        fputc('\n', out);
    }
    for (int i = 0; i < g_npending; i++) fputs(g_pending_a[i], out);
    return quit;
}

/* ------------------------------------------------------------------ */
/* Stream processing                                                    */
/* ------------------------------------------------------------------ */

static int process_stream(FILE *fp, FILE *out)
{
    static char cur[PAT_SIZE];
    static char nxt[PAT_SIZE];

    if (!fgets(cur, (int)sizeof(cur), fp)) return 0;
    int curlen = (int)strlen(cur);
    if (curlen > 0 && cur[curlen-1] == '\n') cur[--curlen] = '\0';
    if (curlen > 0 && cur[curlen-1] == '\r') cur[--curlen] = '\0';
    g_lineno = 1;

    while (1) {
        char *got = fgets(nxt, (int)sizeof(nxt), fp);
        g_is_last = (got == NULL);
        if (got) {
            int nlen = (int)strlen(nxt);
            if (nlen > 0 && nxt[nlen-1] == '\n') nxt[--nlen] = '\0';
            if (nlen > 0 && nxt[nlen-1] == '\r') nxt[--nlen] = '\0';
        }
        int quit = process_line(cur, &curlen, out);
        if (quit || g_is_last) break;
        memcpy(cur, nxt, (size_t)strlen(nxt) + 1);
        curlen = (int)strlen(cur);
        g_lineno++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* In-place editing                                                     */
/* ------------------------------------------------------------------ */

static void process_inplace(const char *path)
{
    char tmpname[4096];
    snprintf(tmpname, sizeof(tmpname), "%s.sedtmp", path);

    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "sed: cannot open '%s': %s\n", path, strerror(errno)); return; }
    FILE *tmp = fopen(tmpname, "w");
    if (!tmp) { fprintf(stderr, "sed: cannot create '%s': %s\n", tmpname, strerror(errno)); fclose(fp); return; }

    for (int i = 0; i < g_ncmds; i++) g_cmds[i].in_range = 0;
    g_lineno = 0;
    process_stream(fp, tmp);
    fclose(fp);
    fclose(tmp);

    if (remove(path) != 0) {
        fprintf(stderr, "sed: cannot remove '%s': %s\n", path, strerror(errno));
        remove(tmpname); return;
    }
    if (rename(tmpname, path) != 0)
        fprintf(stderr, "sed: cannot rename to '%s': %s\n", path, strerror(errno));
}

/* ------------------------------------------------------------------ */
/* Usage / version                                                      */
/* ------------------------------------------------------------------ */

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: sed [OPTION]... SCRIPT [FILE]...\n"
        "   or: sed [OPTION]... -e SCRIPT... [FILE]...\n"
        "\n"
        "Options:\n"
        "  -n            suppress default print\n"
        "  -e SCRIPT     add expression\n"
        "  -f FILE       read script from file\n"
        "  -E, -r        use extended regex (ERE)\n"
        "  -i            edit files in-place\n"
        "  --help        print this help and exit\n"
        "  --version     print version and exit\n"
        "\n"
        "Commands: s/RE/REPL/[gipN]  d  p  q  =  a\\TEXT  i\\TEXT  y/S1/S2/\n"
        "Addressing: N  $  /regex/  N,M  N,+M  addr!\n"
    );
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int argi = 1;
    int script_given = 0;

    while (argi < argc) {
        const char *arg = argv[argi];
        if (strcmp(arg, "--version") == 0) { printf("sed 1.0 (Winix 1.0)\n"); return 0; }
        if (strcmp(arg, "--help")    == 0) { usage(stdout); return 0; }
        if (strcmp(arg, "--")        == 0) { argi++; break; }
        if (arg[0] != '-' || arg[1] == '\0') break;

        const char *p = arg + 1;
        int consumed = 0;
        while (*p && !consumed) {
            char f = *p++;
            switch (f) {
                case 'n': g_suppress = 1; break;
                case 'E': case 'r': g_ere = 1; break;
                case 'i': g_inplace = 1; break;
                case 'e':
                    if (*p) { add_script_str(p); script_given = 1; consumed = 1; }
                    else if (argi + 1 < argc) { add_script_str(argv[++argi]); script_given = 1; consumed = 1; }
                    else die("option requires an argument -- 'e'");
                    break;
                case 'f':
                    if (*p) { add_script_file(p); script_given = 1; consumed = 1; }
                    else if (argi + 1 < argc) { add_script_file(argv[++argi]); script_given = 1; consumed = 1; }
                    else die("option requires an argument -- 'f'");
                    break;
                default:
                    fprintf(stderr, "sed: invalid option -- '%c'\n", f);
                    return 1;
            }
        }
        argi++;
    }

    if (!script_given) {
        if (argi >= argc) { fprintf(stderr, "sed: no script specified\n"); usage(stderr); return 1; }
        add_script_str(argv[argi++]);
    }

    char *full_script = build_full_script();
    parse_script(full_script);
    free(full_script);

    if (argi >= argc) {
        g_lineno = 0;
        process_stream(stdin, stdout);
    } else {
        for (int i = argi; i < argc; i++) {
            if (g_inplace) {
                process_inplace(argv[i]);
            } else {
                FILE *fp = fopen(argv[i], "r");
                if (!fp) { fprintf(stderr, "sed: cannot open '%s': %s\n", argv[i], strerror(errno)); continue; }
                for (int ci = 0; ci < g_ncmds; ci++) g_cmds[ci].in_range = 0;
                g_lineno = 0;
                process_stream(fp, stdout);
                fclose(fp);
            }
        }
    }

    for (int i = 0; i < g_ncmds; i++) {
        Command *cmd = &g_cmds[i];
        if (cmd->a1.compiled) regfree(&cmd->a1.re);
        if (cmd->a2.compiled) regfree(&cmd->a2.re);
        if (cmd->s_compiled)  regfree(&cmd->s_re);
        free(cmd->s_pat);
        free(cmd->s_repl);
        free(cmd->text);
    }
    for (int i = 0; i < g_nscripts; i++) free(g_scripts[i]);

    return 0;
}
