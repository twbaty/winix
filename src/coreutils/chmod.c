#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: chmod <mode> <file>\n");
        return 1;
    }

    const char *mode = argv[1];
    const char *file = argv[2];

#ifdef _WIN32
    // Stub implementation for Windows
    printf("chmod: changing mode of '%s' to '%s' (stubbed — no effect on Windows)\n",
           file, mode);
    return 0;
#else
    // POSIX behavior (for future portability)
    mode_t m = strtol(mode, NULL, 8);
    if (chmod(file, m) != 0) {
        perror("chmod");
        return 1;
    }
    return 0;
#endif
}
