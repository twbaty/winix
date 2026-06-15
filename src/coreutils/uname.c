#include <stdio.h>
#include <string.h>
#include <windows.h>

/* Use RtlGetVersion via ntdll to get the real Windows version,
 * bypassing the compatibility shim that makes GetVersionEx
 * always return 6.2 (Windows 8) on Windows 10+. */
static void get_real_version(DWORD *major, DWORD *minor, DWORD *build) {
    *major = 0; *minor = 0; *build = 0;

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return;

    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOEXW *);
    RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
    if (!fn) return;

    OSVERSIONINFOEXW vi = { sizeof(vi) };
    if (fn(&vi) == 0 /* STATUS_SUCCESS */) {
        *major = vi.dwMajorVersion;
        *minor = vi.dwMinorVersion;
        *build = vi.dwBuildNumber;
    }
}

int main(int argc, char *argv[]) {
    int opt_s = 0, opt_n = 0, opt_r = 0, opt_v = 0, opt_m = 0, opt_a = 0;
    int any = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            puts("Usage: uname [OPTION]...");
            puts("Print certain system information.");
            puts("With no OPTION, same as -s.");
            puts("");
            puts("  -a         print all information");
            puts("  -s         print the kernel name (default)");
            puts("  -n         print the network node hostname");
            puts("  -r         print the kernel release");
            puts("  -v         print the kernel version");
            puts("  -m         print the machine hardware name");
            puts("  -h, --help   display this help and exit");
            puts("      --version  output version information and exit");
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) { puts("uname 1.0 (Winix)"); return 0; }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (const char *p = argv[i] + 1; *p; p++) {
                switch (*p) {
                    case 's': opt_s = 1; any = 1; break;
                    case 'n': opt_n = 1; any = 1; break;
                    case 'r': opt_r = 1; any = 1; break;
                    case 'v': opt_v = 1; any = 1; break;
                    case 'm': opt_m = 1; any = 1; break;
                    case 'a': opt_a = 1; any = 1; break;
                    default:
                        fprintf(stderr, "uname: invalid option -- '%c'\n", *p);
                        return 1;
                }
            }
        }
    }

    /* Default: just -s */
    if (!any) opt_s = 1;

    if (opt_a) {
        opt_s = opt_n = opt_r = opt_v = opt_m = 1;
    }

    DWORD major, minor, build;
    get_real_version(&major, &minor, &build);

    char hostname[256] = "unknown";
    DWORD hsz = sizeof(hostname);
    GetComputerNameA(hostname, &hsz);

    char release[64], version[64];
    snprintf(release, sizeof(release), "%lu.%lu.%lu", major, minor, build);
    snprintf(version, sizeof(version), "Build %lu", build);

    int first = 1;
#define PRINT(s) do { if (!first) putchar(' '); printf("%s", s); first = 0; } while(0)

    if (opt_s) PRINT("Windows");
    if (opt_n) PRINT(hostname);
    if (opt_r) PRINT(release);
    if (opt_v) PRINT(version);
    if (opt_m) PRINT("x86_64");

    putchar('\n');
    return 0;
}
