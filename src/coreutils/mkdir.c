#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <direct.h>
#include <errno.h>

static bool make_parents = false;
static bool verbose      = false;

// Create a single directory, ignoring EEXIST when make_parents is on.
static int make_one(const char *path) {
    if (_mkdir(path) != 0) {
        if (errno == EEXIST && make_parents) return 0;
        perror(path);
        return 1;
    }
    if (verbose) printf("created directory '%s'\n", path);
    return 0;
}

// Recursively create all components of path.
static int make_parents_fn(const char *path) {
    char buf[4096];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (char *p = buf + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            struct stat st;
            if (stat(buf, &st) != 0) {
                if (make_one(buf) != 0) return 1;
            }
            *p = saved;
        }
    }
    return make_one(buf);
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0'; argi++) {
        for (char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'p') make_parents = true;
            else if (*p == 'v') verbose      = true;
            else {
                fprintf(stderr, "mkdir: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
    }

    if (argi >= argc) {
        fprintf(stderr, "Usage: mkdir [-pv] <directory>...\n");
        return 1;
    }

    int ret = 0;
    for (int i = argi; i < argc; i++) {
        ret |= make_parents ? make_parents_fn(argv[i]) : make_one(argv[i]);
    }
    return ret;
}
