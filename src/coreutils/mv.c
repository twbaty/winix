#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>

static bool force   = false;
static bool verbose = false;

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0'; argi++) {
        if (strcmp(argv[argi], "--help") == 0) {
            puts("Usage: mv [OPTION]... SOURCE DEST");
            puts("   or: mv [OPTION]... SOURCE... DIRECTORY");
            puts("Move (rename) SOURCE to DEST, or move SOURCE(s) to DIRECTORY.");
            puts("");
            puts("  -f, --force    do not prompt before overwriting");
            puts("  -v, --verbose  explain what is being done");
            puts("      --help     display this help and exit");
            puts("      --version  output version information and exit");
            return 0;
        }
        if (strcmp(argv[argi], "--version") == 0) { puts("mv 1.0 (Winix)"); return 0; }
        if (argv[argi][1] == '-') { argi++; break; }  // -- ends option parsing
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
        if (argc - argi < 1)
            fprintf(stderr, "mv: missing operand\n");
        else
            fprintf(stderr, "mv: missing destination file operand after '%s'\n", argv[argi]);
        fprintf(stderr, "Try 'mv --help' for more information.\n");
        return 1;
    }

    const char *src = argv[argi];
    const char *dst = argv[argi + 1];

    // Reject moving a file onto itself.
    if (strcmp(src, dst) == 0) {
        fprintf(stderr, "mv: '%s' and '%s' are the same file\n", src, dst);
        return 1;
    }

    // Check source exists.
    struct stat st;
    if (stat(src, &st) != 0) {
        fprintf(stderr, "mv: cannot stat '%s': %s\n", src, strerror(errno));
        return 1;
    }

    // If destination exists and force not set, refuse.
    if (!force && stat(dst, &st) == 0) {
        fprintf(stderr, "mv: '%s' already exists (use -f to overwrite)\n", dst);
        return 1;
    }

    if (rename(src, dst) != 0) {
        fprintf(stderr, "mv: cannot move '%s' to '%s': %s\n", src, dst, strerror(errno));
        return 1;
    }

    if (verbose) printf("'%s' -> '%s'\n", src, dst);
    return 0;
}
