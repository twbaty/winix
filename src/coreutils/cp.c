#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    int verbose = 0, force = 0;
    int argi = 1;

    // parse simple flags: -v (verbose), -f (force)
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "-v") == 0) verbose = 1;
        else if (strcmp(argv[argi], "-f") == 0) force = 1;
        else {
            fprintf(stderr, "cp: unknown option %s\n", argv[argi]);
            return 1;
        }
        argi++;
    }

    if (argc - argi < 2) {
        fprintf(stderr, "Usage: cp [-v] [-f] <source> <destination>\n");
        return 1;
    }

    const char *src = argv[argi];
    const char *dst = argv[argi + 1];

    FILE *in = fopen(src, "rb");
    if (!in) {
        perror("cp (open source)");
        return 1;
    }

    // if not forcing, check if dest exists
    if (!force) {
        FILE *check = fopen(dst, "rb");
        if (check) {
            fclose(check);
            fprintf(stderr, "cp: destination '%s' exists (use -f to overwrite)\n", dst);
            fclose(in);
            return 1;
        }
    }

    FILE *out = fopen(dst, "wb");
    if (!out) {
        perror("cp (open dest)");
        fclose(in);
        return 1;
    }

    char buf[4096];
    size_t bytes;
    while ((bytes = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, bytes, out);
    }

    fclose(in);
    fclose(out);

    if (verbose)
        printf("Copied '%s' → '%s'\n", src, dst);

    return 0;
}
