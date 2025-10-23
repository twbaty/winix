#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: grep <pattern> [file...]\n");
        return 2;
    }

    const char* pattern = argv[1];
    char line[4096];

    // No file arguments → read from stdin
    if (argc == 2) {
        while (fgets(line, sizeof(line), stdin)) {
            if (strstr(line, pattern))
                fputs(line, stdout);
        }
        return 0;
    }

    // Loop through files
    for (int i = 2; i < argc; ++i) {
        FILE* f = strcmp(argv[i], "-") == 0 ? stdin : fopen(argv[i], "r");
        if (!f) {
            fprintf(stderr, "grep: cannot open %s\n", argv[i]);
            continue;
        }
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, pattern))
                fputs(line, stdout);
        }
        if (f != stdin) fclose(f);
    }
    return 0;
}
