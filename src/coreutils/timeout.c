/*
 * timeout.c — Winix coreutil
 *
 * Usage: timeout [OPTION] DURATION COMMAND [ARG...]
 *
 * Run COMMAND with a time limit of DURATION.
 * If COMMAND does not finish in time, it is killed and we exit 124.
 *
 * DURATION format:
 *   A non-negative integer optionally followed by:
 *     s  seconds (default)
 *     m  minutes
 *     h  hours
 *     d  days
 *   Examples: 5  10s  2m  1h  0.5 (decimal not supported — integer only)
 *
 * Options:
 *   -s SIGNAL          (accepted, ignored on Windows)
 *   -k DURATION        (accepted, ignored on Windows)
 *   --preserve-status  exit with child's status even on timeout
 *   --foreground       (no-op on Windows)
 *   --help             Print usage and exit 0
 *   --version          Print version and exit 0
 *
 * Exit codes:
 *   124  command timed out (unless --preserve-status)
 *   125  timeout itself failed
 *   126  command found but could not be invoked
 *   127  command not found
 *   otherwise: the exit code of the child process
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#ifdef _WIN32
#  include <windows.h>
#endif

static void usage(void) {
    puts("Usage: timeout [OPTION] DURATION COMMAND [ARG...]");
    puts("Start COMMAND, and kill it if it runs longer than DURATION.");
    puts("");
    puts("  DURATION   integer seconds, or N with suffix s/m/h/d");
    puts("");
    puts("  -s SIGNAL          signal to send on timeout (ignored on Windows)");
    puts("  -k DURATION        send KILL after this extra time (ignored on Windows)");
    puts("  --preserve-status  exit with child's status even on timeout");
    puts("  --foreground       run command in foreground (no-op on Windows)");
    puts("  --help             display this help and exit");
    puts("  --version          output version information and exit");
}

/*
 * Parse DURATION string → milliseconds.
 * Returns -1 on parse error.
 */
static long long parse_duration_ms(const char *s) {
    if (!s || !*s) return -1;

    char *end = NULL;
    long long val = strtoll(s, &end, 10);
    if (end == s || val < 0) return -1;

    long long ms = val * 1000LL;
    if (*end == '\0' || strcmp(end, "s") == 0) {
        /* seconds (default) */
    } else if (strcmp(end, "m") == 0) {
        ms = val * 60LL * 1000LL;
    } else if (strcmp(end, "h") == 0) {
        ms = val * 3600LL * 1000LL;
    } else if (strcmp(end, "d") == 0) {
        ms = val * 86400LL * 1000LL;
    } else {
        return -1;
    }
    return ms;
}

/*
 * Build a single command-line string from argv[start..argc-1].
 * Each argument that contains a space or quote is wrapped in double-quotes
 * with internal double-quotes escaped as \".
 * Caller must free the returned string.
 */
static char *build_cmdline(int argc, char *argv[], int start) {
    /* Calculate required buffer size */
    size_t total = 1; /* NUL terminator */
    for (int i = start; i < argc; i++) {
        total += strlen(argv[i]) * 2 + 4; /* worst-case quoting */
    }

    char *buf = malloc(total);
    if (!buf) return NULL;
    buf[0] = '\0';

    for (int i = start; i < argc; i++) {
        if (i > start) strcat(buf, " ");

        /* Check if quoting is needed */
        int needs_quote = 0;
        for (const char *p = argv[i]; *p; p++) {
            if (*p == ' ' || *p == '\t' || *p == '"') { needs_quote = 1; break; }
        }

        if (needs_quote) {
            strcat(buf, "\"");
            for (const char *p = argv[i]; *p; p++) {
                if (*p == '"') strcat(buf, "\\\"");
                else {
                    size_t pos = strlen(buf);
                    buf[pos]     = *p;
                    buf[pos + 1] = '\0';
                }
            }
            strcat(buf, "\"");
        } else {
            strcat(buf, argv[i]);
        }
    }
    return buf;
}

int main(int argc, char *argv[]) {
    bool preserve_status = false;
    int  argi            = 1;

    /* Parse options */
    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0)            { usage(); return 0; }
        if (strcmp(argv[argi], "--version") == 0)         { puts("timeout 1.0 (Winix 1.0)"); return 0; }
        if (strcmp(argv[argi], "--preserve-status") == 0) { preserve_status = true; continue; }
        if (strcmp(argv[argi], "--foreground") == 0)      { /* no-op */ continue; }

        if (strcmp(argv[argi], "-s") == 0) {
            /* consume signal name/number argument */
            argi++;
            if (argi >= argc) {
                fprintf(stderr, "timeout: option requires an argument -- 's'\n");
                return 125;
            }
            continue;
        }
        if (strcmp(argv[argi], "-k") == 0) {
            /* consume kill-duration argument */
            argi++;
            if (argi >= argc) {
                fprintf(stderr, "timeout: option requires an argument -- 'k'\n");
                return 125;
            }
            continue;
        }
        if (argv[argi][0] == '-' && argv[argi][1] == '-') {
            fprintf(stderr, "timeout: unrecognized option '%s'\n", argv[argi]);
            return 125;
        }
        /* First non-option is DURATION */
        break;
    }

    if (argi >= argc) {
        fprintf(stderr, "timeout: missing operand\n");
        fprintf(stderr, "Try 'timeout --help' for more information.\n");
        return 125;
    }

    /* Parse DURATION */
    long long timeout_ms = parse_duration_ms(argv[argi]);
    if (timeout_ms < 0) {
        fprintf(stderr, "timeout: invalid time interval '%s'\n", argv[argi]);
        return 125;
    }
    argi++;

    if (argi >= argc) {
        fprintf(stderr, "timeout: missing command\n");
        fprintf(stderr, "Try 'timeout --help' for more information.\n");
        return 125;
    }

    /* argi now points to COMMAND */
#ifdef _WIN32
    /* Build the command-line string for CreateProcess */
    char *cmdline = build_cmdline(argc, argv, argi);
    if (!cmdline) {
        fprintf(stderr, "timeout: out of memory\n");
        return 125;
    }

    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(
            NULL,       /* lpApplicationName  — let Windows find it */
            cmdline,    /* lpCommandLine */
            NULL,       /* process security attrs */
            NULL,       /* thread security attrs */
            TRUE,       /* inherit handles */
            0,          /* creation flags */
            NULL,       /* inherit environment */
            NULL,       /* inherit cwd */
            &si,
            &pi)) {
        DWORD err = GetLastError();
        free(cmdline);
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            fprintf(stderr, "timeout: %s: command not found\n", argv[argi]);
            return 127;
        }
        fprintf(stderr, "timeout: failed to launch '%s' (error %lu)\n", argv[argi], err);
        return 126;
    }
    free(cmdline);

    /* Wait for child to finish or for timeout */
    DWORD wait_ms = (timeout_ms > (long long)0xFFFFFFFELL)
                    ? INFINITE
                    : (DWORD)timeout_ms;

    DWORD wait_result = WaitForSingleObject(pi.hProcess, wait_ms);

    int exit_code;
    if (wait_result == WAIT_TIMEOUT) {
        /* Timed out: kill the child */
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000); /* wait up to 5s for it to die */
        exit_code = 124;

        if (preserve_status) {
            /* Try to get child's exit code after forced termination */
            DWORD child_exit = 1;
            GetExitCodeProcess(pi.hProcess, &child_exit);
            exit_code = (int)child_exit;
        }
    } else {
        /* Child finished in time */
        DWORD child_exit = 0;
        GetExitCodeProcess(pi.hProcess, &child_exit);
        exit_code = (int)child_exit;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exit_code;

#else
    /* Non-Windows stub (should not be reached in Winix) */
    fprintf(stderr, "timeout: not implemented on this platform\n");
    return 125;
#endif
}
