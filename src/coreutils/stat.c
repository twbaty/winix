#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <windows.h>

static const char *file_type(mode_t mode) {
    if (S_ISREG(mode))  return "regular file";
    if (S_ISDIR(mode))  return "directory";
    if (S_ISCHR(mode))  return "character device";
    if (S_ISBLK(mode))  return "block device";
    if (S_ISFIFO(mode)) return "fifo";
    return "unknown";
}

static void fmt_time(time_t t, char *buf, size_t bufsz) {
    struct tm *tm = localtime(&t);
    strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S %z", tm);
}

static void fmt_attrs(const char *path, char *buf, size_t bufsz) {
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        snprintf(buf, bufsz, "?");
        return;
    }
    int i = 0;
    buf[i] = '\0';
    if (attrs & FILE_ATTRIBUTE_READONLY)  { strncat(buf, "readonly ", bufsz - i - 1); i += 9; }
    if (attrs & FILE_ATTRIBUTE_HIDDEN)    { strncat(buf, "hidden ",   bufsz - i - 1); i += 7; }
    if (attrs & FILE_ATTRIBUTE_SYSTEM)    { strncat(buf, "system ",   bufsz - i - 1); i += 7; }
    if (attrs & FILE_ATTRIBUTE_ARCHIVE)   { strncat(buf, "archive ",  bufsz - i - 1); i += 8; }
    if (i == 0) snprintf(buf, bufsz, "normal");
    else buf[i > 0 ? i - 1 : 0] = '\0';  /* trim trailing space */
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: stat <file>...\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];
        struct stat st;
        if (stat(path, &st) != 0) {
            fprintf(stderr, "stat: cannot stat '%s': %s\n", path, strerror(errno));
            ret = 1;
            continue;
        }

        char atime[64], mtime[64], ctime_buf[64];
        fmt_time(st.st_atime, atime,    sizeof(atime));
        fmt_time(st.st_mtime, mtime,    sizeof(mtime));
        fmt_time(st.st_ctime, ctime_buf, sizeof(ctime_buf));

        char attrs[128];
        fmt_attrs(path, attrs, sizeof(attrs));

        printf("  File: %s\n",        path);
        printf("  Type: %s\n",        file_type(st.st_mode));
        printf("  Size: %lld bytes\n",(long long)st.st_size);
        printf("  Attrs: %s\n",       attrs);
        printf("  Access: %s\n",      atime);
        printf("  Modify: %s\n",      mtime);
        printf("  Change: %s\n",      ctime_buf);

        if (i < argc - 1) putchar('\n');
    }

    return ret;
}
