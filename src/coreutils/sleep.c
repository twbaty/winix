#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: sleep NUMBER[SUFFIX]");
        puts("Pause for NUMBER seconds.");
        puts("");
        puts("  SUFFIX may be s (seconds, default), m (minutes), h (hours), or d (days).");
        puts("");
        puts("      --help     display this help and exit");
        puts("      --version  output version information and exit");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("sleep 1.0 (Winix)");
        return 0;
    }

    if (argc < 2) {
        fprintf(stderr, "sleep: missing operand\n");
        fprintf(stderr, "Try 'sleep --help' for more information.\n");
        return 1;
    }

    int sec = atoi(argv[1]);
    if (sec <= 0) {
        fprintf(stderr, "sleep: invalid duration\n");
        return 1;
    }

#ifdef _WIN32
    Sleep(sec * 1000);
#else
    sleep(sec);
#endif
    return 0;
}
