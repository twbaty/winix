#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 1024
#define MAX_LEN 1024

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: tail <file>\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }

    char *lines[MAX_LINES];
    int count = 0;
    char buffer[MAX_LEN];

    while (fgets(buffer, sizeof(buffer), f)) {
        lines[count % MAX_LINES] = strdup(buffer);
        count++;
    }
    fclose(f);

    int start = count > 10 ? count - 10 : 0;
    for (int i = start; i < count; i++)
        fputs(lines[i % MAX_LINES], stdout);

    for (int i = 0; i < (count < MAX_LINES ? count : MAX_LINES); i++)
        free(lines[i % MAX_LINES]);

    return 0;
}
