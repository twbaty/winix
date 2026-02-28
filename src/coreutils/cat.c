#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

static bool number_lines = false;

static void cat_stream(FILE *f, int *line_num) {
    int ch, prev = '\n';
    while ((ch = fgetc(f)) != EOF) {
        if (number_lines && prev == '\n')
            printf("%6d\t", (*line_num)++);
        putchar(ch);
        prev = ch;
    }
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0'; argi++) {
        for (char *p = argv[argi] + 1; *p; p++) {
            if (*p == 'n') number_lines = true;
            else {
                fprintf(stderr, "cat: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
    }

    int line_num = 1;

    if (argi >= argc) {
        cat_stream(stdin, &line_num);
        return 0;
    }

    int ret = 0;
    for (int i = argi; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '\0') {
            cat_stream(stdin, &line_num);
            continue;
        }
        FILE *f = fopen(argv[i], "r");
        if (!f) { fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno)); ret = 1; continue; }
        cat_stream(f, &line_num);
        fclose(f);
    }
    return ret;
}
