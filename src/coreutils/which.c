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

    const char *path_env = getenv("PATH");
    if (!path_env) return 1;

    int ret = 0;
    char full[4096];

    for (int i = 1; i < argc; i++) {
        /* copy PATH so strtok doesn't clobber it across command iterations */
        char path_copy[32768];
        strncpy(path_copy, path_env, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';

        int found = 0;
        char *token = strtok(path_copy, ";");
        while (token) {
            snprintf(full, sizeof(full), "%s\\%s.exe", token, argv[i]);
            if (_access(full, 0) == 0) {
                printf("%s\n", full);
                found = 1;
            }
            token = strtok(NULL, ";");
        }
        if (!found) {
            fprintf(stderr, "which: %s not found\n", argv[i]);
            ret = 1;
        }
    }
    return ret;
}
