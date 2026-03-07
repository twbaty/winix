#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <windows.h>

static int verbose   = 0;
static int recursive = 0;

/* ── Sidecar helpers (.winixmeta) ─────────────────────────────── */

/* Build sidecar path: <path>.winixmeta */
static void meta_path(const char *path, char *buf, size_t bufsz) {
    snprintf(buf, bufsz, "%s.winixmeta", path);
}

/* Read octal mode from sidecar. Returns 1 and sets *mode on success. */
static int read_meta_mode(const char *path, int *mode) {
    char mp[4096];
    meta_path(path, mp, sizeof(mp));
    FILE *f = fopen(mp, "r");
    if (!f) return 0;
    unsigned int m = 0;
    int ok = fscanf(f, "%o", &m) == 1;
    fclose(f);
    if (ok) { *mode = (int)m; return 1; }
    return 0;
}

/* Write octal mode to sidecar (creates/overwrites). Marks it hidden. */
static void write_meta(const char *path, int mode) {
    char mp[4096];
    meta_path(path, mp, sizeof(mp));
    FILE *f = fopen(mp, "w");
    if (!f) return;
    fprintf(f, "%04o\n", mode & 07777);
    fclose(f);
    /* Mark hidden so it stays out of normal dir listings */
    SetFileAttributesA(mp, FILE_ATTRIBUTE_HIDDEN);
}

/* ── Mode computation ─────────────────────────────────────────── */

/*
 * Apply mode string to cur_mode and return the new full POSIX mode (0–07777).
 * Returns -1 on parse error.
 *
 * Handles:
 *   Octal:    0755, 644, etc.
 *   Symbolic: [ugoa]*[+-=][rwxX]+
 */
static int compute_new_mode(const char *mode, int cur_mode) {
    /* ---- octal ---- */
    if (mode[0] >= '0' && mode[0] <= '7') {
        char *end;
        long val = strtol(mode, &end, 8);
        if (*end != '\0' || val < 0 || val > 07777) return -1;
        return (int)val;
    }

    /* ---- symbolic ---- */
    const char *p = mode;
    int who = 0;   /* bitmask: 1=u 2=g 4=o */
    while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
        if (*p == 'u') who |= 1;
        if (*p == 'g') who |= 2;
        if (*p == 'o') who |= 4;
        if (*p == 'a') who = 7;
        p++;
    }
    if (who == 0) who = 7;  /* no who → 'a' (all) */

    if (*p != '+' && *p != '-' && *p != '=') return -1;
    char op = *p++;
    if (*p == '\0') return -1;

    int pbits = 0;  /* r=4 w=2 x=1; X=8 (conditional execute) */
    for (; *p; p++) {
        switch (*p) {
            case 'r': pbits |= 4; break;
            case 'w': pbits |= 2; break;
            case 'x': pbits |= 1; break;
            case 'X': pbits |= 8; break;
            default: return -1;
        }
    }

    /* Resolve X: set x only if any x already set in cur_mode */
    if (pbits & 8) {
        pbits &= ~8;
        if (cur_mode & 0111) pbits |= 1;
    }

    /* Build bit mask covering the chosen who-classes */
    int mask = 0;
    if (who & 1) mask |= (pbits << 6);  /* owner */
    if (who & 2) mask |= (pbits << 3);  /* group */
    if (who & 4) mask |= pbits;          /* other */

    int new_mode = cur_mode;
    switch (op) {
        case '+': new_mode |= mask; break;
        case '-': new_mode &= ~mask; break;
        case '=': {
            int clear = 0;
            if (who & 1) clear |= 0700;
            if (who & 2) clear |= 0070;
            if (who & 4) clear |= 0007;
            new_mode = (cur_mode & ~clear) | mask;
            break;
        }
    }
    return new_mode & 07777;
}

/* Apply mode_str to a single path. Returns 0 on success, 1 on error. */
static int apply_mode(const char *mode_str, const char *path) {
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "chmod: cannot access '%s': %s\n", path, strerror(errno));
        return 1;
    }

    /* Determine current POSIX mode: sidecar first, then infer from Windows attrs */
    int is_dir   = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    int cur_mode = is_dir ? 0755 : 0644;
    if (attrs & FILE_ATTRIBUTE_READONLY) cur_mode &= ~0222;
    read_meta_mode(path, &cur_mode);   /* overrides if sidecar exists */

    int new_mode = compute_new_mode(mode_str, cur_mode);
    if (new_mode < 0) {
        fprintf(stderr, "chmod: invalid mode: '%s'\n", mode_str);
        return 1;
    }

    /* Apply write-bit to Windows READONLY attribute */
    DWORD new_attrs = attrs;
    if (new_mode & 0222)
        new_attrs &= ~FILE_ATTRIBUTE_READONLY;
    else
        new_attrs |=  FILE_ATTRIBUTE_READONLY;

    if (new_attrs != attrs) {
        if (!SetFileAttributesA(path, new_attrs)) {
            fprintf(stderr, "chmod: cannot change permissions of '%s': error %lu\n",
                    path, GetLastError());
            return 1;
        }
    }

    /* Always update sidecar with the full new mode */
    write_meta(path, new_mode);

    if (verbose) {
        if (new_mode != cur_mode)
            printf("mode of '%s' changed from %04o to %04o\n",
                   path, cur_mode, new_mode);
        else
            printf("mode of '%s' retained as %04o\n", path, cur_mode);
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
            /* skip sidecar files */
            size_t nlen = strlen(ent->d_name);
            if (nlen > 11 && strcmp(ent->d_name + nlen - 11, ".winixmeta") == 0)
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
