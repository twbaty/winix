/*
 * tee — duplicate stdin to stdout and file(s)
 *
 * Usage: tee [-ai] [--version] [--help] [FILE ...]
 *   -a  append to files instead of overwriting
 *   -i  ignore SIGINT (no-op on Windows; accepted for compatibility)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "1.0"
#define BUFSZ   65536

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [-ai] [FILE ...]\n\n"
        "Copy stdin to stdout and to each FILE.\n\n"
        "  -a  append to files instead of overwriting\n"
        "  -i  ignore SIGINT (accepted for compatibility)\n"
        "      --version\n"
        "      --help\n",
        prog);
}

int main(int argc, char *argv[]) {
    int append = 0;
    int argi   = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("tee %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help"))    { usage(argv[0]); return 0; }
        if (!strcmp(a, "--"))        { argi++; break; }
        for (const char *p = a + 1; *p; p++) {
            if      (*p == 'a') append = 1;
            else if (*p == 'i') { /* ignore interrupts — no-op */ }
            else {
                fprintf(stderr, "tee: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
    }

    int    nfiles = argc - argi;
    FILE **fps    = NULL;
    int    ret    = 0;

    if (nfiles > 0) {
        fps = (FILE **)malloc((size_t)nfiles * sizeof(FILE *));
        if (!fps) { fprintf(stderr, "tee: out of memory\n"); return 1; }
        for (int i = 0; i < nfiles; i++) {
            fps[i] = fopen(argv[argi + i], append ? "ab" : "wb");
            if (!fps[i]) {
                perror(argv[argi + i]);
                for (int j = 0; j < i; j++) fclose(fps[j]);
                free(fps);
                return 1;
            }
        }
    }

    char  *buf = (char *)malloc(BUFSZ);
    if (!buf) { fprintf(stderr, "tee: out of memory\n"); ret = 1; goto done; }

    size_t n;
    while ((n = fread(buf, 1, BUFSZ, stdin)) > 0) {
        if (fwrite(buf, 1, n, stdout) != n) { ret = 1; break; }
        for (int i = 0; i < nfiles; i++)
            if (fwrite(buf, 1, n, fps[i]) != n) ret = 1;
    }
    free(buf);

done:
    for (int i = 0; i < nfiles; i++) fclose(fps[i]);
    free(fps);
    return ret;
}
