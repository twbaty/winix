#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
#else
  extern char **environ;
#endif

static void usage(void) {
    puts("Usage: env [OPTION]... [NAME=VALUE]... [COMMAND [ARG]...]");
    puts("Print environment variables, or run COMMAND with a modified environment.");
    puts("With no arguments, print the current environment.");
    puts("");
    puts("  -h, --help     display this help and exit");
    puts("      --version  output version information and exit");
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(); return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("env 1.0 (Winix)"); return 0;
    }

#ifdef _WIN32
    // Windows provides environment variables as a contiguous block
    LPCH env = GetEnvironmentStringsA();  // returns double-NUL-terminated block
    if (!env) return 1;

    // Iterate through the block until the final '\0\0'
    for (LPCH p = env; *p; ) {
        puts(p);           // print "NAME=VALUE"
        while (*p) ++p;    // advance to the end of this string
        ++p;               // advance to the start of the next
    }

    FreeEnvironmentStringsA(env);
    return 0;
#else
    for (char **p = environ; *p; ++p)
        puts(*p);
    return 0;
#endif
}
