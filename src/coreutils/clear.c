#include <stdio.h>
#include <string.h>
#include <windows.h>

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: clear [OPTION]...");
        puts("Clear the terminal screen.");
        puts("");
        puts("  -h, --help     display this help and exit");
        puts("      --version  output version information and exit");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("clear 1.0 (Winix)");
        return 0;
    }
    (void)argc; (void)argv;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (!GetConsoleScreenBufferInfo(h, &csbi)) {
        /* Not a console (e.g. redirected) — fall back to ANSI */
        printf("\033[2J\033[H");
        fflush(stdout);
        return 0;
    }

    DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
    COORD origin = {0, 0};
    DWORD written;

    FillConsoleOutputCharacterA(h, ' ', cells, origin, &written);
    FillConsoleOutputAttribute(h, csbi.wAttributes, cells, origin, &written);
    SetConsoleCursorPosition(h, origin);

    return 0;
}
