#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: cat <file> [file...]\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) {
            perror(argv[i]);
            continue;
        }

        int ch;
        while ((ch = fgetc(f)) != EOF)
            putchar(ch);

        fclose(f);
    }
    return 0;
}
