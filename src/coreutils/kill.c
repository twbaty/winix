/*
 * kill.c — Winix coreutil
 *
 * Usage: kill PID...
 *
 * Terminate processes by PID.
 *
 * Options:
 *   --help     display this help and exit
 *   --version  output version information and exit
 *
 * Exit codes: 0 success, 1 error
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: kill PID...");
        puts("Terminate one or more processes by PID.");
        puts("");
        puts("      --help     display this help and exit");
        puts("      --version  output version information and exit");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("kill 1.0 (Winix)");
        return 0;
    }

    if (argc < 2) {
        fprintf(stderr, "kill: missing operand\n");
        fprintf(stderr, "Try 'kill --help' for more information.\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        DWORD pid = (DWORD)atoi(argv[i]);
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!h) { perror("kill"); ret = 1; continue; }
        TerminateProcess(h, 0);
        CloseHandle(h);
    }
    return ret;
}
