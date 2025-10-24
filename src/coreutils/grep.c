#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#endif

// ─────────────────────────────────────────────────────
// Globals for color mode
static int color_enabled = 1;
static int color_auto = 1;
static int color_always = 0;

// ─────────────────────────────────────────────────────
// Fallback strcasestr for Windows
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

// ─────────────────────────────────────────────────────
// Parse --color flag
static void parse_color_flag(int *argc, char **argv) {
    for (int i = 1; i < *argc; ++i) {
        if (strncmp(argv[i], "--color", 7) == 0 || strncmp(argv[i], "--colour", 8) == 0) {
            const char *arg = strchr(argv[i], '=');
            if (arg) arg++;
            else if (i + 1 < *argc) arg = argv[++i];
            else arg = "auto";

            if (strcmp(arg, "always") == 0) { color_enabled = 1; color_auto = 0; color_always = 1; }
            else if (strcmp(arg, "never") == 0) { color_enabled = 0; color_auto = 0; }
            else { color_auto = 1; color_enabled = 1; }

            for (int j = i; j + 1 < *argc; ++j) argv[j] = argv[j + 1];
            (*argc)--;
            return;
        }
    }
}

// ─────────────────────────────────────────────────────
// Color printing helper
static void print_highlight(const char *text, const char *pattern, int ignore_case) {
    const char *p = text;
    size_t patlen = strlen(pattern);

#ifdef _WIN32
    BOOL vt_enabled = FALSE;
    DWORD mode = 0;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode))
        vt_enabled = (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif

    while (*p) {
        const char *match = ignore_case ? strcasestr_portable(p, pattern) : strstr(p, pattern);
        if (!match) {
            fputs(p, stdout);
            break;
        }

        fwrite(p, 1, match - p, stdout);

#ifdef _WIN32
        if (vt_enabled)
            printf("\033[31m%.*s\033[0m", (int)patlen, match);
        else {
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_INTENSITY);
            printf("%.*s", (int)patlen, match);
            SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }
#else
        printf("\033[31m%.*s\033[0m", (int)patlen, match);
#endif
        p = match + patlen;
    }
}

// ─────────────────────────────────────────────────────
// Main entry
int main(int argc, char *argv[]) {
    parse_color_flag(&argc, argv);

    if (argc < 3) {
        fprintf(stderr, "Usage: grep [--color=auto|always|never] <pattern> <file>\n");
        return 1;
    }

    const char *pattern = argv[1];
    const char *filename = argv[2];

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "grep: cannot open '%s'\n", filename);
        return 1;
    }

#ifdef _WIN32
    // Enable VT mode globally (so pipes and cmd.exe can interpret colors)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
#endif

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        int found = strstr(line, pattern) || strcasestr_portable(line, pattern);
        if (found) {
            if (color_enabled)
                print_highlight(line, pattern, 0);
            else
                fputs(line, stdout);
        }
    }

    fclose(f);
    return 0;
}
