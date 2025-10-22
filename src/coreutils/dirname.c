#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: dirname <path>\n");
        return 1;
    }

    static char buf[4096];
    strncpy(buf, argv[1], sizeof(buf) - 1);
    char *slash = strrchr(buf, '/');
    if (!slash) slash = strrchr(buf, '\\');
    if (slash) *slash = '\0';
    else strcpy(buf, ".");
    printf("%s\n", buf);
    return 0;
}
