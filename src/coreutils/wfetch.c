/*
 * wfetch — neofetch-style system info display for Winix
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#ifndef WINIX_VERSION
#  define WINIX_VERSION "0.0"
#endif

/* ANSI colors */
#define RESET  "\033[0m"
#define BOLD   "\033[1m"
#define CYAN   "\033[96m"
#define YELLOW "\033[93m"

/* Logo lines — each renders to exactly 32 visual chars */
#define NLOGO 6
#define LOGO_W 32
static const char *LOGO[NLOGO] = {
    "__        _____ _   _ _____  __ ",
    "\\ \\      / /_ _| \\ | |_ _\\ \\/ / ",
    " \\ \\ /\\ / / | ||  \\| || | >  <  ",
    "  \\ V  V /  | || |\\  || |/  \\   ",
    "   \\_/\\_/  |___|_| \\_|___/_/\\_\\ ",
    "                                ",
};

static void enable_ansi(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

static void get_real_version(DWORD *major, DWORD *minor, DWORD *build) {
    *major = 0; *minor = 0; *build = 0;
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return;
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOEXW *);
    RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
    if (!fn) return;
    OSVERSIONINFOEXW vi;
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (fn(&vi) == 0) {
        *major = vi.dwMajorVersion;
        *minor = vi.dwMinorVersion;
        *build = vi.dwBuildNumber;
    }
}

static void get_reg_str(HKEY root, const char *sub, const char *val,
                        char *buf, DWORD len) {
    HKEY hk;
    buf[0] = '\0';
    if (RegOpenKeyExA(root, sub, 0, KEY_READ, &hk) != ERROR_SUCCESS) return;
    DWORD type, sz = len;
    RegQueryValueExA(hk, val, NULL, &type, (LPBYTE)buf, &sz);
    RegCloseKey(hk);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("wfetch %s\n", WINIX_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: wfetch\n"
                   "Display system information.\n\n"
                   "  --version    show version\n"
                   "  --help       show this help\n");
            return 0;
        }
        fprintf(stderr, "wfetch: invalid option -- '%s'\n", argv[i]);
        return 1;
    }

    enable_ansi();

    /* --- gather system info --- */
    char user[256] = "user";
    DWORD usz = sizeof(user);
    GetUserNameA(user, &usz);

    char host[256] = "host";
    DWORD hsz = sizeof(host);
    GetComputerNameA(host, &hsz);

    DWORD major = 0, minor = 0, build = 0;
    get_real_version(&major, &minor, &build);

    char os_name[256] = "Windows";
    get_reg_str(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        "ProductName", os_name, sizeof(os_name));

    char cpu[128] = "Unknown";
    get_reg_str(HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        "ProcessorNameString", cpu, sizeof(cpu));
    /* trim leading spaces and truncate if very long */
    char *cp = cpu;
    while (*cp == ' ') cp++;
    if (cp != cpu) memmove(cpu, cp, strlen(cp) + 1);
    if (strlen(cpu) > 45) cpu[45] = '\0';

    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    unsigned long long total_mb = mem.ullTotalPhys / (1024ULL * 1024);
    unsigned long long used_mb  = total_mb - mem.ullAvailPhys / (1024ULL * 1024);

    unsigned long long ms   = (unsigned long long)GetTickCount64();
    unsigned long long secs = ms / 1000;
    unsigned long long days = secs / 86400;
    unsigned long long hrs  = (secs % 86400) / 3600;
    unsigned long long mins = (secs % 3600) / 60;

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    const char *arch = "x86_64";
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
        arch = "ARM64";
    else if (si.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64)
        arch = "x86";

    /* --- format info strings --- */
    char title[512], sep[256], os_str[300], shell_str[64], ram_str[64], up_str[64];

    snprintf(title, sizeof(title), "%s@%s", user, host);

    {
        int sl = (int)strlen(title);
        if (sl >= (int)sizeof(sep)) sl = (int)sizeof(sep) - 1;
        for (int i = 0; i < sl; i++) sep[i] = '-';
        sep[sl] = '\0';
    }

    /* ProductName still reads "Windows 10" on Windows 11 machines — fix it */
    if (major == 10 && build >= 22000) {
        char *p = strstr(os_name, "Windows 10");
        if (p) { p[8] = '1'; p[9] = '1'; }
    }

    snprintf(os_str,    sizeof(os_str),    "%s (Build %lu)", os_name, (unsigned long)build);
    snprintf(shell_str, sizeof(shell_str), "Winix %s", WINIX_VERSION);
    snprintf(ram_str,   sizeof(ram_str),   "%llu MB / %llu MB", used_mb, total_mb);

    if (days > 0)
        snprintf(up_str, sizeof(up_str), "%llud %lluh %llum", days, hrs, mins);
    else if (hrs > 0)
        snprintf(up_str, sizeof(up_str), "%lluh %llum", hrs, mins);
    else
        snprintf(up_str, sizeof(up_str), "%llum", mins);

    /* --- info rows --- */
#define NINFO 8
    const char *labels[NINFO] = {
        NULL, NULL, "OS", "Shell", "CPU", "RAM", "Uptime", "Arch"
    };
    const char *values[NINFO] = {
        title, sep, os_str, shell_str, cpu, ram_str, up_str, arch
    };

    int n = NLOGO > NINFO ? NLOGO : NINFO;

    printf("\n");
    for (int i = 0; i < n; i++) {
        /* logo column */
        if (i < NLOGO)
            printf(CYAN "%s" RESET, LOGO[i]);
        else
            printf("%*s", LOGO_W, "");

        printf("  ");

        /* info column */
        if (i < NINFO) {
            if (i == 0) {
                /* user@host */
                printf(BOLD CYAN "%s" RESET, values[i]);
            } else if (i == 1) {
                /* separator */
                printf("%s", values[i]);
            } else {
                /* label: value */
                printf(BOLD YELLOW "%-6s" RESET ": %s", labels[i], values[i]);
            }
        }
        printf("\n");
    }
    printf("\n");
    return 0;
}
