#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int opt_count = 0;   /* -c: prefix lines with occurrence count */
static int opt_dup   = 0;   /* -d: only print duplicate lines */
static int opt_uniq  = 0;   /* -u: only print non-duplicate lines */

int main(int argc, char *argv[]) {
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (const char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'c') opt_count = 1;
            else if (*p == 'd') opt_dup   = 1;
            else if (*p == 'u') opt_uniq  = 1;
            else {
                fprintf(stderr, "uniq: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
        argi++;
    }

    FILE *in  = stdin;
    FILE *out = stdout;

    if (argi < argc) {
        in = fopen(argv[argi++], "r");
        if (!in) { perror("uniq"); return 1; }
    }
    if (argi < argc) {
        out = fopen(argv[argi], "w");
        if (!out) { perror("uniq"); fclose(in); return 1; }
    }

    char prev[4096] = "";
    char line[4096];
    int  count = 0;
    int  have_prev = 0;

    while (fgets(line, sizeof(line), in)) {
        /* Strip trailing newline for comparison */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';

        if (have_prev && strcmp(line, prev) == 0) {
            count++;
        } else {
            /* Flush previous group */
            if (have_prev) {
                int emit = (!opt_dup && !opt_uniq)
                           || (opt_dup  && count > 1)
                           || (opt_uniq && count == 1);
                if (emit) {
                    if (opt_count)
                        fprintf(out, "%7d %s\n", count, prev);
                    else
                        fprintf(out, "%s\n", prev);
                }
            }
            strncpy(prev, line, sizeof(prev) - 1);
            prev[sizeof(prev) - 1] = '\0';
            count = 1;
            have_prev = 1;
        }
    }

    /* Flush last group */
    if (have_prev) {
        int emit = (!opt_dup && !opt_uniq)
                   || (opt_dup  && count > 1)
                   || (opt_uniq && count == 1);
        if (emit) {
            if (opt_count)
                fprintf(out, "%7d %s\n", count, prev);
            else
                fprintf(out, "%s\n", prev);
        }
    }

    if (in  != stdin)  fclose(in);
    if (out != stdout) fclose(out);
    return 0;
}
