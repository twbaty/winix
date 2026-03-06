/*
 * apropos — search command descriptions by keyword
 *
 * Usage:
 *   apropos <keyword> [keyword ...]
 *   -e  --exact       whole-word match instead of substring
 *   -s  --sensitive   case-sensitive search (default: case-insensitive)
 *       --version
 *       --help
 *
 * Searches the built-in command description table and prints every
 * command whose name or description contains the keyword.
 * Multiple keywords are ORed together.
 *
 * Exit codes:  0 = at least one match
 *              1 = no matches
 *              2 = usage error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define APROPOS_VERSION "1.0"

/* ── Built-in command table ──────────────────────────────────── */

typedef struct {
    const char *name;
    const char *desc;
} Cmd;

static const Cmd CMDS[] = {
    { "alias",     "define or display command aliases" },
    { "apropos",   "search command descriptions by keyword" },
    { "awk",       "pattern-action text processing language" },
    { "base64",    "encode or decode data in Base64 format" },
    { "basename",  "strip directory and suffix from a filename" },
    { "bc",        "arbitrary-precision calculator" },
    { "cat",       "concatenate files and print to standard output" },
    { "chmod",     "change file permissions (read-only/writable)" },
    { "chown",     "change file owner and group" },
    { "clear",     "clear the terminal screen" },
    { "cmp",       "compare two files byte by byte" },
    { "column",    "format output into aligned columns" },
    { "comm",      "compare two sorted files line by line" },
    { "cp",        "copy files and directories" },
    { "cut",       "extract fields or characters from each line" },
    { "date",      "print or set the system date and time" },
    { "df",        "report disk space usage of file systems" },
    { "diff",      "compare files line by line (LCS-based)" },
    { "dirname",   "strip the last component from a filename" },
    { "du",        "estimate file and directory disk usage" },
    { "echo",      "display a line of text" },
    { "env",       "print or set the environment" },
    { "expand",    "convert tabs to spaces" },
    { "export",    "mark shell variables for export to child processes" },
    { "false",     "return a failing exit status" },
    { "find",      "search for files in a directory hierarchy" },
    { "fold",      "wrap long lines at a given width" },
    { "grep",      "print lines that match a pattern" },
    { "head",      "output the first lines of a file" },
    { "hexdump",   "display file contents in hexadecimal" },
    { "history",   "display or manipulate command history" },
    { "hostname",  "print or set the system hostname" },
    { "id",        "print user and group identity" },
    { "kill",      "send a signal to a process" },
    { "less",      "view file contents one screen at a time (backward scroll)" },
    { "ln",        "create hard or symbolic links" },
    { "ls",        "list directory contents" },
    { "md5sum",    "compute and verify MD5 message digests" },
    { "mkdir",     "create directories" },
    { "mktemp",    "create temporary files or directories" },
    { "more",      "view file contents one screen at a time" },
    { "mv",        "move or rename files and directories" },
    { "nix",       "terminal text editor (nano-style)" },
    { "nl",        "number lines of a file" },
    { "paste",     "merge lines of files side by side" },
    { "printf",    "format and print data" },
    { "ps",        "report running processes" },
    { "pwd",       "print the current working directory" },
    { "realpath",  "print the resolved absolute path of a file" },
    { "rev",       "reverse the characters of each line" },
    { "rm",        "remove files or directories" },
    { "rmdir",     "remove empty directories" },
    { "sed",       "stream editor for filtering and transforming text" },
    { "seq",       "print a sequence of numbers" },
    { "sha256sum", "compute and verify SHA-256 message digests" },
    { "shuf",      "shuffle lines of a file randomly" },
    { "sleep",     "pause execution for a specified duration" },
    { "sort",      "sort lines of text" },
    { "stat",      "display file or filesystem status" },
    { "tac",       "print lines of a file in reverse order" },
    { "tail",      "output the last lines of a file" },
    { "tee",       "read from stdin and write to stdout and files" },
    { "test",      "evaluate a conditional expression" },
    { "[",         "evaluate a conditional expression (alias for test)" },
    { "time",      "time a command and report elapsed time" },
    { "timeout",   "run a command with a time limit" },
    { "touch",     "update file timestamps or create empty files" },
    { "tr",        "translate, delete, or squeeze characters" },
    { "true",      "return a successful exit status" },
    { "uname",     "print system information" },
    { "unexpand",  "convert spaces to tabs" },
    { "uniq",      "filter or report adjacent duplicate lines" },
    { "uptime",    "show how long the system has been running" },
    { "ver",       "display Winix shell version" },
    { "watch",     "run a command repeatedly at a fixed interval" },
    { "wc",        "count lines, words, and characters in files" },
    { "which",     "locate a command in PATH" },
    { "whoami",    "print the current user name" },
    { "wlint",     "filesystem lint detector: duplicates, empty files/dirs" },
    { "wsim",      "similarity scorer for wlint scan inventories" },
    { "xargs",     "build and execute commands from standard input" },
    { "yes",       "output a string repeatedly until killed" },
    { NULL, NULL }
};

/* ── Search helpers ──────────────────────────────────────────── */

/* Case-insensitive strstr */
static const char *ci_strstr(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            size_t i;
            for (i = 0; i < nlen; i++) {
                if (tolower((unsigned char)haystack[i]) !=
                    tolower((unsigned char)needle[i])) break;
            }
            if (i == nlen) return haystack;
        }
    }
    return NULL;
}

static int is_word_boundary(char c) {
    return c == '\0' || !isalnum((unsigned char)c);
}

/* Whole-word case-insensitive match */
static int ci_whole_word(const char *text, const char *word) {
    size_t wlen = strlen(word);
    const char *p = text;
    while ((p = ci_strstr(p, word)) != NULL) {
        char before = (p == text) ? '\0' : p[-1];
        char after  = p[wlen];
        if (is_word_boundary(before) && is_word_boundary(after))
            return 1;
        p++;
    }
    return 0;
}

/* Case-sensitive whole-word */
static int cs_whole_word(const char *text, const char *word) {
    size_t wlen = strlen(word);
    const char *p = text;
    while ((p = strstr(p, word)) != NULL) {
        char before = (p == text) ? '\0' : p[-1];
        char after  = p[wlen];
        if (is_word_boundary(before) && is_word_boundary(after))
            return 1;
        p++;
    }
    return 0;
}

static int matches(const char *text, const char *kw, int exact, int sensitive) {
    if (exact) {
        return sensitive ? cs_whole_word(text, kw)
                        : ci_whole_word(text, kw);
    } else {
        return sensitive ? (strstr(text, kw)    != NULL)
                        : (ci_strstr(text, kw) != NULL);
    }
}

/* ── Usage ───────────────────────────────────────────────────── */

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] <keyword> [keyword ...]\n\n"
        "Search command descriptions by keyword.\n\n"
        "Options:\n"
        "  -e  --exact      whole-word match (default: substring)\n"
        "  -s  --sensitive  case-sensitive search (default: insensitive)\n"
        "      --version    show version\n"
        "      --help       show this help\n\n"
        "Multiple keywords are ORed: any match prints the command.\n\n"
        "Exit: 0=matches found  1=no matches  2=error\n",
        prog);
}

/* ── main ────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    int exact = 0, sensitive = 0;
    const char **keywords = NULL;
    int nkw = 0;

    if (argc < 2) { usage(argv[0]); return 2; }

    keywords = (const char **)malloc((size_t)argc * sizeof(char *));
    if (!keywords) return 2;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--version")) {
            printf("apropos %s (Winix)\n", APROPOS_VERSION);
            free(keywords); return 0;
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(argv[0]); free(keywords); return 0;
        } else if (!strcmp(a, "--exact")     || !strcmp(a, "-e")) {
            exact = 1;
        } else if (!strcmp(a, "--sensitive") || !strcmp(a, "-s")) {
            sensitive = 1;
        } else if (a[0] == '-') {
            fprintf(stderr, "apropos: invalid option -- '%s'\n", a);
            free(keywords); return 2;
        } else {
            keywords[nkw++] = a;
        }
    }

    if (nkw == 0) {
        fprintf(stderr, "apropos: no keyword specified\n");
        usage(argv[0]); free(keywords); return 2;
    }

    /* Find the longest command name for alignment */
    int max_name = 0;
    for (int i = 0; CMDS[i].name; i++) {
        int n = (int)strlen(CMDS[i].name);
        if (n > max_name) max_name = n;
    }
    if (max_name < 6) max_name = 6;

    int found = 0;
    for (int i = 0; CMDS[i].name; i++) {
        int hit = 0;
        for (int k = 0; k < nkw && !hit; k++) {
            if (matches(CMDS[i].name, keywords[k], exact, sensitive) ||
                matches(CMDS[i].desc, keywords[k], exact, sensitive))
                hit = 1;
        }
        if (hit) {
            printf("%-*s  - %s\n", max_name, CMDS[i].name, CMDS[i].desc);
            found++;
        }
    }

    free(keywords);
    return found > 0 ? 0 : 1;
}
