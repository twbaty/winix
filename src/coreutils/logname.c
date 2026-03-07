/*
 * logname — print the user's login name
 *
 * Usage: logname
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>

#define VERSION "1.0"

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("logname %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[i], "--help")) {
            fprintf(stderr,
                "usage: logname\n\n"
                "Print the current user's login name.\n\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        fprintf(stderr, "logname: extra operand '%s'\n", argv[i]); return 1;
    }

    /* Try LOGNAME or USERNAME env first, then GetUserNameA */
    const char *env = getenv("LOGNAME");
    if (!env) env = getenv("USERNAME");

    if (env) {
        printf("%s\n", env);
        return 0;
    }

    char name[256];
    DWORD sz = sizeof(name);
    if (GetUserNameA(name, &sz)) {
        printf("%s\n", name);
        return 0;
    }

    fprintf(stderr, "logname: no login name\n");
    return 1;
}
