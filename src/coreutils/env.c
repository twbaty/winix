#include <stdio.h>
#include <stdlib.h>

int main(void) {
#ifdef _WIN32
    extern char **__p__environ;   // MinGW’s official CRT symbol
    char **p = __p__environ;
#else
    extern char **environ;        // POSIX systems
    char **p = environ;
#endif

    for (; *p; ++p)
        puts(*p);

    return 0;
}
