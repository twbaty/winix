#include <stdio.h>
#include <string.h>

static int grep_stream(const char* pat, FILE* f) {
    char buf[4096];
    int rc = 1; /* 0 if any match printed */
    while (fgets(buf, sizeof buf, f)) {
        if (strstr(buf, pat)) { fputs(buf, stdout); rc = 0; }
    }
    return rc;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: grep <pattern> [file...]\n");
        return 2;
    }
    const char* pat = argv[1];
    if (argc == 2) {
        return grep_stream(pat, stdin);
    }
    int rc = 1;
    for (int i = 2; i < argc; ++i) {
        const char* path = argv[i];
        if (strcmp(path, "-") == 0) {
            int r = grep_stream(pat, stdin);
            if (r == 0) rc = 0;
            continue;
        }
        FILE* f = fopen(path, "rb");
        if (!f) { fprintf(stderr, "grep: cannot open %s\n", path); continue; }
        int r = grep_stream(pat, f);
        if (r == 0) rc = 0;
        fclose(f);
    }
    return rc;
}
