#include <stdio.h>
#include <windows.h>

int main(void) {
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
