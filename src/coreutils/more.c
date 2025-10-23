#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <windows.h>

#define LINE_BUF 4096

static int get_terminal_height(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 25; // default
}

static void show_prompt(void) {
    printf("\033[7m--More--\033[0m"); // inverse video for prompt
    fflush(stdout);
}

static void clear_prompt(void) {
    printf("\r\033[K"); // clear line
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    FILE *in = stdin;
    if (argc > 1) {
        in = fopen(argv[1], "r");
        if (!in) {
            fprintf(stderr, "more: cannot open %s\n", argv[1]);
            return 1;
        }
    }

    char line[LINE_BUF];
    int term_height = get_terminal_height();
    int lines_shown = 0;
    int done = 0;

    while (!done && fgets(line, sizeof(line), in)) {
        fputs(line, stdout);
        lines_shown++;

        if (lines_shown >= term_height - 1) {
            show_prompt();

            int ch = _getch();
            clear_prompt();

            if (ch == 'q' || ch == 'Q') break;
            else if (ch == ' ') lines_shown = 0;
            else if (ch == '\r' || ch == '\n') lines_shown = term_height - 2;
            else lines_shown = 0;
        }
    }

    if (in != stdin) fclose(in);
    return 0;
}
