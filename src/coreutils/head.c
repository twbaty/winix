#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void head_stream(FILE *f, int n) {
    char line[4096];
    int count = 0;
    while (count < n && fgets(line, sizeof(line), f))
        fputs(line, stdout), count++;
}

int main(int argc, char *argv[]) {
    int n = 10;
    int argi = 1;

    /* Handle --help / --version before other option parsing */
    if (argi < argc && (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0)) {
        puts("Usage: head [OPTION]... [FILE]...");
        puts("Print the first 10 lines of each FILE to standard output.");
        puts("With no FILE, or when FILE is -, read standard input.");
        puts("");
        puts("  -n, --lines=N  print the first N lines instead of the first 10");
        puts("  -h, --help     display this help and exit");
        puts("      --version  output version information and exit");
        return 0;
    }
    if (argi < argc && strcmp(argv[argi], "--version") == 0) {
        puts("head 1.0 (Winix)");
        return 0;
    }

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

    int ret = 0;
    int multiple = (argc - argi) > 1;
    for (int i = argi; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) { fprintf(stderr, "head: %s: %s\n", argv[i], strerror(errno)); ret = 1; continue; }
        if (multiple) printf("==> %s <==\n", argv[i]);
        head_stream(f, n);
        if (multiple && i < argc - 1) putchar('\n');
        fclose(f);
    }
    return ret;
}
