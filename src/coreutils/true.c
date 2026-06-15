/*
 * true.c — Winix coreutil
 * Exit with a status code indicating success.
 */

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: true [ignored command line arguments]");
        puts("Exit with a status code indicating success.");
        puts("");
        puts("  -h, --help     display this help and exit");
        puts("      --version  output version information and exit");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("true 1.0 (Winix)");
        return 0;
    }
    return 0;
}
