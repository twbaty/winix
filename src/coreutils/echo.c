#include <stdio.h>
#include <stdbool.h>
#include <string.h>

static void print_escaped(const char *s) {
    for (; *s; s++) {
        if (*s == '\\' && *(s + 1)) {
            s++;
            switch (*s) {
                case 'n':  putchar('\n'); break;
                case 't':  putchar('\t'); break;
                case 'r':  putchar('\r'); break;
                case '\\': putchar('\\'); break;
                case 'a':  putchar('\a'); break;
                case 'b':  putchar('\b'); break;
                default:   putchar('\\'); putchar(*s); break;
            }
        } else {
            putchar(*s);
        }
    }
}

int main(int argc, char *argv[]) {
    bool no_newline  = false;
    bool escape_seqs = false;
    int argi = 1;

    // Parse leading flags (-n, -e, -ne, -en, --help, -h)
    // Stop flag parsing on first arg that isn't a valid flag.
    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0'; argi++) {
        if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
            puts("Usage: echo [OPTION]... [STRING]...");
            puts("Echo the STRING(s) to standard output.");
            puts("");
            puts("  -n       do not output the trailing newline");
            puts("  -e       enable interpretation of backslash escapes");
            puts("  -h, --help   display this help and exit");
            return 0;
        }
        bool valid = true;
        for (char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'n') no_newline  = true;
            else if (*p == 'e') escape_seqs = true;
            else { valid = false; break; }
        }
        if (!valid) break;  // treat as a literal string (e.g. echo -hello)
    }

    for (int i = argi; i < argc; i++) {
        if (i > argi) putchar(' ');
        if (escape_seqs)
            print_escaped(argv[i]);
        else
            fputs(argv[i], stdout);
    }

    if (!no_newline) putchar('\n');
    return 0;
}
