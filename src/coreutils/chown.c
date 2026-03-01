#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#endif

static int verbose   = 0;
static int recursive = 0;

#ifdef _WIN32
/*
 * Look up `username` via LookupAccountNameA, then transfer ownership of
 * `path` using SetNamedSecurityInfoA with OWNER_SECURITY_INFORMATION.
 * Returns 0 on success, 1 on error.
 */
static int set_owner(const char *path, const char *username) {
    BYTE sid_buf[256];
    DWORD sid_sz   = sizeof(sid_buf);
    char  dom[256];
    DWORD dom_sz   = sizeof(dom);
    SID_NAME_USE sid_type;

    if (!LookupAccountNameA(NULL, username,
                            (PSID)sid_buf, &sid_sz,
                            dom, &dom_sz, &sid_type)) {
        fprintf(stderr, "chown: invalid user '%s': error %lu\n",
                username, GetLastError());
        return 1;
    }

    DWORD err = SetNamedSecurityInfoA(
        (LPSTR)path,
        SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION,
        (PSID)sid_buf,
        NULL, NULL, NULL);

    if (err != ERROR_SUCCESS) {
        fprintf(stderr, "chown: cannot change owner of '%s': error %lu\n",
                path, err);
        return 1;
    }

    if (verbose)
        printf("owner of '%s' changed to '%s'\n", path, username);

    return 0;
}
#endif

static int chown_entry(const char *username, const char *path);

static int chown_recursive(const char *username, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "chown: cannot stat '%s': %s\n", path, strerror(errno));
        return 1;
    }

#ifdef _WIN32
    int ret = set_owner(path, username);
#else
    ret = 0;  /* non-Windows path — real chown handled elsewhere */
#endif

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "chown: cannot open directory '%s': %s\n",
                    path, strerror(errno));
            return 1;
        }
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            ret |= chown_recursive(username, child);
        }
        closedir(d);
    }

    return ret;
}

static int chown_entry(const char *username, const char *path) {
#ifdef _WIN32
    return set_owner(path, username);
#else
    fprintf(stderr, "chown: not supported on this platform\n");
    return 1;
#endif
}

int main(int argc, char *argv[]) {
    int argi = 1;

    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (const char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'v') verbose   = 1;
            else if (*p == 'R') recursive = 1;
            else {
                fprintf(stderr, "chown: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
        argi++;
    }

    if (argc - argi < 2) {
        fprintf(stderr, "Usage: chown [-Rv] <user> <file>...\n");
        return 1;
    }

    const char *username = argv[argi++];
    int ret = 0;

    for (int i = argi; i < argc; i++) {
        if (recursive)
            ret |= chown_recursive(username, argv[i]);
        else
            ret |= chown_entry(username, argv[i]);
    }

    return ret;
}
