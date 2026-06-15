#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: uptime [OPTION]...");
        puts("Tell how long the system has been running.");
        puts("");
        puts("  -h, --help     display this help and exit");
        puts("      --version  output version information and exit");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("uptime 1.0 (Winix)");
        return 0;
    }
    (void)argc; (void)argv;
    /* GetTickCount64 avoids the 49-day overflow of GetTickCount */
    ULONGLONG ms = GetTickCount64();

    ULONGLONG total_secs = ms / 1000;
    ULONGLONG days  = total_secs / 86400;
    ULONGLONG hours = (total_secs % 86400) / 3600;
    ULONGLONG mins  = (total_secs % 3600) / 60;
    ULONGLONG secs  = total_secs % 60;

    /* Current time */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timebuf[16];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", t);

    if (days > 0)
        printf(" %s  up %llu day%s, %02llu:%02llu:%02llu\n",
               timebuf, days, days == 1 ? "" : "s", hours, mins, secs);
    else
        printf(" %s  up %02llu:%02llu:%02llu\n",
               timebuf, hours, mins, secs);

    return 0;
}
