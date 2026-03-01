#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <windows.h>

static int verbose   = 0;
static int recursive = 0;

/*
 * Interpret a mode string against the file's current attributes.
 *
 * Windows only exposes one meaningful permission bit: FILE_ATTRIBUTE_READONLY.
 * We map POSIX modes to it as follows:
 *   - Any write bit present  → clear READONLY (writable)
 *   - No write bits          → set   READONLY
 *
 * Returns:
 *   0   clear READONLY (make writable)
 *   1   set   READONLY (make read-only)
 *  -1   parse error (invalid mode string)
 *  -2   valid mode but has no Windows-mappable effect (e.g. +x, -r)
 */
static int interpret_mode(const char *mode, DWORD cur_attrs) {

    /* ---- octal: 0?NNN ---- */
    if (mode[0] >= '0' && mode[0] <= '7') {
        char *end;
        long val = strtol(mode, &end, 8);
        if (*end != '\0' || val < 0 || val > 07777)
            return -1;
        /* write bits: owner=0200, group=0020, other=0002 */
        return (val & 0222) ? 0 : 1;
    }

    /* ---- symbolic: [ugoa]*[+-=][rwxX]+ ---- */
    const char *p = mode;
    while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') p++;

    if (*p != '+' && *p != '-' && *p != '=') return -1;
    char op = *p++;

    if (*p == '\0') return -1;  /* operator with no permissions */

    int has_r = 0, has_w = 0, has_x = 0;
    for (; *p; p++) {
        switch (*p) {
            case 'r':           has_r = 1; break;
            case 'w':           has_w = 1; break;
            case 'x': case 'X': has_x = 1; break;
            default: return -1;
        }
    }

    /* +r, -r, +x, -x — valid POSIX but nothing to toggle on Windows */
    if (!has_w && op != '=') return -2;

    int cur_writable = !(cur_attrs & FILE_ATTRIBUTE_READONLY);
    int new_writable  = cur_writable;

    switch (op) {
        case '+': new_writable = has_w ? 1 : cur_writable; break;
        case '-': new_writable = has_w ? 0 : cur_writable; break;
        case '=': new_writable = has_w ? 1 : 0;            break;
    }

    return new_writable ? 0 : 1;
}

/* Apply mode_str to a single path. Returns 0 on success, 1 on error. */
static int apply_mode(const char *mode_str, const char *path) {
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "chmod: cannot access '%s': %s\n", path, strerror(errno));
        return 1;
    }

    int result = interpret_mode(mode_str, attrs);

    if (result == -1) {
        fprintf(stderr, "chmod: invalid mode: '%s'\n", mode_str);
        return 1;
    }

    if (result == -2) {
        /* Valid but unmappable on Windows — silently succeed */
        if (verbose)
            printf("chmod: '%s': mode '%s' has no effect on Windows\n", path, mode_str);
        return 0;
    }

    DWORD new_attrs = attrs;
    if (result == 1)
        new_attrs |=  FILE_ATTRIBUTE_READONLY;
    else
        new_attrs &= ~FILE_ATTRIBUTE_READONLY;

    if (new_attrs != attrs) {
        if (!SetFileAttributesA(path, new_attrs)) {
            fprintf(stderr, "chmod: cannot change permissions of '%s': error %lu\n",
                    path, GetLastError());
            return 1;
        }
        if (verbose)
            printf("mode of '%s' changed to %s\n", path,
                   (new_attrs & FILE_ATTRIBUTE_READONLY) ? "read-only" : "writable");
    } else if (verbose) {
        printf("mode of '%s' retained as %s\n", path,
               (attrs & FILE_ATTRIBUTE_READONLY) ? "read-only" : "writable");
    }

    return 0;
}

/* Recursively apply mode to path and all entries beneath it. */
static int chmod_recursive(const char *mode_str, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "chmod: cannot stat '%s': %s\n", path, strerror(errno));
        return 1;
    }

    int ret = apply_mode(mode_str, path);

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "chmod: cannot open directory '%s': %s\n", path, strerror(errno));
            return 1;
        }
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            ret |= chmod_recursive(mode_str, child);
        }
        closedir(d);
    }

    return ret;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (const char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'v') verbose   = 1;
            else if (*p == 'R') recursive = 1;
            else {
                fprintf(stderr, "chmod: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
        argi++;
    }

    if (argc - argi < 2) {
        fprintf(stderr, "Usage: chmod [-Rv] <mode> <file>...\n");
        return 1;
    }

    const char *mode_str = argv[argi++];
    int ret = 0;

    for (int i = argi; i < argc; i++) {
        if (recursive)
            ret |= chmod_recursive(mode_str, argv[i]);
        else
            ret |= apply_mode(mode_str, argv[i]);
    }

    return ret;
}
