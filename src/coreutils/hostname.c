/*
 * hostname.c — Winix coreutil
 *
 * Usage: hostname [-s] [-f]
 *
 * Print the machine hostname.
 *
 * Options:
 *   -s / --short   print only the short name (up to first '.')
 *   -f / --fqdn    print the full hostname (default behaviour on Windows)
 *   --help         Print usage and exit 0
 *   --version      Print version and exit 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#endif

static void usage(void) {
    puts("Usage: hostname [-s] [-f]");
    puts("Print the machine's hostname.");
    puts("");
    puts("  -s, --short   print short name (up to first '.')");
    puts("  -f, --fqdn    print full hostname (default)");
    puts("  --help        display this help and exit");
    puts("  --version     output version information and exit");
}

/* Retrieve hostname into buf (at least buf_size bytes).
 * Returns 0 on success, -1 on failure. */
static int get_hostname(char *buf, int buf_size) {
#ifdef _WIN32
    DWORD size = (DWORD)buf_size;
    /* Try DNS hostname first (gives the FQDN or at least the DNS name) */
    if (GetComputerNameExA(ComputerNameDnsHostname, buf, &size)) {
        return 0;
    }
    /* Fallback: NetBIOS name */
    size = (DWORD)buf_size;
    if (GetComputerNameA(buf, &size)) {
        return 0;
    }
    return -1;
#else
    if (gethostname(buf, (size_t)buf_size) == 0) {
        buf[buf_size - 1] = '\0';
        return 0;
    }
    return -1;
#endif
}

int main(int argc, char *argv[]) {
    int opt_short = 0;
    /* opt_fqdn does nothing extra — full name is the default */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { puts("hostname 1.0 (Winix 1.0)"); return 0; }
        if (strcmp(argv[i], "--short") == 0 || strcmp(argv[i], "-s") == 0) {
            opt_short = 1;
            continue;
        }
        if (strcmp(argv[i], "--fqdn") == 0 || strcmp(argv[i], "-f") == 0) {
            /* full name is the default; just acknowledge the flag */
            opt_short = 0;
            continue;
        }
        /* Handle combined short flags like -sf */
        if (argv[i][0] == '-' && argv[i][1] != '-' && argv[i][1] != '\0') {
            char *p = argv[i] + 1;
            while (*p) {
                if (*p == 's') { opt_short = 1; }
                else if (*p == 'f') { opt_short = 0; }
                else {
                    fprintf(stderr, "hostname: invalid option -- '%c'\n", *p);
                    return 1;
                }
                p++;
            }
            continue;
        }
        fprintf(stderr, "hostname: unrecognized option '%s'\n", argv[i]);
        return 1;
    }

    char buf[256] = {0};
    if (get_hostname(buf, sizeof(buf)) != 0) {
        fprintf(stderr, "hostname: cannot determine hostname\n");
        return 1;
    }

    if (opt_short) {
        /* Truncate at first '.' */
        char *dot = strchr(buf, '.');
        if (dot) *dot = '\0';
    }

    puts(buf);
    return 0;
}
