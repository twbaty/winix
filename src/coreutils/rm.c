#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <windows.h>

static bool force   = false;
static bool recurse = false;
static bool verbose = false;

static int rm_recursive(const char *path);

static int rm_file(const char *path) {
    if (remove(path) != 0) {
        if (!force) perror(path);
        return 1;
    }
    if (verbose) printf("removed '%s'\n", path);
    return 0;
}

static int rm_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (!force) perror(path);
        return force ? 0 : 1;
    }

    if (!(st.st_mode & _S_IFDIR))
        return rm_file(path);

    // It's a directory — enumerate and recurse
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        if (!force) fprintf(stderr, "rm: cannot open dir '%s'\n", path);
        return force ? 0 : 1;
    }

    int ret = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        char child[MAX_PATH];
        snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
        ret |= rm_recursive(child);
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    if (RemoveDirectoryA(path)) {
        if (verbose) printf("removed directory '%s'\n", path);
    } else {
        if (!force) fprintf(stderr, "rm: cannot remove dir '%s'\n", path);
        ret = 1;
    }
    return ret;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0'; argi++) {
        for (char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'f') force   = true;
            else if (*p == 'r' || *p == 'R') recurse = true;
            else if (*p == 'v') verbose = true;
            else {
                fprintf(stderr, "rm: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
    }

    if (argi >= argc) {
        fprintf(stderr, "Usage: rm [-frv] <file>...\n");
        return 1;
    }

    int ret = 0;
    for (int i = argi; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) == 0 && (st.st_mode & _S_IFDIR)) {
            if (!recurse) {
                fprintf(stderr, "rm: cannot remove '%s': is a directory (use -r)\n", argv[i]);
                ret = 1;
                continue;
            }
            ret |= rm_recursive(argv[i]);
        } else {
            ret |= (recurse ? rm_recursive(argv[i]) : rm_file(argv[i]));
        }
    }
    return ret;
}
