/*
 * nice — run a program with modified scheduling priority
 *
 * Usage: nice [-n N] COMMAND [ARG ...]
 *   -n N   niceness adjustment (default +10)
 *          Positive = lower priority, negative = higher (requires elevation)
 *          Maps POSIX -20..19 to Windows priority classes.
 *   --version / --help
 *
 * Windows priority class mapping:
 *   nice <= -15 : REALTIME_PRIORITY_CLASS
 *   nice <= -10 : HIGH_PRIORITY_CLASS
 *   nice <=  -5 : ABOVE_NORMAL_PRIORITY_CLASS
 *   nice <=   5 : NORMAL_PRIORITY_CLASS
 *   nice <=  15 : BELOW_NORMAL_PRIORITY_CLASS
 *   nice  >  15 : IDLE_PRIORITY_CLASS
 *
 * Exit: 125 = nice error, 126 = command found but not executable,
 *       127 = command not found, else = command's exit code
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define VERSION "1.0"

static DWORD niceness_to_class(int n) {
    if (n <= -15) return REALTIME_PRIORITY_CLASS;
    if (n <= -10) return HIGH_PRIORITY_CLASS;
    if (n <=  -5) return ABOVE_NORMAL_PRIORITY_CLASS;
    if (n <=   5) return NORMAL_PRIORITY_CLASS;
    if (n <=  15) return BELOW_NORMAL_PRIORITY_CLASS;
    return IDLE_PRIORITY_CLASS;
}

int main(int argc, char *argv[]) {
    int niceness = 10; /* default GNU nice adjustment */
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("nice %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: nice [-n N] COMMAND [ARG ...]\n\n"
                "Run COMMAND with adjusted scheduling priority.\n"
                "Niceness: -20 (highest) to 19 (lowest). Default adjustment: +10.\n\n"
                "  -n N   add N to niceness (positive = lower priority)\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }
        if (!strncmp(a, "-n", 2)) {
            const char *val = a[2] ? a+2 : (++argi < argc ? argv[argi] : NULL);
            if (!val) { fprintf(stderr, "nice: option requires argument -- 'n'\n"); return 125; }
            niceness = atoi(val);
            continue;
        }
        /* GNU nice also accepts -N as shorthand for -n N */
        if (a[1] == '-' || (a[1] >= '0' && a[1] <= '9')) {
            niceness = atoi(a + (a[1] == '-' ? 0 : 1));
            /* actually this handles negative numbers passed as -[-]N */
        } else {
            fprintf(stderr, "nice: invalid option '%s'\n", a); return 125;
        }
    }

    if (argi >= argc) {
        /* No command — print current nice level (always 0 on Windows) */
        printf("0\n"); return 0;
    }

    /* Build command line */
    char cmdline[32768];
    int pos = 0;
    for (int i = argi; i < argc; i++) {
        if (i > argi) cmdline[pos++] = ' ';
        /* Quote arguments containing spaces */
        int needs_quote = strchr(argv[i], ' ') || strchr(argv[i], '\t');
        if (needs_quote) cmdline[pos++] = '"';
        int len = (int)strlen(argv[i]);
        if (pos + len + 2 < (int)sizeof(cmdline)) {
            memcpy(cmdline + pos, argv[i], (size_t)len);
            pos += len;
        }
        if (needs_quote) cmdline[pos++] = '"';
    }
    cmdline[pos] = '\0';

    DWORD pclass = niceness_to_class(niceness);

    STARTUPINFOA si = {0}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                        pclass | CREATE_NEW_PROCESS_GROUP,
                        NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            fprintf(stderr, "nice: '%s': command not found\n", argv[argi]);
        else
            fprintf(stderr, "nice: cannot run '%s': error %lu\n", argv[argi], err);
        return 127;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
}
