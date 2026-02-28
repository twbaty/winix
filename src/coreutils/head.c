#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void head_stream(FILE *f, int n) {
    char line[4096];
    int count = 0;
    while (count < n && fgets(line, sizeof(line), f))
        fputs(line, stdout), count++;
}

int main(int argc, char *argv[]) {
    int n = 10;
    int argi = 1;

    if (argi < argc && argv[argi][0] == '-' && argv[argi][1] == 'n') {
        // Accept -n N or -nN
        if (argv[argi][2] != '\0') {
            n = atoi(argv[argi] + 2);
            argi++;
        } else if (argi + 1 < argc) {
            n = atoi(argv[++argi]);
            argi++;
        } else {
            fprintf(stderr, "head: option -n requires an argument\n");
            return 1;
        }
        if (n <= 0) { fprintf(stderr, "head: invalid line count\n"); return 1; }
    }

    // No file args — read stdin
    if (argi >= argc) {
        head_stream(stdin, n);
        return 0;
    }

    int multiple = (argc - argi) > 1;
    for (int i = argi; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) { perror(argv[i]); continue; }
        if (multiple) printf("==> %s <==\n", argv[i]);
        head_stream(f, n);
        if (multiple && i < argc - 1) putchar('\n');
        fclose(f);
    }
    return 0;
}
