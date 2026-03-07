/*
 * nohup — run a command immune to hangups, with output to a non-tty
 *
 * Usage: nohup COMMAND [ARG ...]
 *   Runs COMMAND with stdout/stderr redirected to nohup.out (or $HOME/nohup.out)
 *   if stdout is a terminal. On Windows there is no SIGHUP, but we still
 *   redirect output and detach from the console.
 *   --version / --help
 *
 * Exit: 125 = nohup error, 126 = not executable, 127 = not found,
 *       else = command's exit code
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <windows.h>

#define VERSION "1.0"

int main(int argc, char *argv[]) {
    if (argc >= 2 && !strcmp(argv[1], "--version")) { printf("nohup %s (Winix)\n", VERSION); return 0; }
    if (argc >= 2 && !strcmp(argv[1], "--help")) {
        fprintf(stderr,
            "usage: nohup COMMAND [ARG ...]\n\n"
            "Run COMMAND ignoring hangup signals.\n"
            "Redirects stdout to nohup.out if stdout is a terminal.\n\n"
            "      --version\n"
            "      --help\n");
        return 0;
    }
    if (argc < 2) { fprintf(stderr, "nohup: missing operand\n"); return 125; }

    /* If stdout is a tty, redirect to nohup.out */
    HANDLE hstdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    int stdout_is_tty = GetConsoleMode(hstdout, &mode);

    HANDLE hout = INVALID_HANDLE_VALUE;
    if (stdout_is_tty) {
        /* Try current directory first, then HOME */
        const char *outfile = "nohup.out";
        hout = CreateFileA(outfile,
            GENERIC_WRITE, FILE_SHARE_READ, NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hout == INVALID_HANDLE_VALUE) {
            const char *home = getenv("HOME");
            if (!home) home = getenv("USERPROFILE");
            if (home) {
                char path[MAX_PATH];
                snprintf(path, sizeof(path), "%s\\nohup.out", home);
                hout = CreateFileA(path,
                    GENERIC_WRITE, FILE_SHARE_READ, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hout != INVALID_HANDLE_VALUE) outfile = path;
            }
        }
        if (hout == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "nohup: cannot open output file\n"); return 125;
        }
        /* Seek to end for append */
        SetFilePointer(hout, 0, NULL, FILE_END);
        fprintf(stderr, "nohup: appending output to '%s'\n", outfile);
    }

    /* Build command line */
    char cmdline[32768];
    int pos = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1) cmdline[pos++] = ' ';
        int needs_quote = strchr(argv[i], ' ') != NULL || strchr(argv[i], '\t') != NULL;
        if (needs_quote) cmdline[pos++] = '"';
        int len = (int)strlen(argv[i]);
        if (pos + len + 2 < (int)sizeof(cmdline)) {
            memcpy(cmdline + pos, argv[i], (size_t)len); pos += len;
        }
        if (needs_quote) cmdline[pos++] = '"';
    }
    cmdline[pos] = '\0';

    STARTUPINFOA si = {0}; si.cb = sizeof(si);
    if (hout != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = hout;
        si.hStdError  = hout;
    }

    PROCESS_INFORMATION pi = {0};
    DWORD flags = 0; /* Don't CREATE_NO_WINDOW — let it inherit */

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, flags, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            fprintf(stderr, "nohup: '%s': command not found\n", argv[1]);
        else
            fprintf(stderr, "nohup: cannot run '%s': error %lu\n", argv[1], err);
        if (hout != INVALID_HANDLE_VALUE) CloseHandle(hout);
        return 127;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (hout != INVALID_HANDLE_VALUE) CloseHandle(hout);
    return (int)exit_code;
}
