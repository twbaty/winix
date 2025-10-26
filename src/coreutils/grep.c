#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <stdbool.h>

#ifdef _WIN32
#define ANSI_RED   "\x1b[31m"
#define ANSI_RESET "\x1b[0m"
#else
#define ANSI_RED   "\033[31m"
#define ANSI_RESET "\033[0m"
#endif

static bool use_color = false;

static void grep_stream(FILE *fp, const char *pattern, const char *filename, bool show_filename) {
    char line[4096];
    size_t patlen = strlen(pattern);

    while (fgets(line, sizeof(line), fp)) {
        char *p = strstr(line, pattern);
        if (p) {
            if (show_filename && filename)
                printf("%s:", filename);

            if (use_color && isatty(fileno(stdout))) {
                // Print text before match
                fwrite(line, 1, p - line, stdout);
                // Highlight match
                printf(ANSI_RED "%.*s" ANSI_RESET, (int)patlen, pattern);
                // Remainder of line
                fputs(p + patlen, stdout);
            } else {
                fputs(line, stdout);
                if (line[strlen(line) - 1] != '\n')
                    putchar('\n');
            }
        }
    }
}

int main(int argc, char *argv[]) {
    bool stdin_is_pipe = !_isatty(_fileno(stdin));
    const char *pattern = NULL;
    int argi = 1;

    if (argc < 2 && !stdin_is_pipe) {
        fprintf(stderr, "Usage: grep [--color=auto|always|never] <pattern> [file...]\n");
        return 1;
    }

    // Color option parsing
    if (argc > 1 && strncmp(argv[1], "--color=", 8) == 0) {
        const char *opt = argv[1] + 8;
        if (strcmp(opt, "always") == 0) use_color = true;
        else if (strcmp(opt, "never") == 0) use_color = false;
        else use_color = isatty(fileno(stdout));  // auto
        argi++;
    } else {
        use_color = isatty(fileno(stdout));
    }

    if (argc > argi)
        pattern = argv[argi++];
    else if (!stdin_is_pipe) {
        fprintf(stderr, "grep: missing search pattern\n");
        return 1;
    }

    if (stdin_is_pipe) {
        grep_stream(stdin, pattern, NULL, false);
        return 0;
    }

    if (argc <= argi) {
        fprintf(stderr, "grep: missing file operand\n");
        return 1;
    }

    int exitcode = 1;
    for (int i = argi; i < argc; ++i) {
        const char *fname = argv[i];
        FILE *fp = fopen(fname, "r");
        if (!fp) {
            fprintf(stderr, "grep: cannot open %s\n", fname);
            continue;
        }
        grep_stream(fp, pattern, fname, argc - argi > 1);
        fclose(fp);
        exitcode = 0;
    }

    return exitcode;
}
