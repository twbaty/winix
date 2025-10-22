#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: chown <user> <file>\n");
        return 1;
    }

    const char *user = argv[1];
    const char *file = argv[2];

#ifdef _WIN32
    printf("chown: changing owner of '%s' to '%s' (stubbed — no effect on Windows)\n",
           file, user);
    return 0;
#else
    // future POSIX-compatible logic
    struct passwd *pwd = getpwnam(user);
    if (!pwd) {
        fprintf(stderr, "chown: invalid user '%s'\n", user);
        return 1;
    }
    if (chown(file, pwd->pw_uid, -1) != 0) {
        perror("chown");
        return 1;
    }
    return 0;
#endif
}
