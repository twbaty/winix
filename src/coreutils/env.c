#include <stdio.h>
#include <stdlib.h>

int main(void) {
#ifdef _WIN32
    char **environ = *_p__environ();   // MS runtime accessor
#else
    extern char **environ;
#endif

    for (char **p = environ; *p; ++p)
        puts(*p);

    return 0;
}
