/*
 * pathchk — check whether file names are valid or portable
 *
 * Usage: pathchk [OPTIONS] NAME ...
 *   -p   check for POSIX portability (no non-portable chars)
 *   -P   check for empty name or leading hyphen
 *   --portability  equivalent to -p -P
 *   --version / --help
 *
 * Exit: 0 = all names valid, 1 = at least one invalid
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <windows.h>

#define VERSION "1.0"

static int g_posix    = 0;  /* -p: check POSIX portable chars */
static int g_leading  = 0;  /* -P: check empty / leading hyphen */

/* POSIX portable filename character set: [A-Za-z0-9._-] */
static int is_portable_char(char c) {
    return isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-';
}

/* Windows reserved names: CON, PRN, AUX, NUL, COM1-9, LPT1-9 */
static int is_reserved_name(const char *name) {
    static const char * const reserved[] = {
        "CON","PRN","AUX","NUL",
        "COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9",
        "LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9",
        NULL
    };
    for (int i = 0; reserved[i]; i++)
        if (!_stricmp(name, reserved[i])) return 1;
    return 0;
}

static int check_name(const char *name) {
    int ok = 1;

    if (g_leading) {
        if (!*name) {
            fprintf(stderr, "pathchk: '%s': empty file name\n", name); ok = 0;
        }
        if (*name == '-') {
            fprintf(stderr, "pathchk: '%s': leading hyphen\n", name); ok = 0;
        }
    }

    if (!*name) return ok;

    /* Windows-specific checks (always) */
    /* Check for invalid characters in Windows paths */
    const char *win_invalid = "<>:\"|?*";
    for (const char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32) {
            fprintf(stderr, "pathchk: '%s': contains control character\n", name); ok = 0; break;
        }
        if (strchr(win_invalid, *p) && *p != ':' && *p != '\\' && *p != '/') {
            fprintf(stderr, "pathchk: '%s': invalid character '%c'\n", name, *p); ok = 0; break;
        }
    }

    /* Check each path component */
    char buf[4096];
    strncpy(buf, name, sizeof(buf) - 1); buf[sizeof(buf)-1] = '\0';
    char *comp = buf;
    while (*comp) {
        char *slash = strpbrk(comp, "/\\");
        char saved = 0;
        if (slash) { saved = *slash; *slash = '\0'; }

        {
            /* Check reserved Windows names */
            if (is_reserved_name(comp)) {
                fprintf(stderr, "pathchk: '%s': reserved name '%s'\n", name, comp); ok = 0;
            }
            /* Check trailing dot/space (Windows strips them) */
            int clen = (int)strlen(comp);
            if (clen > 0 && (comp[clen-1] == '.' || comp[clen-1] == ' ')) {
                fprintf(stderr, "pathchk: '%s': component ends with '.' or ' '\n", name); ok = 0;
            }
            /* POSIX portable check */
            if (g_posix) {
                for (const char *p = comp; *p; p++) {
                    if (!is_portable_char(*p)) {
                        fprintf(stderr, "pathchk: '%s': non-portable character '%c'\n", name, *p); ok = 0; break;
                    }
                }
                /* POSIX max component: 14 chars (historical); use 255 for modern */
                if ((int)strlen(comp) > 255) {
                    fprintf(stderr, "pathchk: '%s': component too long\n", name); ok = 0;
                }
            }
        }

        if (!slash) break;
        *slash = saved;
        comp = slash + 1;
    }

    /* Check total path length */
    if ((int)strlen(name) > MAX_PATH - 1) {
        fprintf(stderr, "pathchk: '%s': path too long\n", name); ok = 0;
    }

    return ok;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version"))     { printf("pathchk %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help"))        {
            fprintf(stderr,
                "usage: pathchk [OPTIONS] NAME ...\n\n"
                "Check whether file names are valid or portable.\n\n"
                "  -p            check POSIX portable character set\n"
                "  -P            check for empty name or leading hyphen\n"
                "  --portability equivalent to -p -P\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--portability")) { g_posix = g_leading = 1; continue; }
        if (!strcmp(a, "--"))            { argi++; break; }
        for (const char *p = a + 1; *p; p++) {
            if      (*p == 'p') g_posix   = 1;
            else if (*p == 'P') g_leading = 1;
            else { fprintf(stderr, "pathchk: invalid option -- '%c'\n", *p); return 1; }
        }
    }

    if (argi >= argc) { fprintf(stderr, "pathchk: missing operand\n"); return 1; }

    int ret = 0;
    for (int i = argi; i < argc; i++)
        if (!check_name(argv[i])) ret = 1;
    return ret;
}
