#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    const char *path = (argc > 1) ? argv[1] : ".";
    struct stat pathStat;

    if (stat(path, &pathStat) != 0) {
        perror("ls");
        return 1;
    }

    if (S_ISREG(pathStat.st_mode)) {
        // Just print the file name if it’s a single file
        printf("%s\n", path);
        return 0;
    }

    if (!S_ISDIR(pathStat.st_mode)) {
        fprintf(stderr, "ls: not a file or directory: %s\n", path);
        return 1;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        perror("ls");
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        printf("%s  ", entry->d_name);
    }
    printf("\n");
    closedir(dir);
    return 0;
}
