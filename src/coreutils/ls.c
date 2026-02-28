#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>

static bool show_all      = false;
static bool long_list     = false;
static bool human_readable = false;

static void fmt_size(long long bytes, char *buf, size_t buflen) {
    const char *units[] = { "B", "K", "M", "G", "T" };
    double val = (double)bytes;
    int u = 0;
    while (val >= 1024.0 && u < 4) { val /= 1024.0; ++u; }
    if (u == 0)
        snprintf(buf, buflen, "%lld B", bytes);
    else
        snprintf(buf, buflen, "%.1f %s", val, units[u]);
}

static void list_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        perror("ls");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!show_all && entry->d_name[0] == '.')
            continue;

        if (long_list) {
            struct stat st;
            char fullpath[1024];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
            if (stat(fullpath, &st) == 0) {
                // Type + simplified permissions
                char type  = S_ISDIR(st.st_mode) ? 'd' : '-';
                char write = (st.st_mode & _S_IWRITE) ? 'w' : '-';
                char perm[5];
                snprintf(perm, sizeof(perm), "%cr%c-", type, write);

                // Modification time
                char timebuf[20];
                struct tm *tm = localtime(&st.st_mtime);
                strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm);

                // Size
                if (human_readable) {
                    char szstr[16];
                    fmt_size((long long)st.st_size, szstr, sizeof(szstr));
                    printf("%s  %8s  %s  %s\n", perm, szstr, timebuf, entry->d_name);
                } else {
                    printf("%s  %8lld  %s  %s\n", perm, (long long)st.st_size, timebuf, entry->d_name);
                }
            } else {
                printf("??????????  %s\n", entry->d_name);
            }
        } else {
            printf("%s  ", entry->d_name);
        }
    }
    if (!long_list) printf("\n");

    closedir(dir);
}

int main(int argc, char *argv[]) {
    // Parse flags and paths
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == '-') continue;  // ignore long options (--color=auto etc.)
            if (strchr(argv[i], 'a')) show_all       = true;
            if (strchr(argv[i], 'l')) long_list      = true;
            if (strchr(argv[i], 'h')) human_readable = true;
        }
    }

    bool listed = false;
    int ret = 0;

    // Process each path argument
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') continue;

        const char *path = argv[i];
        struct stat st;

        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                list_directory(path);
            } else {
                printf("%s\n", path);
            }
            listed = true;
        } else {
            fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
            ret = 1;
        }
    }

    // Default to current directory if no paths provided
    if (!listed && ret == 0) {
        list_directory(".");
    }

    return ret;
}
