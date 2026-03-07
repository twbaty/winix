/*
 * link — call the link function to create a hard link
 *
 * Usage: link FILE1 FILE2
 *   Creates a hard link FILE2 pointing to FILE1.
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>

#define VERSION "1.0"

int main(int argc, char *argv[]) {
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("link %s (Winix)\n", VERSION); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        fprintf(stderr, "usage: link FILE1 FILE2\n\nCreate a hard link FILE2 pointing to FILE1.\n\n      --version\n      --help\n");
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "link: missing operand\nusage: link FILE1 FILE2\n"); return 1;
    }
    if (!CreateHardLinkA(argv[2], argv[1], NULL)) {
        fprintf(stderr, "link: cannot create link '%s' to '%s': error %lu\n",
                argv[2], argv[1], GetLastError());
        return 1;
    }
    return 0;
}
