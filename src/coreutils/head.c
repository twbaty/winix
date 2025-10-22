#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: head <file>\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }

    char line[1024];
    int count = 0;
    while (fgets(line, sizeof(line), f) && count < 10) {
        fputs(line, stdout);
        count++;
    }
    fclose(f);
    return 0;
}
