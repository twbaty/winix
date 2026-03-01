#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <ctype.h>
#include <stdbool.h>

#ifdef _WIN32
#define ANSI_RED   "\x1b[31m"
#define ANSI_RESET "\x1b[0m"
#else
#define ANSI_RED   "\033[31m"
#define ANSI_RESET "\033[0m"
#endif

static bool use_color = false;
static bool case_insensitive = false;

static char *find_match(const char *hay, const char *needle) {
    if (!case_insensitive)
        return strstr(hay, needle);
    if (!*needle) return (char *)hay;
    size_t nlen = strlen(needle);
    for (; *hay; hay++) {
        if (tolower((unsigned char)*hay) == tolower((unsigned char)*needle)) {
            size_t i;
            for (i = 1; i < nlen; i++) {
                if (tolower((unsigned char)hay[i]) != tolower((unsigned char)needle[i]))
                    break;
            }
            if (i == nlen) return (char *)hay;
        }
    }
    return NULL;
}

static void grep_stream(FILE *fp, const char *pattern, const char *filename, bool show_filename) {
    char line[4096];
    size_t patlen = strlen(pattern);

    while (fgets(line, sizeof(line), fp)) {
        char *p = find_match(line, pattern);
        if (p) {
            if (show_filename && filename)
                printf("%s:", filename);

            if (use_color && isatty(fileno(stdout))) {
                // Print text before match
                fwrite(line, 1, p - line, stdout);
                // Highlight the actual matched text
                printf(ANSI_RED "%.*s" ANSI_RESET, (int)patlen, p);
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

    // Default: color on if stdout is a terminal
    use_color = isatty(fileno(stdout));

    if (argc < 2 && !stdin_is_pipe) {
        fprintf(stderr, "Usage: grep [-i] [--color=auto|always|never] <pattern> [file...]\n");
        return 1;
    }

    // Flag parsing
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (strcmp(argv[argi], "-i") == 0) {
            case_insensitive = true;
        } else if (strncmp(argv[argi], "--color=", 8) == 0) {
            const char *opt = argv[argi] + 8;
            if (strcmp(opt, "always") == 0) use_color = true;
            else if (strcmp(opt, "never") == 0) use_color = false;
            else use_color = isatty(fileno(stdout));
        } else {
            break;  // not a recognized flag — treat as pattern
        }
        argi++;
    }

    if (argi < argc)
        pattern = argv[argi++];
    else if (!stdin_is_pipe) {
        fprintf(stderr, "grep: missing search pattern\n");
        return 1;
    }

    if (stdin_is_pipe) {
        if (pattern) grep_stream(stdin, pattern, NULL, false);
        return 0;
    }

    if (!pattern || argi >= argc) {
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
