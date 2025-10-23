#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <windows.h>

#define LINE_BUF 4096

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

static void status_line(const char *msg){
    // inverse video
    printf("\033[7m%s\033[0m", msg);
    fflush(stdout);
}
static void clear_line(void){ printf("\r\033[K"); fflush(stdout); }

static int ci_strstr(const char* hay, const char* needle){
    // case-insensitive substring search; returns 1 if found
    size_t nlen = strlen(needle);
    if(!nlen) return 1;
    for(const char* p=hay; *p; ++p){
        size_t i=0;
        while(p[i] && i<nlen){
            char a = p[i], b = needle[i];
            if(a>='A' && a<='Z') a = (char)(a - 'A' + 'a');
            if(b>='A' && b<='Z') b = (char)(b - 'A' + 'a');
            if(a!=b) break;
            i++;
        }
        if(i==nlen) return 1;
    }
    return 0;
}

int main(int argc, char *argv[]){
    FILE *in = stdin;
    if(argc > 1){
        in = fopen(argv[1], "rb");
        if(!in){ fprintf(stderr,"less: cannot open %s\n", argv[1]); return 1; }
    }

    // read all lines (simple but effective; we can stream later if needed)
    Lines ls; lines_init(&ls);
    char buf[LINE_BUF];
    while(fgets(buf, sizeof(buf), in)){
        lines_push(&ls, buf);
    }
    if(in!=stdin) fclose(in);

    int rows = term_rows();
    int cols = term_cols();
    int page_rows = rows > 1 ? rows - 1 : rows;

    size_t top = 0;                 // index of first visible line
    char last_search[256] = {0};    // remember last /pattern
    int search_ci = 1;              // case-insensitive search by default

    for(;;){
        // draw page
        system(""); // enable ANSI on Windows 10+ consoles
        for(int r=0; r<page_rows; ++r){
            size_t idx = top + r;
            if(idx >= ls.size) { printf("\n"); continue; }
            // truncate soft to terminal width
            if((int)strlen(ls.data[idx]) > cols){
                fwrite(ls.data[idx], 1, cols-1, stdout);
                putchar('\n');
            }else{
                fputs(ls.data[idx], stdout);
                // ensure newline
                if(ls.data[idx][strlen(ls.data[idx])-1] != '\n') putchar('\n');
            }
        }
        // status
        char bar[256];
        int pct = ls.size ? (int)((top*100)/ (ls.size>page_rows? (ls.size-1):ls.size)) : 100;
        snprintf(bar, sizeof(bar),
                 "--Less-- (%zu/%zu) %d%%  (q=quit, space/pgdn, b/pgup, arrows, /=search, n=next)",
                 top+1, ls.size, pct);
        status_line(bar);

        int ch = _getch();
        clear_line();

        // CTRL+C/CTRL+Q quick exit
        if(ch == 3 /*Ctrl-C*/ || ch=='q' || ch=='Q'){ break; }

        if(ch == ' ' || ch == 0x22 /*Shift+Space not reliable*/){
            if(top + page_rows < ls.size) top += page_rows; // page down
        } else if(ch == 'b' || ch == 'B'){
            if((int)top - page_rows >= 0) top -= page_rows; else top = 0; // page up
        } else if(ch == 224 || ch == 0){ // arrows
            int k = _getch();
            if(k == 80){ // down
                if(top + 1 < ls.size) top += 1;
            } else if(k == 72){ // up
                if(top > 0) top -= 1;
            } else if(k == 81){ // PgDn
                if(top + page_rows < ls.size) top += page_rows;
            } else if(k == 73){ // PgUp
                if((int)top - page_rows >= 0) top -= page_rows; else top = 0;
            } else if(k == 79){ // End
                if(ls.size > (size_t)page_rows) top = ls.size - page_rows;
            } else if(k == 71){ // Home
                top = 0;
            }
        } else if(ch == '/'){
            // prompt for search
            clear_line();
            status_line("/"); clear_line();
            printf("/");
            char pat[256]={0}; size_t pi=0;
            for(;;){
                int c = _getch();
                if(c == '\r'){ break; }
                if(c == 27){ pat[0]=0; break; } // ESC cancels
                if(c == 8){ if(pi){ pi--; pat[pi]=0; printf("\b \b"); } continue; }
                if(pi < sizeof(pat)-1){ pat[pi++]=(char)c; pat[pi]=0; putchar((char)c); }
            }
            // store if any
            if(pat[0]) strncpy(last_search, pat, sizeof(last_search)-1);

            // find from next line down
            if(last_search[0]){
                size_t i = top+1;
                int found = 0;
                for(; i<ls.size; ++i){
                    if(search_ci ? ci_strstr(ls.data[i], last_search)
                                 : (strstr(ls.data[i], last_search)!=NULL)){
                        found = 1; break;
                    }
                }
                if(found){
                    // show match line on top+one for context
                    top = (i>0)? i-1 : 0;
                } else {
                    status_line("--pattern not found--"); Sleep(600); clear_line();
                }
            }
        } else if(ch == 'n' || ch == 'N'){
            if(last_search[0]){
                size_t i = top+1;
                int found = 0;
                for(; i<ls.size; ++i){
                    if(search_ci ? ci_strstr(ls.data[i], last_search)
                                 : (strstr(ls.data[i], last_search)!=NULL)){
                        found = 1; break;
                    }
                }
                if(found){
                    top = (i>0)? i-1 : 0;
                } else {
                    status_line("--no next match--"); Sleep(600); clear_line();
                }
            } else {
                status_line("--no previous search--"); Sleep(600); clear_line();
            }
        }
        // clamp
        if(ls.size <= (size_t)page_rows) top = 0;
        else if(top > ls.size - page_rows) top = ls.size - page_rows;
    }

    lines_free(&ls);
    return 0;
}
