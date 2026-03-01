#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <ctype.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#define ANSI_RED   "\x1b[31m"
#define ANSI_RESET "\x1b[0m"

static bool use_color        = false;
static bool case_insensitive = false;
static bool invert           = false;   /* -v */
static bool count_only       = false;   /* -c */
static bool line_numbers     = false;   /* -n */
static bool files_only       = false;   /* -l */
static bool recursive        = false;   /* -r */
static bool quiet            = false;   /* -q */

static int matched_any = 0;   /* track overall match for exit code */

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
    long  lineno  = 0;
    long  match_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        char *p = find_match(line, pattern);
        bool matched = (p != NULL);

        if (invert) matched = !matched;

        if (!matched) continue;

        matched_any = 1;

        if (quiet)      continue;
        if (files_only) { printf("%s\n", filename ? filename : "(stdin)"); return; }
        if (count_only) { match_count++; continue; }

        if (show_filename && filename) printf("%s:", filename);
        if (line_numbers)              printf("%ld:", lineno);

        if (!invert && use_color && _isatty(_fileno(stdout)) && p) {
            fwrite(line, 1, p - line, stdout);
            printf(ANSI_RED "%.*s" ANSI_RESET, (int)patlen, p);
            fputs(p + patlen, stdout);
        } else {
            fputs(line, stdout);
            if (line[strlen(line) - 1] != '\n') putchar('\n');
        }
    }

    if (count_only) {
        if (show_filename && filename) printf("%s:", filename);
        printf("%ld\n", match_count);
    }
}

static void grep_path(const char *pattern, const char *path, bool show_filename);

static void grep_dir(const char *pattern, const char *dirpath) {
    DIR *d = opendir(dirpath);
    if (!d) {
        fprintf(stderr, "grep: cannot open directory '%s': %s\n", dirpath, strerror(errno));
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", dirpath, ent->d_name);
        grep_path(pattern, child, true);
    }
    closedir(d);
}

static void grep_path(const char *pattern, const char *path, bool show_filename) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "grep: cannot stat '%s': %s\n", path, strerror(errno));
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        if (recursive)
            grep_dir(pattern, path);
        else
            fprintf(stderr, "grep: '%s': Is a directory\n", path);
        return;
    }
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "grep: cannot open '%s': %s\n", path, strerror(errno));
        return;
    }
    grep_stream(fp, pattern, path, show_filename);
    fclose(fp);
}

int main(int argc, char *argv[]) {
    int argi = 1;

    use_color = _isatty(_fileno(stdout));

    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (strcmp(argv[argi], "--") == 0) { argi++; break; }
        if (strncmp(argv[argi], "--color=", 8) == 0) {
            const char *opt = argv[argi] + 8;
            if (strcmp(opt, "always") == 0)      use_color = true;
            else if (strcmp(opt, "never") == 0)  use_color = false;
            else                                  use_color = _isatty(_fileno(stdout));
            argi++; continue;
        }
        for (const char *p = argv[argi] + 1; *p; p++) {
            switch (*p) {
                case 'i': case_insensitive = true; break;
                case 'v': invert           = true; break;
                case 'c': count_only       = true; break;
                case 'n': line_numbers     = true; break;
                case 'l': files_only       = true; break;
                case 'r': recursive        = true; break;
                case 'q': quiet            = true; break;
                default:
                    fprintf(stderr, "grep: invalid option -- '%c'\n", *p);
                    return 2;
            }
        }
        argi++;
    }

    if (argi >= argc) {
        fprintf(stderr, "Usage: grep [-ivncrlq] [--color=auto|always|never] <pattern> [file...]\n");
        return 2;
    }

    const char *pattern = argv[argi++];

    /* No file operands — read stdin */
    if (argi >= argc) {
        grep_stream(stdin, pattern, NULL, false);
        return matched_any ? 0 : 1;
    }

    int nfiles = argc - argi;
    bool show_filename = (nfiles > 1) || recursive;

    for (int i = argi; i < argc; i++)
        grep_path(pattern, argv[i], show_filename);

    return matched_any ? 0 : 1;
}
