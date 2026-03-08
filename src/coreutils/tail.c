/*
 * tail — output the last part of files
 *
 * Usage: tail [OPTIONS] [FILE...]
 *   -n N      output last N lines (default 10); +N = from line N
 *   -c N      output last N bytes; +N = from byte N
 *   -f        follow: keep reading as file grows (poll every 250ms)
 *   -F        follow by name (reopen if file rotated) — implies -f
 *   -q        suppress filename headers
 *   -v        always print filename headers
 *   --version / --help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <windows.h>

/* WINIX_VERSION injected by CMake */
#ifndef WINIX_VERSION
#define WINIX_VERSION "unknown"
#endif

#define LINE_LEN 4096

static int   opt_lines   = 10;
static long  opt_bytes   = -1;    /* -1 = use lines mode */
static bool  from_start  = false; /* +N prefix on -n/-c */
static bool  follow      = false; /* -f */
static bool  follow_name = false; /* -F */
static bool  quiet       = false; /* -q */
static bool  verbose     = false; /* -v */

/* ── tail last N lines ──────────────────────────────────────── */

static void tail_lines(FILE *f, int n) {
    char **ring = malloc((size_t)n * sizeof(char *));
    if (!ring) { fprintf(stderr, "tail: out of memory\n"); return; }
    for (int i = 0; i < n; i++) ring[i] = NULL;

    char buf[LINE_LEN];
    int count = 0;
    while (fgets(buf, sizeof(buf), f)) {
        int slot = count % n;
        free(ring[slot]);
        ring[slot] = strdup(buf);
        if (!ring[slot]) { fprintf(stderr, "tail: out of memory\n"); break; }
        count++;
    }

    int start = count > n ? count - n : 0;
    for (int i = start; i < count; i++) {
        char *line = ring[i % n];
        if (line) fputs(line, stdout);
    }
    for (int i = 0; i < n; i++) free(ring[i]);
    free(ring);
}

/* ── tail from line N (1-based) ─────────────────────────────── */

static void tail_from_line(FILE *f, int start_line) {
    char buf[LINE_LEN];
    int lineno = 0;
    while (fgets(buf, sizeof(buf), f)) {
        lineno++;
        if (lineno >= start_line)
            fputs(buf, stdout);
    }
}

/* ── tail last N bytes ──────────────────────────────────────── */

static void tail_bytes(FILE *f, long n) {
    /* ring buffer approach — works for both regular files and pipes */
    char *ring = malloc((size_t)n);
    if (!ring) { fprintf(stderr, "tail: out of memory\n"); return; }
    long pos = 0, total = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        ring[pos % n] = (char)c;
        pos++;
        total++;
    }
    long count = total < n ? total : n;
    long start = total < n ? 0 : pos % n;
    for (long i = 0; i < count; i++)
        putchar((unsigned char)ring[(start + i) % n]);
    free(ring);
}

/* ── tail from byte N ───────────────────────────────────────── */

static void tail_from_byte(FILE *f, long start) {
    if (fseek(f, start - 1, SEEK_SET) == 0) {
        int c;
        while ((c = fgetc(f)) != EOF) putchar(c);
    }
}

/* ── follow mode (-f) ───────────────────────────────────────── */

static void follow_file(const char *path, FILE *f) {
    char buf[LINE_LEN];
    for (;;) {
        while (fgets(buf, sizeof(buf), f))
            fputs(buf, stdout);
        fflush(stdout);

        if (follow_name) {
            /* check if file was rotated (new inode / smaller size) */
            FILE *f2 = fopen(path, "r");
            if (f2) {
                /* get current position in original */
                long pos = ftell(f);
                fseek(f2, 0, SEEK_END);
                long newsize = ftell(f2);
                if (newsize < pos) {
                    /* file was rotated/truncated — reopen */
                    fclose(f);
                    f = f2;
                    rewind(f);
                } else {
                    fclose(f2);
                }
            }
        }

        Sleep(250); /* poll every 250ms */
    }
}

/* ── main ───────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-' && argv[argi][1]; argi++) {
        if (strcmp(argv[argi], "--") == 0) { argi++; break; }

        if (strcmp(argv[argi], "--version") == 0) {
            printf("tail (Winix) %s\n", WINIX_VERSION); return 0;
        }
        if (strcmp(argv[argi], "--follow") == 0 ||
            strcmp(argv[argi], "--follow=descriptor") == 0) {
            follow = true; continue;
        }
        if (strcmp(argv[argi], "--follow=name") == 0) {
            follow = follow_name = true; continue;
        }
        if (strcmp(argv[argi], "--help") == 0) {
            fprintf(stderr,
                "Usage: tail [OPTIONS] [FILE...]\n\n"
                "  -n N   output last N lines (default 10)\n"
                "  -n +N  output from line N\n"
                "  -c N   output last N bytes\n"
                "  -c +N  output from byte N\n"
                "  -f     follow: keep reading as file grows\n"
                "  -F     follow by name (handles log rotation)\n"
                "  -q     suppress filename headers\n"
                "  -v     always show filename headers\n"
                "  --version / --help\n");
            return 0;
        }

        /* -n N or -n +N */
        if (argv[argi][1] == 'n') {
            const char *val = argv[argi][2] ? argv[argi] + 2
                            : (argi + 1 < argc ? argv[++argi] : NULL);
            if (!val) { fprintf(stderr, "tail: -n requires argument\n"); return 1; }
            if (*val == '+') { from_start = true; val++; }
            opt_lines = atoi(val);
            if (opt_lines <= 0 && !from_start) {
                fprintf(stderr, "tail: invalid line count\n"); return 1;
            }
            continue;
        }

        /* -c N or -c +N */
        if (argv[argi][1] == 'c') {
            const char *val = argv[argi][2] ? argv[argi] + 2
                            : (argi + 1 < argc ? argv[++argi] : NULL);
            if (!val) { fprintf(stderr, "tail: -c requires argument\n"); return 1; }
            if (*val == '+') { from_start = true; val++; }
            opt_bytes = atol(val);
            continue;
        }

        for (const char *p = argv[argi] + 1; *p; p++) {
            switch (*p) {
                case 'f': follow      = true; break;
                case 'F': follow = follow_name = true; break;
                case 'q': quiet       = true; break;
                case 'v': verbose     = true; break;
                default:
                    fprintf(stderr, "tail: invalid option -- '%c'\n", *p);
                    return 1;
            }
        }
    }

    int ret = 0;
    int nfiles = argc - argi;
    bool show_header = verbose || (nfiles > 1 && !quiet);

    if (nfiles == 0) {
        /* stdin */
        if (opt_bytes >= 0) {
            from_start ? tail_from_byte(stdin, opt_bytes)
                       : tail_bytes(stdin, opt_bytes);
        } else {
            from_start ? tail_from_line(stdin, opt_lines)
                       : tail_lines(stdin, opt_lines);
        }
        if (follow) follow_file(NULL, stdin);
        return 0;
    }

    for (int i = argi; i < argc; i++) {
        const char *path = argv[i];
        FILE *f = fopen(path, "r");
        if (!f) {
            fprintf(stderr, "tail: %s: %s\n", path, strerror(errno));
            ret = 1;
            continue;
        }

        if (show_header) {
            if (i > argi) putchar('\n');
            printf("==> %s <==\n", path);
        }

        if (opt_bytes >= 0) {
            from_start ? tail_from_byte(f, opt_bytes)
                       : tail_bytes(f, opt_bytes);
        } else {
            from_start ? tail_from_line(f, opt_lines)
                       : tail_lines(f, opt_lines);
        }
        fflush(stdout);

        if (follow && i == argc - 1) {
            /* only follow the last file (GNU behaviour) */
            follow_file(path, f);
        }

        fclose(f);
    }

    return ret;
}
