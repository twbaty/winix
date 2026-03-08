/*
 * install — copy files and set attributes
 *
 * Usage: install [OPTIONS] SOURCE DEST
 *        install [OPTIONS] SOURCE ... DIRECTORY
 *        install -d DIRECTORY ...
 *
 *   -d         create directories instead of copying
 *   -m MODE    set permission bits (octal, default 755)
 *   -o OWNER   set ownership (Windows: sets file owner via ACL)
 *   -g GROUP   set group (Windows: sets primary group via ACL)
 *   -v         verbose
 *   -p         preserve timestamps
 *   -s         strip (no-op on Windows — no strip utility)
 *   -b         backup: rename existing dest to dest~ before overwrite
 *   -D         create leading directories of DEST
 *   --version / --help
 *
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/utime.h>
#include <dirent.h>
#include <io.h>

#define VERSION "1.0"

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <direct.h>
#endif

static int  g_verbose   = 0;
static int  g_preserve  = 0;
static int  g_backup    = 0;
static int  g_mkdirs    = 0;   /* -D: create leading dirs */
static char g_mode[16]  = "755";
static const char *g_owner = NULL;
static const char *g_group = NULL;

/* ── mkdir -p equivalent ─────────────────────────────────────── */
static int mkdirp(const char *path) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char tmp = *p; *p = '\0';
            struct stat st;
            if (stat(buf, &st) != 0) {
#ifdef _WIN32
                if (_mkdir(buf) != 0 && errno != EEXIST) {
#else
                if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
#endif
                    fprintf(stderr, "install: cannot create directory '%s': %s\n",
                            buf, strerror(errno));
                    return 1;
                }
            }
            *p = tmp;
        }
    }
    struct stat st;
    if (stat(buf, &st) != 0) {
#ifdef _WIN32
        if (_mkdir(buf) != 0 && errno != EEXIST) {
#else
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
#endif
            fprintf(stderr, "install: cannot create directory '%s': %s\n",
                    buf, strerror(errno));
            return 1;
        }
    }
    return 0;
}

/* ── Apply Windows ACL owner ────────────────────────────────── */
#ifdef _WIN32
static void apply_owner(const char *path, const char *username, int is_group) {
    BYTE sid_buf[256];
    DWORD sid_sz = sizeof(sid_buf);
    char  dom[256]; DWORD dom_sz = sizeof(dom);
    SID_NAME_USE st;
    if (!LookupAccountNameA(NULL, username, (PSID)sid_buf, &sid_sz, dom, &dom_sz, &st)) {
        fprintf(stderr, "install: invalid %s '%s': error %lu\n",
                is_group ? "group" : "owner", username, GetLastError());
        return;
    }
    SECURITY_INFORMATION si = is_group ? GROUP_SECURITY_INFORMATION : OWNER_SECURITY_INFORMATION;
    PSID owner_sid = is_group ? NULL : (PSID)sid_buf;
    PSID group_sid = is_group ? (PSID)sid_buf : NULL;
    SetNamedSecurityInfoA((LPSTR)path, SE_FILE_OBJECT, si, owner_sid, group_sid, NULL, NULL);
}
#endif

/* ── Copy one file ───────────────────────────────────────────── */
static int copy_file(const char *src, const char *dst) {
    /* backup existing dest */
    if (g_backup) {
        struct stat st;
        if (stat(dst, &st) == 0) {
            char bak[4096];
            snprintf(bak, sizeof(bak), "%s~", dst);
            rename(dst, bak);
        }
    }

    FILE *in = fopen(src, "rb");
    if (!in) {
        fprintf(stderr, "install: cannot open '%s': %s\n", src, strerror(errno));
        return 1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fprintf(stderr, "install: cannot create '%s': %s\n", dst, strerror(errno));
        fclose(in);
        return 1;
    }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fprintf(stderr, "install: write error on '%s': %s\n", dst, strerror(errno));
            fclose(in); fclose(out);
            return 1;
        }
    }
    fclose(out);

    /* preserve timestamps */
    if (g_preserve) {
        struct stat st;
        if (stat(src, &st) == 0) {
            struct utimbuf ut;
            ut.actime  = st.st_atime;
            ut.modtime = st.st_mtime;
            utime(dst, &ut);
        }
    }

    fclose(in);

    /* apply mode (map leading digit to read-only if 4xx) */
    {
        unsigned int mode = (unsigned int)strtol(g_mode, NULL, 8);
        if (!(mode & 0200)) {
            /* remove write permission → read-only on Windows */
            struct stat st;
            if (stat(dst, &st) == 0)
                chmod(dst, st.st_mode & ~_S_IWRITE);
        }
    }

#ifdef _WIN32
    if (g_owner) apply_owner(dst, g_owner, 0);
    if (g_group) apply_owner(dst, g_group, 1);
#endif

    if (g_verbose) printf("'%s' -> '%s'\n", src, dst);
    return 0;
}

/* ── Create directory mode ───────────────────────────────────── */
static int install_dir(const char *path) {
    if (mkdirp(path) != 0) return 1;
#ifdef _WIN32
    if (g_owner) apply_owner(path, g_owner, 0);
    if (g_group) apply_owner(path, g_group, 1);
#endif
    if (g_verbose) printf("created directory '%s'\n", path);
    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;
    int dir_mode = 0;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("install %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: install [OPTIONS] SOURCE DEST\n"
                "       install [OPTIONS] SOURCE... DIRECTORY\n"
                "       install -d DIRECTORY...\n\n"
                "  -d         create directories\n"
                "  -m MODE    permission bits (octal, default 755)\n"
                "  -o OWNER   set owner\n"
                "  -g GROUP   set group\n"
                "  -v         verbose\n"
                "  -p         preserve timestamps\n"
                "  -b         backup existing files (dest~)\n"
                "  -D         create leading directories of DEST\n"
                "  -s         strip (no-op on Windows)\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--")) { argi++; break; }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 'd': dir_mode  = 1; break;
                case 'v': g_verbose = 1; break;
                case 'p': g_preserve= 1; break;
                case 'b': g_backup  = 1; break;
                case 'D': g_mkdirs  = 1; break;
                case 's': break; /* strip — no-op */
                case 'm': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "install: option requires argument -- 'm'\n"); return 1; }
                    snprintf(g_mode, sizeof(g_mode), "%s", v);
                    p = v + strlen(v) - 1; break;
                }
                case 'o': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "install: option requires argument -- 'o'\n"); return 1; }
                    g_owner = v; p = v + strlen(v) - 1; break;
                }
                case 'g': {
                    const char *v = p[1] ? p+1 : (++argi < argc ? argv[argi] : NULL);
                    if (!v) { fprintf(stderr, "install: option requires argument -- 'g'\n"); return 1; }
                    g_group = v; p = v + strlen(v) - 1; break;
                }
                default: fprintf(stderr, "install: invalid option -- '%c'\n", *p); return 1;
            }
        }
    }

    if (argi >= argc) {
        fprintf(stderr, "install: missing operand\n");
        return 1;
    }

    /* -d mode: create directories */
    if (dir_mode) {
        int ret = 0;
        for (int i = argi; i < argc; i++)
            ret |= install_dir(argv[i]);
        return ret;
    }

    /* copy mode */
    int nfiles = argc - argi;
    if (nfiles < 2) {
        fprintf(stderr, "install: missing destination\n");
        return 1;
    }

    const char *dest = argv[argc - 1];
    struct stat dst_st;
    int dest_is_dir = (stat(dest, &dst_st) == 0 && S_ISDIR(dst_st.st_mode));

    if (nfiles > 2 && !dest_is_dir) {
        fprintf(stderr, "install: target '%s' is not a directory\n", dest);
        return 1;
    }

    int ret = 0;
    for (int i = argi; i < argc - 1; i++) {
        const char *src = argv[i];
        char dstpath[4096];

        if (dest_is_dir) {
            /* strip leading path from src for the filename */
            const char *base = strrchr(src, '/');
            if (!base) base = strrchr(src, '\\');
            base = base ? base + 1 : src;
            snprintf(dstpath, sizeof(dstpath), "%s/%s", dest, base);
        } else {
            snprintf(dstpath, sizeof(dstpath), "%s", dest);
            /* -D: create leading directories */
            if (g_mkdirs) {
                char tmp[4096];
                snprintf(tmp, sizeof(tmp), "%s", dstpath);
                char *slash = strrchr(tmp, '/');
                if (!slash) slash = strrchr(tmp, '\\');
                if (slash) { *slash = '\0'; mkdirp(tmp); }
            }
        }

        ret |= copy_file(src, dstpath);
    }

    return ret;
}
