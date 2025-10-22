#include <stdio.h>
#include <stdlib.h>

int main(void) {
#ifdef _WIN32
    char **environ = *_environ;   // Use function-style deref
#else
    extern char **environ;
#endif

    for (char **p = environ; *p; ++p)
        printf("%s\n", *p);

    return 0;
}
