#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>     // isatty()
#include <stdbool.h>
#include <errno.h>

#define BLUE   "\033[1;34m"
#define GREEN  "\033[1;32m"
#define CYAN   "\033[1;36m"
#define MAGENT "\033[1;35m"
#define YELLO  "\033[1;33m"
#define RED    "\033[1;31m"
#define RESET  "\033[0m"

static bool color_auto = true;
static bool color_enabled = false;

static void parse_color_flag(int *argc, char **argv) {
    for (int i = 1; i < *argc; ++i) {
        if (strncmp(argv[i], "--color", 7) == 0) {
            const char *arg = strchr(argv[i], '=');
            if (arg) arg++;
            else if (i + 1 < *argc) arg = argv[++i];
            else arg = "auto";

            if (strcmp(arg, "always") == 0) { color_enabled = true; color_auto = false; }
            else if (strcmp(arg, "never") == 0) { color_enabled = false; color_auto = false; }
            else { color_auto = true; }
            for (int j = i; j + 1 < *argc; ++j) argv[j] = argv[j + 1];
            (*argc)--;
            return;
        }
    }
}

static const char *color_for(const struct stat *st, const char *name) {
    if (!color_enabled) return "";
    if (S_ISDIR(st->st_mode)) return BLUE;
    if (S_ISLNK(st->st_mode)) return CYAN;
    if (st->st_mode & S_IXUSR) return GREEN;
    const char *ext = strrchr(name, '.');
    if (ext) {
        if (!strcmp(ext, ".zip") || !strcmp(ext, ".gz") || !strcmp(ext, ".tar") ||
            !strcmp(ext, ".bz2") || !strcmp(ext, ".7z"))
            return MAGENT;
    }
    return "";
}

int main(int argc, char *argv[]) {
    parse_color_flag(&argc, argv);
    if (color_auto && isatty(STDOUT_FILENO)) color_enabled = true;

    const char *path = ".";
    bool show_all = false, long_list = false;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (strchr(argv[i], 'a')) show_all = true;
            if (strchr(argv[i], 'l')) long_list = true;
        } else path = argv[i];
    }

    DIR *dir = opendir(path);
    if (!dir) { perror("ls"); return 1; }

    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (!show_all && ent->d_name[0] == '.') continue;
        struct stat st;
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s/%s", path, ent->d_name);
        if (stat(buf, &st) != 0) continue;

        const char *color = color_for(&st, ent->d_name);
        const char *reset = color_enabled ? RESET : "";
        if (long_list) {
            printf("%10lld %s%s%s\n",
                   (long long)st.st_size, color, ent->d_name, reset);
        } else {
            printf("%s%s%s  ", color, ent->d_name, reset);
        }
    }
    if (!long_list) putchar('\n');
    closedir(dir);
    return 0;
}
