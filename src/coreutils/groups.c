/*
 * groups — print group memberships for a user
 *
 * Usage: groups [USER ...]
 *   With no USER, prints groups for the current user.
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <lm.h>

#pragma comment(lib, "netapi32.lib")

#define VERSION "1.0"

static int print_groups_for(const char *username) {
    /* Convert to wide */
    wchar_t wuser[256] = {0};
    if (username)
        MultiByteToWideChar(CP_UTF8, 0, username, -1, wuser, 256);

    /* Get local group memberships */
    LOCALGROUP_USERS_INFO_0 *buf = NULL;
    DWORD entries = 0, total = 0;
    NET_API_STATUS st = NetUserGetLocalGroups(
        NULL,
        username ? wuser : NULL,
        0, LG_INCLUDE_INDIRECT,
        (LPBYTE *)&buf, MAX_PREFERRED_LENGTH,
        &entries, &total);

    if (st != NERR_Success) {
        /* Fallback: get token groups */
        HANDLE htok = NULL;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &htok)) {
            fprintf(stderr, "groups: cannot open token\n"); return 1;
        }
        DWORD needed = 0;
        GetTokenInformation(htok, TokenGroups, NULL, 0, &needed);
        TOKEN_GROUPS *tg = malloc(needed);
        if (!tg || !GetTokenInformation(htok, TokenGroups, tg, needed, &needed)) {
            CloseHandle(htok); free(tg); return 1;
        }
        CloseHandle(htok);

        if (username) printf("%s : ", username);
        int first = 1;
        for (DWORD i = 0; i < tg->GroupCount; i++) {
            if (!(tg->Groups[i].Attributes & SE_GROUP_ENABLED)) continue;
            char name[256] = {0}, dom[256] = {0};
            DWORD nlen = 256, dlen = 256;
            SID_NAME_USE use;
            if (LookupAccountSidA(NULL, tg->Groups[i].Sid,
                                  name, &nlen, dom, &dlen, &use)) {
                if (use == SidTypeGroup || use == SidTypeAlias ||
                    use == SidTypeWellKnownGroup) {
                    if (!first) printf(" ");
                    printf("%s", name);
                    first = 0;
                }
            }
        }
        printf("\n");
        free(tg);
        return 0;
    }

    if (username) printf("%s : ", username);
    for (DWORD i = 0; i < entries; i++) {
        char name[256] = {0};
        WideCharToMultiByte(CP_UTF8, 0, buf[i].lgrui0_name, -1, name, sizeof(name), NULL, NULL);
        if (i) printf(" ");
        printf("%s", name);
    }
    printf("\n");
    NetApiBufferFree(buf);
    return 0;
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("groups %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[i], "--help")) {
            fprintf(stderr,
                "usage: groups [USER ...]\n\n"
                "Print group memberships for each USER (default: current user).\n\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
    }

    if (argc < 2) return print_groups_for(NULL);

    int ret = 0;
    for (int i = 1; i < argc; i++)
        ret |= print_groups_for(argv[i]);
    return ret;
}
