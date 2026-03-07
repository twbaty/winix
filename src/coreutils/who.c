/*
 * who — show who is logged on
 *
 * Usage: who [OPTIONS]
 *   -q      quick — print login names and count only
 *   -H      print column headers
 *   am i    print info about the current user only
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <wtsapi32.h>

#pragma comment(lib, "wtsapi32.lib")

#define VERSION "1.0"

static int g_quick   = 0;
static int g_header  = 0;
static int g_am_i    = 0;

static const char *session_state(WTS_CONNECTSTATE_CLASS s) {
    switch (s) {
        case WTSActive:       return "Active";
        case WTSConnected:    return "Connected";
        case WTSIdle:         return "Idle";
        case WTSDisconnected: return "Disc";
        case WTSListen:       return "Listen";
        default:              return "?";
    }
}

int main(int argc, char *argv[]) {
    int argi = 1;
    for (; argi < argc; argi++) {
        if (!strcmp(argv[argi], "--version")) { printf("who %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[argi], "--help")) {
            fprintf(stderr,
                "usage: who [OPTIONS]\n\n"
                "Show who is currently logged on.\n\n"
                "  -H    print column headers\n"
                "  -q    quick: names and count only\n"
                "  am i  show info for current terminal\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(argv[argi], "-q")) { g_quick  = 1; continue; }
        if (!strcmp(argv[argi], "-H")) { g_header = 1; continue; }
        if (!strcmp(argv[argi], "am") && argi + 1 < argc && !strcmp(argv[argi+1], "i")) {
            g_am_i = 1; argi++; continue;
        }
        if (argv[argi][0] == '-') {
            for (const char *p = argv[argi]+1; *p; p++) {
                if (*p == 'q') g_quick = 1;
                else if (*p == 'H') g_header = 1;
                else { fprintf(stderr, "who: invalid option -- '%c'\n", *p); return 1; }
            }
        }
    }

    WTS_SESSION_INFOA *sessions = NULL;
    DWORD count = 0;
    if (!WTSEnumerateSessionsA(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        fprintf(stderr, "who: cannot enumerate sessions: error %lu\n", GetLastError());
        return 1;
    }

    if (g_header && !g_quick)
        printf("%-16s %-12s %-20s %s\n", "NAME", "LINE", "TIME", "COMMENT");

    int printed = 0;
    for (DWORD i = 0; i < count; i++) {
        /* Skip non-active and service sessions */
        if (sessions[i].State != WTSActive &&
            sessions[i].State != WTSConnected &&
            sessions[i].State != WTSDisconnected) continue;

        /* Get username */
        char *uname = NULL;
        DWORD ulen = 0;
        WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE,
            sessions[i].SessionId, WTSUserName, &uname, &ulen);
        if (!uname || !*uname) { WTSFreeMemory(uname); continue; }

        /* am i: only show current session */
        if (g_am_i) {
            DWORD cur = WTSGetActiveConsoleSessionId();
            if (sessions[i].SessionId != cur) { WTSFreeMemory(uname); continue; }
        }

        if (g_quick) {
            if (printed) printf(" ");
            printf("%s", uname);
        } else {
            /* Get logon time */
            WTSINFOA *info = NULL;
            DWORD ilen = 0;
            char timebuf[32] = "?";
            if (WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE,
                    sessions[i].SessionId, WTSSessionInfo,
                    (char **)&info, &ilen) && info) {
                SYSTEMTIME st;
                FileTimeToSystemTime((FILETIME *)&info->LogonTime, &st);
                snprintf(timebuf, sizeof(timebuf), "%04d-%02d-%02d %02d:%02d",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
                WTSFreeMemory(info);
            }
            printf("%-16s %-12s %-20s (%s)\n",
                uname,
                sessions[i].pWinStationName,
                timebuf,
                session_state(sessions[i].State));
        }
        printed++;
        WTSFreeMemory(uname);
    }

    if (g_quick) {
        printf("\n# users=%d\n", printed);
    }

    WTSFreeMemory(sessions);
    return 0;
}
