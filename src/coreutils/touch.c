#include <stdio.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: touch <file>\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "ab+");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fclose(f);
    return 0;
}
