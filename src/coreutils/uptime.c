#include <stdio.h>
#include <windows.h>
#include <time.h>

int main(void) {
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
