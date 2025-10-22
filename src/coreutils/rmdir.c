#include <stdio.h>

#ifdef _WIN32
#include <direct.h>  // for _rmdir
#else
#include <unistd.h>  // for rmdir
#endif

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: rmdir <directory>\n");
        return 1;
    }

#ifdef _WIN32
    int result = _rmdir(argv[1]);
#else
    int result = rmdir(argv[1]);
#endif

    if (result != 0)
        perror(argv[1]);

    return 0;
}
