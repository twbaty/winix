#include <stdio.h>
#include <stdlib.h>

int main(void) {
#ifdef _WIN32
    extern char **_environ;   // Windows-specific name
    char **environ = _environ;
#else
    extern char **environ;    // POSIX
#endif

    for (char **p = environ; *p; ++p)
        printf("%s\n", *p);

    return 0;
}
