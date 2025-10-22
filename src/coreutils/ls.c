#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#define PATH_SEP '/'
#endif

static int show_all = 0;
static int long_format = 0;

static void print_permissions(mode_t mode) {
    char perms[11];
    perms[0] = (S_ISDIR(mode)) ? 'd' : '-';
    perms[1] = (mode & S_IRUSR) ? 'r' : '-';
    perms[2] = (mode & S_IWUSR) ? 'w' : '-';
    perms[3] = (mode & S_IXUSR) ? 'x' : '-';
    perms[4] = (mode & S_IRGRP) ? 'r' : '-';
    perms[5] = (mode & S_IWGRP) ? 'w' : '-';
    perms[6] = (mode & S_IXGRP) ? 'x' : '-';
    perms[7] = (mode & S_IROTH) ? 'r' : '-';
    perms[8] = (mode & S_IWOTH) ? 'w' : '-';
    perms[9] = (mode & S_IXOTH) ? 'x' : '-';
    perms[10] = '\0';
    printf("%s", perms);
}

static void list_file(const char *path, const char *name) {
    struct stat st;
    char fullpath[4096];
    snprintf(fullpath, sizeof(fullpath), "%s%c%s", path, PATH_SEP, name);

    if (stat(fullpath, &st) != 0) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", name, strerror(errno));
        return;
    }

    if (long_format) {
        print_permissions(st.st_mode);
        printf(" %5lld ", (long long)st.st_size);

        char timebuf[32];
        struct tm *tm_info = localtime(&st.st_mtime);
        strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm_info);
        printf("%s ", timebuf);
    }

    printf("%s\n", name);
}

int main(int argc, char *argv[]) {
    const char *path = ".";
    int i;

    for (i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (strchr(argv[i], 'a')) show_all = 1;
            if (strchr(argv[i], 'l')) long_format = 1;
        } else {
            path = argv[i];
        }
    }

    struct stat st;
    if (stat(path, &st) == 0 && !S_ISDIR(st.st_mode)) {
        // Single file
        list_file(".", path);
        return 0;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "ls: cannot open directory '%s': %s\n", path, strerror(errno));
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!show_all && entry->d_name[0] == '.')
            continue;
        list_file(path, entry->d_name);
    }

    closedir(dir);
    return 0;
}
