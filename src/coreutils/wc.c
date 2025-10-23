#include <stdio.h>
#include <string.h>
#include <ctype.h>

int main(int argc, char* argv[]) {
    int lines = 0, words = 0, chars = 0;
    int total_lines = 0, total_words = 0, total_chars = 0;

    if (argc == 1) {
        // No args → read stdin
        int c, in_word = 0;
        while ((c = fgetc(stdin)) != EOF) {
            chars++;
            if (c == '\n') lines++;
            if (isspace(c)) in_word = 0;
            else if (!in_word) { words++; in_word = 1; }
        }
        printf("%10d %10d %10d\n", lines, words, chars);
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        FILE* f = strcmp(argv[i], "-") == 0 ? stdin : fopen(argv[i], "r");
        if (!f) {
            fprintf(stderr, "wc: cannot open %s\n", argv[i]);
            continue;
        }
        lines = words = chars = 0;
        int c, in_word = 0;
        while ((c = fgetc(f)) != EOF) {
            chars++;
            if (c == '\n') lines++;
            if (isspace(c)) in_word = 0;
            else if (!in_word) { words++; in_word = 1; }
        }
        printf("%10d %10d %10d %s\n", lines, words, chars, argv[i]);
        total_lines += lines;
        total_words += words;
        total_chars += chars;
        if (f != stdin) fclose(f);
    }

    if (argc > 2)
        printf("%10d %10d %10d total\n", total_lines, total_words, total_chars);

    return 0;
}
