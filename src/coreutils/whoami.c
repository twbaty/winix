#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: whoami [OPTION]...");
        puts("Print the user name associated with the current effective user ID.");
        puts("");
        puts("  -h, --help     display this help and exit");
        puts("      --version  output version information and exit");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("whoami 1.0 (Winix)");
        return 0;
    }
    (void)argc; (void)argv;
#ifdef _WIN32
    char name[256];
    DWORD size = sizeof(name);
    if (GetUserNameA(name, &size)) {
        printf("%s\n", name);
        return 0;
    }
    fprintf(stderr, "whoami: failed to get username\n");
    return 1;
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        printf("%s\n", pw->pw_name);
        return 0;
    }
    fprintf(stderr, "whoami: failed to get username\n");
    return 1;
#endif
}
