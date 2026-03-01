#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: basename <path> [suffix]\n");
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
