#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

static int show_long = 0;

static const char *fmt_mem(SIZE_T bytes, char *buf, size_t bufsz) {
    if (bytes >= 1024 * 1024 * 1024)
        snprintf(buf, bufsz, "%.1fG", (double)bytes / (1024*1024*1024));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, bufsz, "%.1fM", (double)bytes / (1024*1024));
    else if (bytes >= 1024)
        snprintf(buf, bufsz, "%.1fK", (double)bytes / 1024);
    else
        snprintf(buf, bufsz, "%lluB", (unsigned long long)bytes);
    return buf;
}

int main(int argc, char *argv[]) {
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (const char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'l') show_long = 1;
            else if (*p == 'e' || *p == 'a') { /* default is all */ }
            else {
                fprintf(stderr, "ps: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
        argi++;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ps: cannot snapshot processes: error %lu\n", GetLastError());
        return 1;
    }

    if (show_long)
        printf("  %5s  %5s  %8s  %8s  %-32s\n", "PID", "PPID", "RSS", "VIRT", "NAME");
    else
        printf("  %5s  %5s  %-32s\n", "PID", "PPID", "NAME");

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe)) {
        do {
            if (show_long) {
                char rss_buf[16] = "-", virt_buf[16] = "-";
                HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (ph) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(ph, &pmc, sizeof(pmc))) {
                        fmt_mem(pmc.WorkingSetSize, rss_buf, sizeof(rss_buf));
                        fmt_mem(pmc.PagefileUsage,  virt_buf, sizeof(virt_buf));
                    }
                    CloseHandle(ph);
                }
                printf("  %5lu  %5lu  %8s  %8s  %-32s\n",
                       pe.th32ProcessID, pe.th32ParentProcessID,
                       rss_buf, virt_buf, pe.szExeFile);
            } else {
                printf("  %5lu  %5lu  %-32s\n",
                       pe.th32ProcessID, pe.th32ParentProcessID, pe.szExeFile);
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return 0;
}
