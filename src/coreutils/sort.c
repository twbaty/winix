#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX 65536

static bool reverse_sort   = false;
static bool unique_sort    = false;
static bool ignore_case    = false;

static int cmp(const void *a, const void *b) {
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    int r = ignore_case ? _stricmp(sa, sb) : strcmp(sa, sb);
    return reverse_sort ? -r : r;
}

static int sort_stream(FILE *f, char **lines, int *count) {
    char buf[4096];
    while (fgets(buf, sizeof(buf), f) && *count < MAX) {
        lines[(*count)++] = strdup(buf);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int argi = 1;

    /* Honour the shell's case setting: WINIX_CASE=off → case-insensitive
     * by default (explicit -f is still accepted and is a no-op in that state). */
    {
        const char *wcase = getenv("WINIX_CASE");
        if (wcase && strcmp(wcase, "off") == 0)
            ignore_case = true;
    }

    for (; argi < argc && argv[argi][0] == '-'; argi++) {
        for (char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'r') reverse_sort = true;
            else if (*p == 'u') unique_sort  = true;
            else if (*p == 'f') ignore_case  = true;
            else {
                fprintf(stderr, "sort: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
    }

    char **lines = malloc(MAX * sizeof(char *));
    if (!lines) { fprintf(stderr, "sort: out of memory\n"); return 1; }
    int n = 0;

    int ret = 0;
    if (argi >= argc) {
        sort_stream(stdin, lines, &n);
    } else {
        for (int i = argi; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) { fprintf(stderr, "sort: %s: %s\n", argv[i], strerror(errno)); ret = 1; continue; }
            sort_stream(f, lines, &n);
            fclose(f);
        }
    }

    qsort(lines, n, sizeof(char *), cmp);

    // Print, skipping adjacent duplicates when -u is active.
    // Keep freed lines separate from the comparison pointer.
    char *last = NULL;
    for (int i = 0; i < n; i++) {
        if (!unique_sort || last == NULL ||
            (ignore_case ? _stricmp(lines[i], last) : strcmp(lines[i], last)) != 0) {
            fputs(lines[i], stdout);
            last = lines[i];
        }
    }
    for (int i = 0; i < n; i++) free(lines[i]);

    free(lines);
    return ret;
}
