#include <stdio.h>

#ifdef _WIN32
#include <direct.h>  // for _mkdir
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mkdir <directory>\n");
        return 1;
    }

#ifdef _WIN32
    int result = _mkdir(argv[1]);
#else
    int result = mkdir(argv[1], 0755);
#endif

    if (result != 0)
        perror(argv[1]);

    return 0;
}
