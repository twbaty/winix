#include <stdio.h>
#include <string.h>
#include <windows.h>

static int human = 0;

static void fmt_size(ULONGLONG bytes, char *buf, size_t bufsz) {
    if (!human) {
        /* 1K-blocks like GNU df */
        snprintf(buf, bufsz, "%llu", (unsigned long long)(bytes / 1024));
        return;
    }
    if (bytes >= (ULONGLONG)1024*1024*1024*1024)
        snprintf(buf, bufsz, "%.1fT", (double)bytes / ((ULONGLONG)1024*1024*1024*1024));
    else if (bytes >= 1024*1024*1024)
        snprintf(buf, bufsz, "%.1fG", (double)bytes / (1024*1024*1024));
    else if (bytes >= 1024*1024)
        snprintf(buf, bufsz, "%.1fM", (double)bytes / (1024*1024));
    else if (bytes >= 1024)
        snprintf(buf, bufsz, "%.1fK", (double)bytes / 1024);
    else
        snprintf(buf, bufsz, "%lluB", (unsigned long long)bytes);
}

static int print_drive(const char *root) {
    ULARGE_INTEGER free_bytes, total_bytes, total_free;
    if (!GetDiskFreeSpaceExA(root, &free_bytes, &total_bytes, &total_free)) {
        /* Drive not ready (e.g. empty optical drive) — skip silently */
        return 0;
    }

    ULONGLONG used = total_bytes.QuadPart - total_free.QuadPart;
    int pct = (total_bytes.QuadPart > 0)
              ? (int)(used * 100 / total_bytes.QuadPart)
              : 0;

    char vol[MAX_PATH] = "";
    GetVolumeInformationA(root, vol, sizeof(vol), NULL, NULL, NULL, NULL, 0);

    char size_s[16], used_s[16], avail_s[16];
    fmt_size(total_bytes.QuadPart, size_s, sizeof(size_s));
    fmt_size(used,                 used_s, sizeof(used_s));
    fmt_size(free_bytes.QuadPart,  avail_s, sizeof(avail_s));

    /* Strip trailing backslash for display */
    char mount[8];
    snprintf(mount, sizeof(mount), "%.2s", root);   /* e.g. "C:" */

    printf("%-16s  %10s  %10s  %10s  %4d%%  %s\n",
           vol[0] ? vol : root,
           size_s, used_s, avail_s, pct, mount);
    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (const char *p = argv[argi] + 1; *p; p++) {
            if (*p == 'h') human = 1;
            else {
                fprintf(stderr, "df: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
        argi++;
    }

    printf("%-16s  %10s  %10s  %10s  %5s  %s\n",
           "Filesystem", human ? "Size" : "1K-blocks",
           "Used", "Available", "Use%", "Mounted");

    if (argi < argc) {
        /* Specific paths requested */
        for (int i = argi; i < argc; i++) {
            char root[8] = "";
            snprintf(root, sizeof(root), "%.2s\\", argv[i]);
            print_drive(root);
        }
    } else {
        /* Enumerate all logical drives */
        char drives[256];
        DWORD len = GetLogicalDriveStringsA(sizeof(drives), drives);
        if (!len) {
            fprintf(stderr, "df: cannot enumerate drives: error %lu\n", GetLastError());
            return 1;
        }
        for (char *d = drives; *d; d += strlen(d) + 1)
            print_drive(d);
    }

    return 0;
}
