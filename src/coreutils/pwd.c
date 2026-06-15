#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define GETCWD _getcwd
#else
#include <unistd.h>
#define GETCWD getcwd
#endif

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: pwd [OPTION]...");
        puts("Print the full filename of the current working directory.");
        puts("");
        puts("  -h, --help     display this help and exit");
        puts("      --version  output version information and exit");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("pwd 1.0 (Winix)");
        return 0;
    }

    char buffer[MAX_PATH];
    if (GETCWD(buffer, sizeof(buffer)) != NULL) {
        printf("%s\n", buffer);
        return 0;
    } else {
        perror("pwd");
        return 1;
    }
}
