#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    const char *path = (argc > 1) ? argv[1] : ".";

    struct stat st;
    if (stat(path, &st) != 0) {
        perror("ls");
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            perror("ls");
            return 1;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] != '.')
                printf("%s  ", entry->d_name);
        }
        printf("\n");
        closedir(dir);
    } else {
        // File or something else — print the filename itself
        printf("%s\n", path);
    }

    return 0;
}
