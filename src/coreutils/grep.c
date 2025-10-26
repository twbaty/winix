#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <stdbool.h>

// Simple Winix grep: supports stdin or files, no regex (just substring match)

static void grep_stream(FILE *fp, const char *pattern, const char *filename, bool show_filename) {
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, pattern)) {
            if (show_filename && filename)
                printf("%s:", filename);
            fputs(line, stdout);
            if (line[strlen(line) - 1] != '\n')
                putchar('\n');
        }
    }
}

int main(int argc, char *argv[]) {
    // Detect piped input (stdin redirected)
    bool stdin_is_pipe = !_isatty(_fileno(stdin));

    if (argc < 2 && !stdin_is_pipe) {
        fprintf(stderr, "Usage: grep [--color=auto|always|never] <pattern> [file...]\n");
        return 1;
    }

    const char *pattern = (argc >= 2) ? argv[1] : NULL;
    if (!pattern && !stdin_is_pipe) {
        fprintf(stderr, "grep: missing search pattern\n");
        return 1;
    }

    // Case 1: piped data to stdin, e.g.  cat file | grep foo
    if (stdin_is_pipe) {
        grep_stream(stdin, pattern, NULL, false);
        return 0;
    }

    // Case 2: explicit files
    if (argc < 3) {
        fprintf(stderr, "grep: missing file operand\n");
        return 1;
    }

    int exitcode = 1;
    for (int i = 2; i < argc; ++i) {
        const char *fname = argv[i];
        FILE *fp = fopen(fname, "r");
        if (!fp) {
            fprintf(stderr, "grep: cannot open %s\n", fname);
            continue;
        }
        grep_stream(fp, pattern, fname, argc > 3);
        fclose(fp);
        exitcode = 0;
    }
    return exitcode;
}
