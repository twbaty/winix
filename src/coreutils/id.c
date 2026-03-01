/*
 * id.c — Winix coreutil
 *
 * Usage: id [OPTION]... [ignored]
 *
 * Print user and group identity information.
 * On Windows: uses GetUserNameA and token elevation check.
 * Fake POSIX UID/GID: 1000 for the current user.
 * Well-known RID 544 = Administrators.
 *
 * Options:
 *   -u          print only the effective UID (or name with -n)
 *   -g          print only the effective GID (or name with -n)
 *   -G          print all group IDs (or names with -n)
 *   -n          with -u/-g/-G, print name instead of number
 *   -r          with -u/-g/-G, print real ID (same as effective on Windows)
 *   --help      Print usage and exit 0
 *   --version   Print version and exit 0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef _WIN32
#  include <windows.h>
#  include <lmcons.h>    /* UNLEN */
#endif

#define FAKE_UID 1000
#define FAKE_GID 1000
#define ADMIN_GID 544

static void usage(void) {
    puts("Usage: id [OPTION]...");
    puts("Print user and group information for the current process.");
    puts("");
    puts("  -u          print only the effective user ID");
    puts("  -g          print only the effective group ID");
    puts("  -G          print all group IDs");
    puts("  -n          with -u, -g, -G: print names instead of numbers");
    puts("  -r          with -u, -g, -G: print real ID (same as effective on Windows)");
    puts("  --help      display this help and exit");
    puts("  --version   output version information and exit");
}

/* Returns true if the current process token has elevated privileges. */
static bool is_elevated(void) {
#ifdef _WIN32
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elev;
    DWORD sz = sizeof(elev);
    bool elevated = false;
    if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &sz))
        elevated = (elev.TokenIsElevated != 0);
    CloseHandle(token);
    return elevated;
#else
    return false;
#endif
}

/* Get the current username into buf (size at least UNLEN+1). Returns true on success. */
static bool get_username(char *buf, size_t bufsz) {
#ifdef _WIN32
    DWORD sz = (DWORD)bufsz;
    return GetUserNameA(buf, &sz) != 0;
#else
    const char *u = getenv("USER");
    if (!u) u = "unknown";
    strncpy(buf, u, bufsz - 1);
    buf[bufsz - 1] = '\0';
    return true;
#endif
}

int main(int argc, char *argv[]) {
    bool opt_u = false, opt_g = false, opt_G = false;
    bool opt_n = false, opt_r = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)    { usage(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { puts("id 1.0 (Winix 1.0)"); return 0; }
        if (strcmp(argv[i], "--") == 0)        { break; }

        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (char *p = argv[i] + 1; *p; p++) {
                switch (*p) {
                    case 'u': opt_u = true; break;
                    case 'g': opt_g = true; break;
                    case 'G': opt_G = true; break;
                    case 'n': opt_n = true; break;
                    case 'r': opt_r = true; break;
                    default:
                        fprintf(stderr, "id: invalid option -- '%c'\n", *p);
                        return 1;
                }
            }
        }
        /* Non-option arguments (username) are silently ignored in this
         * simplified Windows implementation. */
    }

    char username[UNLEN + 2] = "unknown";
    get_username(username, sizeof(username));

    bool admin = is_elevated();

    /* -u: print UID (or name) only */
    if (opt_u) {
        if (opt_n) printf("%s\n", username);
        else       printf("%d\n", FAKE_UID);
        return 0;
    }

    /* -g: print GID (or name) only */
    if (opt_g) {
        if (opt_n) printf("%s\n", username);
        else       printf("%d\n", FAKE_GID);
        return 0;
    }

    /* -G: print all GIDs (or names) */
    if (opt_G) {
        if (opt_n) {
            printf("%s", username);
            if (admin) printf(" Administrators");
        } else {
            printf("%d", FAKE_GID);
            if (admin) printf(" %d", ADMIN_GID);
        }
        printf("\n");
        return 0;
    }

    /* Default: print full identity line */
    /* uid=1000(username) gid=1000(username) groups=1000(username)[,544(Administrators)] */
    printf("uid=%d(%s) gid=%d(%s) groups=%d(%s)",
           FAKE_UID, username, FAKE_GID, username, FAKE_GID, username);
    if (admin)
        printf(",%d(Administrators)", ADMIN_GID);
    printf("\n");

    (void)opt_r; /* -r is accepted but equivalent to -u/-g/-G on Windows */
    return 0;
}
