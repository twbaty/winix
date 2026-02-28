#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

static void count_stream(FILE *f, int *lines, int *words, int *chars) {
    *lines = *words = *chars = 0;
    int c, in_word = 0;
    while ((c = fgetc(f)) != EOF) {
        (*chars)++;
        if (c == '\n') (*lines)++;
        if (isspace(c)) in_word = 0;
        else if (!in_word) { (*words)++; in_word = 1; }
    }
}

static void print_counts(int lines, int words, int chars,
                         bool show_l, bool show_w, bool show_c,
                         const char *label) {
    if (show_l) printf("%8d", lines);
    if (show_w) printf("%8d", words);
    if (show_c) printf("%8d", chars);
    if (label)  printf(" %s", label);
    printf("\n");
}

int main(int argc, char* argv[]) {
    bool show_l = false, show_w = false, show_c = false;
    int first_file = argc; // index of first non-flag arg

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (char *p = argv[i] + 1; *p; ++p) {
                if      (*p == 'l') show_l = true;
                else if (*p == 'w') show_w = true;
                else if (*p == 'c') show_c = true;
                else {
                    fprintf(stderr, "wc: invalid option -- '%c'\n", *p);
                    return 1;
                }
            }
        } else {
            if (first_file == argc) first_file = i;
        }
    }

    // No flags → show all three
    if (!show_l && !show_w && !show_c)
        show_l = show_w = show_c = true;

    // No file args → read stdin
    if (first_file == argc) {
        int l, w, c;
        count_stream(stdin, &l, &w, &c);
        print_counts(l, w, c, show_l, show_w, show_c, NULL);
        return 0;
    }

    int total_l = 0, total_w = 0, total_c = 0;
    int file_count = 0;

    for (int i = first_file; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') continue; // skip flags

        FILE *f = strcmp(argv[i], "-") == 0 ? stdin : fopen(argv[i], "r");
        if (!f) {
            fprintf(stderr, "wc: cannot open %s\n", argv[i]);
            continue;
        }

        int l, w, c;
        count_stream(f, &l, &w, &c);
        print_counts(l, w, c, show_l, show_w, show_c, argv[i]);

        total_l += l; total_w += w; total_c += c;
        file_count++;

        if (f != stdin) fclose(f);
    }

    if (file_count > 1)
        print_counts(total_l, total_w, total_c, show_l, show_w, show_c, "total");

    return 0;
}
