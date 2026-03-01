/*
 * time.c — Winix coreutil
 *
 * Usage: time COMMAND [ARG...]
 *
 * Run COMMAND and print real/user/sys elapsed times to stderr.
 *
 * Output format (matches bash):
 *   real    0m1.234s
 *   user    0m0.456s
 *   sys     0m0.012s
 *
 * Options:
 *   --help     display this help and exit
 *   --version  output version information and exit
 *
 * Exit codes: exit code of COMMAND, or 127 if not found, 126 if can't run
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>

static void usage(void) {
    puts("Usage: time COMMAND [ARG...]");
    puts("Run COMMAND and report real, user, and sys time on stderr.");
    puts("");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
}

static void print_time(const char *label, long long ns100) {
    if (ns100 < 0) ns100 = 0;
    long long ms  = ns100 / 10000;
    long long min = ms / 60000;
    double    sec = (ms % 60000) / 1000.0;
    fprintf(stderr, "%s\t%lldm%.3fs\n", label, min, sec);
}

static long long filetime_to_ns100(FILETIME ft) {
    return ((long long)ft.dwHighDateTime << 32) | (long long)ft.dwLowDateTime;
}

/* Build a quoted command-line string from argv[start..argc-1]. */
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

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[argi], "--version") == 0) { puts("time 1.0 (Winix 1.4)"); return 0; }
        if (argv[argi][0] != '-') break;
        if (strcmp(argv[argi], "--") == 0)        { argi++; break; }
        fprintf(stderr, "time: invalid option -- '%s'\n", argv[argi]);
        return 1;
    }

    if (argi >= argc) {
        fprintf(stderr, "time: missing command\n");
        return 1;
    }

    /* Resolve command path */
    char found_exe[MAX_PATH] = "";
    const char *cmd_name = argv[argi];
    int has_path = (strchr(cmd_name, '\\') || strchr(cmd_name, '/') ||
                    (cmd_name[0] && cmd_name[1] == ':'));

    if (!has_path) {
        char with_ext[512];
        snprintf(with_ext, sizeof(with_ext), "%s.exe", cmd_name);
        if (SearchPathA(NULL, with_ext, NULL, MAX_PATH, found_exe, NULL) == 0) {
            char selfdir[MAX_PATH];
            if (GetModuleFileNameA(NULL, selfdir, MAX_PATH) > 0) {
                char *sep = strrchr(selfdir, '\\');
                if (sep) {
                    *(sep + 1) = '\0';
                    snprintf(found_exe, MAX_PATH, "%s%s.exe", selfdir, cmd_name);
                    if (GetFileAttributesA(found_exe) == INVALID_FILE_ATTRIBUTES)
                        found_exe[0] = '\0';
                }
            }
        }
    }

    char *cmdline = build_cmdline(argc, argv, argi);
    if (!cmdline) { fprintf(stderr, "time: out of memory\n"); return 1; }

    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    const char *app = found_exe[0] ? found_exe : (has_path ? cmd_name : NULL);

    /* Record wall-clock start */
    FILETIME ft_wall_start, ft_wall_end;
    GetSystemTimeAsFileTime(&ft_wall_start);

    if (!CreateProcessA(app, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        free(cmdline);
        GetSystemTimeAsFileTime(&ft_wall_end);
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            fprintf(stderr, "time: %s: command not found\n", argv[argi]);
            return 127;
        }
        fprintf(stderr, "time: failed to launch '%s' (error %lu)\n", argv[argi], err);
        return 126;
    }
    free(cmdline);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetSystemTimeAsFileTime(&ft_wall_end);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    /* Get CPU times */
    FILETIME ft_create, ft_exit, ft_kernel, ft_user;
    long long user_ns100 = 0, sys_ns100 = 0;
    if (GetProcessTimes(pi.hProcess, &ft_create, &ft_exit, &ft_kernel, &ft_user)) {
        user_ns100 = filetime_to_ns100(ft_user);
        sys_ns100  = filetime_to_ns100(ft_kernel);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    long long real_ns100 = filetime_to_ns100(ft_wall_end) - filetime_to_ns100(ft_wall_start);

    print_time("real", real_ns100);
    print_time("user", user_ns100);
    print_time("sys",  sys_ns100);

    return (int)exit_code;
}
