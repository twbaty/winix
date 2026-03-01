/*
 * watch.c — Winix coreutil
 *
 * Usage: watch [-n INTERVAL] [--no-title] COMMAND [ARG...]
 *
 * Run COMMAND repeatedly, clearing the screen between runs and printing a
 * header with the interval, command, and current time.  Press Ctrl+C to stop.
 *
 * Options:
 *   -n N, --interval=N   seconds between runs (default 2; decimals OK: 0.5)
 *   --no-title           suppress the header bar
 *   --help               display this help and exit
 *   --version            output version information and exit
 *
 * Exit codes: 0 on Ctrl+C exit, 1 on error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>

static void usage(void) {
    puts("Usage: watch [-n INTERVAL] [--no-title] COMMAND [ARG...]");
    puts("Run COMMAND repeatedly, showing output and elapsed time.");
    puts("");
    puts("  -n N, --interval=N   seconds between runs (default 2)");
    puts("  --no-title           suppress header line");
    puts("  --help               display this help and exit");
    puts("  --version            output version information and exit");
    puts("");
    puts("  Press Ctrl+C to exit.");
}

/* ── Globals ────────────────────────────────────────────────────────────── */

static volatile LONG g_stop = 0; /* set by Ctrl+C handler */

static BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        InterlockedExchange(&g_stop, 1);
        return TRUE;
    }
    return FALSE;
}

/* ── Console helpers ────────────────────────────────────────────────────── */

static void enable_ansi(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

static int term_cols(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return 80;
}

static void clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

/* ── Header ─────────────────────────────────────────────────────────────── */

static void print_header(const char *cmd, double interval) {
    SYSTEMTIME st;
    GetLocalTime(&st);

    int cols = term_cols();

    /* Left side: "Every N.Xs: CMD" */
    char left[512];
    snprintf(left, sizeof(left), "Every %.1fs: %s", interval, cmd);

    /* Right side: timestamp */
    char right[64];
    snprintf(right, sizeof(right), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    int llen = (int)strlen(left);
    int rlen = (int)strlen(right);
    int gap  = cols - llen - rlen;
    if (gap < 1) gap = 1;

    /* Print header line in inverse video */
    printf("\033[7m%s%*s%s\033[0m\n\n", left, gap, "", right);
    fflush(stdout);
}

/* ── Command building ───────────────────────────────────────────────────── */

static char *build_cmdline(int argc, char *argv[], int start) {
    size_t total = 1;
    for (int i = start; i < argc; i++)
        total += strlen(argv[i]) * 2 + 4;

    char *buf = (char *)malloc(total);
    if (!buf) return NULL;
    buf[0] = '\0';

    for (int i = start; i < argc; i++) {
        if (i > start) strcat(buf, " ");
        int needs_quote = 0;
        for (const char *p = argv[i]; *p; p++)
            if (*p == ' ' || *p == '\t' || *p == '"') { needs_quote = 1; break; }
        if (needs_quote) {
            strcat(buf, "\"");
            for (const char *p = argv[i]; *p; p++) {
                if (*p == '"') strcat(buf, "\\\"");
                else { size_t pos = strlen(buf); buf[pos] = *p; buf[pos+1] = '\0'; }
            }
            strcat(buf, "\"");
        } else {
            strcat(buf, argv[i]);
        }
    }
    return buf;
}

/* Build a flat display string "cmd arg1 arg2 ..." (no quoting, for header). */
static char *build_display(int argc, char *argv[], int start) {
    size_t total = 1;
    for (int i = start; i < argc; i++)
        total += strlen(argv[i]) + 1;
    char *buf = (char *)malloc(total);
    if (!buf) return NULL;
    buf[0] = '\0';
    for (int i = start; i < argc; i++) {
        if (i > start) strcat(buf, " ");
        strcat(buf, argv[i]);
    }
    return buf;
}

/* ── Run one iteration ──────────────────────────────────────────────────── */

/*
 * Run `cmdline` via cmd.exe /C, inheriting stdio.
 * Returns the child's exit code, or -1 on launch failure.
 */
static int run_once(const char *cmdline) {
    char full[8192];
    snprintf(full, sizeof(full), "cmd.exe /C %s", cmdline);

    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(NULL, full, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        return -1;

    /* Wait for child, but abort early if Ctrl+C was pressed */
    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
        if (InterlockedCompareExchange(&g_stop, 0, 0)) break;
    }

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

/* ── Interval sleep (interruptible) ─────────────────────────────────────── */

/* Sleep for `ms` milliseconds, returning false early if Ctrl+C is pressed. */
static bool sleep_interval(DWORD ms) {
    const DWORD step = 100;
    while (ms > 0) {
        if (InterlockedCompareExchange(&g_stop, 0, 0)) return false;
        DWORD t = ms > step ? step : ms;
        Sleep(t);
        ms -= t;
    }
    return !InterlockedCompareExchange(&g_stop, 0, 0);
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    double interval  = 2.0;
    bool   no_title  = false;
    int    argi      = 1;

    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[argi], "--version") == 0) { puts("watch 1.0 (Winix 1.4)"); return 0; }
        if (strcmp(argv[argi], "--no-title") == 0) { no_title = true; continue; }

        if (strcmp(argv[argi], "-n") == 0) {
            if (++argi >= argc) { fputs("watch: -n requires an argument\n", stderr); return 1; }
            interval = atof(argv[argi]);
            if (interval <= 0.0) { fputs("watch: interval must be positive\n", stderr); return 1; }
            continue;
        }
        if (strncmp(argv[argi], "-n", 2) == 0) {
            interval = atof(argv[argi] + 2);
            if (interval <= 0.0) { fputs("watch: interval must be positive\n", stderr); return 1; }
            continue;
        }
        if (strncmp(argv[argi], "--interval=", 11) == 0) {
            interval = atof(argv[argi] + 11);
            if (interval <= 0.0) { fputs("watch: interval must be positive\n", stderr); return 1; }
            continue;
        }
        if (strcmp(argv[argi], "--") == 0) { argi++; break; }
        if (argv[argi][0] == '-') {
            fprintf(stderr, "watch: invalid option -- '%s'\n", argv[argi]);
            return 1;
        }
        break;
    }

    if (argi >= argc) {
        fputs("watch: missing command\n", stderr);
        fputs("Try 'watch --help' for more information.\n", stderr);
        return 1;
    }

    enable_ansi();
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    char *cmdline = build_cmdline(argc, argv, argi);
    char *display = build_display(argc, argv, argi);
    if (!cmdline || !display) { fputs("watch: out of memory\n", stderr); return 1; }

    DWORD interval_ms = (DWORD)(interval * 1000.0);

    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        clear_screen();
        if (!no_title) print_header(display, interval);
        run_once(cmdline);
        if (!sleep_interval(interval_ms)) break;
    }

    /* Restore — move to a clean line after Ctrl+C */
    printf("\n");
    fflush(stdout);

    free(cmdline);
    free(display);
    SetConsoleCtrlHandler(ctrl_handler, FALSE);
    return 0;
}
