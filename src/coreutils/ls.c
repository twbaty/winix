/*
 * ls — list directory contents
 *
 * Usage: ls [OPTIONS] [FILE ...]
 *   -a        include entries starting with .
 *   -l        long listing format
 *   -h        human-readable sizes (with -l)
 *   -1        one entry per line
 *   --color[=WHEN]   colorize output: always, auto (default), never
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <io.h>
#include <windows.h>

#define VERSION "1.1"

/* color modes */
#define COLOR_NEVER  0
#define COLOR_AUTO   1
#define COLOR_ALWAYS 2

static bool show_all       = false;
static bool long_list      = false;
static bool human_readable = false;
static bool one_per_line   = false;
static int  g_color        = COLOR_AUTO;
static bool g_use_color    = false;   /* resolved at runtime */

/* ANSI codes */
#define ANSI_RESET    "\033[0m"
#define ANSI_DIR      "\033[1;34m"   /* bold blue   — directories  */
#define ANSI_EXE      "\033[1;32m"   /* bold green  — executables  */
#define ANSI_LINK     "\033[1;36m"   /* bold cyan   — symlinks     */
#define ANSI_ARCHIVE  "\033[1;31m"   /* bold red    — archives     */

static const char *archive_exts[] = {
    ".zip", ".tar", ".gz", ".bz2", ".xz", ".7z", ".rar",
    ".tgz", ".tbz2", ".txz", ".cab", ".iso", NULL
};

static const char *exe_exts[] = {
    ".exe", ".bat", ".cmd", ".com", ".ps1", ".sh", NULL
};

/* case-insensitive suffix match */
static bool has_ext(const char *name, const char *ext) {
    size_t nl = strlen(name), el = strlen(ext);
    if (nl < el) return false;
#ifdef _WIN32
    return _stricmp(name + nl - el, ext) == 0;
#else
    return strcasecmp(name + nl - el, ext) == 0;
#endif
}

static const char *entry_color(const char *fullpath, const char *name, const struct stat *st) {
    if (!g_use_color) return NULL;
    if (S_ISDIR(st->st_mode))  return ANSI_DIR;
    /* symlink — check reparse point via Windows API (lstat unavailable on MinGW) */
    {
        DWORD attr = GetFileAttributesA(fullpath);
        if (attr != INVALID_FILE_ATTRIBUTES &&
            (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return ANSI_LINK;
    }
    for (int i = 0; archive_exts[i]; i++)
        if (has_ext(name, archive_exts[i])) return ANSI_ARCHIVE;
    for (int i = 0; exe_exts[i]; i++)
        if (has_ext(name, exe_exts[i])) return ANSI_EXE;
    return NULL;
}

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

/* comparison for qsort */
static int cmp_name(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static void list_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "ls: cannot open '%s': %s\n", path, strerror(errno));
        return;
    }

    /* collect entries for sorting */
    char **names = NULL;
    int nnames = 0, ncap = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (!show_all && entry->d_name[0] == '.') continue;
        if (nnames >= ncap) {
            ncap = ncap ? ncap * 2 : 64;
            char **tmp = realloc(names, (size_t)ncap * sizeof(char *));
            if (!tmp) { perror("ls"); closedir(dir); return; }
            names = tmp;
        }
        names[nnames++] = strdup(entry->d_name);
    }
    closedir(dir);

    qsort(names, (size_t)nnames, sizeof(char *), cmp_name);

    for (int i = 0; i < nnames; i++) {
        const char *name = names[i];
        char fullpath[4096];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, name);

        struct stat st;
        bool have_stat = (stat(fullpath, &st) == 0);

        const char *color = have_stat ? entry_color(fullpath, name, &st) : NULL;

        if (long_list) {
            if (have_stat) {
                char type  = S_ISDIR(st.st_mode) ? 'd' : '-';
                char write = (st.st_mode & _S_IWRITE) ? 'w' : '-';
                char perm[5];
                snprintf(perm, sizeof(perm), "%cr%c-", type, write);

                char timebuf[20];
                struct tm *tm = localtime(&st.st_mtime);
                strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm);

                if (human_readable) {
                    char szstr[16];
                    fmt_size((long long)st.st_size, szstr, sizeof(szstr));
                    printf("%s  %8s  %s  ", perm, szstr, timebuf);
                } else {
                    printf("%s  %8lld  %s  ", perm, (long long)st.st_size, timebuf);
                }
            } else {
                printf("??????????  ");
            }
            if (color) printf("%s%s%s\n", color, name, ANSI_RESET);
            else        printf("%s\n", name);
        } else if (one_per_line) {
            if (color) printf("%s%s%s\n", color, name, ANSI_RESET);
            else        printf("%s\n", name);
        } else {
            if (color) printf("%s%s%s  ", color, name, ANSI_RESET);
            else        printf("%s  ", name);
        }

        free(names[i]);
    }
    free(names);

    if (!long_list && !one_per_line) printf("\n");
}

int main(int argc, char *argv[]) {
    one_per_line = !_isatty(_fileno(stdout));

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (a[0] != '-') continue;

        if (a[1] == '-') {
            /* long options */
            if (strcmp(a, "--version") == 0) {
                printf("ls %s (Winix)\n", VERSION); return 0;
            }
            if (strcmp(a, "--help") == 0) {
                fprintf(stderr,
                    "usage: ls [OPTIONS] [FILE ...]\n\n"
                    "  -a            include entries starting with .\n"
                    "  -l            long listing\n"
                    "  -h            human-readable sizes\n"
                    "  -1            one entry per line\n"
                    "  --color[=WHEN]  always, auto (default), never\n"
                    "      --version\n"
                    "      --help\n");
                return 0;
            }
            if (strcmp(a, "--color") == 0 || strcmp(a, "--color=auto") == 0)
                g_color = COLOR_AUTO;
            else if (strcmp(a, "--color=always") == 0 || strcmp(a, "--color=yes") == 0)
                g_color = COLOR_ALWAYS;
            else if (strcmp(a, "--color=never") == 0 || strcmp(a, "--color=no") == 0)
                g_color = COLOR_NEVER;
            continue;
        }

        /* short options */
        if (strchr(a, 'a')) show_all       = true;
        if (strchr(a, 'l')) long_list      = true;
        if (strchr(a, 'h')) human_readable = true;
        if (strchr(a, '1')) one_per_line   = true;
    }

    /* resolve color: auto = use color only when stdout is a tty */
    if (g_color == COLOR_ALWAYS) g_use_color = true;
    else if (g_color == COLOR_AUTO) g_use_color = _isatty(_fileno(stdout));
    else g_use_color = false;

    bool listed = false;
    int ret = 0;

    /* when multiple targets are given, print "dirname:" headers like GNU ls */
    int n_targets = 0;
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] != '-') n_targets++;
    bool need_header = (n_targets > 1);
    bool first_dir = true;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') continue;

        const char *path = argv[i];
        struct stat st;

        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (need_header) {
                    if (!first_dir) printf("\n");
                    printf("%s:\n", path);
                    first_dir = false;
                }
                list_directory(path);
            } else {
                const char *name = path;
                const char *color = entry_color(path, name, &st);
                if (long_list) {
                    char type  = '-';
                    char write = (st.st_mode & _S_IWRITE) ? 'w' : '-';
                    char perm[5];
                    snprintf(perm, sizeof(perm), "%cr%c-", type, write);

                    char timebuf[20];
                    struct tm *tm = localtime(&st.st_mtime);
                    strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm);

                    if (human_readable) {
                        char szstr[16];
                        fmt_size((long long)st.st_size, szstr, sizeof(szstr));
                        printf("%s  %8s  %s  ", perm, szstr, timebuf);
                    } else {
                        printf("%s  %8lld  %s  ", perm, (long long)st.st_size, timebuf);
                    }
                    if (color) printf("%s%s%s\n", color, name, ANSI_RESET);
                    else        printf("%s\n", name);
                } else {
                    if (color) printf("%s%s%s\n", color, name, ANSI_RESET);
                    else        printf("%s\n", name);
                }
            }
            listed = true;
        } else {
            fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
            ret = 1;
        }
    }

    if (!listed && ret == 0)
        list_directory(".");

    return ret;
}
