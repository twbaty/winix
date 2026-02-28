#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <string.h>

static bool force   = false;
static bool verbose = false;

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0'; argi++) {
        for (char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'f') force   = true;
            else if (*p == 'v') verbose = true;
            else {
                fprintf(stderr, "mv: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
    }

    if (argc - argi < 2) {
        fprintf(stderr, "Usage: mv [-fv] <source> <destination>\n");
        return 1;
    }

    const char *src = argv[argi];
    const char *dst = argv[argi + 1];

    // If destination exists and force not set, refuse.
    struct stat st;
    if (!force && stat(dst, &st) == 0) {
        fprintf(stderr, "mv: '%s' already exists (use -f to overwrite)\n", dst);
        return 1;
    }

    if (rename(src, dst) != 0) {
        perror("mv");
        return 1;
    }

    if (verbose) printf("'%s' -> '%s'\n", src, dst);
    return 0;
}
