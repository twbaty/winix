#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

static int human   = 0;
static int summary = 0;
static int all     = 0;  /* -a: show individual files too */

static void fmt_size(long long kb, char *buf, size_t bufsz) {
    if (!human) {
        snprintf(buf, bufsz, "%lld", kb);
        return;
    }
    double bytes = (double)kb * 1024.0;
    if (bytes >= (double)1024*1024*1024*1024)
        snprintf(buf, bufsz, "%.1fT", bytes / ((double)1024*1024*1024*1024));
    else if (bytes >= 1024*1024*1024)
        snprintf(buf, bufsz, "%.1fG", bytes / (1024*1024*1024));
    else if (bytes >= 1024*1024)
        snprintf(buf, bufsz, "%.1fM", bytes / (1024*1024));
    else if (bytes >= 1024)
        snprintf(buf, bufsz, "%.1fK", bytes / 1024);
    else
        snprintf(buf, bufsz, "%.0fB", bytes);
}

/* Returns total size in 1K blocks */
static long long du_path(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "du: cannot stat '%s': %s\n", path, strerror(errno));
        return 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        long long kb = (st.st_size + 1023) / 1024;
        if (all && !summary) {
            char buf[16];
            fmt_size(kb, buf, sizeof(buf));
            printf("%s\t%s\n", buf, path);
        }
        return kb;
    }

    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "du: cannot open '%s': %s\n", path, strerror(errno));
        return 0;
    }

    long long total = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        total += du_path(child);
    }
    closedir(d);

    if (!summary) {
        char buf[16];
        fmt_size(total, buf, sizeof(buf));
        printf("%s\t%s\n", buf, path);
    }

    return total;
}

int main(int argc, char *argv[]) {
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (const char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'h') human   = 1;
            else if (*p == 's') summary = 1;
            else if (*p == 'a') all     = 1;
            else {
                fprintf(stderr, "du: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
        argi++;
    }

    if (argi >= argc) {
        /* Default: current directory */
        long long total = du_path(".");
        if (summary) {
            char buf[16];
            fmt_size(total, buf, sizeof(buf));
            printf("%s\t.\n", buf);
        }
        return 0;
    }

    int ret = 0;
    for (int i = argi; i < argc; i++) {
        long long total = du_path(argv[i]);
        if (summary) {
            char buf[16];
            fmt_size(total, buf, sizeof(buf));
            printf("%s\t%s\n", buf, argv[i]);
        }
    }
    return ret;
}
