#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    if (argc != 2) { fprintf(stderr,"Usage: stat <file>\n"); return 1; }
    struct stat s;
    if (stat(argv[1], &s) != 0) { perror("stat"); return 1; }
    printf("Size: %lld bytes\nModified: %lld\n", (long long)s.st_size, (long long)s.st_mtime);
    return 0;
}
