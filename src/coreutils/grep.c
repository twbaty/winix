#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <io.h>
#include <unistd.h>

#define COLOR_MATCH "\033[1;31m"
#define COLOR_RESET "\033[0m"

static bool color_enabled = false;
static bool color_auto = true;
static bool color_always = false;

// Portable case-insensitive substring search
static const char *strcasestr_portable(const char *haystack, const char *needle) {
    if (!*needle) return haystack;
    for (const char *p = haystack; *p; ++p) {
        const char *h = p;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++; n++;
        }
        if (!*n) return p;
    }
    return NULL;
}

static void parse_color_flag(int *argc, char **argv) {
    for (int i = 1; i < *argc; ++i) {
        if (strncmp(argv[i], "--color", 7) == 0 || strncmp(argv[i], "--colour", 8) == 0) {
            const char *arg = strchr(argv[i], '=');
            if (arg) arg++;
            else if (i + 1 < *argc) arg = argv[++i];
            else arg = "auto";

            if (strcmp(arg, "always") == 0) { color_enabled = true; color_auto = false; color_always = true; }
            else if (strcmp(arg, "never") == 0) { color_enabled = false; color_auto = false; }
            else { color_auto = true; }

            for (int j = i; j + 1 < *argc; ++j) argv[j] = argv[j + 1];
            (*argc)--;
            return;
        }
    }
}

static const char *strcasestr_portable(const char *haystack, const char *needle);

static void highlight_and_print(const char *line, const char *pattern) {
    const char *p = line;
    size_t plen = strlen(pattern);
    while (*p) {
        const char *match = strcasestr_portable(p, pattern);
        if (!match) {
            fputs(p, stdout);
            break;
        }
        fwrite(p, 1, match - p, stdout);
        if (color_enabled) fputs(COLOR_MATCH, stdout);
        fwrite(match, 1, plen, stdout);
        if (color_enabled) fputs(COLOR_RESET, stdout);
        p = match + plen;
    }
}

int main(int argc, char *argv[]) {
    parse_color_flag(&argc, argv);
    if (color_auto && isatty(STDOUT_FILENO))
        color_enabled = true;

    if (argc < 2) {
        fprintf(stderr, "Usage: grep [--color=auto|always|never] <pattern> [file]\n");
        return 1;
    }

    const char *pattern = argv[1];
    FILE *fp = (argc > 2) ? fopen(argv[2], "r") : stdin;
    if (!fp) { perror("grep"); return 1; }

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        if (strcasestr_portable(line, pattern)) {
            highlight_and_print(line, pattern);
            if (line[strlen(line) - 1] != '\n') putchar('\n');
        }
    }

    if (fp != stdin) fclose(fp);
    return 0;
}
