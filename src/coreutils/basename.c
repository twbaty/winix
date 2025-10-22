#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: basename <path>\n");
        return 1;
    }

    char *path = argv[1];
    char *base = strrchr(path, '/');
    if (!base) base = strrchr(path, '\\'); // Windows path fallback
    printf("%s\n", base ? base + 1 : path);
    return 0;
}
