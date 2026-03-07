/*
 * users — print login names of users currently logged in
 *
 * Usage: users
 *   --version / --help
 *
 * Prints a space-separated list of logged-on usernames, sorted.
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <wtsapi32.h>

#pragma comment(lib, "wtsapi32.lib")

#define VERSION "1.0"

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("users %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[i], "--help")) {
            fprintf(stderr,
                "usage: users\n\n"
                "Print login names of users currently logged on.\n\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        fprintf(stderr, "users: extra operand '%s'\n", argv[i]); return 1;
    }

    WTS_SESSION_INFOA *sessions = NULL;
    DWORD count = 0;
    if (!WTSEnumerateSessionsA(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        fprintf(stderr, "users: cannot enumerate sessions: error %lu\n", GetLastError());
        return 1;
    }

    char **names = malloc(count * sizeof(char *));
    int n = 0;

    for (DWORD i = 0; i < count; i++) {
        if (sessions[i].State != WTSActive &&
            sessions[i].State != WTSConnected &&
            sessions[i].State != WTSDisconnected) continue;

        char *uname = NULL; DWORD ulen = 0;
        WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE,
            sessions[i].SessionId, WTSUserName, &uname, &ulen);
        if (uname && *uname) {
            names[n++] = strdup(uname);
        }
        WTSFreeMemory(uname);
    }
    WTSFreeMemory(sessions);

    qsort(names, (size_t)n, sizeof(char *), cmp_str);

    for (int i = 0; i < n; i++) {
        if (i) printf(" ");
        printf("%s", names[i]);
        free(names[i]);
    }
    if (n) printf("\n");
    free(names);
    return 0;
}
