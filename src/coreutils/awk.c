/*
 * awk.c — Winix coreutil
 *
 * Usage: awk [-F sep] [-v var=val] 'prog' [file...]
 *        awk [-F sep] [-v var=val] -f script [file...]
 *
 * Pattern-action text processor (practical POSIX awk subset).
 *
 * Features:
 *   BEGIN/END, /regex/ and expression patterns, range patterns (p1,p2)
 *   $0-$NF field access/assignment, NF/FS/OFS/ORS/RS/NR/FILENAME
 *   Arithmetic, comparison, string concatenation, regex match ~ !~
 *   if/else  while  for(;;)  for(k in arr)  do/while  break continue next exit
 *   print/printf (stdout + piped), associative arrays, delete
 *   Compound assignment  ++/--  ternary ?:
 *   String: length substr index split sub gsub match sprintf tolower toupper
 *   Math: sin cos atan2 exp log sqrt int rand srand
 *   system(cmd), user-defined functions, -F -v -f
 *
 * Exit: 0 success, 1 error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

/* strndup is POSIX; provide our own to avoid feature-test macros */
static char *awk_strndup(const char *s, size_t n) {
    size_t len = strlen(s); if (n > len) n = len;
    char *p = malloc(n + 1); if (!p) return NULL;
    memcpy(p, s, n); p[n] = '\0'; return p;
}

/* ── Tokens ──────────────────────────────────────────────────────────────── */

typedef enum {
    T_EOF, T_NEWLINE, T_SEMI,
    T_NUM, T_STR, T_REGEX, T_NAME,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PCT, T_CARET,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_COMMA, T_DOLLAR,
    T_ASSIGN, T_PLUS_EQ, T_MINUS_EQ, T_STAR_EQ, T_SLASH_EQ, T_PCT_EQ,
    T_INC, T_DEC,
    T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
    T_MATCH, T_NOMATCH,
    T_AND, T_OR, T_NOT,
    T_QUESTION, T_COLON,
    T_PIPE, T_APPEND,
    T_IN,
    T_IF, T_ELSE, T_WHILE, T_FOR, T_DO,
    T_PRINT, T_PRINTF,
    T_RETURN, T_NEXT, T_EXIT, T_DELETE, T_BREAK, T_CONTINUE,
    T_FUNCTION, T_BEGIN, T_END,
} Tok;

/* ── Parser state ─────────────────────────────────────────────────────────── */

typedef struct {
    const char *src;
    int         pos;
    int         tok_start;
    Tok         tok;
    double      num;
    char        id[256];
    char        str[2048];
    char        re[512];
    bool        prev_val;   /* last token was a "value" (for / disambiguation) */
} P;

/* ── Value type ───────────────────────────────────────────────────────────── */

#define VAL_UNDEF 0
#define VAL_NUM   1
#define VAL_STR   2

typedef struct { double n; char s[512]; int flags; } Val;

static double val_num(const Val *v) {
    if (v->flags & VAL_NUM) return v->n;
    if (v->flags & VAL_STR) return atof(v->s);
    return 0.0;
}
static const char *val_str(Val *v) {
    if (v->flags & VAL_STR) return v->s;
    if (v->flags & VAL_NUM) {
        if (v->n == (long long)v->n && v->n >= -1e15 && v->n <= 1e15)
            snprintf(v->s, sizeof(v->s), "%.0f", v->n);
        else
            snprintf(v->s, sizeof(v->s), "%.6g", v->n);
        v->flags |= VAL_STR;
    } else { v->s[0] = '\0'; v->flags |= VAL_STR; }
    return v->s;
}
static bool val_true(const Val *v) {
    if (v->flags & VAL_NUM) return v->n != 0.0;
    if (v->flags & VAL_STR) return v->s[0] != '\0';
    return false;
}
static Val make_num(double n)       { Val v; v.n=n; v.s[0]='\0'; v.flags=VAL_NUM; return v; }
static Val make_str(const char *s)  { Val v; v.n=0; v.flags=VAL_STR;
    strncpy(v.s,s,sizeof(v.s)-1); v.s[sizeof(v.s)-1]='\0'; return v; }
static Val make_undef(void)         { Val v; v.n=0; v.s[0]='\0'; v.flags=VAL_UNDEF; return v; }

/* Compare: string comparison if both are string-typed, else numeric */
static int val_cmp(Val *a, Val *b) {
    bool astr = (a->flags==VAL_STR)||(a->flags==VAL_UNDEF&&a->s[0]);
    bool bstr = (b->flags==VAL_STR)||(b->flags==VAL_UNDEF&&b->s[0]);
    if (astr && bstr) return strcmp(val_str(a), val_str(b));
    double na=val_num(a), nb=val_num(b);
    return na<nb?-1:na>nb?1:0;
}

/* ── Global interpreter state ────────────────────────────────────────────── */

#define MAX_VARS   512
#define MAX_ARRS    32
#define MAX_RULES  128
#define MAX_FUNCS   32
#define MAX_FIELDS 128
#define ARR_BKTS    64

typedef struct { char name[64]; Val val; } Var;
static Var  g_vars[MAX_VARS];
static int  g_nvars;

typedef struct AEnt { char key[256]; Val val; struct AEnt *next; } AEnt;
typedef struct { char name[64]; AEnt *bkt[ARR_BKTS]; } Arr;
static Arr  g_arrs[MAX_ARRS];
static int  g_narrs;

typedef enum { PAT_BEGIN, PAT_END, PAT_ALWAYS, PAT_EXPR, PAT_REGEX, PAT_RANGE } PatKind;
typedef struct { PatKind kind; char *pat; char *pat2; char *act; bool in_range; } Rule;
static Rule g_rules[MAX_RULES];
static int  g_nrules;

typedef struct { char name[64]; char params[8][64]; int nparams; char *body; } Func;
static Func g_funcs[MAX_FUNCS];
static int  g_nfuncs;

static char  g_line[8192];
static char  g_fdup[8192];
static char *g_fld[MAX_FIELDS];
static int   g_nf;
static long  g_nr;
static char  g_fs[64]       = " ";
static char  g_ofs[64]      = " ";
static char  g_ors[64]      = "\n";
static char  g_rs[8]        = "\n";
static char  g_filename[512];
static char  g_ofmt[32]     = "%.6g";

static bool  g_next;
static bool  g_exit;
static int   g_exit_code;
static bool  g_break;
static bool  g_continue;
static bool  g_return;
static Val   g_return_val;
static int   g_rstart;      /* RSTART */
static int   g_rlength = -1; /* RLENGTH */

#define MAX_PIPES 8
static struct { char cmd[256]; FILE *fp; } g_pipes[MAX_PIPES];
static int g_npipes;

/* ── Variable storage ─────────────────────────────────────────────────────── */

static Val *find_var(const char *n) {
    for (int i=0;i<g_nvars;i++) if(!strcmp(g_vars[i].name,n)) return &g_vars[i].val;
    return NULL;
}
static Val get_var(const char *n) {
    /* Built-ins */
    if (!strcmp(n,"NR"))       return make_num(g_nr);
    if (!strcmp(n,"NF"))       return make_num(g_nf);
    if (!strcmp(n,"FS"))       return make_str(g_fs);
    if (!strcmp(n,"OFS"))      return make_str(g_ofs);
    if (!strcmp(n,"ORS"))      return make_str(g_ors);
    if (!strcmp(n,"RS"))       return make_str(g_rs);
    if (!strcmp(n,"FILENAME")) return make_str(g_filename);
    if (!strcmp(n,"RSTART"))   return make_num(g_rstart);
    if (!strcmp(n,"RLENGTH"))  return make_num(g_rlength);
    Val *v = find_var(n);
    return v ? *v : make_undef();
}
static void set_var(const char *n, Val v) {
    if (!strcmp(n,"NR"))  { g_nr = (long)val_num(&v); return; }
    if (!strcmp(n,"NF"))  { g_nf = (int)val_num(&v);  return; }
    if (!strcmp(n,"FS"))  { strncpy(g_fs, val_str(&v), sizeof(g_fs)-1); return; }
    if (!strcmp(n,"OFS")) { strncpy(g_ofs, val_str(&v), sizeof(g_ofs)-1); return; }
    if (!strcmp(n,"ORS")) { strncpy(g_ors, val_str(&v), sizeof(g_ors)-1); return; }
    if (!strcmp(n,"RS"))  { strncpy(g_rs, val_str(&v), sizeof(g_rs)-1); return; }
    for (int i=0;i<g_nvars;i++) {
        if (!strcmp(g_vars[i].name,n)) { g_vars[i].val=v; return; }
    }
    if (g_nvars<MAX_VARS) {
        strncpy(g_vars[g_nvars].name,n,63); g_vars[g_nvars++].val=v;
    }
}

/* ── Array storage ────────────────────────────────────────────────────────── */

static unsigned arr_hash(const char *k) {
    unsigned h=5381; for(;*k;k++) h=h*33^(unsigned char)*k; return h%ARR_BKTS;
}
static Arr *find_arr(const char *n, bool create) {
    for (int i=0;i<g_narrs;i++) if(!strcmp(g_arrs[i].name,n)) return &g_arrs[i];
    if (!create || g_narrs>=MAX_ARRS) return NULL;
    strncpy(g_arrs[g_narrs].name,n,63);
    memset(g_arrs[g_narrs].bkt,0,sizeof(g_arrs[g_narrs].bkt));
    return &g_arrs[g_narrs++];
}
static Val arr_get(const char *aname, const char *key) {
    Arr *a = find_arr(aname, false); if (!a) return make_undef();
    for (AEnt *e=a->bkt[arr_hash(key)];e;e=e->next)
        if (!strcmp(e->key,key)) return e->val;
    return make_undef();
}
static void arr_set(const char *aname, const char *key, Val v) {
    Arr *a = find_arr(aname, true); if (!a) return;
    unsigned b = arr_hash(key);
    for (AEnt *e=a->bkt[b];e;e=e->next) { if(!strcmp(e->key,key)){e->val=v;return;} }
    AEnt *e = calloc(1,sizeof(AEnt));
    if (!e) { fputs("awk: out of memory\n", stderr); exit(1); }
    strncpy(e->key,key,sizeof(e->key)-1); e->val=v; e->next=a->bkt[b]; a->bkt[b]=e;
}
static bool arr_has(const char *aname, const char *key) {
    Arr *a = find_arr(aname, false); if (!a) return false;
    for (AEnt *e=a->bkt[arr_hash(key)];e;e=e->next) if(!strcmp(e->key,key)) return true;
    return false;
}
static void arr_del(const char *aname, const char *key) {
    Arr *a = find_arr(aname, false); if (!a) return;
    unsigned b = arr_hash(key); AEnt **pp = &a->bkt[b];
    while (*pp) { if(!strcmp((*pp)->key,key)){AEnt *t=*pp;*pp=t->next;free(t);return;} pp=&(*pp)->next; }
}
static void arr_del_all(const char *aname) {
    Arr *a = find_arr(aname, false); if (!a) return;
    for (int b=0;b<ARR_BKTS;b++) { AEnt *e=a->bkt[b]; while(e){AEnt*n=e->next;free(e);e=n;} a->bkt[b]=NULL; }
}
/* Collect all keys; caller must free the array (not the strings) */
static int arr_keys(const char *aname, char ***keys_out) {
    Arr *a = find_arr(aname, false);
    if (!a) { *keys_out=NULL; return 0; }
    int n=0;
    for (int b=0;b<ARR_BKTS;b++) for(AEnt *e=a->bkt[b];e;e=e->next) n++;
    char **ks = malloc(n * sizeof(char *));
    if (!ks) { fputs("awk: out of memory\n", stderr); exit(1); }
    int i=0;
    for (int b=0;b<ARR_BKTS;b++) for(AEnt *e=a->bkt[b];e;e=e->next) ks[i++]=e->key;
    *keys_out = ks; return n;
}

/* ── Field management ─────────────────────────────────────────────────────── */

static void split_fields(void) {
    strncpy(g_fdup, g_line, sizeof(g_fdup)-1); g_fdup[sizeof(g_fdup)-1]='\0';
    g_nf = 0;
    char *p = g_fdup;
    if (!strcmp(g_fs," ")) {  /* default: split on whitespace runs */
        while (*p && isspace((unsigned char)*p)) p++;
        while (*p && g_nf < MAX_FIELDS-1) {
            g_fld[g_nf++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) *p++ = '\0';
            while (*p && isspace((unsigned char)*p)) p++;
        }
    } else if (g_fs[1] == '\0') { /* single char FS */
        char sep = g_fs[0];
        while (g_nf < MAX_FIELDS-1) {
            g_fld[g_nf++] = p;
            char *e = strchr(p, sep);
            if (!e) break;
            *e = '\0'; p = e+1;
        }
    } else { /* multi-char: treat as literal string separator */
        int flen = strlen(g_fs);
        while (g_nf < MAX_FIELDS-1) {
            g_fld[g_nf++] = p;
            char *e = strstr(p, g_fs);
            if (!e) break;
            *e = '\0'; p = e + flen;
        }
    }
}

static void set_line(const char *s) {
    strncpy(g_line, s, sizeof(g_line)-1); g_line[sizeof(g_line)-1]='\0';
    split_fields();
}

static void rebuild_line(void) {
    g_line[0] = '\0';
    for (int i=0; i<g_nf; i++) {
        if (i) strncat(g_line, g_ofs, sizeof(g_line)-strlen(g_line)-1);
        if (g_fld[i]) strncat(g_line, g_fld[i], sizeof(g_line)-strlen(g_line)-1);
    }
}

/* Static field storage for assigned fields */
static char g_fld_store[MAX_FIELDS][512];

static void set_field(int n, const char *val) {
    if (n < 0) return;
    if (n == 0) { set_line(val); return; }
    /* Extend fields if needed */
    while (g_nf < n) {
        g_fld_store[g_nf][0] = '\0';
        g_fld[g_nf] = g_fld_store[g_nf];
        g_nf++;
    }
    if (n <= MAX_FIELDS-1) {
        strncpy(g_fld_store[n-1], val, sizeof(g_fld_store[0])-1);
        g_fld[n-1] = g_fld_store[n-1];
    }
    rebuild_line();
}

/* ── Regex engine ─────────────────────────────────────────────────────────── */

static int re_atom_len(const char *re) {
    if (!re[0]) return 0;
    if (re[0]=='[') {
        int i=1;
        if (re[i]=='^') i++;
        if (re[i]==']') i++;
        while (re[i] && re[i]!=']') { if(re[i]=='\\') i++; i++; }
        return re[i] ? i+1 : i;
    }
    if (re[0]=='\\' && re[1]) return 2;
    return 1;
}

static bool re_match_atom(const char *re, int alen, char c) {
    if (!c) return false;
    if (alen==1) { return re[0]=='.' || re[0]==c; }
    if (alen==2 && re[0]=='\\') {
        char e=re[1];
        if(e=='n') return c=='\n'; if(e=='t') return c=='\t';
        if(e=='r') return c=='\r';
        return c==e;
    }
    if (re[0]=='[') {
        bool neg=(re[1]=='^'); int i=neg?2:1;
        if(re[i]==']') { if(c==']') { if(!neg) return true; } i++; }
        bool found=false;
        while (i<alen-1) {
            if (re[i+1]=='-' && i+2<alen-1) {
                if(c>=(unsigned char)re[i] && c<=(unsigned char)re[i+2]) found=true;
                i+=3;
            } else if (re[i]=='\\' && i+1<alen-1) {
                char e=re[i+1];
                char ec=(e=='n')?'\n':(e=='t')?'\t':e;
                if(c==ec) found=true; i+=2;
            } else { if(c==(unsigned char)re[i]) found=true; i++; }
        }
        return neg ? !found : found;
    }
    return false;
}

static const char *re_here(const char *re, const char *str);

static const char *re_here(const char *re, const char *str) {
    if (!re[0]) return str;
    if (re[0]=='$' && !re[1]) return *str?NULL:str;
    int alen = re_atom_len(re);
    if (!alen) return str;
    char q = re[alen];
    if (q=='*'||q=='+'||q=='?') {
        const char *rest = re+alen+1;
        if (q=='?') {
            if (re_match_atom(re,alen,*str)) {
                const char *e=re_here(rest,str+1); if(e) return e;
            }
            return re_here(rest,str);
        }
        const char *p=str;
        while (*p && re_match_atom(re,alen,*p)) p++;
        while (p >= str+(q=='+'?1:0)) {
            const char *e=re_here(rest,p); if(e) return e;
            if(p==str) break; p--;
        }
        return NULL;
    }
    if (!re_match_atom(re,alen,*str)) return NULL;
    return re_here(re+alen, str+1);
}

static bool re_match(const char *pat, const char *str, int *rs, int *rl) {
    bool anc=(pat[0]=='^'); const char *p=anc?pat+1:pat;
    int slen=strlen(str);
    for (int i=0;i<=slen;i++) {
        const char *e=re_here(p,str+i);
        if (e) { if(rs)*rs=i+1; if(rl)*rl=(int)(e-(str+i)); return true; }
        if (anc) break;
    }
    if(rs)*rs=0; if(rl)*rl=-1; return false;
}

/* ── Lexer ────────────────────────────────────────────────────────────────── */

static void next(P *p) {
    /* Skip spaces/tabs and line continuations */
    for (;;) {
        char c=p->src[p->pos];
        if (c==' '||c=='\t'||c=='\r') { p->pos++; continue; }
        if (c=='\\' && p->src[p->pos+1]=='\n') { p->pos+=2; continue; }
        break;
    }
    /* Block comments */
    if (p->src[p->pos]=='/' && p->src[p->pos+1]=='*') {
        p->pos+=2;
        while (p->src[p->pos] && !(p->src[p->pos]=='*'&&p->src[p->pos+1]=='/')) p->pos++;
        if (p->src[p->pos]) p->pos+=2;
        next(p); return;
    }
    /* Line comments */
    if (p->src[p->pos]=='#') {
        while (p->src[p->pos] && p->src[p->pos]!='\n') p->pos++;
        next(p); return;
    }

    p->tok_start = p->pos;
    char c = p->src[p->pos];

    if (!c)  { p->tok=T_EOF;     return; }
    if (c=='\n') { p->pos++; p->tok=T_NEWLINE; p->prev_val=false; return; }
    if (c==';')  { p->pos++; p->tok=T_SEMI;    p->prev_val=false; return; }

    /* Number */
    if (isdigit((unsigned char)c)||(c=='.'&&isdigit((unsigned char)p->src[p->pos+1]))) {
        char buf[64]; int bi=0;
        while (isdigit((unsigned char)p->src[p->pos])||p->src[p->pos]=='.') buf[bi++]=p->src[p->pos++];
        if ((p->src[p->pos]=='e'||p->src[p->pos]=='E')) {
            buf[bi++]=p->src[p->pos++];
            if (p->src[p->pos]=='+'||p->src[p->pos]=='-') buf[bi++]=p->src[p->pos++];
            while (isdigit((unsigned char)p->src[p->pos])) buf[bi++]=p->src[p->pos++];
        }
        buf[bi]='\0'; p->num=atof(buf); p->tok=T_NUM; p->prev_val=true; return;
    }

    /* String */
    if (c=='"') {
        p->pos++; int bi=0;
        while (p->src[p->pos] && p->src[p->pos]!='"') {
            if (p->src[p->pos]=='\\') {
                p->pos++;
                switch(p->src[p->pos]) {
                    case 'n': p->str[bi++]='\n'; break; case 't': p->str[bi++]='\t'; break;
                    case 'r': p->str[bi++]='\r'; break; case 'a': p->str[bi++]='\a'; break;
                    case '\\': p->str[bi++]='\\'; break; case '"': p->str[bi++]='"'; break;
                    case '/':  p->str[bi++]='/';  break;
                    default:   p->str[bi++]='\\'; p->str[bi++]=p->src[p->pos]; break;
                }
            } else { if(bi<(int)sizeof(p->str)-1) p->str[bi++]=p->src[p->pos]; }
            p->pos++;
        }
        if (p->src[p->pos]=='"') p->pos++;
        p->str[bi]='\0'; p->tok=T_STR; p->prev_val=true; return;
    }

    /* Identifier / keyword */
    if (isalpha((unsigned char)c)||c=='_') {
        int bi=0;
        while (isalnum((unsigned char)p->src[p->pos])||p->src[p->pos]=='_')
            p->id[bi++]=p->src[p->pos++];
        p->id[bi]='\0';
        if (!strcmp(p->id,"if"))       { p->tok=T_IF;       p->prev_val=false; return; }
        if (!strcmp(p->id,"else"))     { p->tok=T_ELSE;     p->prev_val=false; return; }
        if (!strcmp(p->id,"while"))    { p->tok=T_WHILE;    p->prev_val=false; return; }
        if (!strcmp(p->id,"for"))      { p->tok=T_FOR;      p->prev_val=false; return; }
        if (!strcmp(p->id,"do"))       { p->tok=T_DO;       p->prev_val=false; return; }
        if (!strcmp(p->id,"print"))    { p->tok=T_PRINT;    p->prev_val=false; return; }
        if (!strcmp(p->id,"printf"))   { p->tok=T_PRINTF;   p->prev_val=false; return; }
        if (!strcmp(p->id,"return"))   { p->tok=T_RETURN;   p->prev_val=false; return; }
        if (!strcmp(p->id,"next"))     { p->tok=T_NEXT;     p->prev_val=false; return; }
        if (!strcmp(p->id,"exit"))     { p->tok=T_EXIT;     p->prev_val=false; return; }
        if (!strcmp(p->id,"delete"))   { p->tok=T_DELETE;   p->prev_val=false; return; }
        if (!strcmp(p->id,"break"))    { p->tok=T_BREAK;    p->prev_val=false; return; }
        if (!strcmp(p->id,"continue")) { p->tok=T_CONTINUE; p->prev_val=false; return; }
        if (!strcmp(p->id,"function")) { p->tok=T_FUNCTION; p->prev_val=false; return; }
        if (!strcmp(p->id,"BEGIN"))    { p->tok=T_BEGIN;    p->prev_val=false; return; }
        if (!strcmp(p->id,"END"))      { p->tok=T_END;      p->prev_val=false; return; }
        if (!strcmp(p->id,"in"))       { p->tok=T_IN;       p->prev_val=false; return; }
        p->tok=T_NAME; p->prev_val=true; return;
    }

    /* Regex literal: '/' when not following a value */
    if (c=='/' && !p->prev_val) {
        p->pos++; int bi=0;
        while (p->src[p->pos] && p->src[p->pos]!='/') {
            if (p->src[p->pos]=='\\') {
                if (bi<(int)sizeof(p->re)-2) p->re[bi++]=p->src[p->pos];
                p->pos++;
            }
            if (bi<(int)sizeof(p->re)-1) p->re[bi++]=p->src[p->pos];
            p->pos++;
        }
        if (p->src[p->pos]=='/') p->pos++;
        p->re[bi]='\0'; p->tok=T_REGEX; p->prev_val=true; return;
    }

    char d=p->src[p->pos+1];
    /* Two-char operators */
    if (c=='='&&d=='='){p->pos+=2;p->tok=T_EQ;     p->prev_val=false;return;}
    if (c=='!'&&d=='='){p->pos+=2;p->tok=T_NE;     p->prev_val=false;return;}
    if (c=='<'&&d=='='){p->pos+=2;p->tok=T_LE;     p->prev_val=false;return;}
    if (c=='>'&&d=='='){p->pos+=2;p->tok=T_GE;     p->prev_val=false;return;}
    if (c=='&'&&d=='&'){p->pos+=2;p->tok=T_AND;    p->prev_val=false;return;}
    if (c=='|'&&d=='|'){p->pos+=2;p->tok=T_OR;     p->prev_val=false;return;}
    if (c=='!'&&d=='~'){p->pos+=2;p->tok=T_NOMATCH;p->prev_val=false;return;}
    if (c=='+'&&d=='+'){p->pos+=2;p->tok=T_INC;    p->prev_val=true; return;}
    if (c=='-'&&d=='-'){p->pos+=2;p->tok=T_DEC;    p->prev_val=true; return;}
    if (c=='+'&&d=='='){p->pos+=2;p->tok=T_PLUS_EQ; p->prev_val=false;return;}
    if (c=='-'&&d=='='){p->pos+=2;p->tok=T_MINUS_EQ;p->prev_val=false;return;}
    if (c=='*'&&d=='='){p->pos+=2;p->tok=T_STAR_EQ; p->prev_val=false;return;}
    if (c=='/'&&d=='='){p->pos+=2;p->tok=T_SLASH_EQ;p->prev_val=false;return;}
    if (c=='%'&&d=='='){p->pos+=2;p->tok=T_PCT_EQ;  p->prev_val=false;return;}
    if (c=='>'&&d=='>'){p->pos+=2;p->tok=T_APPEND;  p->prev_val=false;return;}

    p->pos++;
    switch(c) {
        case '+': p->tok=T_PLUS;     p->prev_val=false; return;
        case '-': p->tok=T_MINUS;    p->prev_val=false; return;
        case '*': p->tok=T_STAR;     p->prev_val=false; return;
        case '/': p->tok=T_SLASH;    p->prev_val=false; return;
        case '%': p->tok=T_PCT;      p->prev_val=false; return;
        case '^': p->tok=T_CARET;    p->prev_val=false; return;
        case '(': p->tok=T_LPAREN;   p->prev_val=false; return;
        case ')': p->tok=T_RPAREN;   p->prev_val=true;  return;
        case '{': p->tok=T_LBRACE;   p->prev_val=false; return;
        case '}': p->tok=T_RBRACE;   p->prev_val=true;  return;
        case '[': p->tok=T_LBRACKET; p->prev_val=false; return;
        case ']': p->tok=T_RBRACKET; p->prev_val=true;  return;
        case ',': p->tok=T_COMMA;    p->prev_val=false; return;
        case '$': p->tok=T_DOLLAR;   p->prev_val=false; return;
        case '=': p->tok=T_ASSIGN;   p->prev_val=false; return;
        case '<': p->tok=T_LT;       p->prev_val=false; return;
        case '>': p->tok=T_GT;       p->prev_val=false; return;
        case '~': p->tok=T_MATCH;    p->prev_val=false; return;
        case '!': p->tok=T_NOT;      p->prev_val=false; return;
        case '?': p->tok=T_QUESTION; p->prev_val=false; return;
        case ':': p->tok=T_COLON;    p->prev_val=false; return;
        case '|': p->tok=T_PIPE;     p->prev_val=false; return;
        default:
            fprintf(stderr,"awk: unexpected char '%c'\n",c);
            p->tok=T_EOF;
    }
}

static void skip_sep(P *p) {
    while (p->tok==T_NEWLINE||p->tok==T_SEMI) next(p);
}

/* ── Forward declarations ────────────────────────────────────────────────── */

static Val  parse_expr(P *p);
static void exec_stmt(P *p);
static void exec_block(P *p);

/* ── Printf helper ────────────────────────────────────────────────────────── */

static void do_printf(FILE *fp, const char *fmt, Val *args, int nargs) {
    int ai=0;
    for (const char *f=fmt; *f; f++) {
        if (*f!='%') { fputc(*f,fp); continue; }
        f++;
        if (*f=='%') { fputc('%',fp); continue; }
        /* collect flags, width, precision */
        char spec[32]; int si=0; spec[si++]='%';
        while (*f=='-'||*f=='+'||*f==' '||*f=='0'||*f=='#') spec[si++]=*f++;
        while (isdigit((unsigned char)*f)) spec[si++]=*f++;
        if (*f=='.') { spec[si++]=*f++; while(isdigit((unsigned char)*f)) spec[si++]=*f++; }
        spec[si++]=*f; spec[si]='\0';
        Val zero=make_num(0);
        Val *a = (ai<nargs)?&args[ai++]:&zero;
        switch(*f) {
            case 'd': case 'i': { spec[si-1]='d'; fprintf(fp,spec,(int)val_num(a)); break; }
            case 'f': case 'e': case 'E': case 'g': case 'G':
                fprintf(fp,spec,val_num(a)); break;
            case 's': fprintf(fp,spec,val_str(a)); break;
            case 'c': { int ch=(int)val_num(a); if(!ch&&(a->flags&VAL_STR)&&a->s[0]) ch=a->s[0]; fputc(ch?ch:' ',fp); break; }
            case 'o': fprintf(fp,spec,(int)val_num(a)); break;
            case 'x': case 'X': { spec[si-1]=*f; fprintf(fp,spec,(unsigned int)(long long)val_num(a)); break; }
            default: fputs(spec,fp); break;
        }
    }
}

static char *sprintf_val(const char *fmt, Val *args, int nargs) {
    static char buf[4096];
    FILE *tmp = tmpfile();
    if (!tmp) { buf[0]='\0'; return buf; }
    do_printf(tmp, fmt, args, nargs);
    fflush(tmp); rewind(tmp);
    int n=(int)fread(buf,1,sizeof(buf)-1,tmp); buf[n]='\0';
    fclose(tmp); return buf;
}

/* ── Built-in string/math functions ─────────────────────────────────────── */

static Val call_builtin(const char *name, Val *args, int nargs) {
    Val z=make_num(0);
    /* Copy up to 3 args into lvalue temporaries so &A(i) is valid */
    Val tmp[3];
    tmp[0]=(nargs>0)?args[0]:z; tmp[1]=(nargs>1)?args[1]:z; tmp[2]=(nargs>2)?args[2]:z;
#define A(i) tmp[(i)<3?(i):0]

    if (!strcmp(name,"length")) {
        if (nargs==0) return make_num(strlen(g_line));
        /* check if it's an array name */
        Val a=A(0);
        return make_num(strlen(val_str(&a)));
    }
    if (!strcmp(name,"substr")) {
        Val s=A(0); const char *str=val_str(&s);
        int m=(int)val_num(&args[1])-1; if(m<0)m=0;
        int slen=strlen(str);
        int n=(nargs>=3)?(int)val_num(&args[2]):slen;
        if(m>slen) return make_str("");
        if(m+n>slen) n=slen-m;
        char buf[512]; memcpy(buf,str+m,n); buf[n]='\0';
        return make_str(buf);
    }
    if (!strcmp(name,"index")) {
        Val s=A(0),t=A(1);
        const char *p=strstr(val_str(&s),val_str(&t));
        return make_num(p? (int)(p-val_str(&s))+1 : 0);
    }
    if (!strcmp(name,"split")) {
        if (nargs<2) return z;
        (void)A(0);
        /* get array name from the expression that produced A(1) - workaround:
           since we can't get the array name here, caller passes it differently.
           This function is handled specially in parse_primary. */
        return z;
    }
    if (!strcmp(name,"tolower")) { Val s=A(0); const char *p=val_str(&s);
        char buf[512]; int i=0; while(*p&&i<510){buf[i++]=tolower((unsigned char)*p++);} buf[i]='\0'; return make_str(buf); }
    if (!strcmp(name,"toupper")) { Val s=A(0); const char *p=val_str(&s);
        char buf[512]; int i=0; while(*p&&i<510){buf[i++]=toupper((unsigned char)*p++);} buf[i]='\0'; return make_str(buf); }
    if (!strcmp(name,"sprintf")) {
        if (nargs<1) return make_str("");
        Val fmtv=A(0); return make_str(sprintf_val(val_str(&fmtv),args+1,nargs-1)); }
    if (!strcmp(name,"sin"))   return make_num(sin(val_num(&A(0))));
    if (!strcmp(name,"cos"))   return make_num(cos(val_num(&A(0))));
    if (!strcmp(name,"atan2")) return make_num(atan2(val_num(&A(0)),val_num(&A(1))));
    if (!strcmp(name,"exp"))   return make_num(exp(val_num(&A(0))));
    if (!strcmp(name,"log"))   { double v=val_num(&A(0)); return make_num(v>0?log(v):0); }
    if (!strcmp(name,"sqrt"))  { double v=val_num(&A(0)); return make_num(v>=0?sqrt(v):0); }
    if (!strcmp(name,"int"))   { double v=val_num(&A(0)); return make_num(v>=0?floor(v):ceil(v)); }
    if (!strcmp(name,"rand"))  return make_num((double)rand()/((double)RAND_MAX+1.0));
    if (!strcmp(name,"srand")) { unsigned s=(nargs>0)?(unsigned)val_num(&A(0)):(unsigned)time(NULL); srand(s); return make_num(s); }
    if (!strcmp(name,"system")){ Val s=A(0); return make_num(system(val_str(&s))); }
    if (!strcmp(name,"match")) {
        Val sv=A(0), rv=A(1); const char *s=val_str(&sv), *re=val_str(&rv);
        int rs=0,rl=-1;
        bool ok=re_match(re,s,&rs,&rl);
        g_rstart=rs; g_rlength=rl;
        set_var("RSTART",make_num(rs)); set_var("RLENGTH",make_num(rl));
        return make_num(ok?rs:0);
    }
#undef A
    return z;
}

/* ── Expression parser ────────────────────────────────────────────────────── */

/* Check if current token can start a new primary expression (for concat) */
static bool can_start_primary(Tok t) {
    return t==T_NUM||t==T_STR||t==T_NAME||t==T_DOLLAR||t==T_LPAREN||
           t==T_REGEX||t==T_NOT||t==T_MINUS||t==T_INC||t==T_DEC;
}

/* Helper: make a "key" string from multiple subscripts */
static void make_arr_key(P *p, char *key, int keylen) {
    key[0]='\0';
    Val v = parse_expr(p);
    strncat(key,val_str(&v),keylen-1);
    while (p->tok==T_COMMA) {
        next(p);
        strncat(key,"\034",keylen-strlen(key)-1);
        Val v2=parse_expr(p);
        strncat(key,val_str(&v2),keylen-strlen(key)-1);
    }
}

static Val parse_primary(P *p) {
    /* Prefix ++ -- */
    if (p->tok==T_INC) {
        next(p);
        if (p->tok==T_NAME) { char nm[64]; strcpy(nm,p->id); next(p);
            Val v=get_var(nm); v.n=val_num(&v)+1; v.flags=VAL_NUM; set_var(nm,v); return v; }
        if (p->tok==T_DOLLAR) {
            next(p); Val iv=parse_primary(p); int n=(int)val_num(&iv);
            const char *cur=(n==0)?g_line:(n<=g_nf&&n>0)?g_fld[n-1]:"";
            double nv=atof(cur)+1;
            char buf[64]; snprintf(buf,sizeof(buf),"%.6g",nv);
            set_field(n,buf); return make_num(nv);
        }
        return make_num(0);
    }
    if (p->tok==T_DEC) {
        next(p);
        if (p->tok==T_NAME) { char nm[64]; strcpy(nm,p->id); next(p);
            Val v=get_var(nm); v.n=val_num(&v)-1; v.flags=VAL_NUM; set_var(nm,v); return v; }
        return make_num(0);
    }
    /* Unary minus */
    if (p->tok==T_MINUS) { next(p); Val v=parse_primary(p); return make_num(-val_num(&v)); }
    /* Logical NOT */
    if (p->tok==T_NOT) { next(p); Val v=parse_primary(p); return make_num(val_true(&v)?0:1); }

    /* Parenthesised */
    if (p->tok==T_LPAREN) {
        next(p); Val v=parse_expr(p);
        if (p->tok==T_RPAREN) next(p);
        /* Check for postfix ++ -- */
        if (p->tok==T_INC) { next(p); return v; }
        if (p->tok==T_DEC) { next(p); return v; }
        return v;
    }

    /* Number / string / regex literal */
    if (p->tok==T_NUM)   { Val v=make_num(p->num); next(p); return v; }
    if (p->tok==T_STR)   { Val v=make_str(p->str); next(p); return v; }
    if (p->tok==T_REGEX) { /* regex as pattern: match against $0 */
        int rs=0,rl=-1;
        Val v=make_num(re_match(p->re,g_line,&rs,&rl)?1:0);
        next(p); return v;
    }

    /* $ field access */
    if (p->tok==T_DOLLAR) {
        next(p);
        Val iv = parse_primary(p);
        int n = (int)val_num(&iv);
        /* Field assignment? */
        Tok op = p->tok;
        if (op==T_ASSIGN||op==T_PLUS_EQ||op==T_MINUS_EQ||op==T_STAR_EQ||op==T_SLASH_EQ||op==T_PCT_EQ) {
            next(p); Val rhs=parse_expr(p);
            double old_n=(n==0)?atof(g_line):(n>0&&n<=g_nf)?atof(g_fld[n-1]):0;
            double rv=val_num(&rhs);
            Val nv;
            switch(op){
                case T_PLUS_EQ:  nv=make_num(old_n+rv); break;
                case T_MINUS_EQ: nv=make_num(old_n-rv); break;
                case T_STAR_EQ:  nv=make_num(old_n*rv); break;
                case T_SLASH_EQ: nv=make_num(rv?old_n/rv:0); break;
                case T_PCT_EQ:   nv=make_num(rv?fmod(old_n,rv):0); break;
                default:         nv=rhs; break;
            }
            set_field(n, val_str(&nv));
            return nv;
        }
        /* Postfix ++ -- on field */
        if (p->tok==T_INC||p->tok==T_DEC) {
            bool inc=(p->tok==T_INC); next(p);
            const char *cur=(n==0)?g_line:(n>0&&n<=g_nf)?g_fld[n-1]:"";
            double ov=atof(cur), nv=inc?ov+1:ov-1;
            char buf[64]; snprintf(buf,sizeof(buf),"%.6g",nv); set_field(n,buf);
            return make_num(ov);
        }
        /* Read field */
        if (n==0) return make_str(g_line);
        if (n>0&&n<=g_nf) return make_str(g_fld[n-1]);
        return make_undef();
    }

    /* Name: variable, array access, function call */
    if (p->tok==T_NAME) {
        char name[256]; strcpy(name,p->id); next(p);

        /* Array subscript */
        if (p->tok==T_LBRACKET) {
            next(p);
            char key[512]; make_arr_key(p,key,sizeof(key));
            if (p->tok==T_RBRACKET) next(p);
            /* Assignment? */
            Tok op=p->tok;
            if (op==T_ASSIGN||op==T_PLUS_EQ||op==T_MINUS_EQ||op==T_STAR_EQ||op==T_SLASH_EQ||op==T_PCT_EQ) {
                next(p); Val rhs=parse_expr(p);
                Val old=arr_get(name,key); double ov=val_num(&old), rv=val_num(&rhs); Val nv;
                switch(op){
                    case T_PLUS_EQ:  nv=make_num(ov+rv); break;
                    case T_MINUS_EQ: nv=make_num(ov-rv); break;
                    case T_STAR_EQ:  nv=make_num(ov*rv); break;
                    case T_SLASH_EQ: nv=make_num(rv?ov/rv:0); break;
                    case T_PCT_EQ:   nv=make_num(rv?fmod(ov,rv):0); break;
                    default:         nv=rhs; break;
                }
                arr_set(name,key,nv); return nv;
            }
            if (p->tok==T_INC||p->tok==T_DEC) {
                bool inc=(p->tok==T_INC); next(p);
                Val old=arr_get(name,key); double ov=val_num(&old);
                arr_set(name,key,make_num(inc?ov+1:ov-1)); return make_num(ov);
            }
            return arr_get(name,key);
        }

        /* Function call */
        if (p->tok==T_LPAREN) {
            next(p);
            /* Collect arguments */
            Val args[16]; int nargs=0;
            char argnames[16][64]; /* for split(s, arr) etc. — array names */
            memset(argnames,0,sizeof(argnames));
            while (p->tok!=T_RPAREN&&p->tok!=T_EOF&&nargs<16) {
                /* Check if this arg is a bare name followed by ) or , (array arg) */
                if (p->tok==T_NAME) {
                    char an[64]; strcpy(an,p->id);
                    int saved_pos=p->pos; Tok saved_tok=p->tok; bool saved_pv=p->prev_val;
                    char saved_id[256]; strcpy(saved_id,p->id);
                    next(p);
                    if (p->tok==T_RBRACKET||p->tok==T_RPAREN||p->tok==T_COMMA||p->tok==T_LBRACKET) {
                        if (p->tok==T_LBRACKET) { /* arr[key] — fall through to expr eval */
                            p->pos=saved_pos; p->tok=saved_tok; p->prev_val=saved_pv; strcpy(p->id,saved_id);
                            next(p); /* re-read */
                            /* nope, just re-parse */
                            p->pos=saved_pos; p->tok=saved_tok; p->prev_val=saved_pv; strcpy(p->id,saved_id);
                        } else {
                            strncpy(argnames[nargs],an,63);
                            args[nargs++]=get_var(an);
                            if (p->tok==T_COMMA) next(p); continue;
                        }
                    } else {
                        p->pos=saved_pos; p->tok=saved_tok; p->prev_val=saved_pv; strcpy(p->id,saved_id);
                    }
                }
                /* T_REGEX as a function arg → use the pattern string directly */
                if (p->tok==T_REGEX) {
                    args[nargs++]=make_str(p->re); next(p);
                    if (p->tok==T_COMMA) next(p);
                    continue;
                }
                args[nargs++]=parse_expr(p);
                if (p->tok==T_COMMA) next(p); else break;
            }
            if (p->tok==T_RPAREN) next(p);

            /* Special cases that need array names */
            if (!strcmp(name,"split") && nargs>=2) {
                Val sv=args[0]; const char *s=val_str(&sv);
                const char *sep=(nargs>=3)?val_str(&args[2]):g_fs;
                const char *aname=argnames[1][0]?argnames[1]:name;
                arr_del_all(aname);
                char tmp[2048]; strncpy(tmp,s,sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
                char *p2=tmp; int cnt=0;
                if (!strcmp(sep," ")) {
                    while(*p2&&isspace((unsigned char)*p2)) p2++;
                    while(*p2) {
                        char *e=p2; while(*e&&!isspace((unsigned char)*e)) e++;
                        char saved2=*e; *e='\0';
                        char k[16]; snprintf(k,sizeof(k),"%d",++cnt);
                        arr_set(aname,k,make_str(p2));
                        *e=saved2; p2=e; while(*p2&&isspace((unsigned char)*p2)) p2++;
                    }
                } else if (sep[1]=='\0') {
                    char sc=sep[0];
                    while(1) { char *e=strchr(p2,sc);
                        char saved2=e?*e:'\0'; if(e)*e='\0';
                        char k[16]; snprintf(k,sizeof(k),"%d",++cnt);
                        arr_set(aname,k,make_str(p2)); if(!e) break; *e=saved2; p2=e+1; }
                } else {
                    int sl=strlen(sep);
                    while(1) { char *e=strstr(p2,sep);
                        char saved2=e?*e:'\0'; if(e)*e='\0';
                        char k[16]; snprintf(k,sizeof(k),"%d",++cnt);
                        arr_set(aname,k,make_str(p2)); if(!e) break; *e=saved2; p2=e+sl; }
                }
                return make_num(cnt);
            }
            if (!strcmp(name,"sub")||!strcmp(name,"gsub")) {
                bool is_gsub=(!strcmp(name,"gsub"));
                Val rv=args[0], replv=args[1];
                const char *re=val_str(&rv), *repl=val_str(&replv);
                const char *target=(nargs>=3)?val_str(&args[2]):g_line;
                char out[8192]; int oi=0; int cnt=0;
                const char *s=target;
                while(*s && oi<(int)sizeof(out)-1) {
                    int rs=0,rl=-1;
                    bool m=re_match(re,s,&rs,&rl);
                    if (!m) { out[oi++]=*s++; continue; }
                    for(int i=0;i<rs-1&&oi<(int)sizeof(out)-1;i++) out[oi++]=s[i];
                    for(const char *r2=repl;*r2&&oi<(int)sizeof(out)-1;r2++) out[oi++]=*r2;
                    s+=rs-1+rl; cnt++;
                    if(!is_gsub) break;
                }
                while(*s&&oi<(int)sizeof(out)-1) out[oi++]=*s++;
                out[oi]='\0';
                if (nargs>=3 && argnames[2][0]) set_var(argnames[2],make_str(out));
                else { strncpy(g_line,out,sizeof(g_line)-1); split_fields(); }
                return make_num(cnt);
            }
            if (!strcmp(name,"length") && nargs==1 && argnames[0][0]) {
                /* length of array */
                Arr *a=find_arr(argnames[0],false); if(!a) return make_num(0);
                int cnt=0; for(int b=0;b<ARR_BKTS;b++) for(AEnt *e=a->bkt[b];e;e=e->next) cnt++;
                return make_num(cnt);
            }

            /* User-defined function? */
            for (int fi=0;fi<g_nfuncs;fi++) {
                if (!strcmp(g_funcs[fi].name,name)) {
                    Func *fn=&g_funcs[fi];
                    /* Save param vars */
                    Val saved[8]; char snames[8][64];
                    int np=fn->nparams;
                    for(int i=0;i<np;i++) {
                        strcpy(snames[i],fn->params[i]);
                        saved[i]=get_var(fn->params[i]);
                        set_var(fn->params[i], i<nargs?args[i]:make_undef());
                    }
                    /* Execute body */
                    bool old_ret=g_return; g_return=false;
                    Val old_rv=g_return_val; g_return_val=make_undef();
                    P bp; bp.src=fn->body; bp.pos=0; bp.prev_val=false; next(&bp);
                    exec_block(&bp);
                    Val result=g_return_val;
                    g_return=old_ret; g_return_val=old_rv;
                    /* Restore param vars */
                    for(int i=0;i<np;i++) set_var(snames[i],saved[i]);
                    return result;
                }
            }

            return call_builtin(name,args,nargs);
        }

        /* Plain variable — assignment or read */
        Tok op=p->tok;
        if (op==T_ASSIGN||op==T_PLUS_EQ||op==T_MINUS_EQ||op==T_STAR_EQ||op==T_SLASH_EQ||op==T_PCT_EQ) {
            next(p); Val rhs=parse_expr(p);
            Val old=get_var(name); double ov=val_num(&old), rv=val_num(&rhs); Val nv;
            switch(op){
                case T_PLUS_EQ:  nv=make_num(ov+rv); break;
                case T_MINUS_EQ: nv=make_num(ov-rv); break;
                case T_STAR_EQ:  nv=make_num(ov*rv); break;
                case T_SLASH_EQ: nv=make_num(rv?ov/rv:0); break;
                case T_PCT_EQ:   nv=make_num(rv?fmod(ov,rv):0); break;
                default:         nv=rhs; break;
            }
            set_var(name,nv); return nv;
        }
        if (p->tok==T_INC) { next(p); Val v=get_var(name); double ov=val_num(&v); set_var(name,make_num(ov+1)); return make_num(ov); }
        if (p->tok==T_DEC) { next(p); Val v=get_var(name); double ov=val_num(&v); set_var(name,make_num(ov-1)); return make_num(ov); }
        return get_var(name);
    }

    return make_undef();
}

static Val parse_postfix(P *p) { return parse_primary(p); } /* handled inline */

static Val parse_power(P *p) {
    Val v=parse_postfix(p);
    if (p->tok==T_CARET) { next(p); Val r=parse_power(p); return make_num(pow(val_num(&v),val_num(&r))); }
    return v;
}
static Val parse_unary(P *p)  { return parse_power(p); } /* prefix handled in primary */

static Val parse_term(P *p) {
    Val v=parse_unary(p);
    while (p->tok==T_STAR||p->tok==T_SLASH||p->tok==T_PCT) {
        Tok op=p->tok; next(p); Val r=parse_unary(p);
        double a=val_num(&v), b=val_num(&r);
        if (op==T_STAR) v=make_num(a*b);
        else if (op==T_SLASH) v=make_num(b?a/b:0);
        else v=make_num(b?fmod(a,b):0);
    }
    return v;
}
static Val parse_add(P *p) {
    Val v=parse_term(p);
    while (p->tok==T_PLUS||p->tok==T_MINUS) {
        Tok op=p->tok; next(p); Val r=parse_term(p);
        v=make_num(op==T_PLUS?val_num(&v)+val_num(&r):val_num(&v)-val_num(&r));
    }
    return v;
}
static Val parse_concat(P *p) {
    Val v=parse_add(p);
    while (can_start_primary(p->tok)) {
        Val r=parse_add(p);
        char buf[1024];
        snprintf(buf,sizeof(buf),"%s%s",val_str(&v),val_str(&r));
        v=make_str(buf);
    }
    return v;
}
static Val parse_cmp(P *p) {
    Val v=parse_concat(p);
    while (p->tok==T_LT||p->tok==T_GT||p->tok==T_LE||p->tok==T_GE||p->tok==T_EQ||p->tok==T_NE) {
        Tok op=p->tok; next(p); Val r=parse_concat(p);
        int c=val_cmp(&v,&r);
        switch(op){
            case T_LT: v=make_num(c<0?1:0); break; case T_GT: v=make_num(c>0?1:0); break;
            case T_LE: v=make_num(c<=0?1:0);break; case T_GE: v=make_num(c>=0?1:0);break;
            case T_EQ: v=make_num(c==0?1:0);break; case T_NE: v=make_num(c!=0?1:0);break;
            default: break;
        }
    }
    return v;
}
static Val parse_match_op(P *p) {
    Val v=parse_cmp(p);
    while (p->tok==T_MATCH||p->tok==T_NOMATCH) {
        bool neg=(p->tok==T_NOMATCH); next(p);
        /* If RHS is a /re/ literal, use the regex string directly.
           Otherwise evaluate as an expression and use its string value. */
        char patbuf[512]; const char *pat;
        if (p->tok==T_REGEX) {
            strncpy(patbuf,p->re,sizeof(patbuf)-1); patbuf[sizeof(patbuf)-1]='\0';
            pat=patbuf; next(p);
        } else {
            Val r=parse_cmp(p); pat=val_str(&r);
        }
        int rs=0,rl=-1; bool m=re_match(pat,val_str(&v),&rs,&rl);
        v=make_num((neg?!m:m)?1:0);
    }
    /* 'in' array membership */
    if (p->tok==T_IN) {
        next(p); char aname[64];
        if(p->tok==T_NAME){strcpy(aname,p->id);next(p);}else aname[0]='\0';
        v=make_num(arr_has(aname,val_str(&v))?1:0);
    }
    return v;
}
static Val parse_and(P *p) {
    Val v=parse_match_op(p);
    while (p->tok==T_AND) {
        next(p); Val r=parse_match_op(p);
        v=make_num((val_true(&v)&&val_true(&r))?1:0);
    }
    return v;
}
static Val parse_or(P *p) {
    Val v=parse_and(p);
    while (p->tok==T_OR) {
        next(p); Val r=parse_and(p);
        v=make_num((val_true(&v)||val_true(&r))?1:0);
    }
    return v;
}
static Val parse_expr(P *p) {
    Val v=parse_or(p);
    if (p->tok==T_QUESTION) {
        next(p); bool cond=val_true(&v);
        Val a=parse_or(p);
        if (p->tok==T_COLON) next(p);
        Val b=parse_or(p);
        return cond?a:b;
    }
    return v;
}

/* ── Pipe cache (forward declaration for exec_print) ─────────────────────── */
static FILE *get_pipe(const char *cmd);

/* ── Print / printf ───────────────────────────────────────────────────────── */

static void exec_print(P *p, bool is_printf) {
    FILE *out=stdout;
    Val args[32]; int nargs=0;
    bool print_default=(p->tok==T_NEWLINE||p->tok==T_SEMI||p->tok==T_RBRACE||
                        p->tok==T_EOF||p->tok==T_PIPE);
    if (!is_printf && print_default) {
        /* print with no args: print $0 */
        char *pipe_cmd=NULL;
        if (p->tok==T_PIPE) { next(p); Val cv=parse_expr(p); pipe_cmd=strdup(val_str(&cv)); }
        if (pipe_cmd) { out=get_pipe(pipe_cmd); free(pipe_cmd); if(!out)out=stdout; }
        fputs(g_line,out); fputs(g_ors,out);
        return;
    }
    /* collect args */
    while (p->tok!=T_NEWLINE&&p->tok!=T_SEMI&&p->tok!=T_RBRACE&&p->tok!=T_EOF&&
           p->tok!=T_PIPE&&p->tok!=T_APPEND&&nargs<32) {
        args[nargs++]=parse_expr(p);
        if (p->tok==T_COMMA) { next(p); continue; }
        break;
    }
    char *pipe_cmd=NULL;
    if (p->tok==T_PIPE) {
        next(p); Val cv=parse_expr(p); pipe_cmd=strdup(val_str(&cv));
        if(!pipe_cmd){fputs("awk: out of memory\n",stderr);exit(1);}
        out=get_pipe(pipe_cmd); if(!out)out=stdout;
    }
    if (is_printf) {
        if (nargs>0) do_printf(out,val_str(&args[0]),args+1,nargs-1);
    } else {
        for(int i=0;i<nargs;i++) { if(i) fputs(g_ofs,out); fputs(val_str(&args[i]),out); }
        fputs(g_ors,out);
    }
    if (pipe_cmd) free(pipe_cmd);
}

/* ── Statement executor ───────────────────────────────────────────────────── */

static void exec_block(P *p) {
    if (p->tok==T_LBRACE) { next(p); skip_sep(p);
        while(p->tok!=T_RBRACE&&p->tok!=T_EOF&&!g_exit&&!g_return&&!g_next&&!g_break&&!g_continue)
            exec_stmt(p);
        if (p->tok==T_RBRACE) next(p);
    } else { exec_stmt(p); }
}

/* Scan body extent (like bc.c): returns strdup of body source including { } */
static char *extract_body(P *p) {
    /* For braced body: include the '{' (p->pos is past '{', so back up 1).
       For single-statement: tok_start is the beginning of the current token. */
    int start=(p->tok==T_LBRACE)? p->pos-1 : p->tok_start;
    if (p->tok==T_LBRACE) {
        int d=1; next(p);
        while(d>0&&p->tok!=T_EOF){
            if(p->tok==T_LBRACE)d++; else if(p->tok==T_RBRACE)d--;
            next(p);
        }
    } else {
        while(p->tok!=T_NEWLINE&&p->tok!=T_SEMI&&p->tok!=T_EOF) next(p);
    }
    int end=p->pos;
    return awk_strndup(p->src+start, end-start);
}

static void exec_stmt(P *p) {
    skip_sep(p);
    if (p->tok==T_EOF||p->tok==T_RBRACE||g_exit||g_return||g_next) return;

    if (p->tok==T_PRINT)  { next(p); exec_print(p,false); skip_sep(p); return; }
    if (p->tok==T_PRINTF) { next(p); exec_print(p,true);  skip_sep(p); return; }
    if (p->tok==T_NEXT)   { next(p); g_next=true; return; }
    if (p->tok==T_EXIT)   { g_exit=true; next(p);
        if (p->tok!=T_NEWLINE&&p->tok!=T_SEMI&&p->tok!=T_RBRACE&&p->tok!=T_EOF) {
            Val v=parse_expr(p); g_exit_code=(int)val_num(&v);
        } return; }
    if (p->tok==T_BREAK)    { next(p); g_break=true; return; }
    if (p->tok==T_CONTINUE) { next(p); g_continue=true; return; }
    if (p->tok==T_RETURN)   { next(p); g_return=true;
        if (p->tok!=T_NEWLINE&&p->tok!=T_SEMI&&p->tok!=T_RBRACE&&p->tok!=T_EOF)
            g_return_val=parse_expr(p);
        else g_return_val=make_undef();
        return; }

    if (p->tok==T_DELETE) {
        next(p);
        if (p->tok==T_NAME) { char nm[64]; strcpy(nm,p->id); next(p);
            if (p->tok==T_LBRACKET) { next(p); char k[512]; make_arr_key(p,k,sizeof(k));
                if(p->tok==T_RBRACKET)next(p); arr_del(nm,k); }
            else arr_del_all(nm);
        } skip_sep(p); return; }

    if (p->tok==T_IF) {
        next(p); if(p->tok==T_LPAREN)next(p);
        Val cond=parse_expr(p); if(p->tok==T_RPAREN)next(p);
        skip_sep(p);
        if (val_true(&cond)) {
            exec_block(p); skip_sep(p);
            if (p->tok==T_ELSE) { next(p); skip_sep(p);
                /* skip else branch */
                char *skp=extract_body(p); free(skp); }
        } else {
            char *skp=extract_body(p); free(skp); skip_sep(p);
            if (p->tok==T_ELSE) { next(p); skip_sep(p); exec_block(p); }
        }
        skip_sep(p); return;
    }

    if (p->tok==T_WHILE) {
        next(p); int cond_pos=p->pos; if(p->tok==T_LPAREN)next(p);
        Val cv=parse_expr(p); if(p->tok==T_RPAREN)next(p); skip_sep(p);
        char *body=extract_body(p);
        while(val_true(&cv)&&!g_exit&&!g_return&&!g_next&&!g_break) {
            P bp; bp.src=body; bp.pos=0; bp.prev_val=false; next(&bp);
            g_continue=false; exec_block(&bp);
            if(g_break){g_break=false;break;}
            g_continue=false;
            P cp; cp.src=p->src; cp.pos=cond_pos; cp.prev_val=false; next(&cp);
            cv=parse_expr(&cp);
        }
        g_break=false; free(body); skip_sep(p); return;
    }

    if (p->tok==T_DO) {
        next(p); skip_sep(p);
        char *body=extract_body(p);
        skip_sep(p); if(p->tok==T_WHILE)next(p);
        if(p->tok==T_LPAREN)next(p);
        int cond_pos=p->pos;
        parse_expr(p); /* consume cond once to get position */
        if(p->tok==T_RPAREN)next(p);
        bool go=true;
        while(go&&!g_exit&&!g_return&&!g_next) {
            P bp; bp.src=body; bp.pos=0; bp.prev_val=false; next(&bp);
            g_continue=false; g_break=false; exec_block(&bp);
            if(g_break){g_break=false;break;}
            P cp; cp.src=p->src; cp.pos=cond_pos; cp.prev_val=false; next(&cp);
            Val cv=parse_expr(&cp); go=val_true(&cv);
        }
        g_break=false; free(body); skip_sep(p); return;
    }

    if (p->tok==T_FOR) {
        next(p); if(p->tok==T_LPAREN)next(p);
        /* peek: is it for(k in arr) ? */
        int saved_pos=p->pos; bool saved_pv=p->prev_val;
        Tok saved_tok=p->tok; char saved_id[256]; strcpy(saved_id,p->id);
        if (p->tok==T_NAME) {
            char keyname[64]; strcpy(keyname,p->id); next(p);
            if (p->tok==T_IN) {
                next(p); char aname[64];
                if(p->tok==T_NAME){strcpy(aname,p->id);next(p);}else aname[0]='\0';
                if(p->tok==T_RPAREN)next(p); skip_sep(p);
                char *body=extract_body(p);
                char **keys; int nk=arr_keys(aname,&keys);
                for(int ki=0;ki<nk&&!g_exit&&!g_return&&!g_next&&!g_break;ki++) {
                    set_var(keyname,make_str(keys[ki]));
                    P bp; bp.src=body; bp.pos=0; bp.prev_val=false; next(&bp);
                    g_continue=false; exec_block(&bp);
                    if(g_break){g_break=false;break;} g_continue=false;
                }
                g_break=false; if(keys)free(keys); free(body); skip_sep(p); return;
            }
            /* not for-in: restore and fall through to for(;;) */
            p->pos=saved_pos; p->tok=saved_tok; p->prev_val=saved_pv; strcpy(p->id,saved_id);
        }
        /* for (init; cond; update) */
        if(p->tok!=T_SEMI) parse_expr(p); /* init */
        int cond_pos=p->pos;
        if(p->tok==T_SEMI)next(p);
        Val cv=make_num(1);
        if(p->tok!=T_SEMI) cv=parse_expr(p);
        int upd_pos=p->pos;
        if(p->tok==T_SEMI)next(p);
        while(p->tok!=T_RPAREN&&p->tok!=T_EOF)next(p);
        int upd_end=p->pos;
        if(p->tok==T_RPAREN)next(p); skip_sep(p);
        char *body=extract_body(p);
        while(val_true(&cv)&&!g_exit&&!g_return&&!g_next&&!g_break) {
            P bp; bp.src=body; bp.pos=0; bp.prev_val=false; next(&bp);
            g_continue=false; exec_block(&bp);
            if(g_break){g_break=false;break;}
            if(upd_end>upd_pos){
                char *upd=awk_strndup(p->src+upd_pos,upd_end-upd_pos);
                P up; up.src=upd; up.pos=0; up.prev_val=false; next(&up); parse_expr(&up); free(upd);
            }
            if(p->src[cond_pos]==';'||p->src[cond_pos]==')') cv=make_num(1);
            else { P cp; cp.src=p->src; cp.pos=cond_pos; cp.prev_val=false; next(&cp); cv=parse_expr(&cp); }
        }
        g_break=false; free(body); skip_sep(p); return;
    }

    /* Expression statement */
    parse_expr(p);
    skip_sep(p);
}

/* ── Pipe cache ───────────────────────────────────────────────────────────── */

static FILE *get_pipe(const char *cmd) {
    for(int i=0;i<g_npipes;i++) if(!strcmp(g_pipes[i].cmd,cmd)) return g_pipes[i].fp;
    if(g_npipes>=MAX_PIPES){fputs("awk: too many pipes\n",stderr);return NULL;}
    FILE *fp=popen(cmd,"w"); if(!fp) return NULL;
    strncpy(g_pipes[g_npipes].cmd,cmd,sizeof(g_pipes[0].cmd)-1);
    g_pipes[g_npipes++].fp=fp; return fp;
}
static void close_pipes(void) {
    for(int i=0;i<g_npipes;i++) pclose(g_pipes[i].fp); g_npipes=0;
}

/* ── Program parser (character-level) ───────────────────────────────────── */

static bool is_ws(char c) { return c==' '||c=='\t'||c=='\r'; }

static void scan_skip_str(const char *s, int *pos) {
    (*pos)++; while(s[*pos]&&s[*pos]!='"'){if(s[*pos]=='\\')(*pos)++;(*pos)++;} if(s[*pos])(*pos)++;
}
static void scan_skip_re(const char *s, int *pos) {
    (*pos)++; while(s[*pos]&&s[*pos]!='/'){if(s[*pos]=='\\')(*pos)++;(*pos)++;} if(s[*pos])(*pos)++;
}
static void scan_skip_line(const char *s, int *pos) { while(s[*pos]&&s[*pos]!='\n')(*pos)++; }
static void scan_skip_blk(const char *s, int *pos) {
    *pos+=2; while(s[*pos]&&!(s[*pos]=='*'&&s[*pos+1]=='/'))(*pos)++;
    if(s[*pos])*pos+=2;
}
/* Find matching '}', pos at '{'. Returns pos just past '}'. */
static int find_block_end(const char *s, int pos) {
    pos++; int d=1; bool pv=false;
    while(s[pos]&&d>0) {
        char c=s[pos];
        if(c=='#'){scan_skip_line(s,&pos);continue;}
        if(c=='/'&&s[pos+1]=='*'){scan_skip_blk(s,&pos);continue;}
        if(c=='"'){scan_skip_str(s,&pos);pv=true;continue;}
        if(c=='/'&&!pv){scan_skip_re(s,&pos);pv=true;continue;}
        if(c=='{'||c=='(')d++;
        else if(c=='}'||c==')')d--;
        if(isalnum((unsigned char)c)||c=='_'||c==')'||c==']'||c=='"')pv=true;
        else if(c!=' '&&c!='\t'&&c!='\n')pv=false;
        pos++;
    }
    return pos;
}

static void parse_program(const char *src) {
    int pos=0, n=(int)strlen(src);
    while(pos<n) {
        /* skip whitespace/blank lines */
        while(pos<n&&(is_ws(src[pos])||src[pos]=='\n'||src[pos]=='\r'))pos++;
        if(pos>=n)break;
        if(src[pos]=='#'){scan_skip_line(src,&pos);continue;}
        if(src[pos]=='/'&&src[pos+1]=='*'){scan_skip_blk(src,&pos);continue;}

        /* function definition */
        if(!strncmp(src+pos,"function",8)&&!isalnum((unsigned char)src[pos+8])&&src[pos+8]!='_') {
            pos+=8;
            while(pos<n&&is_ws(src[pos]))pos++;
            if(g_nfuncs>=MAX_FUNCS){fputs("awk: too many functions\n",stderr);return;}
            Func *fn=&g_funcs[g_nfuncs++]; memset(fn,0,sizeof(*fn));
            int ni=0; while(pos<n&&(isalnum((unsigned char)src[pos])||src[pos]=='_'))fn->name[ni++]=src[pos++];
            fn->name[ni]='\0';
            while(pos<n&&is_ws(src[pos]))pos++;
            if(pos<n&&src[pos]=='('){pos++;
                while(pos<n&&src[pos]!=')') {
                    while(pos<n&&(is_ws(src[pos])||src[pos]==','))pos++;
                    if(src[pos]==')')break;
                    int pi=0;
                    while(pos<n&&(isalnum((unsigned char)src[pos])||src[pos]=='_')&&src[pos]!=')'&&src[pos]!=',')
                        fn->params[fn->nparams][pi++]=src[pos++];
                    fn->params[fn->nparams][pi]='\0';
                    if(pi>0&&fn->nparams<8)fn->nparams++;
                }
                if(pos<n&&src[pos]==')')pos++;
            }
            while(pos<n&&(is_ws(src[pos])||src[pos]=='\n'))pos++;
            if(pos<n&&src[pos]=='{'){
                int end=find_block_end(src,pos);
                fn->body=awk_strndup(src+pos,end-pos); pos=end;
            }
            continue;
        }

        if(g_nrules>=MAX_RULES){fputs("awk: too many rules\n",stderr);return;}
        Rule *r=&g_rules[g_nrules++]; memset(r,0,sizeof(*r));

        /* BEGIN / END */
        if(!strncmp(src+pos,"BEGIN",5)&&!isalnum((unsigned char)src[pos+5])&&src[pos+5]!='_'){
            r->kind=PAT_BEGIN; pos+=5; while(pos<n&&is_ws(src[pos]))pos++;
        } else if(!strncmp(src+pos,"END",3)&&!isalnum((unsigned char)src[pos+3])&&src[pos+3]!='_'){
            r->kind=PAT_END; pos+=3; while(pos<n&&is_ws(src[pos]))pos++;
        } else if(src[pos]=='{') {
            r->kind=PAT_ALWAYS;
        } else {
            /* Extract pattern: scan to first unparenthesized '{' or newline */
            int pat_start=pos, pdepth=0; bool pv=false; bool has_comma=false;
            int brace_pos=-1;
            int pp=pos;
            while(pp<n) {
                char c=src[pp];
                if(c=='#'){scan_skip_line(src,&pp);break;}
                if(c=='/'&&src[pp+1]=='*'){scan_skip_blk(src,&pp);continue;}
                if(c=='"'){scan_skip_str(src,&pp);pv=true;continue;}
                if(c=='/'&&!pv&&pdepth==0){scan_skip_re(src,&pp);pv=true;continue;}
                if(c=='('||c=='['){pdepth++;pv=false;pp++;continue;}
                if(c==')'||c==']'){pdepth--;pv=true;pp++;continue;}
                if(c=='{'&&pdepth==0){brace_pos=pp;break;}
                if((c=='\n')&&pdepth==0){brace_pos=-1;pp++;break;}
                if(c==','&&pdepth==0) has_comma=true;
                if(isalnum((unsigned char)c)||c=='_'||c=='$'||c=='"')pv=true;
                else if(c!=' '&&c!='\t') pv=false;
                pp++;
            }
            int pat_end=(brace_pos>=0)?brace_pos:pp;
            while(pat_end>pat_start&&is_ws(src[pat_end-1]))pat_end--;
            /* Trim trailing newlines */
            while(pat_end>pat_start&&(src[pat_end-1]=='\n'||src[pat_end-1]=='\r'))pat_end--;

            /* Determine pattern kind */
            char *psrc=awk_strndup(src+pat_start,pat_end-pat_start);
            /* skip leading whitespace in psrc to check if it's /regex/ */
            const char *pt=psrc; while(is_ws(*pt))pt++;
            if(*pt=='/') {
                /* /regex/ */
                r->kind=PAT_REGEX;
                const char *re_start=pt+1;
                const char *re_end=strrchr(re_start,'/');
                if(re_end) r->pat=awk_strndup(re_start,re_end-re_start);
                else r->pat=strdup(re_start);
            } else if(has_comma) {
                r->kind=PAT_RANGE;
                /* split on first unparenthesized comma */
                int pd2=0;
                for(int i=0;psrc[i];i++){
                    if(psrc[i]=='('||psrc[i]=='[')pd2++;
                    else if(psrc[i]==')'||psrc[i]==']')pd2--;
                    else if(psrc[i]==','&&pd2==0){
                        r->pat=awk_strndup(psrc,i); r->pat2=strdup(psrc+i+1); break;
                    }
                }
                if(!r->pat){r->pat=strdup(psrc);r->pat2=strdup("");}
            } else {
                r->kind=PAT_EXPR;
                r->pat=strdup(psrc);
            }
            free(psrc);
            pos=(brace_pos>=0)?brace_pos:pp;
        }

        /* Read action { ... } */
        while(pos<n&&is_ws(src[pos]))pos++;
        if(pos<n&&src[pos]=='{') {
            int end=find_block_end(src,pos);
            r->act=awk_strndup(src+pos,end-pos); pos=end;
        } else {
            r->act=strdup("{ print }");
        }
    }
}

/* ── Pattern evaluation ───────────────────────────────────────────────────── */

static bool eval_pattern(Rule *r) {
    switch(r->kind) {
        case PAT_BEGIN:  return false; /* handled separately */
        case PAT_END:    return false;
        case PAT_ALWAYS: return true;
        case PAT_REGEX:  { int rs=0,rl=-1; return re_match(r->pat,g_line,&rs,&rl); }
        case PAT_EXPR: {
            if(!r->pat||!r->pat[0]) return true;
            P p; p.src=r->pat; p.pos=0; p.prev_val=false; next(&p);
            Val v=parse_expr(&p); return val_true(&v);
        }
        case PAT_RANGE: {
            if(r->in_range) {
                /* check second pattern */
                bool end=false;
                if(r->pat2&&r->pat2[0]) {
                    const char *trim=r->pat2; while(is_ws(*trim))trim++;
                    if(*trim=='/'){
                        int rs=0,rl=-1; end=re_match(trim+1,g_line,&rs,&rl);
                    } else {
                        P p; p.src=r->pat2; p.pos=0; p.prev_val=false; next(&p);
                        Val v=parse_expr(&p); end=val_true(&v);
                    }
                }
                if(end) r->in_range=false;
                return true;
            } else {
                bool start=false;
                if(r->pat&&r->pat[0]) {
                    const char *trim=r->pat; while(is_ws(*trim))trim++;
                    if(*trim=='/'){
                        int rs=0,rl=-1; start=re_match(trim+1,g_line,&rs,&rl);
                    } else {
                        P p; p.src=r->pat; p.pos=0; p.prev_val=false; next(&p);
                        Val v=parse_expr(&p); start=val_true(&v);
                    }
                }
                if(start){r->in_range=true; return true;}
                return false;
            }
        }
    }
    return false;
}

static void run_rules(PatKind kind) {
    for(int i=0;i<g_nrules;i++) {
        if(g_rules[i].kind!=kind) continue;
        if(!g_rules[i].act) continue;
        g_next=false; g_break=false; g_continue=false; g_return=false;
        P p; p.src=g_rules[i].act; p.pos=0; p.prev_val=false; next(&p);
        exec_block(&p);
        if(g_exit) return;
    }
}

/* ── Main processing loop ─────────────────────────────────────────────────── */

static void process_stream(FILE *fp, const char *fname) {
    strncpy(g_filename, fname, sizeof(g_filename)-1);
    char line[8192];
    while(!g_exit && fgets(line,sizeof(line),fp)) {
        /* Strip record separator */
        int ln=strlen(line);
        if(ln>0&&line[ln-1]=='\n'){line[--ln]='\0';}
        if(ln>0&&line[ln-1]=='\r'){line[--ln]='\0';}
        g_nr++;
        set_line(line);
        set_var("NR",make_num(g_nr));
        set_var("NF",make_num(g_nf));

        for(int i=0;i<g_nrules&&!g_exit;i++) {
            Rule *r=&g_rules[i];
            if(r->kind==PAT_BEGIN||r->kind==PAT_END) continue;
            if(!r->act) continue;
            if(!eval_pattern(r)) continue;
            g_next=false; g_break=false; g_continue=false; g_return=false;
            P p; p.src=r->act; p.pos=0; p.prev_val=false; next(&p);
            exec_block(&p);
            if(g_next) break;
        }
    }
}

/* ── Usage ────────────────────────────────────────────────────────────────── */

static void usage(void) {
    puts("Usage: awk [-F sep] [-v var=val] 'prog' [file...]");
    puts("       awk [-F sep] [-v var=val] -f script [file...]");
    puts("");
    puts("  -F sep     field separator (default: whitespace)");
    puts("  -v var=val assign variable before execution");
    puts("  -f file    read program from file");
    puts("  --help     display this help and exit");
    puts("  --version  output version information and exit");
    puts("");
    puts("Patterns: BEGIN  END  /regex/  expr  pat1,pat2");
    puts("Actions:  print printf if while for delete");
    puts("Built-ins: NR NF FS OFS ORS RS FILENAME");
    puts("Functions: length substr index split sub gsub match sprintf");
    puts("           tolower toupper sin cos atan2 exp log sqrt int rand srand system");
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    const char *prog=NULL;
    char *prog_buf=NULL;
    char **files=NULL; int nfiles=0;
    char *pre_assigns[64]; int npre=0;

    srand((unsigned)time(NULL));

    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help"))    {usage();return 0;}
        if(!strcmp(argv[i],"--version")) {puts("awk 1.0 (Winix 1.4)");return 0;}
        if(!strcmp(argv[i],"-F")) {
            if(++i>=argc){fputs("awk: -F requires argument\n",stderr);return 1;}
            strncpy(g_fs,argv[i],sizeof(g_fs)-1);
            /* handle escape sequences in FS */
            if(!strcmp(g_fs,"\\t")) strcpy(g_fs,"\t");
            continue;
        }
        if(!strncmp(argv[i],"-F",2)&&argv[i][2]) {
            strncpy(g_fs,argv[i]+2,sizeof(g_fs)-1);
            if(!strcmp(g_fs,"\\t")) strcpy(g_fs,"\t");
            continue;
        }
        if(!strcmp(argv[i],"-v")) {
            if(++i>=argc){fputs("awk: -v requires argument\n",stderr);return 1;}
            if(npre<64) pre_assigns[npre++]=argv[i]; continue;
        }
        if(!strncmp(argv[i],"-v",2)&&argv[i][2]) {
            if(npre<64) pre_assigns[npre++]=argv[i]+2; continue;
        }
        if(!strcmp(argv[i],"-f")) {
            if(++i>=argc){fputs("awk: -f requires argument\n",stderr);return 1;}
            FILE *sf=fopen(argv[i],"r");
            if(!sf){fprintf(stderr,"awk: cannot open %s\n",argv[i]);return 1;}
            fseek(sf,0,SEEK_END); long sz=ftell(sf); rewind(sf);
            prog_buf=malloc(sz+1);
            if(!prog_buf){fputs("awk: out of memory\n",stderr);return 1;}
            sz=(long)fread(prog_buf,1,sz,sf); prog_buf[sz]='\0'; fclose(sf);
            prog=prog_buf; continue;
        }
        if(!strncmp(argv[i],"-f",2)&&argv[i][2]) {
            FILE *sf=fopen(argv[i]+2,"r");
            if(!sf){fprintf(stderr,"awk: cannot open %s\n",argv[i]+2);return 1;}
            fseek(sf,0,SEEK_END); long sz=ftell(sf); rewind(sf);
            prog_buf=malloc(sz+1);
            if(!prog_buf){fputs("awk: out of memory\n",stderr);return 1;}
            sz=(long)fread(prog_buf,1,sz,sf); prog_buf[sz]='\0'; fclose(sf);
            prog=prog_buf; continue;
        }
        if(argv[i][0]=='-'&&argv[i][1]!='\0'){
            fprintf(stderr,"awk: invalid option '%s'\n",argv[i]); return 1;
        }
        /* first non-option is the program (if not set via -f) */
        if(!prog&&!prog_buf) { prog=argv[i]; continue; }
        /* remaining are files */
        if(!files) { files=malloc((argc)*sizeof(char *)); if(!files){fputs("awk: out of memory\n",stderr);return 1;} }
        files[nfiles++]=argv[i];
    }

    if(!prog) { fputs("awk: no program given\n",stderr); fputs("Try 'awk --help'\n",stderr); return 1; }

    /* Apply pre-assignments */
    for(int i=0;i<npre;i++) {
        char *eq=strchr(pre_assigns[i],'=');
        if(!eq) continue;
        *eq='\0'; char *vname=pre_assigns[i]; char *vval=eq+1;
        /* handle \t etc in value */
        char vbuf[512]; int bi=0;
        for(const char *vp=vval;*vp&&bi<510;vp++){
            if(*vp=='\\'&&vp[1]=='t'){vbuf[bi++]='\t';vp++;}
            else if(*vp=='\\'&&vp[1]=='n'){vbuf[bi++]='\n';vp++;}
            else vbuf[bi++]=*vp;
        }
        vbuf[bi]='\0';
        /* set FS/OFS/etc through set_var */
        if(!strcmp(vname,"FS")) strncpy(g_fs,vbuf,sizeof(g_fs)-1);
        else if(!strcmp(vname,"OFS")) strncpy(g_ofs,vbuf,sizeof(g_ofs)-1);
        else if(!strcmp(vname,"ORS")) strncpy(g_ors,vbuf,sizeof(g_ors)-1);
        else set_var(vname, make_str(vbuf));
        *eq='=';
    }

    parse_program(prog);
    if(prog_buf) free(prog_buf);

    /* Run BEGIN rules */
    run_rules(PAT_BEGIN);
    if(g_exit) goto done;

    /* Skip stdin/files only for pure BEGIN-only programs (no data rules, no END rules).
       Even END-only programs need to read stdin to track NR/NF. */
    int needsinput=0;
    for(int i=0;i<g_nrules;i++)
        if(g_rules[i].kind!=PAT_BEGIN) needsinput++;

    /* Process input files */
    if(nfiles==0 && needsinput==0) goto done;
    if(nfiles==0) {
        process_stream(stdin,"");
    } else {
        for(int i=0;i<nfiles&&!g_exit;i++) {
            FILE *fp=fopen(files[i],"r");
            if(!fp){fprintf(stderr,"awk: cannot open %s\n",files[i]);continue;}
            process_stream(fp,files[i]); fclose(fp);
        }
    }

done:
    /* Run END rules (even after exit) */
    g_exit=false; g_next=false;
    run_rules(PAT_END);
    close_pipes();
    if(files) free(files);
    /* Free rules */
    for(int i=0;i<g_nrules;i++){free(g_rules[i].pat);free(g_rules[i].pat2);free(g_rules[i].act);}
    for(int i=0;i<g_nfuncs;i++){free(g_funcs[i].body);}
    /* Free arrays */
    for(int i=0;i<g_narrs;i++) arr_del_all(g_arrs[i].name);
    return g_exit_code;
}
