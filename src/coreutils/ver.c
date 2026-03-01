#include <stdio.h>
#include <string.h>
#include <windows.h>

#define WINIX_VERSION "0.9"

static void get_real_version(DWORD *major, DWORD *minor, DWORD *build) {
    *major = 0; *minor = 0; *build = 0;

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return;

    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOEXW *);
    RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
    if (!fn) return;

    OSVERSIONINFOEXW vi = { sizeof(vi) };
    if (fn(&vi) == 0) {
        *major = vi.dwMajorVersion;
        *minor = vi.dwMinorVersion;
        *build = vi.dwBuildNumber;
    }
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("Winix %s\n", WINIX_VERSION);
            return 0;
        }
    }

    DWORD major, minor, build;
    get_real_version(&major, &minor, &build);

    printf("Winix %s  (Windows %lu.%lu, Build %lu)\n",
           WINIX_VERSION, major, minor, build);
    return 0;
}
