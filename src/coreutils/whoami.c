#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

int main(void) {
#ifdef _WIN32
    char name[256];
    DWORD size = sizeof(name);
    if (GetUserNameA(name, &size))
        printf("%s\n", name);
    else
        perror("whoami");
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw)
        printf("%s\n", pw->pw_name);
    else
        perror("whoami");
#endif
    return 0;
}
