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
 *   Ctrl+W  23  Find
 *   Ctrl+K  11  Cut line
 *   Ctrl+U  21  Paste line above
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

#define VERSION      "nix 1.0 (Winix 1.0)"
#define MAX_PATH_LEN MAX_PATH
#define LINE_CAP_INIT 64

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
    char *clip;              /* single-line clipboard (Ctrl+K / Ctrl+U) */
    int   cliplen;
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

/* Ensure e->lcap >= need (total line slots). */
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

static void editor_init(Editor *e) {
    e->lcap  = 64;
    e->lines = (Line *)malloc(e->lcap * sizeof(Line));
    if (!e->lines) { fprintf(stderr, "nix: OOM\n"); exit(1); }
    e->nlines = 1;
    line_init(&e->lines[0]);
    e->cx = e->cy = 0;
    e->top_row = e->left_col = 0;
    e->modified   = false;
    e->filename[0] = '\0';
    e->msg[0]      = '\0';
    e->clip        = NULL;
    e->cliplen     = 0;
}

static void editor_free(Editor *e) {
    for (int i = 0; i < e->nlines; i++) line_free(&e->lines[i]);
    free(e->lines);
    free(e->clip);
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

static void scroll_view(Editor *e) {
    int rows    = term_rows();
    int cols    = term_cols();
    int content = rows - 2;
    if (content < 1) content = 1;

    if (e->cy < e->top_row)
        e->top_row = e->cy;
    if (e->cy >= e->top_row + content)
        e->top_row = e->cy - content + 1;

    if (e->cx < e->left_col)
        e->left_col = e->cx;
    if (e->cx >= e->left_col + cols)
        e->left_col = e->cx - cols + 1;
    if (e->left_col < 0)
        e->left_col = 0;
}

/* ── Prompt helper ──────────────────────────────────────────────────────── */

/*
 * Show 'prompt' in the status bar and accept a single-line response.
 * ESC cancels (buf[0] = '\0').  Enter confirms.
 */
static void prompt_in_status(const char *prompt, char *buf, int bufsz) {
    int rows = term_rows();
    int cols = term_cols();
    int pos  = 0;
    buf[0]   = '\0';

    for (;;) {
        /* Redraw the status bar with prompt + current input */
        move_cursor(0, rows - 1);
        char line[512];
        snprintf(line, sizeof(line), "  %s%s", prompt, buf);
        int llen = (int)strlen(line);
        printf("\033[7m%s", line);
        for (int i = llen; i < cols; i++) putchar(' ');
        printf("\033[0m");

        /* Place cursor after typed text */
        move_cursor(2 + (int)strlen(prompt) + pos, rows - 1);
        fflush(stdout);

        int c = _getch();
        if (c == 13) break;                              /* Enter */
        if (c == 27) { buf[0] = '\0'; break; }          /* ESC — cancel */
        if (c == 8 && pos > 0) { buf[--pos] = '\0'; }   /* Backspace */
        else if (c >= 32 && c < 127 && pos < bufsz - 1) {
            buf[pos++] = (char)c;
            buf[pos]   = '\0';
        }
    }
}

/* ── Rendering ──────────────────────────────────────────────────────────── */

static void draw(Editor *e) {
    int rows    = term_rows();
    int cols    = term_cols();
    int content = rows - 2;
    if (content < 1) content = 1;

    hide_cursor();

    /* ── Title bar (row 0) ── */
    move_cursor(0, 0);
    const char *fname = e->filename[0] ? e->filename : "[No Name]";
    /* Extract basename for display */
    const char *base = fname;
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
    for (int r = 0; r < content; r++) {
        move_cursor(0, r + 1);
        int li = e->top_row + r;
        if (li < e->nlines) {
            Line *ln    = &e->lines[li];
            int   start = e->left_col < ln->len ? e->left_col : ln->len;
            int   vis   = ln->len - start;
            if (vis > cols) vis = cols;
            if (vis > 0) fwrite(ln->d + start, 1, vis, stdout);
        }
        printf("\033[K"); /* clear to end of line */
    }

    /* ── Status bar (row rows-1) ── */
    move_cursor(0, rows - 1);
    char status[512];
    if (e->msg[0]) {
        snprintf(status, sizeof(status), "  %s", e->msg);
        e->msg[0] = '\0';
    } else {
        const char *hints =
            "  ^S:Save  ^Q:Quit  ^X:Save+Quit  ^W:Find  ^K:Cut  ^U:Paste";
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
    move_cursor(e->cx - e->left_col, e->cy - e->top_row + 1);
    show_cursor();
    fflush(stdout);
}

/* ── Find ───────────────────────────────────────────────────────────────── */

static void editor_find(Editor *e) {
    static char last_pat[256];
    char pat[256];
    prompt_in_status("Find: ", pat, sizeof(pat));
    if (pat[0]) strncpy(last_pat, pat, sizeof(last_pat) - 1);
    if (!last_pat[0]) return;

    /* Search forward from cy+1, wrapping around */
    for (int i = 1; i <= e->nlines; i++) {
        int li    = (e->cy + i) % e->nlines;
        char *hit = strstr(e->lines[li].d, last_pat);
        if (hit) {
            e->cy = li;
            e->cx = (int)(hit - e->lines[li].d);
            snprintf(e->msg, sizeof(e->msg), "Found '%s'", last_pat);
            return;
        }
    }
    snprintf(e->msg, sizeof(e->msg), "Not found: %s", last_pat);
}

/* ── Key handler ────────────────────────────────────────────────────────── */

/* Returns 1 to continue editing, 0 to quit. */
static int handle_key(Editor *e, int ch) {
    int rows    = term_rows();
    int content = rows - 2;
    if (content < 1) content = 1;

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
                line_delete_char(cur, e->cx);
                e->modified = true;
            } else if (e->cy < e->nlines - 1) {
                /* Join with next line */
                Line *next = &e->lines[e->cy + 1];
                line_ensure(cur, cur->len + next->len);
                memcpy(cur->d + cur->len, next->d, next->len + 1);
                cur->len += next->len;
                editor_delete_line(e, e->cy + 1);
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
            /* 'n' or any other key — quit without saving */
        }
        return 0;

    case 24: /* Ctrl+X — save + quit */
        if (e->modified && e->filename[0]) editor_save(e);
        return 0;

    case 23: /* Ctrl+W — find */
        editor_find(e);
        break;

    case 11: { /* Ctrl+K — cut line */
        Line *cur = &e->lines[e->cy];
        free(e->clip);
        e->clip = (char *)malloc(cur->len + 1);
        if (e->clip) {
            memcpy(e->clip, cur->d, cur->len + 1);
            e->cliplen = cur->len;
        }
        if (e->nlines > 1) {
            editor_delete_line(e, e->cy);
            if (e->cy >= e->nlines) e->cy = e->nlines - 1;
        } else {
            cur->d[0] = '\0';
            cur->len  = 0;
        }
        e->cx       = 0;
        e->modified = true;
        break;
    }

    case 21: /* Ctrl+U — paste line above cursor */
        if (e->clip) {
            editor_insert_line(e, e->cy); /* may realloc e->lines */
            Line *nl = &e->lines[e->cy];  /* fresh pointer */
            line_ensure(nl, e->cliplen);
            memcpy(nl->d, e->clip, e->cliplen + 1);
            nl->len     = e->cliplen;
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
        for (int i = 0; i < 4; i++) {
            line_insert_char(cur, e->cx, ' ');
            e->cx++;
        }
        e->modified = true;
        break;
    }

    case 13: { /* Enter — split line at cursor */
        editor_insert_line(e, e->cy + 1); /* may realloc e->lines */
        Line *cur  = &e->lines[e->cy];    /* re-fetch after possible realloc */
        Line *next = &e->lines[e->cy + 1];
        int   tail = cur->len - e->cx;
        if (tail > 0) {
            line_ensure(next, tail);
            memcpy(next->d, cur->d + e->cx, tail + 1);
            next->len     = tail;
            cur->d[e->cx] = '\0';
            cur->len      = e->cx;
        }
        e->cy++;
        e->cx       = 0;
        e->modified = true;
        break;
    }

    case 8: { /* Backspace — delete left / join with previous line */
        Line *cur = &e->lines[e->cy];
        if (e->cx > 0) {
            line_delete_char(cur, e->cx - 1);
            e->cx--;
            e->modified = true;
        } else if (e->cy > 0) {
            Line *prev = &e->lines[e->cy - 1];
            int   plen = prev->len;
            line_ensure(prev, prev->len + cur->len);
            memcpy(prev->d + prev->len, cur->d, cur->len + 1);
            prev->len += cur->len;
            editor_delete_line(e, e->cy);
            e->cy--;
            e->cx       = plen;
            e->modified = true;
        }
        break;
    }

    default:
        if (ch >= 32 && ch < 256) {
            Line *cur = &e->lines[e->cy];
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
               "  Ctrl+W        Find text\n"
               "  Ctrl+K        Cut current line to clipboard\n"
               "  Ctrl+U        Paste clipboard line above cursor\n"
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
