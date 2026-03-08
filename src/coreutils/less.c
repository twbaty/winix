/*
 * less — pager with ANSI colour support
 *
 * Usage: less [-R] [-S] [FILE]
 *   -R   pass raw ANSI colour codes through (default; flag accepted for compat)
 *   -S   chop long lines instead of wrapping (default behaviour here)
 *   -N   show line numbers (not yet implemented)
 *   q    quit
 *   space/PgDn  page down
 *   b/PgUp      page up
 *   arrows      scroll one line
 *   /           search
 *   n           next match
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <windows.h>

#define LINE_BUF 8192

typedef struct {
    char **data;
    size_t size;
    size_t cap;
} Lines;

static void lines_init(Lines *ls){ ls->data=NULL; ls->size=0; ls->cap=0; }
static void lines_push(Lines *ls, const char *s){
    if(ls->size==ls->cap){
        size_t n = ls->cap? ls->cap*2:1024;
        char **nd = (char**)realloc(ls->data, n*sizeof(char*));
        if(!nd){fprintf(stderr,"less: OOM\n"); exit(1);}
        ls->data=nd; ls->cap=n;
    }
    size_t len = strlen(s);
    char *cpy = (char*)malloc(len+1);
    if(!cpy){fprintf(stderr,"less: OOM\n"); exit(1);}
    memcpy(cpy, s, len+1);
    ls->data[ls->size++] = cpy;
}
static void lines_free(Lines *ls){
    for(size_t i=0;i<ls->size;i++) free(ls->data[i]);
    free(ls->data);
}

static int term_rows(void){
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 25;
}
static int term_cols(void){
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return 80;
}

/* Count visible (non-ANSI-escape) characters in a string. */
static int visible_len(const char *s){
    int n = 0;
    while(*s){
        if(*s == '\033' && *(s+1) == '['){
            /* skip ESC [ ... final-byte (letter) */
            s += 2;
            while(*s && !(*s >= '@' && *s <= '~')) s++;
            if(*s) s++;
        } else if(*s == '\r' || *s == '\n'){
            break;
        } else {
            n++; s++;
        }
    }
    return n;
}

/*
 * Write at most `max_cols` visible characters from `s`, passing ANSI escape
 * sequences through intact (they count as 0 visible columns).
 * Returns after writing max_cols visible chars or end of line.
 */
static void write_cols(const char *s, int max_cols){
    int vis = 0;
    while(*s && *s != '\n' && *s != '\r'){
        if(*s == '\033' && *(s+1) == '['){
            /* pass the whole escape sequence through */
            const char *start = s;
            s += 2;
            while(*s && !(*s >= '@' && *s <= '~')) s++;
            if(*s) s++;
            fwrite(start, 1, (size_t)(s - start), stdout);
        } else {
            if(vis >= max_cols) break;
            putchar((unsigned char)*s++);
            vis++;
        }
    }
    /* reset colours before newline so status bar isn't tinted */
    fputs("\033[0m\n", stdout);
}

static void status_line(const char *msg){
    printf("\033[7m%s\033[0m", msg);
    fflush(stdout);
}
static void clear_line(void){ printf("\r\033[K"); fflush(stdout); }

static int ci_strstr(const char* hay, const char* needle){
    size_t nlen = strlen(needle);
    if(!nlen) return 1;
    for(const char* p=hay; *p; ++p){
        size_t i=0;
        while(i<nlen && p[i]){
            char a = p[i], b = needle[i];
            if(a>='A' && a<='Z') a = (char)(a-'A'+'a');
            if(b>='A' && b<='Z') b = (char)(b-'A'+'a');
            if(a!=b) break;
            i++;
        }
        if(i==nlen) return 1;
    }
    return 0;
}

int main(int argc, char *argv[]){
    FILE *in = stdin;
    int argi = 1;

    /* parse flags */
    for(; argi < argc && argv[argi][0] == '-'; argi++){
        const char *a = argv[argi];
        if(!strcmp(a, "--")) { argi++; break; }
        /* -R accepted for compatibility — ANSI is always passed through */
        /* -S, -N accepted and silently ignored (default behaviour) */
        for(const char *p = a+1; *p; p++){
            if(*p == 'R' || *p == 'S' || *p == 'N') continue;
            fprintf(stderr, "less: invalid option -- '%c'\n", *p);
            return 1;
        }
    }

    if(argi < argc){
        in = fopen(argv[argi], "rb");
        if(!in){ fprintf(stderr,"less: cannot open %s\n", argv[argi]); return 1; }
    }

    /* Enable ANSI virtual terminal processing on Windows 10+ */
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hout, &mode);
    SetConsoleMode(hout, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    Lines ls; lines_init(&ls);
    char buf[LINE_BUF];
    while(fgets(buf, sizeof(buf), in)){
        lines_push(&ls, buf);
    }
    if(in!=stdin) fclose(in);

    int rows = term_rows();
    int cols = term_cols();
    int page_rows = rows > 1 ? rows - 1 : rows;

    size_t top = 0;
    char last_search[256] = {0};
    int search_ci = 1;

    for(;;){
        /* draw page */
        for(int r=0; r<page_rows; ++r){
            size_t idx = top + r;
            if(idx >= ls.size){ printf("\n"); continue; }
            int vlen = visible_len(ls.data[idx]);
            if(vlen > cols){
                write_cols(ls.data[idx], cols - 1);
            } else {
                fputs(ls.data[idx], stdout);
                /* ensure newline */
                size_t slen = strlen(ls.data[idx]);
                if(slen == 0 || (ls.data[idx][slen-1] != '\n'))
                    putchar('\n');
            }
        }

        /* status bar */
        char bar[256];
        int pct = ls.size ? (int)((top*100)/(ls.size>=(size_t)page_rows? ls.size-page_rows+1:1)) : 100;
        if(pct > 100) pct = 100;
        snprintf(bar, sizeof(bar),
            "--Less-- (%zu/%zu) %d%%  q=quit  Space/b=page  arrows=scroll  /=search  n=next",
            top+1, ls.size, pct);
        status_line(bar);

        int ch = _getch();
        clear_line();

        if(ch==3 || ch=='q' || ch=='Q') break;

        if(ch==' '){
            if(top + page_rows < ls.size) top += page_rows;
        } else if(ch=='b' || ch=='B'){
            if((int)top - page_rows >= 0) top -= page_rows; else top = 0;
        } else if(ch==224 || ch==0){
            int k = _getch();
            if(k==80){ if(top+1 < ls.size) top++; }
            else if(k==72){ if(top>0) top--; }
            else if(k==81){ if(top+page_rows < ls.size) top+=page_rows; }
            else if(k==73){ if((int)top-page_rows>=0) top-=page_rows; else top=0; }
            else if(k==79){ if(ls.size>(size_t)page_rows) top=ls.size-page_rows; }
            else if(k==71){ top=0; }
        } else if(ch=='/'){
            clear_line(); printf("/");
            char pat[256]={0}; size_t pi=0;
            for(;;){
                int c=_getch();
                if(c=='\r') break;
                if(c==27){ pat[0]=0; break; }
                if(c==8){ if(pi){ pi--; pat[pi]=0; printf("\b \b"); } continue; }
                if(pi<sizeof(pat)-1){ pat[pi++]=(char)c; pat[pi]=0; putchar((char)c); }
            }
            if(pat[0]) strncpy(last_search, pat, sizeof(last_search)-1);
            if(last_search[0]){
                size_t i=top+1; int found=0;
                for(;i<ls.size;++i){
                    if(search_ci?ci_strstr(ls.data[i],last_search):(strstr(ls.data[i],last_search)!=NULL))
                        { found=1; break; }
                }
                if(found) top=(i>0)?i-1:0;
                else{ status_line("--pattern not found--"); Sleep(600); clear_line(); }
            }
        } else if(ch=='n' || ch=='N'){
            if(last_search[0]){
                size_t i=top+1; int found=0;
                for(;i<ls.size;++i){
                    if(search_ci?ci_strstr(ls.data[i],last_search):(strstr(ls.data[i],last_search)!=NULL))
                        { found=1; break; }
                }
                if(found) top=(i>0)?i-1:0;
                else{ status_line("--no next match--"); Sleep(600); clear_line(); }
            } else { status_line("--no previous search--"); Sleep(600); clear_line(); }
        }

        /* clamp */
        if(ls.size<=(size_t)page_rows) top=0;
        else if(top>ls.size-page_rows) top=ls.size-page_rows;
    }

    /* reset terminal colours on exit */
    fputs("\033[0m", stdout);
    lines_free(&ls);
    return 0;
}
