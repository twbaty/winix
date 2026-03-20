/*
 * stdbuf — run a command with modified I/O stream buffering
 *
 * Usage: stdbuf [OPTIONS] COMMAND [ARG ...]
 *   -i MODE   stdin buffering:  L=line, 0=unbuffered, SIZE=block
 *   -o MODE   stdout buffering: L=line, 0=unbuffered, SIZE=block
 *   -e MODE   stderr buffering: L=line, 0=unbuffered, SIZE=block
 *   --version / --help
 *
 * Windows note: stdbuf works on Linux via LD_PRELOAD injection into the
 * child process's C runtime. Windows has no equivalent mechanism. This
 * implementation runs the command normally (buffering is not modified)
 * and warns when unbuffered/line-buffered output is requested. For most
 * Winix pipeline use cases (grep, awk, sed) this is sufficient since
 * Windows console I/O is already line-buffered when connected to a tty.
 *
 * Exit: 125 = stdbuf error, 127 = command not found, else = command exit code
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define VERSION "1.0"

int main(int argc, char *argv[]) {
    int argi = 1;
    int warned = 0;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("stdbuf %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: stdbuf [OPTIONS] COMMAND [ARG ...]\n\n"
                "Run COMMAND with modified stream buffering.\n\n"
                "  -i MODE   stdin buffering  (0=unbuffered, L=line, N=block size)\n"
                "  -o MODE   stdout buffering (0=unbuffered, L=line, N=block size)\n"
                "  -e MODE   stderr buffering (0=unbuffered, L=line, N=block size)\n\n"
                "Note: on Windows, buffering cannot be injected into child processes.\n"
                "The command is run normally; this flag is accepted for compatibility.\n\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }

        for (const char *p = a + 1; *p; p++) {
            if (*p == 'i' || *p == 'o' || *p == 'e') {
                const char *val = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                if (!val) { fprintf(stderr, "stdbuf: option requires argument -- '%c'\n", *p); return 125; }
                if (!warned) {
                    fprintf(stderr,
                        "stdbuf: warning: buffering cannot be changed for child processes on Windows.\n"
                        "stdbuf: the command will run with default buffering.\n");
                    warned = 1;
                }
                break;
            } else {
                fprintf(stderr, "stdbuf: invalid option -- '%c'\n", *p); return 125;
            }
        }
    }

    if (argi >= argc) { fprintf(stderr, "stdbuf: missing operand\n"); return 125; }

    /* Build and run command */
    char cmdline[32768];
    int pos = 0;
    for (int i = argi; i < argc; i++) {
        if (i > argi) cmdline[pos++] = ' ';
        int needs_quote = strchr(argv[i], ' ') || strchr(argv[i], '\t');
        if (needs_quote) cmdline[pos++] = '"';
        int len = (int)strlen(argv[i]);
        if (pos + len + 2 < (int)sizeof(cmdline)) {
            memcpy(cmdline + pos, argv[i], (size_t)len); pos += len;
        }
        if (needs_quote) cmdline[pos++] = '"';
    }
    cmdline[pos] = '\0';

    STARTUPINFOA si = {0}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            fprintf(stderr, "stdbuf: '%s': command not found\n", argv[argi]);
        else
            fprintf(stderr, "stdbuf: cannot run '%s': error %lu\n", argv[argi], err);
        return 127;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
}
