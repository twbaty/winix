#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <io.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: which <command>\n");
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

    printf("%s not found\n", argv[1]);
    return 1;
}
