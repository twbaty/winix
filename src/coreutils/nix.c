/*
 * nix — nano-style text editor for Winix
 *
 * Screen layout:
 *   Row 0        : title bar  (inverse video)
 *   Rows 1..R-2  : file content
 *   Row R-1      : status bar (inverse video)
 *
 * Key bindings:
 *   Ctrl+S  19  Save file
 *   Ctrl+Q  17  Quit (prompts if modified)
 *   Ctrl+X  24  Save + quit
 *   Ctrl+G   7  Go to line N
 *   Ctrl+W  23  Find (prompts for pattern)
 *   Ctrl+N  14  Find next (repeats last pattern)
 *   Ctrl+R  18  Find + replace
 *   Ctrl+Z  26  Undo
 *   Ctrl+K  11  Cut line (repeat to accumulate; any other key resets)
 *   Ctrl+U  21  Paste all cut lines above cursor
 *   Ctrl+A   1  Start of line
 *   Ctrl+E   5  End of line
 *   Ctrl+C   3  Force quit
 *   Tab      9  Insert 4 spaces
 *   Enter   13  Split line at cursor
 *   BS       8  Delete left / join with prev line
 *   Del    224,83  Delete right / join with next line
 *   Arrows 224,72/80/75/77  Move cursor
 *   Home   224,71  Start of line
 *   End    224,79  End of line
 *   PgUp   224,73  Page up
 *   PgDn   224,81  Page down
 *   ESC     27  Cancel prompt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <conio.h>
#include <windows.h>

#define VERSION       "nix 1.2 (Winix 1.0)"
#define MAX_PATH_LEN  MAX_PATH
#define LINE_CAP_INIT 64
#define UNDO_MAX      512

/* ── Undo record ────────────────────────────────────────────────────────── */

typedef enum {
    UT_INS_CHAR,       /* typed char at (cx,cy); undo: delete at (cx,cy) */
    UT_DEL_CHAR_BS,    /* BS deleted char at (cx-1,cy); undo: insert back */
    UT_DEL_CHAR_DEL,   /* Del deleted char at (cx,cy); undo: insert back */
    UT_JOIN_PREV,      /* BS at col 0 joined line cy onto cy-1 at col aux */
    UT_JOIN_NEXT,      /* Del at EOL joined next line at col aux */
    UT_SPLIT,          /* Enter split line cy at col cx */
    UT_TAB,            /* Tab inserted 4 spaces at (cx,cy) */
    UT_CUT,            /* Ctrl+K cut line (aux=1 deleted, 0 cleared) */
    UT_PASTE,          /* Ctrl+U inserted line at cy */
    UT_REPLACE,        /* Ctrl+R replaced match: text=original, textlen=patlen, aux=replen */
} UndoType;

typedef struct {
    UndoType type;
    int  cx, cy;    /* cursor position BEFORE the operation */
    char ch;        /* char for INS/DEL_CHAR ops */
    int  aux;       /* join col for JOIN_PREV/NEXT; delete-flag for CUT */
    char *text;     /* heap: saved line content (CUT only) */
    int   textlen;
} UndoRecord;

/* ── Syntax highlighting language ───────────────────────────────────────── */

typedef enum { LANG_NONE, LANG_C, LANG_SH, LANG_PY, LANG_JSON } LangType;

/* ── Data model ─────────────────────────────────────────────────────────── */

typedef struct {
    char *d;
    int   len, cap;
} Line;

typedef struct {
    Line *lines;
    int   nlines, lcap;
    int   cx, cy;            /* cursor: col index, line index (0-based) */
    int   top_row, left_col; /* viewport origin */
    bool  modified;
    char  filename[MAX_PATH_LEN];
    char  msg[256];          /* transient status-bar message (shown once) */
    char **clip_lines;       /* multi-line clipboard (Ctrl+K / Ctrl+U) */
    int    clip_nlines;
    bool   clip_active;      /* true if last op was Ctrl+K (enables line accumulation) */
    char  last_search[256];  /* last Find pattern, shared by ^W and ^N */
    LangType lang;           /* syntax highlighting language */
    /* undo ring buffer */
    UndoRecord undo_ring[UNDO_MAX];
    int        undo_head;    /* oldest entry index */
    int        undo_cnt;     /* number of valid entries */
} Editor;

/* ── Console helpers ────────────────────────────────────────────────────── */

static HANDLE hout;

static int term_rows(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hout, &csbi))
        return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 25;
}

static int term_cols(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hout, &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return 80;
}

static void move_cursor(int col, int row) {
    COORD c = { (SHORT)col, (SHORT)row };
    SetConsoleCursorPosition(hout, c);
}

static void hide_cursor(void) {
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hout, &ci);
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hout, &ci);
}

static void show_cursor(void) {
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hout, &ci);
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(hout, &ci);
}

static void enable_ansi(void) {
    DWORD mode;
    GetConsoleMode(hout, &mode);
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hout, mode);
}

/* ── Line helpers ───────────────────────────────────────────────────────── */

static void line_init(Line *l) {
    l->cap = LINE_CAP_INIT;
    l->d   = (char *)malloc(l->cap);
    if (!l->d) { fprintf(stderr, "nix: OOM\n"); exit(1); }
    l->d[0] = '\0';
    l->len  = 0;
}

/* Ensure l->cap >= need + 1 (room for 'need' chars + null terminator). */
static void line_ensure(Line *l, int need) {
    if (need + 1 <= l->cap) return;
    int nc = l->cap * 2;
    if (nc < need + 1) nc = need + 1;
    char *nd = (char *)realloc(l->d, nc);
    if (!nd) { fprintf(stderr, "nix: OOM\n"); exit(1); }
    l->d   = nd;
    l->cap = nc;
}

static void line_insert_char(Line *l, int pos, char c) {
    line_ensure(l, l->len + 1);
    memmove(l->d + pos + 1, l->d + pos, l->len - pos + 1); /* incl. null */
    l->d[pos] = c;
    l->len++;
}

static void line_delete_char(Line *l, int pos) {
    if (pos < 0 || pos >= l->len) return;
    memmove(l->d + pos, l->d + pos + 1, l->len - pos); /* incl. null */
    l->len--;
}

static void line_free(Line *l) {
    free(l->d);
    l->d   = NULL;
    l->len = l->cap = 0;
}

/* ── Editor helpers ─────────────────────────────────────────────────────── */

static void editor_ensure_lines(Editor *e, int need) {
    if (need <= e->lcap) return;
    int nc = e->lcap * 2;
    if (nc < need) nc = need;
    Line *nl = (Line *)realloc(e->lines, nc * sizeof(Line));
    if (!nl) { fprintf(stderr, "nix: OOM\n"); exit(1); }
    e->lines = nl;
    e->lcap  = nc;
}

/* Insert a new empty line at position 'at'; shifts lines at 'at' down. */
static void editor_insert_line(Editor *e, int at) {
    editor_ensure_lines(e, e->nlines + 1);
    memmove(e->lines + at + 1, e->lines + at,
            (e->nlines - at) * sizeof(Line));
    line_init(&e->lines[at]);
    e->nlines++;
}

/* Remove line at position 'at'; shifts subsequent lines up. */
static void editor_delete_line(Editor *e, int at) {
    line_free(&e->lines[at]);
    memmove(e->lines + at, e->lines + at + 1,
            (e->nlines - at - 1) * sizeof(Line));
    e->nlines--;
}

/*
 * Split line at_line at at_col: chars from at_col onwards move to a new
 * line inserted at at_line+1.  The original line is truncated to at_col.
 * NOTE: may realloc e->lines — do not hold stale Line* across this call.
 */
static void editor_split_line(Editor *e, int at_line, int at_col) {
    editor_insert_line(e, at_line + 1);
    Line *cur  = &e->lines[at_line];
    Line *next = &e->lines[at_line + 1];
    int   tail = cur->len - at_col;
    if (tail > 0) {
        line_ensure(next, tail);
        memcpy(next->d, cur->d + at_col, tail + 1);
        next->len      = tail;
        cur->d[at_col] = '\0';
        cur->len       = at_col;
    }
}

/* Join line at_line with the line below it (at_line+1). */
static void editor_join_lines(Editor *e, int at_line) {
    Line *cur  = &e->lines[at_line];
    Line *next = &e->lines[at_line + 1];
    line_ensure(cur, cur->len + next->len);
    memcpy(cur->d + cur->len, next->d, next->len + 1);
    cur->len += next->len;
    editor_delete_line(e, at_line + 1);
}

static void editor_init(Editor *e) {
    e->lcap  = 64;
    e->lines = (Line *)malloc(e->lcap * sizeof(Line));
    if (!e->lines) { fprintf(stderr, "nix: OOM\n"); exit(1); }
    e->nlines      = 1;
    line_init(&e->lines[0]);
    e->cx = e->cy  = 0;
    e->top_row     = e->left_col = 0;
    e->modified    = false;
    e->filename[0] = '\0';
    e->msg[0]      = '\0';
    e->clip_lines  = NULL;
    e->clip_nlines = 0;
    e->lang        = LANG_NONE;
    e->clip_active = false;
    e->last_search[0] = '\0';
    e->undo_head   = 0;
    e->undo_cnt    = 0;
}

static void editor_free(Editor *e) {
    for (int i = 0; i < e->nlines; i++) line_free(&e->lines[i]);
    free(e->lines);
    for (int i = 0; i < e->clip_nlines; i++) free(e->clip_lines[i]);
    free(e->clip_lines);
    /* Free any heap text in the undo ring */
    for (int i = 0; i < e->undo_cnt; i++) {
        int slot = (e->undo_head + i) % UNDO_MAX;
        free(e->undo_ring[slot].text);
    }
}

/* ── Undo helpers ───────────────────────────────────────────────────────── */

static void undo_push(Editor *e, UndoRecord r) {
    if (e->undo_cnt == UNDO_MAX) {
        /* Ring is full — drop oldest, freeing its heap text */
        free(e->undo_ring[e->undo_head].text);
        e->undo_ring[e->undo_head].text = NULL;
        e->undo_head = (e->undo_head + 1) % UNDO_MAX;
        e->undo_cnt--;
    }
    int slot = (e->undo_head + e->undo_cnt) % UNDO_MAX;
    e->undo_ring[slot] = r;
    e->undo_cnt++;
}

/* Returns 1 and fills *r on success; 0 if stack is empty. */
static int undo_pop(Editor *e, UndoRecord *r) {
    if (e->undo_cnt == 0) return 0;
    e->undo_cnt--;
    int slot = (e->undo_head + e->undo_cnt) % UNDO_MAX;
    *r = e->undo_ring[slot];
    e->undo_ring[slot].text = NULL; /* ownership transferred to caller */
    return 1;
}

static void editor_apply_undo(Editor *e) {
    UndoRecord r;
    if (!undo_pop(e, &r)) {
        snprintf(e->msg, sizeof(e->msg), "Nothing to undo");
        return;
    }

    switch (r.type) {

    case UT_INS_CHAR:
        /* Undo insert: delete the char that was inserted at (cx, cy) */
        line_delete_char(&e->lines[r.cy], r.cx);
        break;

    case UT_DEL_CHAR_BS:
        /* Undo backspace: re-insert the char that was at (cx-1, cy) */
        line_insert_char(&e->lines[r.cy], r.cx - 1, r.ch);
        break;

    case UT_DEL_CHAR_DEL:
        /* Undo delete-key: re-insert the char that was at (cx, cy) */
        line_insert_char(&e->lines[r.cy], r.cx, r.ch);
        break;

    case UT_JOIN_PREV:
        /*
         * Undo BS-at-col-0: line (r.cy-1) was extended by joining r.cy onto
         * it at column r.aux.  Reverse by splitting (r.cy-1) at r.aux.
         */
        editor_split_line(e, r.cy - 1, r.aux);
        break;

    case UT_JOIN_NEXT:
        /*
         * Undo Del-at-EOL: line r.cy was extended at column r.aux.
         * Reverse by splitting r.cy at r.aux.
         */
        editor_split_line(e, r.cy, r.aux);
        break;

    case UT_SPLIT:
        /* Undo Enter: join lines r.cy and r.cy+1 back together */
        editor_join_lines(e, r.cy);
        break;

    case UT_TAB:
        /* Undo Tab: delete the 4 spaces inserted at (cx, cy) */
        for (int i = 0; i < 4; i++)
            line_delete_char(&e->lines[r.cy], r.cx);
        break;

    case UT_CUT:
        /* Undo Ctrl+K: restore the cut line */
        if (r.aux) {
            /* Line was deleted — re-insert it */
            editor_insert_line(e, r.cy);
        }
        /* Restore line content (both delete and clear cases) */
        {
            Line *ln = &e->lines[r.cy];
            line_ensure(ln, r.textlen);
            memcpy(ln->d, r.text, r.textlen + 1);
            ln->len = r.textlen;
        }
        free(r.text);
        break;

    case UT_PASTE:
        /* Undo Ctrl+U: delete the line that was pasted */
        editor_delete_line(e, r.cy);
        if (r.cy > e->nlines - 1) /* clamp in case it was the last line */
            ; /* cursor restore below handles it */
        break;

    case UT_REPLACE: {
        /* Undo replace: delete the replacement (aux chars), re-insert original (text) */
        Line *ln = &e->lines[r.cy];
        for (int i = 0; i < r.aux; i++)
            line_delete_char(ln, r.cx);
        for (int i = 0; i < r.textlen; i++)
            line_insert_char(ln, r.cx + i, r.text[i]);
        free(r.text);
        break;
    }
    }

    /* Restore cursor to where it was before the operation */
    e->cy = r.cy;
    e->cx = r.cx;
    if (e->cy >= e->nlines) e->cy = e->nlines - 1;
    if (e->cx > e->lines[e->cy].len) e->cx = e->lines[e->cy].len;
    e->modified = true;
}

/* ── File I/O ───────────────────────────────────────────────────────────── */

/* Returns 1 if file opened, 0 if new (path doesn't exist). */
static int editor_load(Editor *e, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    line_free(&e->lines[0]);
    e->nlines = 0;

    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        int len = (int)strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';

        editor_ensure_lines(e, e->nlines + 1);
        line_init(&e->lines[e->nlines]);
        line_ensure(&e->lines[e->nlines], len);
        memcpy(e->lines[e->nlines].d, buf, len + 1);
        e->lines[e->nlines].len = len;
        e->nlines++;
    }
    fclose(f);

    if (e->nlines == 0) {
        editor_ensure_lines(e, 1);
        line_init(&e->lines[0]);
        e->nlines = 1;
    }
    return 1;
}

/* Returns 1 on success, 0 on error. */
static int editor_save(Editor *e) {
    FILE *f = fopen(e->filename, "wb");
    if (!f) return 0;
    for (int i = 0; i < e->nlines; i++) {
        fwrite(e->lines[i].d, 1, e->lines[i].len, f);
        fputc('\n', f);
    }
    fclose(f);
    e->modified = false;
    return 1;
}

/* ── Viewport scrolling ─────────────────────────────────────────────────── */

/* Width of line-number gutter: right-aligned digits + one trailing space. */
static int gutter_width(Editor *e) {
    int n = e->nlines, d = 1;
    while (n >= 10) { d++; n /= 10; }
    return d + 1; /* digits + trailing space */
}

static void scroll_view(Editor *e) {
    int rows     = term_rows();
    int cols     = term_cols();
    int content  = rows - 2;
    if (content < 1) content = 1;
    int vis_cols = cols - gutter_width(e);
    if (vis_cols < 1) vis_cols = 1;

    if (e->cy < e->top_row)
        e->top_row = e->cy;
    if (e->cy >= e->top_row + content)
        e->top_row = e->cy - content + 1;

    if (e->cx < e->left_col)
        e->left_col = e->cx;
    if (e->cx >= e->left_col + vis_cols)
        e->left_col = e->cx - vis_cols + 1;
    if (e->left_col < 0)
        e->left_col = 0;
}

/* ── Prompt helper ──────────────────────────────────────────────────────── */

/*
 * Show 'prompt' in the status bar and accept a single-line response.
 * Returns 1 on Enter (confirmed), 0 on ESC (cancelled, buf[0] set to '\0').
 */
static int prompt_in_status(const char *prompt, char *buf, int bufsz) {
    int rows = term_rows();
    int cols = term_cols();
    int pos  = 0;
    buf[0]   = '\0';

    for (;;) {
        move_cursor(0, rows - 1);
        char line[512];
        snprintf(line, sizeof(line), "  %s%s", prompt, buf);
        int llen = (int)strlen(line);
        printf("\033[7m%s", line);
        for (int i = llen; i < cols; i++) putchar(' ');
        printf("\033[0m");

        move_cursor(2 + (int)strlen(prompt) + pos, rows - 1);
        fflush(stdout);

        int c = _getch();
        if (c == 13) return 1;                          /* Enter — confirm */
        if (c == 27) { buf[0] = '\0'; return 0; }      /* ESC — cancel */
        if (c == 8 && pos > 0) { buf[--pos] = '\0'; }  /* Backspace */
        else if (c >= 32 && c < 127 && pos < bufsz - 1) {
            buf[pos++] = (char)c;
            buf[pos]   = '\0';
        }
    }
}

/* ── Rendering ──────────────────────────────────────────────────────────── */

/* ── Syntax highlighting ─────────────────────────────────────────────────── */

#define HL_NORMAL   0
#define HL_KEYWORD  1
#define HL_STRING   2
#define HL_COMMENT  3
#define HL_NUMBER   4
#define HL_PREPROC  5
#define HL_MAX_LINE 4096

static const char * const HL_ANSI[] = {
    "\033[0m",    /* NORMAL  — default */
    "\033[36m",   /* KEYWORD — cyan    */
    "\033[32m",   /* STRING  — green   */
    "\033[90m",   /* COMMENT — dark    */
    "\033[33m",   /* NUMBER  — yellow  */
    "\033[35m",   /* PREPROC — magenta */
};

static const char * const C_KW[] = {
    "auto","break","case","char","const","continue","default","do",
    "double","else","enum","extern","float","for","goto","if","inline",
    "int","long","register","return","short","signed","sizeof","static",
    "struct","switch","typedef","union","unsigned","void","volatile","while",
    "NULL","true","false","nullptr","bool",
    "int8_t","int16_t","int32_t","int64_t",
    "uint8_t","uint16_t","uint32_t","uint64_t","size_t","wchar_t",
    NULL
};
static const char * const SH_KW[] = {
    "if","then","elif","else","fi","for","while","do","done",
    "case","esac","in","return","exit","break","continue",
    "function","local","export","unset","readonly","shift",
    "echo","printf","read","source","true","false",NULL
};
static const char * const PY_KW[] = {
    "False","None","True","and","as","assert","async","await",
    "break","class","continue","def","del","elif","else","except",
    "finally","for","from","global","if","import","in","is","lambda",
    "nonlocal","not","or","pass","raise","return","try","while","with","yield",
    NULL
};
static const char * const JSON_KW[] = { "true","false","null", NULL };

static LangType detect_lang(const char *path) {
    const char *dot = NULL;
    for (const char *p = path; *p; p++) if (*p == '.') dot = p;
    if (!dot) return LANG_NONE;
    if (!_stricmp(dot,".c")  || !_stricmp(dot,".h")  ||
        !_stricmp(dot,".cpp")|| !_stricmp(dot,".cc") ||
        !_stricmp(dot,".cxx")|| !_stricmp(dot,".hpp")) return LANG_C;
    if (!_stricmp(dot,".sh") || !_stricmp(dot,".bash")|| !_stricmp(dot,".zsh"))
        return LANG_SH;
    if (!_stricmp(dot,".py"))   return LANG_PY;
    if (!_stricmp(dot,".json")) return LANG_JSON;
    return LANG_NONE;
}

static int hl_isword(char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
}

static int hl_is_kw(const char *text, int pos, int len,
                    const char * const *kw) {
    for (int i = 0; kw[i]; i++) {
        int kl = (int)strlen(kw[i]);
        if (len == kl && strncmp(text + pos, kw[i], kl) == 0) return 1;
    }
    return 0;
}

/*
 * Fill hl[0..len-1] with HL_* codes for each character.
 * in_blk: non-zero if entering the line inside a C block comment.
 * Returns block-comment state at END of line (for threading to next line).
 */
static int hl_compute(const char *text, int len,
                      unsigned char *hl, LangType lang, int in_blk) {
    int i = 0;
    memset(hl, HL_NORMAL, (size_t)len);
    if (lang == LANG_NONE || len == 0) return 0;

    if (lang == LANG_C) {
        /* Carry block-comment state in from previous line */
        if (in_blk) {
            while (i < len) {
                hl[i] = HL_COMMENT;
                if (text[i]=='*' && i+1<len && text[i+1]=='/') {
                    hl[i+1] = HL_COMMENT; i += 2; in_blk = 0; break;
                }
                i++;
            }
        }
        while (i < len) {
            /* Preprocessor line */
            if (i == 0 && text[0] == '#') {
                while (i < len) hl[i++] = HL_PREPROC; break;
            }
            /* Line comment */
            if (text[i]=='/' && i+1<len && text[i+1]=='/') {
                while (i < len) hl[i++] = HL_COMMENT; break;
            }
            /* Block comment open */
            if (text[i]=='/' && i+1<len && text[i+1]=='*') {
                hl[i] = hl[i+1] = HL_COMMENT; i += 2; in_blk = 1;
                while (i < len) {
                    hl[i] = HL_COMMENT;
                    if (text[i]=='*' && i+1<len && text[i+1]=='/') {
                        hl[i+1] = HL_COMMENT; i += 2; in_blk = 0; break;
                    }
                    i++;
                }
                continue;
            }
            /* String / char literal */
            if (text[i]=='"' || text[i]=='\'') {
                char q = text[i]; hl[i++] = HL_STRING;
                while (i < len) {
                    hl[i] = HL_STRING;
                    if (text[i]=='\\' && i+1<len) { hl[++i]=HL_STRING; i++; continue; }
                    if (text[i]==q)   { i++; break; }
                    i++;
                }
                continue;
            }
            /* Number */
            if (text[i]>='0' && text[i]<='9' && (i==0||!hl_isword(text[i-1]))) {
                while (i<len && (hl_isword(text[i])||text[i]=='.')) hl[i++]=HL_NUMBER;
                continue;
            }
            /* Identifier / keyword */
            if (hl_isword(text[i]) && !(text[i]>='0'&&text[i]<='9')) {
                int j = i;
                while (j < len && hl_isword(text[j])) j++;
                unsigned char c = hl_is_kw(text,i,j-i,C_KW) ? HL_KEYWORD : HL_NORMAL;
                while (i < j) hl[i++] = c;
                continue;
            }
            i++;
        }

    } else if (lang == LANG_SH) {
        /* Skip leading whitespace then check for comment */
        int ws = 0;
        while (ws < len && (text[ws]==' '||text[ws]=='\t')) ws++;
        if (ws < len && text[ws] == '#') {
            while (i < len) hl[i++] = HL_COMMENT; return 0;
        }
        while (i < len) {
            if (text[i]=='"' || text[i]=='\'') {
                char q = text[i]; hl[i++] = HL_STRING;
                while (i < len) {
                    hl[i] = HL_STRING;
                    if (q=='"' && text[i]=='\\' && i+1<len) { hl[++i]=HL_STRING; i++; continue; }
                    if (text[i]==q) { i++; break; }
                    i++;
                }
                continue;
            }
            if (text[i]>='0'&&text[i]<='9'&&(i==0||!hl_isword(text[i-1]))) {
                while (i<len&&(text[i]>='0'&&text[i]<='9'||text[i]=='.')) hl[i++]=HL_NUMBER;
                continue;
            }
            if (hl_isword(text[i]) && !(text[i]>='0'&&text[i]<='9')) {
                int j = i;
                while (j < len && hl_isword(text[j])) j++;
                unsigned char c = hl_is_kw(text,i,j-i,SH_KW) ? HL_KEYWORD : HL_NORMAL;
                while (i < j) hl[i++] = c;
                continue;
            }
            i++;
        }

    } else if (lang == LANG_PY) {
        while (i < len) {
            if (text[i] == '#') { while (i<len) hl[i++]=HL_COMMENT; break; }
            if (text[i]=='"' || text[i]=='\'') {
                char q = text[i];
                /* Triple-quote? */
                int triple = (i+2<len && text[i+1]==q && text[i+2]==q);
                if (triple) {
                    hl[i]=hl[i+1]=hl[i+2]=HL_STRING; i+=3;
                    while (i+2<len) {
                        hl[i]=HL_STRING;
                        if (text[i]==q&&text[i+1]==q&&text[i+2]==q) {
                            hl[i]=hl[i+1]=hl[i+2]=HL_STRING; i+=3; goto py_after_str;
                        }
                        i++;
                    }
                    while (i<len) hl[i++]=HL_STRING;
                    py_after_str:;
                } else {
                    hl[i++]=HL_STRING;
                    while (i<len) {
                        hl[i]=HL_STRING;
                        if (text[i]=='\\' && i+1<len) { hl[++i]=HL_STRING; i++; continue; }
                        if (text[i]==q)   { i++; break; }
                        i++;
                    }
                }
                continue;
            }
            if (text[i]>='0'&&text[i]<='9'&&(i==0||!hl_isword(text[i-1]))) {
                while (i<len&&(hl_isword(text[i])||text[i]=='.')) hl[i++]=HL_NUMBER;
                continue;
            }
            if (hl_isword(text[i]) && !(text[i]>='0'&&text[i]<='9')) {
                int j = i;
                while (j < len && hl_isword(text[j])) j++;
                unsigned char c = hl_is_kw(text,i,j-i,PY_KW) ? HL_KEYWORD : HL_NORMAL;
                while (i < j) hl[i++] = c;
                continue;
            }
            i++;
        }

    } else if (lang == LANG_JSON) {
        while (i < len) {
            if (text[i] == '"') {
                hl[i++] = HL_STRING;
                while (i < len) {
                    hl[i] = HL_STRING;
                    if (text[i]=='\\' && i+1<len) { hl[++i]=HL_STRING; i++; continue; }
                    if (text[i]=='"') { i++; break; }
                    i++;
                }
                continue;
            }
            if ((text[i]>='0'&&text[i]<='9') || (text[i]=='-'&&i+1<len&&text[i+1]>='0'&&text[i+1]<='9')) {
                while (i<len&&(hl_isword(text[i])||text[i]=='.'||text[i]=='-'||text[i]=='+'||text[i]=='e'||text[i]=='E'))
                    hl[i++]=HL_NUMBER;
                continue;
            }
            if (hl_isword(text[i]) && !(text[i]>='0'&&text[i]<='9')) {
                int j = i;
                while (j<len && hl_isword(text[j])) j++;
                unsigned char c = hl_is_kw(text,i,j-i,JSON_KW) ? HL_KEYWORD : HL_NORMAL;
                while (i < j) hl[i++] = c;
                continue;
            }
            i++;
        }
    }
    return in_blk;
}

/*
 * Scan lines 0..(target_line-1) to determine C block-comment state
 * at the start of target_line.  Fast path for non-C languages.
 */
static int hl_block_state(Editor *e, int target_line) {
    if (e->lang != LANG_C) return 0;
    int in_blk = 0;
    for (int li = 0; li < target_line && li < e->nlines; li++) {
        const char *p = e->lines[li].d;
        int len = e->lines[li].len, i = 0;
        while (i < len) {
            if (!in_blk) {
                if (p[i]=='/'&&i+1<len&&p[i+1]=='/') break;
                if (p[i]=='/'&&i+1<len&&p[i+1]=='*') { in_blk=1; i+=2; continue; }
                if (p[i]=='"'||p[i]=='\'') {
                    char q=p[i++];
                    while (i<len && p[i]!=q) { if (p[i]=='\\') i++; i++; }
                    if (i<len) i++;
                    continue;
                }
            } else {
                if (p[i]=='*'&&i+1<len&&p[i+1]=='/') { in_blk=0; i+=2; continue; }
            }
            i++;
        }
    }
    return in_blk;
}

/*
 * Render the visible slice [start, start+vis_cols) of a line with
 * syntax highlighting.  Emits ANSI codes only on color transitions.
 * Returns the block-comment state at the END of the full line
 * (for passing to the next line).
 */
static int draw_line_hl(const char *text, int len,
                        int start, int vis_cols,
                        LangType lang, int in_blk) {
    if (vis_cols <= 0) return in_blk;

    if (lang == LANG_NONE) {
        fwrite(text + start, 1, vis_cols, stdout);
        return 0;
    }

    unsigned char hl[HL_MAX_LINE];
    int scan_len = len < HL_MAX_LINE ? len : HL_MAX_LINE;
    int new_blk  = hl_compute(text, scan_len, hl, lang, in_blk);

    int cur = -1;
    int end = start + vis_cols;
    if (end > scan_len) end = scan_len;

    for (int i = start; i < end; i++) {
        if (hl[i] != cur) { printf("%s", HL_ANSI[hl[i]]); cur = hl[i]; }
        putchar((unsigned char)text[i]);
    }
    /* Chars beyond HL_MAX_LINE (no highlighting) */
    for (int i = end; i < start + vis_cols && i < len; i++)
        putchar((unsigned char)text[i]);

    if (cur > HL_NORMAL) printf("\033[0m");
    return new_blk;
}

static void draw(Editor *e) {
    int rows    = term_rows();
    int cols    = term_cols();
    int content = rows - 2;
    if (content < 1) content = 1;

    hide_cursor();

    /* ── Title bar (row 0) ── */
    move_cursor(0, 0);
    const char *fname = e->filename[0] ? e->filename : "[No Name]";
    const char *base  = fname;
    for (const char *p = fname; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    char title[512];
    snprintf(title, sizeof(title), "  nix -- %s%s",
             base, e->modified ? " [Modified]" : "");
    int tlen = (int)strlen(title);
    printf("\033[7m%s", title);
    for (int i = tlen; i < cols; i++) putchar(' ');
    printf("\033[0m");

    /* ── Content rows (1 .. rows-2) ── */
    int gutter   = gutter_width(e);
    int vis_cols = cols - gutter;
    if (vis_cols < 1) vis_cols = 1;
    int blk = hl_block_state(e, e->top_row);
    for (int r = 0; r < content; r++) {
        move_cursor(0, r + 1);
        int li = e->top_row + r;
        if (li < e->nlines) {
            /* Line number gutter: right-aligned, dim */
            printf("\033[2m%*d \033[0m", gutter - 1, li + 1);
            Line *ln    = &e->lines[li];
            int   start = e->left_col < ln->len ? e->left_col : ln->len;
            int   vis   = ln->len - start;
            if (vis > vis_cols) vis = vis_cols;
            blk = draw_line_hl(ln->d, ln->len, start, vis, e->lang, blk);
        } else {
            printf("\033[2m%*s~\033[0m", gutter - 1, "");
            blk = 0;
        }
        printf("\033[K");
    }

    /* ── Status bar (row rows-1) ── */
    move_cursor(0, rows - 1);
    char status[512];
    if (e->msg[0]) {
        snprintf(status, sizeof(status), "  %s", e->msg);
        e->msg[0] = '\0';
    } else {
        const char *hints =
            "  ^S:Save  ^Q:Quit  ^W:Find  ^G:Goto  ^R:Repl  ^Z:Undo  ^K:Cut  ^U:Paste";
        char right[64];
        snprintf(right, sizeof(right), "Ln:%d Col:%d  ",
                 e->cy + 1, e->cx + 1);
        int hlen = (int)strlen(hints);
        int rlen = (int)strlen(right);
        int pad  = cols - hlen - rlen;
        if (pad < 0) pad = 0;
        snprintf(status, sizeof(status), "%s%*s%s", hints, pad, "", right);
    }
    int slen = (int)strlen(status);
    printf("\033[7m%s", status);
    for (int i = slen; i < cols; i++) putchar(' ');
    printf("\033[0m");

    /* ── Place cursor ── */
    move_cursor(gutter + e->cx - e->left_col, e->cy - e->top_row + 1);
    show_cursor();
    fflush(stdout);
}

/* ── Find ───────────────────────────────────────────────────────────────── */

/* Search forward from cy+1 using e->last_search.  Wraps around. */
static void editor_do_search(Editor *e) {
    if (!e->last_search[0]) {
        snprintf(e->msg, sizeof(e->msg), "No previous search pattern");
        return;
    }
    for (int i = 1; i <= e->nlines; i++) {
        int   li  = (e->cy + i) % e->nlines;
        char *hit = strstr(e->lines[li].d, e->last_search);
        if (hit) {
            e->cy = li;
            e->cx = (int)(hit - e->lines[li].d);
            snprintf(e->msg, sizeof(e->msg), "Found '%s'", e->last_search);
            return;
        }
    }
    snprintf(e->msg, sizeof(e->msg), "Not found: %s", e->last_search);
}

/* Ctrl+W: prompt for pattern, then search. */
static void editor_find(Editor *e) {
    char pat[256];
    prompt_in_status("Find: ", pat, sizeof(pat));
    if (pat[0])
        strncpy(e->last_search, pat, sizeof(e->last_search) - 1);
    editor_do_search(e);
}

/* Ctrl+N: repeat last search without prompting. */
static void editor_find_next(Editor *e) {
    editor_do_search(e);
}

/* Ctrl+R: interactive find + replace. */
static void editor_replace(Editor *e) {
    /* ── Step 1: pattern ── */
    char pat[256];
    if (!prompt_in_status("Replace: ", pat, sizeof(pat))) return; /* ESC */
    if (pat[0])
        strncpy(e->last_search, pat, sizeof(e->last_search) - 1);
    if (!e->last_search[0]) return;

    int patlen = (int)strlen(e->last_search);

    /* ── Step 2: replacement ── */
    char rep[256];
    if (!prompt_in_status("With: ", rep, sizeof(rep))) return; /* ESC */
    int replen = (int)strlen(rep);

    /* ── Step 3: interactive loop — search from current position, no wrap ── */
    int count   = 0;
    int rep_all = 0;
    int cy      = e->cy;
    int cx      = e->cx;

    for (;;) {
        /* Find next occurrence from (cy, cx) forward */
        int found_cy = -1, found_cx = -1;
        for (int row = cy; row < e->nlines; row++) {
            int sc  = (row == cy) ? cx : 0;
            char *hit = strstr(e->lines[row].d + sc, e->last_search);
            if (hit) {
                found_cy = row;
                found_cx = (int)(hit - e->lines[row].d);
                break;
            }
        }
        if (found_cy == -1) break; /* no more matches */

        e->cy = found_cy;
        e->cx = found_cx;

        if (!rep_all) {
            scroll_view(e);
            draw(e);
            char resp[4];
            int confirmed = prompt_in_status(
                "Replace? (y/n/a/ESC): ", resp, sizeof(resp));
            if (!confirmed) break;                      /* ESC — stop */
            if (resp[0] == 'n' || resp[0] == 'N') {    /* skip */
                cx = found_cx + patlen;
                cy = found_cy;
                if (cx > e->lines[cy].len) { cy++; cx = 0; }
                if (cy >= e->nlines) break;
                continue;
            }
            if (resp[0] == 'a' || resp[0] == 'A')
                rep_all = 1; /* fall through to replace */
        }

        /* ── Perform replacement ── */
        char *saved = (char *)malloc(patlen + 1);
        if (saved) memcpy(saved, e->last_search, patlen + 1);
        undo_push(e, (UndoRecord){
            UT_REPLACE, found_cx, found_cy, 0, replen, saved, patlen
        });

        Line *ln = &e->lines[found_cy];
        for (int i = 0; i < patlen; i++)
            line_delete_char(ln, found_cx);
        for (int i = 0; i < replen; i++)
            line_insert_char(ln, found_cx + i, rep[i]);

        e->cx       = found_cx + replen;
        e->modified = true;
        count++;

        /* Advance past the replacement */
        cx = found_cx + replen;
        cy = found_cy;
    }

    snprintf(e->msg, sizeof(e->msg), "%d replacement%s",
             count, count == 1 ? "" : "s");
}

/* ── Key handler ────────────────────────────────────────────────────────── */

/* Returns 1 to continue editing, 0 to quit. */
static int handle_key(Editor *e, int ch) {
    int rows    = term_rows();
    int content = rows - 2;
    if (content < 1) content = 1;

    /* Any key other than Ctrl+K breaks an ongoing cut sequence */
    e->clip_active = false;

    /* ── Extended keys (arrows, Del, Home, End, PgUp, PgDn) ── */
    if (ch == 224 || ch == 0) {
        int k = _getch();
        switch (k) {
        case 72: /* Up */
            if (e->cy > 0) e->cy--;
            if (e->cx > e->lines[e->cy].len) e->cx = e->lines[e->cy].len;
            break;
        case 80: /* Down */
            if (e->cy < e->nlines - 1) e->cy++;
            if (e->cx > e->lines[e->cy].len) e->cx = e->lines[e->cy].len;
            break;
        case 75: /* Left */
            if (e->cx > 0) {
                e->cx--;
            } else if (e->cy > 0) {
                e->cy--;
                e->cx = e->lines[e->cy].len;
            }
            break;
        case 77: /* Right */
            if (e->cx < e->lines[e->cy].len) {
                e->cx++;
            } else if (e->cy < e->nlines - 1) {
                e->cy++;
                e->cx = 0;
            }
            break;
        case 71: /* Home */
            e->cx = 0;
            break;
        case 79: /* End */
            e->cx = e->lines[e->cy].len;
            break;
        case 73: /* PgUp */
            e->cy -= content;
            if (e->cy < 0) e->cy = 0;
            if (e->cx > e->lines[e->cy].len) e->cx = e->lines[e->cy].len;
            break;
        case 81: /* PgDn */
            e->cy += content;
            if (e->cy >= e->nlines) e->cy = e->nlines - 1;
            if (e->cx > e->lines[e->cy].len) e->cx = e->lines[e->cy].len;
            break;
        case 83: { /* Delete */
            Line *cur = &e->lines[e->cy];
            if (e->cx < cur->len) {
                undo_push(e, (UndoRecord){
                    UT_DEL_CHAR_DEL, e->cx, e->cy,
                    cur->d[e->cx], 0, NULL, 0
                });
                line_delete_char(cur, e->cx);
                e->modified = true;
            } else if (e->cy < e->nlines - 1) {
                undo_push(e, (UndoRecord){
                    UT_JOIN_NEXT, e->cx, e->cy, 0, e->cx, NULL, 0
                });
                editor_join_lines(e, e->cy);
                e->modified = true;
            }
            break;
        }
        }
        return 1;
    }

    /* ── Regular keys ── */
    switch (ch) {

    case 3: /* Ctrl+C — force quit */
        return 0;

    case 19: /* Ctrl+S — save */
        if (!e->filename[0]) {
            snprintf(e->msg, sizeof(e->msg), "No filename — use nix <file>");
            break;
        }
        if (editor_save(e))
            snprintf(e->msg, sizeof(e->msg), "Saved: %s", e->filename);
        else
            snprintf(e->msg, sizeof(e->msg), "Error: cannot save %s",
                     e->filename);
        break;

    case 17: /* Ctrl+Q — quit (prompt if modified) */
        if (e->modified) {
            char resp[4];
            prompt_in_status("Unsaved changes. Save? (y/n/ESC): ",
                             resp, sizeof(resp));
            if (!resp[0]) break; /* ESC — stay */
            if ((resp[0] == 'y' || resp[0] == 'Y') && e->filename[0])
                editor_save(e);
        }
        return 0;

    case 24: /* Ctrl+X — save + quit (prompts if modified, like nano) */
        if (e->modified) {
            char resp[4];
            prompt_in_status("Unsaved changes. Save? (y/n/ESC): ",
                             resp, sizeof(resp));
            if (!resp[0]) break; /* ESC — stay */
            if ((resp[0] == 'y' || resp[0] == 'Y') && e->filename[0])
                editor_save(e);
        }
        return 0;

    case 23: /* Ctrl+W — find */
        editor_find(e);
        break;

    case 14: /* Ctrl+N — find next */
        editor_find_next(e);
        break;

    case 18: /* Ctrl+R — find + replace */
        editor_replace(e);
        break;

    case 26: /* Ctrl+Z — undo */
        editor_apply_undo(e);
        break;

    case 7: { /* Ctrl+G — go to line */
        char buf[32];
        if (prompt_in_status("Go to line: ", buf, sizeof(buf)) && buf[0]) {
            int target = atoi(buf);
            if (target < 1) target = 1;
            if (target > e->nlines) target = e->nlines;
            e->cy = target - 1;
            e->cx = 0;
            snprintf(e->msg, sizeof(e->msg), "Line %d of %d", target, e->nlines);
        }
        break;
    }

    case 11: { /* Ctrl+K — cut line (consecutive presses accumulate) */
        Line *cur = &e->lines[e->cy];
        /* Save to undo: store line content, note whether line gets deleted */
        char *saved = (char *)malloc(cur->len + 1);
        if (saved) memcpy(saved, cur->d, cur->len + 1);
        int will_delete = (e->nlines > 1) ? 1 : 0;
        undo_push(e, (UndoRecord){
            UT_CUT, e->cx, e->cy, 0, will_delete, saved, cur->len
        });
        /* If not continuing a cut sequence, start fresh clipboard */
        if (!e->clip_active) {
            for (int i = 0; i < e->clip_nlines; i++) free(e->clip_lines[i]);
            free(e->clip_lines);
            e->clip_lines  = NULL;
            e->clip_nlines = 0;
        }
        /* Append current line to clipboard */
        char **nl = (char **)realloc(e->clip_lines,
                                     (e->clip_nlines + 1) * sizeof(char *));
        if (nl) {
            e->clip_lines = nl;
            char *lc = (char *)malloc(cur->len + 1);
            if (lc) {
                memcpy(lc, cur->d, cur->len + 1);
                e->clip_lines[e->clip_nlines++] = lc;
            }
        }
        if (will_delete) {
            editor_delete_line(e, e->cy);
            if (e->cy >= e->nlines) e->cy = e->nlines - 1;
        } else {
            cur->d[0] = '\0';
            cur->len  = 0;
        }
        e->cx          = 0;
        e->modified    = true;
        e->clip_active = true;  /* next Ctrl+K will accumulate */
        break;
    }

    case 21: /* Ctrl+U — paste all clipboard lines above cursor */
        if (e->clip_nlines > 0) {
            /* Push one UT_PASTE record per line (LIFO undo removes last first) */
            for (int i = 0; i < e->clip_nlines; i++) {
                undo_push(e, (UndoRecord){
                    UT_PASTE, 0, e->cy + i, 0, 0, NULL, 0
                });
            }
            /* Insert all lines starting at current cursor row */
            for (int i = 0; i < e->clip_nlines; i++) {
                editor_insert_line(e, e->cy + i); /* may realloc e->lines */
                Line *nl = &e->lines[e->cy + i];  /* fresh pointer after realloc */
                int llen = (int)strlen(e->clip_lines[i]);
                line_ensure(nl, llen);
                memcpy(nl->d, e->clip_lines[i], llen + 1);
                nl->len = llen;
            }
            e->cx       = 0;
            e->modified = true;
        }
        break;

    case 1: /* Ctrl+A — start of line */
        e->cx = 0;
        break;

    case 5: /* Ctrl+E — end of line */
        e->cx = e->lines[e->cy].len;
        break;

    case 9: { /* Tab — insert 4 spaces */
        Line *cur = &e->lines[e->cy];
        undo_push(e, (UndoRecord){
            UT_TAB, e->cx, e->cy, 0, 0, NULL, 0
        });
        for (int i = 0; i < 4; i++) {
            line_insert_char(cur, e->cx, ' ');
            e->cx++;
        }
        e->modified = true;
        break;
    }

    case 13: { /* Enter — split line at cursor */
        undo_push(e, (UndoRecord){
            UT_SPLIT, e->cx, e->cy, 0, 0, NULL, 0
        });
        editor_split_line(e, e->cy, e->cx); /* may realloc e->lines */
        e->cy++;
        e->cx       = 0;
        e->modified = true;
        break;
    }

    case 8: { /* Backspace — delete left / join with previous line */
        Line *cur = &e->lines[e->cy];
        if (e->cx > 0) {
            undo_push(e, (UndoRecord){
                UT_DEL_CHAR_BS, e->cx, e->cy,
                cur->d[e->cx - 1], 0, NULL, 0
            });
            line_delete_char(cur, e->cx - 1);
            e->cx--;
            e->modified = true;
        } else if (e->cy > 0) {
            int prev_len = e->lines[e->cy - 1].len;
            undo_push(e, (UndoRecord){
                UT_JOIN_PREV, e->cx, e->cy, 0, prev_len, NULL, 0
            });
            editor_join_lines(e, e->cy - 1);
            e->cy--;
            e->cx       = prev_len;
            e->modified = true;
        }
        break;
    }

    default:
        if (ch >= 32 && ch < 256) {
            Line *cur = &e->lines[e->cy];
            undo_push(e, (UndoRecord){
                UT_INS_CHAR, e->cx, e->cy, 0, 0, NULL, 0
            });
            line_insert_char(cur, e->cx, (char)ch);
            e->cx++;
            e->modified = true;
        }
        break;
    }
    return 1;
}

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        printf("%s\n", VERSION);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: nix [file]\n\n"
               "Nano-style text editor for Winix.\n\n"
               "Key bindings:\n"
               "  Ctrl+S        Save file\n"
               "  Ctrl+Q        Quit (prompts if modified)\n"
               "  Ctrl+X        Save and quit\n"
               "  Ctrl+W        Find text (prompt)\n"
               "  Ctrl+N        Find next (repeat last search)\n"
               "  Ctrl+R        Find and replace\n"
               "  Ctrl+Z        Undo last edit\n"
               "  Ctrl+K        Cut current line (repeat to accumulate multiple lines)\n"
               "  Ctrl+U        Paste clipboard lines above cursor\n"
               "  Ctrl+A        Move to start of line\n"
               "  Ctrl+E        Move to end of line\n"
               "  Tab           Insert 4 spaces\n"
               "  Enter         Split line at cursor\n"
               "  Backspace     Delete left / join with previous line\n"
               "  Delete        Delete right / join with next line\n"
               "  Arrow keys    Move cursor\n"
               "  Home / End    Start / end of line\n"
               "  PgUp / PgDn   Page up / down\n"
               "  ESC           Cancel prompt\n");
        return 0;
    }

    hout = GetStdHandle(STD_OUTPUT_HANDLE);
    enable_ansi();

    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD  orig_in_mode;
    GetConsoleMode(hin, &orig_in_mode);
    SetConsoleMode(hin, 0); /* raw input: disable echo, line buffering */

    Editor e;
    editor_init(&e);

    if (argc >= 2) {
        strncpy(e.filename, argv[1], MAX_PATH_LEN - 1);
        e.filename[MAX_PATH_LEN - 1] = '\0';
        e.lang = detect_lang(e.filename);
        if (!editor_load(&e, argv[1]))
            snprintf(e.msg, sizeof(e.msg), "New file: %s", argv[1]);
    }

    printf("\033[2J"); /* clear screen */
    fflush(stdout);

    for (;;) {
        scroll_view(&e);
        draw(&e);
        int ch = _getch();
        if (!handle_key(&e, ch)) break;
    }

    /* Restore console state */
    SetConsoleMode(hin, orig_in_mode);
    printf("\033[2J");
    move_cursor(0, 0);
    show_cursor();
    fflush(stdout);

    editor_free(&e);
    return 0;
}
