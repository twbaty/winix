#include <stdio.h>
#include <string.h>

static void usage(void) {
    puts("Usage: dirname [OPTION] NAME...");
    puts("Output each NAME with its last non-slash component and trailing slashes removed.");
    puts("");
    puts("      --help     display this help and exit");
    puts("      --version  output version information and exit");
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "--help") == 0)    { usage(); return 0; }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) { puts("dirname 1.0 (Winix)"); return 0; }

    if (argc < 2) {
        fprintf(stderr, "dirname: missing operand\n");
        fprintf(stderr, "Try 'dirname --help' for more information.\n");
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
