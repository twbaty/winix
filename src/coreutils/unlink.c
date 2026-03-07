/*
 * unlink — call the unlink function to remove a file
 *
 * Usage: unlink FILE
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define VERSION "1.0"

int main(int argc, char *argv[]) {
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("unlink %s (Winix)\n", VERSION); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        fprintf(stderr, "usage: unlink FILE\n\nRemove FILE using the unlink syscall (single file, no options).\n\n      --version\n      --help\n");
        return 0;
    }
    if (argc != 2) {
        fprintf(stderr, "unlink: missing operand\nusage: unlink FILE\n"); return 1;
    }
    if (remove(argv[1]) != 0) { perror(argv[1]); return 1; }
    return 0;
}
