#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: rmdir <directory>\n");
        return 1;
    }

    if (rmdir(argv[1]) != 0)
        perror(argv[1]);

    return 0;
}
