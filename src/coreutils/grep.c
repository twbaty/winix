#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static int case_insensitive = 0;
static int show_line_numbers = 0;
static int invert_match = 0;

static int match_line(const char *line, const char *pattern) {
    if (case_insensitive) {
        for (size_t i = 0; line[i] && pattern[i]; i++) {
            char a = tolower(line[i]);
            char b = tolower(pattern[i]);
            if (a != b) return 0;
        }
        return strstr(strlwr(strdup(line)), strlwr(strdup(pattern))) != NULL;
    } else {
        return strstr(line, pattern) != NULL;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: grep [-i] [-n] [-v] <pattern> [file]\n");
        return 1;
    }

    int arg_index = 1;
    while (arg_index < argc && argv[arg_index][0] == '-') {
        if (strcmp(argv[arg_index], "-i") == 0)
            case_insensitive = 1;
        else if (strcmp(argv[arg_index], "-n") == 0)
            show_line_numbers = 1;
        else if (strcmp(argv[arg_index], "-v") == 0)
            invert_match = 1;
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[arg_index]);
            return 1;
        }
        arg_index++;
    }

    if (arg_index >= argc) {
        fprintf(stderr, "Missing pattern\n");
        return 1;
    }

    const char *pattern = argv[arg_index++];
    FILE *fp = NULL;

    if (arg_index < argc) {
        fp = fopen(argv[arg_index], "r");
        if (!fp) {
            perror("grep");
            return 1;
        }
    } else {
        fp = stdin;
    }

    char line[4096];
    long line_num = 0;
    int matched_any = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        int matched = match_line(line, pattern);
        if (invert_match) matched = !matched;

        if (matched) {
            matched_any = 1;
            if (show_line_numbers)
                printf("%ld:", line_num);
            fputs(line, stdout);
        }
    }

    if (fp != stdin)
        fclose(fp);

    return matched_any ? 0 : 1;
}
