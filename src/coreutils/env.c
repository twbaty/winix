#include <stdio.h>
#include <stdlib.h>

int main(void) {
#ifdef _WIN32
    extern char **_environ;   // MinGW runtime symbol
    char **p = _environ;
#else
    extern char **environ;    // POSIX symbol
    char **p = environ;
#endif

    for (; *p; ++p)
        puts(*p);

    return 0;
}
