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

    const char *arg = argv[1];
    char *end;
    double val = strtod(arg, &end);
    if (end == arg || val < 0) {
        fprintf(stderr, "sleep: invalid time interval '%s'\n", arg);
        return 1;
    }
    double seconds = val;
    if      (*end == 's' || *end == '\0') seconds = val;
    else if (*end == 'm') seconds = val * 60;
    else if (*end == 'h') seconds = val * 3600;
    else if (*end == 'd') seconds = val * 86400;
    else {
        fprintf(stderr, "sleep: invalid time interval '%s'\n", arg);
        return 1;
    }

#ifdef _WIN32
    Sleep((DWORD)(seconds * 1000));
#else
    unsigned long secs  = (unsigned long)seconds;
    unsigned long usecs = (unsigned long)((seconds - secs) * 1e6);
    sleep(secs);
    if (usecs) usleep(usecs);
#endif
    return 0;
}
