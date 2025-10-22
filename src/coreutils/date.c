#include <stdio.h>
#include <time.h>

int main(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm) {
        perror("date");
        return 1;
    }

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    printf("%s\n", buf);
    return 0;
}
