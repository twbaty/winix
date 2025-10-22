#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: sleep <seconds>\n");
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
