#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: rm <file> [file...]\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (remove(argv[i]) != 0)
            perror(argv[i]);
    }
    return 0;
}
