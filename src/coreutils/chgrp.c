/*
 * chgrp — change group ownership of files
 *
 * Usage: chgrp [-Rv] GROUP FILE ...
 *   -R   recursive
 *   -v   verbose
 *   --version / --help
 *
 * Windows note: Windows does not have Unix-style group ownership.
 * This implementation updates the primary group in the file's security
 * descriptor via SetNamedSecurityInfoA (GROUP_SECURITY_INFORMATION).
 * Most Windows ACL checks ignore the primary group field; effective
 * permissions are controlled by the DACL.  This matches the behaviour
 * of other Windows-hosted Unix emulators (Cygwin, MSYS2).
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#define VERSION "1.0"

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#endif

static int verbose   = 0;
static int recursive = 0;

#ifdef _WIN32
static int set_group(const char *path, const char *groupname) {
    BYTE sid_buf[256];
    DWORD sid_sz = sizeof(sid_buf);
    char  dom[256];
    DWORD dom_sz = sizeof(dom);
    SID_NAME_USE sid_type;

    if (!LookupAccountNameA(NULL, groupname,
                            (PSID)sid_buf, &sid_sz,
                            dom, &dom_sz, &sid_type)) {
        fprintf(stderr, "chgrp: invalid group '%s': error %lu\n",
                groupname, GetLastError());
        return 1;
    }

    DWORD err = SetNamedSecurityInfoA(
        (LPSTR)path,
        SE_FILE_OBJECT,
        GROUP_SECURITY_INFORMATION,
        NULL,
        (PSID)sid_buf,
        NULL, NULL);

    if (err != ERROR_SUCCESS) {
        fprintf(stderr, "chgrp: cannot change group of '%s': error %lu\n",
                path, err);
        return 1;
    }

    if (verbose)
        printf("group of '%s' changed to '%s'\n", path, groupname);

    return 0;
}
#endif

static int chgrp_entry(const char *groupname, const char *path);

static int chgrp_recursive(const char *groupname, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "chgrp: cannot stat '%s': %s\n", path, strerror(errno));
        return 1;
    }

#ifdef _WIN32
    int ret = set_group(path, groupname);
#else
    int ret = 0;
#endif

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "chgrp: cannot open directory '%s': %s\n",
                    path, strerror(errno));
            return 1;
        }
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            ret |= chgrp_recursive(groupname, child);
        }
        closedir(d);
    }

    return ret;
}

static int chgrp_entry(const char *groupname, const char *path) {
#ifdef _WIN32
    return set_group(path, groupname);
#else
    fprintf(stderr, "chgrp: not supported on this platform\n");
    return 1;
#endif
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("chgrp %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: chgrp [-Rv] GROUP FILE ...\n\n"
                "Change group of each FILE to GROUP.\n\n"
                "  -R   recursive\n"
                "  -v   verbose\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }
        for (const char *p = a + 1; *p; p++) {
            if      (*p == 'R') recursive = 1;
            else if (*p == 'v') verbose   = 1;
            else { fprintf(stderr, "chgrp: invalid option -- '%c'\n", *p); return 1; }
        }
    }

    if (argc - argi < 2) {
        fprintf(stderr, "chgrp: missing operand\nusage: chgrp [-Rv] GROUP FILE ...\n");
        return 1;
    }

    const char *groupname = argv[argi++];
    int ret = 0;

    for (int i = argi; i < argc; i++) {
        if (recursive)
            ret |= chgrp_recursive(groupname, argv[i]);
        else
            ret |= chgrp_entry(groupname, argv[i]);
    }

    return ret;
}
