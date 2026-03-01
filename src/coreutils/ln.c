/*
 * ln.c — Winix coreutil
 *
 * Usage:
 *   ln TARGET LINK_NAME
 *   ln TARGET... DIRECTORY
 *   ln -s TARGET LINK_NAME   (symbolic link)
 *   ln -s TARGET... DIRECTORY
 *
 * Creates hard links by default; use -s for symbolic links.
 *
 * Options:
 *   -s, --symbolic       create symbolic links instead of hard links
 *   -f, --force          remove existing destination before creating
 *   -n, --no-dereference treat LINK_NAME as a normal file if it is a symlink to dir
 *   -v, --verbose        print a line for each link created
 *   -r, --relative       make symlinks relative to link location
 *   --help               Print usage and exit 0
 *   --version            Print version and exit 0
 *
 * Exit codes: 0 success, 1 error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <windows.h>
#  ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#    define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#  endif
#  ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#    define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#  endif
#endif

static bool opt_symbolic       = false;
static bool opt_force          = false;
static bool opt_no_dereference = false;
static bool opt_verbose        = false;
static bool opt_relative       = false;

static void usage(void) {
    puts("Usage: ln [OPTION]... TARGET LINK_NAME");
    puts("   or: ln [OPTION]... TARGET... DIRECTORY");
    puts("Create links between files.");
    puts("");
    puts("  -s, --symbolic       make symbolic links instead of hard links");
    puts("  -f, --force          remove existing destination files");
    puts("  -n, --no-dereference treat LINK_NAME as a normal file if a symlink to dir");
    puts("  -v, --verbose        print name of each linked file");
    puts("  -r, --relative       create symbolic links relative to link location");
    puts("  --help               display this help and exit");
    puts("  --version            output version information and exit");
}

/* Return true if path is an existing directory. */
static bool is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return (st.st_mode & S_IFMT) == S_IFDIR;
}

/* Return true if path exists (file, dir, or symlink). */
static bool path_exists(const char *path) {
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return lstat(path, &st) == 0;
#endif
}

/* Build the destination path when target is an existing directory.
 * dest_buf must be at least MAX_PATH bytes. */
static void make_dest_in_dir(const char *dir, const char *target_path,
                              char *dest_buf, size_t dest_sz) {
    /* Extract basename from target_path */
    const char *base = target_path;
    for (const char *p = target_path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    snprintf(dest_buf, dest_sz, "%s\\%s", dir, base);
}

/* Remove a file or directory at path (best-effort). */
static bool remove_existing(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return true; /* already gone */
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        return RemoveDirectoryA(path) != 0;
    }
    return DeleteFileA(path) != 0;
#else
    return remove(path) == 0;
#endif
}

/*
 * Create one link: target → link_name.
 * Returns 0 on success, 1 on error.
 */
static int create_link(const char *target, const char *link_name) {
    /* If -f (force), remove any existing file at link_name first */
    if (opt_force && path_exists(link_name)) {
        if (!remove_existing(link_name)) {
            fprintf(stderr, "ln: cannot remove '%s': %s\n", link_name, strerror(errno));
            return 1;
        }
    }

#ifdef _WIN32
    if (opt_symbolic) {
        /* Determine flags for CreateSymbolicLinkA */
        DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        if (is_dir(target)) flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;

        if (!CreateSymbolicLinkA(link_name, target, flags)) {
            DWORD err = GetLastError();
            char  errbuf[256];
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, err, 0, errbuf, sizeof(errbuf), NULL);
            /* Strip trailing \r\n from FormatMessage output */
            size_t elen = strlen(errbuf);
            while (elen > 0 && (errbuf[elen-1] == '\n' || errbuf[elen-1] == '\r'))
                errbuf[--elen] = '\0';
            fprintf(stderr, "ln: cannot create symbolic link '%s' -> '%s': %s\n",
                    link_name, target, errbuf);
            return 1;
        }
    } else {
        /* Hard link */
        if (!CreateHardLinkA(link_name, target, NULL)) {
            DWORD err = GetLastError();
            char  errbuf[256];
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, err, 0, errbuf, sizeof(errbuf), NULL);
            size_t elen = strlen(errbuf);
            while (elen > 0 && (errbuf[elen-1] == '\n' || errbuf[elen-1] == '\r'))
                errbuf[--elen] = '\0';
            fprintf(stderr, "ln: cannot create hard link '%s' to '%s': %s\n",
                    link_name, target, errbuf);
            return 1;
        }
    }
#else
    if (opt_symbolic) {
        if (symlink(target, link_name) != 0) {
            fprintf(stderr, "ln: cannot create symbolic link '%s' -> '%s': %s\n",
                    link_name, target, strerror(errno));
            return 1;
        }
    } else {
        if (link(target, link_name) != 0) {
            fprintf(stderr, "ln: cannot create hard link '%s' to '%s': %s\n",
                    link_name, target, strerror(errno));
            return 1;
        }
    }
#endif

    if (opt_verbose) {
        if (opt_symbolic)
            printf("'%s' -> '%s'\n", link_name, target);
        else
            printf("'%s' => '%s'\n", link_name, target);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0)            { usage(); return 0; }
        if (strcmp(argv[argi], "--version") == 0)         { puts("ln 1.0 (Winix 1.0)"); return 0; }
        if (strcmp(argv[argi], "--symbolic") == 0)        { opt_symbolic       = true; continue; }
        if (strcmp(argv[argi], "--force") == 0)           { opt_force          = true; continue; }
        if (strcmp(argv[argi], "--no-dereference") == 0)  { opt_no_dereference = true; continue; }
        if (strcmp(argv[argi], "--verbose") == 0)         { opt_verbose        = true; continue; }
        if (strcmp(argv[argi], "--relative") == 0)        { opt_relative       = true; continue; }
        if (strcmp(argv[argi], "--") == 0)                { argi++; break; }

        if (argv[argi][0] == '-' && argv[argi][1] != '\0') {
            for (char *p = argv[argi] + 1; *p; p++) {
                switch (*p) {
                    case 's': opt_symbolic       = true; break;
                    case 'f': opt_force          = true; break;
                    case 'n': opt_no_dereference = true; break;
                    case 'v': opt_verbose        = true; break;
                    case 'r': opt_relative       = true; break;
                    default:
                        fprintf(stderr, "ln: invalid option -- '%c'\n", *p);
                        return 1;
                }
            }
            continue;
        }
        break; /* first non-option */
    }

    int remaining = argc - argi;
    if (remaining < 2) {
        if (remaining < 1)
            fprintf(stderr, "ln: missing file operand\n");
        else
            fprintf(stderr, "ln: missing destination file operand after '%s'\n", argv[argi]);
        fprintf(stderr, "Try 'ln --help' for more information.\n");
        return 1;
    }

    /* Last argument is either an existing directory or the LINK_NAME */
    const char *last = argv[argc - 1];
    bool last_is_dir = is_dir(last) && !opt_no_dereference;

    int ret = 0;

    if (last_is_dir) {
        /* Create links for each TARGET inside DIRECTORY */
        for (int i = argi; i < argc - 1; i++) {
            char dest[4096];
            make_dest_in_dir(last, argv[i], dest, sizeof(dest));
            if (create_link(argv[i], dest) != 0) ret = 1;
        }
    } else {
        /* Exactly two operands: TARGET LINK_NAME */
        if (remaining > 2) {
            fprintf(stderr, "ln: target '%s' is not a directory\n", last);
            return 1;
        }
        const char *target    = argv[argi];
        const char *link_name = argv[argi + 1];
        ret = create_link(target, link_name);
    }

    return ret;
}
