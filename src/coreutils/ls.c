#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>

static bool show_all = false;
static bool long_list = false;

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
                printf("%10lld %s\n", (long long)st.st_size, entry->d_name);
            } else {
                printf("?????????? %s\n", entry->d_name);
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
            if (strchr(argv[i], 'a')) show_all = true;
            if (strchr(argv[i], 'l')) long_list = true;
        }
    }

    bool listed = false;

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
            fprintf(stderr, "ls: No such file or directory\n");
        }
    }

    // Default to current directory if no paths provided
    if (!listed) {
        list_directory(".");
    }

    return 0;
}
