#include <stdio.h>
#include <string.h>

static void usage(void) {
    puts("Usage: basename NAME [SUFFIX]");
    puts("   or: basename OPTION... NAME...");
    puts("Print NAME with any leading directory components removed.");
    puts("If specified, also remove a trailing SUFFIX.");
    puts("");
    puts("      --help     display this help and exit");
    puts("      --version  output version information and exit");
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "--help") == 0)    { usage(); return 0; }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) { puts("basename 1.0 (Winix)"); return 0; }

    if (argc < 2) {
        fprintf(stderr, "basename: missing operand\n");
        fprintf(stderr, "Try 'basename --help' for more information.\n");
        return 1;
    }
    if (argc > 3) {
        fprintf(stderr, "basename: extra operand '%s'\n", argv[3]);
        fprintf(stderr, "Try 'basename --help' for more information.\n");
        return 1;
    }

    char *path = argv[1];

    /* Strip trailing slashes */
    size_t len = strlen(path);
    while (len > 1 && (path[len-1] == '/' || path[len-1] == '\\'))
        len--;
    path[len] = '\0';

    /* Find last path separator */
    char *base = strrchr(path, '/');
    if (!base) base = strrchr(path, '\\');
    base = base ? base + 1 : path;

    /* Strip optional suffix */
    if (argc == 3) {
        const char *suffix = argv[2];
        size_t blen = strlen(base);
        size_t slen = strlen(suffix);
        if (slen < blen && strcmp(base + blen - slen, suffix) == 0)
            base[blen - slen] = '\0';
    }

    printf("%s\n", base);
    return 0;
}
