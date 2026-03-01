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

    /* Normalize separators */
    for (char *q = buf; *q; q++)
        if (*q == '/') *q = '\\';

    /* Skip drive prefix ("C:\") and UNC prefix ("\\") so we never
     * try to mkdir("C:") or mkdir("\\") which fails with EACCES. */
    char *p = buf;
    if (p[0] && p[1] == ':') p += 2;   /* skip "C:" */
    if (*p == '\\') p++;                 /* skip leading "\" */

    for (; *p; p++) {
        if (*p == '\\') {
            char saved = *p;
            *p = '\0';
            if (_mkdir(buf) != 0 && errno != EEXIST) {
                struct stat st;
                if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    fprintf(stderr, "mkdir: cannot create directory '%s': %s\n",
                            buf, strerror(errno));
                    return 1;
                }
            }
            *p = saved;
        }
    }

    /* Final component */
    if (_mkdir(buf) != 0 && errno != EEXIST) {
        struct stat st;
        if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "mkdir: cannot create directory '%s': %s\n",
                    buf, strerror(errno));
            return 1;
        }
    }
    if (verbose) printf("created directory '%s'\n", buf);
    return 0;
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
