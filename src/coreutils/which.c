#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <io.h>

int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: which COMMAND...");
        puts("Locate a command in the PATH.");
        puts("");
        puts("      --help     display this help and exit");
        puts("      --version  output version information and exit");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("which 1.0 (Winix)");
        return 0;
    }

    if (argc < 2) {
        fprintf(stderr, "which: missing operand\n");
        fprintf(stderr, "Try 'which --help' for more information.\n");
        return 1;
    }

    char *path = getenv("PATH");
    if (!path) return 1;

    char full[4096];
    char *token = strtok(path, ";");
    while (token) {
        snprintf(full, sizeof(full), "%s\\%s.exe", token, argv[1]);
        if (_access(full, 0) == 0) {
            printf("%s\n", full);
            return 0;
        }
        token = strtok(NULL, ";");
    }

    fprintf(stderr, "which: %s not found\n", argv[1]);
    return 1;
}
