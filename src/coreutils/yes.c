/*
 * yes.c — Winix coreutil
 *
 * Usage: yes [STRING]
 *
 * Repeatedly print STRING (default "y") followed by a newline until killed
 * or a write fails (SIGPIPE / broken pipe).
 *
 * Options:
 *   --help     Print usage and exit 0
 *   --version  Print version and exit 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define BUF_SIZE 8192

static void usage(void) {
    puts("Usage: yes [STRING]");
    puts("Repeatedly output STRING (default 'y') until killed.");
    puts("");
    puts("  --help     display this help and exit");
    puts("  --version  output version information and exit");
}

int main(int argc, char *argv[]) {
    /* Option parsing — only --help and --version before the string arg */
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(); return 0; }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("yes 1.0 (Winix 1.0)");
        return 0;
    }

    const char *word = (argc > 1) ? argv[1] : "y";

    /* Build a buffer filled with repeated "word\n" */
    size_t wlen = strlen(word);
    size_t line_len = wlen + 1; /* word + '\n' */

    /* Fill a buffer with as many repetitions as fit */
    static char buf[BUF_SIZE];
    size_t buf_used = 0;

    /* Fill buffer; if the line is longer than BUF_SIZE, just use one copy */
    if (line_len >= BUF_SIZE) {
        /* Single-shot: word might be huge, stream it directly */
        while (1) {
            if (fputs(word, stdout) == EOF) return 0;
            if (putchar('\n') == EOF) return 0;
            if (fflush(stdout) != 0) return 0;
        }
    }

    /* Pack repetitions into buffer */
    while (buf_used + line_len <= BUF_SIZE) {
        memcpy(buf + buf_used, word, wlen);
        buf[buf_used + wlen] = '\n';
        buf_used += line_len;
    }

    /* Write buffer in a tight loop; exit 0 on any write failure (SIGPIPE) */
#ifdef _WIN32
    /* Windows doesn't have SIGPIPE; fwrite returning 0 means broken pipe */
#else
    /* Ignore SIGPIPE — let fwrite return an error instead of crashing */
    signal(SIGPIPE, SIG_IGN);
#endif

    while (1) {
        size_t written = fwrite(buf, 1, buf_used, stdout);
        if (written < buf_used) {
            /* Write failed — broken pipe or closed consumer; exit cleanly */
            return 0;
        }
    }

    /* unreachable */
    return 0;
}
