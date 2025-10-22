#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define GETCWD _getcwd
#else
#include <unistd.h>
#define GETCWD getcwd
#endif

int main(void) {
    char buffer[1024];
    if (GETCWD(buffer, sizeof(buffer)) != NULL) {
        printf("%s\n", buffer);
        return 0;
    } else {
        perror("pwd");
        return 1;
    }
}
