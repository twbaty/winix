#include <stdio.h>
#include <stdlib.h>

int main(void) {
#ifdef _WIN32
    extern char **_environ;   /* Provided by the MS runtime */
    char **p = _environ;
#else
    extern char **environ;
    char **p = environ;
#endif

    for (; *p; ++p)
        puts(*p);

    return 0;
}
