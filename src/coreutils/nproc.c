/*
 * nproc — print number of available processing units
 *
 * Usage: nproc [--all] [--ignore=N] [--version] [--help]
 *   (no args)   print number of processors available to current process
 *   --all       print total installed processors
 *   --ignore=N  subtract N from the count (minimum 1)
 *
 * Exit: 0 = success
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define VERSION "1.0"

int main(int argc, char *argv[]) {
    int all    = 0;
    int ignore = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--version")) { printf("nproc %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: nproc [--all] [--ignore=N]\n\n"
                "Print number of available processing units.\n\n"
                "  --all       print total installed processors\n"
                "  --ignore=N  subtract N from the result\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
        if (!strcmp(a, "--all")) { all = 1; continue; }
        if (!strncmp(a, "--ignore=", 9)) { ignore = atoi(a + 9); continue; }
        fprintf(stderr, "nproc: invalid option '%s'\n", a);
        return 1;
    }

    int count;
    if (all) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        count = (int)si.dwNumberOfProcessors;
    } else {
        /* Available to current process via GetActiveProcessorCount */
        DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        count = (n > 0) ? (int)n : 1;
    }

    count -= ignore;
    if (count < 1) count = 1;

    printf("%d\n", count);
    return 0;
}
