/*
 * rmdir.c — Winix coreutil
 *
 * Usage: rmdir [OPTION]... DIRECTORY...
 *
 * Remove the DIRECTORY(ies), if they are empty.
 *
 * Options:
 *   --help     display this help and exit
 *   --version  output version information and exit
 *
 * Exit codes: 0 success, 1 error
 */

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>  // for _rmdir
#else
#include <unistd.h>  // for rmdir
#endif

static void usage(void) {
    puts("Usage: rmdir [OPTION]... DIRECTORY...");
    puts("Remove the DIRECTORY(ies), if they are empty.");
    puts("");
    puts("      --help     display this help and exit");
    puts("      --version  output version information and exit");
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[argi], "--version") == 0) { puts("rmdir 1.0 (Winix)"); return 0; }
        if (strcmp(argv[argi], "--") == 0)        { argi++; break; }
        if (argv[argi][0] != '-') break;
    }

    if (argi >= argc) {
        fprintf(stderr, "rmdir: missing operand\n");
        fprintf(stderr, "Try 'rmdir --help' for more information.\n");
        return 1;
    }

    int ret = 0;
    for (int i = argi; i < argc; i++) {
#ifdef _WIN32
        int result = _rmdir(argv[i]);
#else
        int result = rmdir(argv[i]);
#endif
        if (result != 0) {
            perror(argv[i]);
            ret = 1;
        }
    }
    return ret;
}
