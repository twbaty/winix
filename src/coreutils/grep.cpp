/*
 * grep — search for patterns in files
 *
 * Usage: grep [OPTION]... PATTERN [FILE]...
 *   -E   extended regexp (ERE)
 *   -F   fixed string
 *   -G   basic regexp (default; BRE approximated via ECMAScript + conversions)
 *   -i   ignore case
 *   -v   invert match
 *   -c   count only
 *   -n   line numbers
 *   -l   files with matches
 *   -o   only matching
 *   -q   quiet
 *   -r/-R recursive
 *   -w   word match
 *   -x   line match
 *   -m N max matches per file
 *   --color=auto|always|never
 *   --version / --help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <regex>
#include <string>

#ifndef WINIX_VERSION
#define WINIX_VERSION "unknown"
#endif

#define ANSI_RED   "\x1b[31m"
#define ANSI_RESET "\x1b[0m"
#define LINE_BUF   8192

/* ------------------------------------------------------------------ */
/*  Options                                                             */
/* ------------------------------------------------------------------ */
static bool use_color        = false;
static bool case_insensitive = false;
static bool invert           = false;
static bool count_only       = false;
static bool line_numbers     = false;
static bool files_only       = false;
static bool recursive        = false;
static bool quiet            = false;
static bool fixed_strings    = false;
static bool extended_re      = false;  /* -E */
static bool word_match       = false;
static bool line_match       = false;
static bool only_matching    = false;
static long max_matches      = 0;

static int matched_any = 0;

/* compiled regex */
static std::regex g_re;
static bool       g_re_compiled = false;

/* ------------------------------------------------------------------ */
/*  BRE → ECMAScript minimal conversion                               */
/*  Handles the most common BRE constructs that differ from ERE:       */
/*    \( \)  →  ( )                                                    */
/*    \{ \}  →  { }                                                    */
/*    \+ \?  →  + ?                                                    */
/* ------------------------------------------------------------------ */
static std::string bre_to_ecma(const std::string &pat) {
    std::string out;
    out.reserve(pat.size() * 2);
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            char next = pat[i + 1];
            if (next == '(' || next == ')' ||
                next == '{' || next == '}' ||
                next == '+' || next == '?') {
                out += next;   /* remove the backslash */
                i++;
                continue;
            }
        }
        out += pat[i];
    }
    return out;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */
static inline bool is_word_char(unsigned char c) {
    return isalnum(c) || c == '_';
}

static bool word_ok(const std::string &line, size_t start, size_t mlen) {
    if (start > 0 && is_word_char((unsigned char)line[start - 1]))
        return false;
    size_t end = start + mlen;
    if (end < line.size() && is_word_char((unsigned char)line[end]))
        return false;
    return true;
}

/* Case-insensitive strstr */
static const char *stristr(const char *hay, const char *needle) {
    if (!*needle) return hay;
    size_t nlen = strlen(needle);
    for (; *hay; hay++) {
        if (tolower((unsigned char)*hay) == tolower((unsigned char)*needle)) {
            bool ok = true;
            for (size_t i = 1; i < nlen; i++) {
                if (tolower((unsigned char)hay[i]) !=
                    tolower((unsigned char)needle[i])) { ok = false; break; }
            }
            if (ok) return hay;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Print colored line (regex mode)                                    */
/* ------------------------------------------------------------------ */
static void print_line_colored(const std::string &line) {
    size_t pos = 0;
    std::regex_constants::match_flag_type flags =
        std::regex_constants::match_default;
    while (pos <= line.size()) {
        std::smatch m;
        std::string sub = line.substr(pos);
        if (!std::regex_search(sub, m, g_re) || m.length() == 0) {
            fwrite(line.c_str() + pos, 1, line.size() - pos, stdout);
            break;
        }
        fwrite(line.c_str() + pos, 1, (size_t)m.position(), stdout);
        fputs(ANSI_RED, stdout);
        fwrite(line.c_str() + pos + m.position(), 1, (size_t)m.length(), stdout);
        fputs(ANSI_RESET, stdout);
        pos += (size_t)m.position() + (size_t)m.length();
        (void)flags;
    }
}

/* ------------------------------------------------------------------ */
/*  grep one open stream                                               */
/* ------------------------------------------------------------------ */
static void grep_stream(const char *pattern, FILE *fp,
                        const char *filename, bool show_filename)
{
    char  buf[LINE_BUF];
    long  lineno      = 0;
    long  match_count = 0;
    long  file_hits   = 0;
    size_t patlen     = strlen(pattern);

    while (fgets(buf, sizeof(buf), fp)) {
        lineno++;
        std::string raw(buf);

        /* strip trailing \r\n for matching; keep for output */
        std::string line = raw;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();

        bool matched = false;
        size_t m_start = 0, m_len = 0;

        if (fixed_strings) {
            const char *p = case_insensitive
                ? stristr(line.c_str(), pattern)
                : strstr(line.c_str(), pattern);
            if (p) {
                m_start = (size_t)(p - line.c_str());
                m_len   = patlen;
                matched = true;
                if (word_match && !word_ok(line, m_start, m_len)) matched = false;
                if (line_match && (m_start != 0 || m_len != line.size())) matched = false;
            }
        } else {
            std::smatch m;
            if (std::regex_search(line, m, g_re)) {
                m_start = (size_t)m.position();
                m_len   = (size_t)m.length();
                matched = true;
                if (word_match && !word_ok(line, m_start, m_len)) matched = false;
                if (line_match && (m_start != 0 || m_len != line.size())) matched = false;
            }
        }

        if (invert) matched = !matched;
        if (!matched) continue;

        matched_any = 1;
        match_count++;
        file_hits++;

        if (quiet) {
            if (max_matches > 0 && file_hits >= max_matches) return;
            continue;
        }
        if (files_only) {
            printf("%s\n", filename ? filename : "(stdin)");
            return;
        }
        if (count_only) {
            if (max_matches > 0 && file_hits >= max_matches) break;
            continue;
        }

        /* -o: print only matching portion(s) */
        if (only_matching && !invert) {
            if (fixed_strings) {
                if (show_filename && filename) printf("%s:", filename);
                if (line_numbers)              printf("%ld:", lineno);
                bool color = use_color && _isatty(_fileno(stdout));
                if (color) printf(ANSI_RED "%.*s" ANSI_RESET "\n", (int)patlen, line.c_str() + m_start);
                else       printf("%.*s\n", (int)patlen, line.c_str() + m_start);
            } else {
                std::string tail = line;
                bool first = true;
                while (!tail.empty()) {
                    std::smatch m;
                    if (!std::regex_search(tail, m, g_re) || m.length() == 0) break;
                    size_t abs_so = line.size() - tail.size() + m.position();
                    size_t mlen2  = m.length();
                    bool ok = true;
                    if (word_match && !word_ok(line, abs_so, mlen2)) ok = false;
                    if (line_match && (abs_so != 0 || abs_so + mlen2 != line.size())) ok = false;
                    if (ok) {
                        if (show_filename && filename) printf("%s:", filename);
                        if (line_numbers)              printf("%ld:", lineno);
                        bool color = use_color && _isatty(_fileno(stdout));
                        if (color) printf(ANSI_RED "%.*s" ANSI_RESET "\n", (int)mlen2, line.c_str() + abs_so);
                        else       printf("%.*s\n", (int)mlen2, line.c_str() + abs_so);
                    }
                    tail = tail.substr(m.position() + m.length());
                    (void)first; first = false;
                }
            }
        } else {
            /* Normal output */
            if (show_filename && filename) printf("%s:", filename);
            if (line_numbers)              printf("%ld:", lineno);

            bool color = !invert && use_color && _isatty(_fileno(stdout)) && !fixed_strings;
            if (color && g_re_compiled) {
                print_line_colored(line);
                putchar('\n');
            } else if (!invert && use_color && _isatty(_fileno(stdout)) && fixed_strings && matched) {
                fwrite(line.c_str(), 1, m_start, stdout);
                printf(ANSI_RED "%.*s" ANSI_RESET, (int)patlen, line.c_str() + m_start);
                fputs(line.c_str() + m_start + patlen, stdout);
                putchar('\n');
            } else {
                fputs(raw.c_str(), stdout);
                if (raw.empty() || raw.back() != '\n') putchar('\n');
            }
        }

        if (max_matches > 0 && file_hits >= max_matches) break;
    }

    if (count_only) {
        if (show_filename && filename) printf("%s:", filename);
        printf("%ld\n", match_count);
    }
}

/* ------------------------------------------------------------------ */
/*  Recursive directory walk                                           */
/* ------------------------------------------------------------------ */
static void grep_path(const char *pattern, const char *path, bool show_filename);

static void grep_dir(const char *pattern, const char *dirpath) {
    DIR *d = opendir(dirpath);
    if (!d) {
        fprintf(stderr, "grep: cannot open directory '%s': %s\n", dirpath, strerror(errno));
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", dirpath, ent->d_name);
        grep_path(pattern, child, true);
    }
    closedir(d);
}

static void grep_path(const char *pattern, const char *path, bool show_filename) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "grep: cannot stat '%s': %s\n", path, strerror(errno));
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        if (recursive) grep_dir(pattern, path);
        else fprintf(stderr, "grep: '%s': Is a directory\n", path);
        return;
    }
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "grep: cannot open '%s': %s\n", path, strerror(errno));
        return;
    }
    grep_stream(pattern, fp, path, show_filename);
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    int argi = 1;

    use_color = (_isatty(_fileno(stdout)) != 0);

    {
        const char *wcase = getenv("WINIX_CASE");
        if (wcase && strcmp(wcase, "off") == 0) case_insensitive = true;
    }

    /* ---- options ---- */
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        const char *arg = argv[argi];
        if (strcmp(arg, "--") == 0) { argi++; break; }

        if (arg[1] == '-') {
            /* Long options */
            if (strcmp(arg, "--version") == 0) {
                printf("grep (Winix) %s\n", WINIX_VERSION); return 0;
            }
            if (strcmp(arg, "--help") == 0) {
                fprintf(stderr,
                    "Usage: grep [OPTION]... PATTERN [FILE]...\n\n"
                    "  -E, --extended-regexp    ERE pattern\n"
                    "  -F, --fixed-strings      Fixed string, no regex\n"
                    "  -G, --basic-regexp       BRE pattern (default)\n"
                    "  -i, --ignore-case        Ignore case\n"
                    "  -v, --invert-match       Print non-matching lines\n"
                    "  -w, --word-regexp        Match whole words\n"
                    "  -x, --line-regexp        Match whole lines\n"
                    "  -c, --count              Print count of matches\n"
                    "  -n, --line-number        Print line numbers\n"
                    "  -l, --files-with-matches Print filenames only\n"
                    "  -o, --only-matching      Print matching part only\n"
                    "  -q, --quiet              No output\n"
                    "  -m N, --max-count=N      Stop after N matches\n"
                    "  -r, -R, --recursive      Recurse into directories\n"
                    "  --color=auto|always|never\n"
                    "  --version / --help\n");
                return 0;
            }
            if (strcmp(arg, "--ignore-case") == 0)        { case_insensitive = true; argi++; continue; }
            if (strcmp(arg, "--invert-match") == 0)       { invert           = true; argi++; continue; }
            if (strcmp(arg, "--count") == 0)              { count_only       = true; argi++; continue; }
            if (strcmp(arg, "--line-number") == 0)        { line_numbers     = true; argi++; continue; }
            if (strcmp(arg, "--files-with-matches") == 0) { files_only       = true; argi++; continue; }
            if (strcmp(arg, "--recursive") == 0)          { recursive        = true; argi++; continue; }
            if (strcmp(arg, "--quiet") == 0 ||
                strcmp(arg, "--silent") == 0)             { quiet            = true; argi++; continue; }
            if (strcmp(arg, "--fixed-strings") == 0)      { fixed_strings    = true; argi++; continue; }
            if (strcmp(arg, "--extended-regexp") == 0)    { extended_re      = true; argi++; continue; }
            if (strcmp(arg, "--basic-regexp") == 0)       { extended_re      = false; argi++; continue; }
            if (strcmp(arg, "--word-regexp") == 0)        { word_match       = true; argi++; continue; }
            if (strcmp(arg, "--line-regexp") == 0)        { line_match       = true; argi++; continue; }
            if (strcmp(arg, "--only-matching") == 0)      { only_matching    = true; argi++; continue; }
            if (strncmp(arg, "--color=", 8) == 0) {
                const char *opt = arg + 8;
                if (strcmp(opt, "always") == 0) use_color = true;
                else if (strcmp(opt, "never") == 0) use_color = false;
                else use_color = (_isatty(_fileno(stdout)) != 0);
                argi++; continue;
            }
            if (strcmp(arg, "--color") == 0) { use_color = (_isatty(_fileno(stdout)) != 0); argi++; continue; }
            if (strncmp(arg, "--max-count=", 12) == 0) {
                max_matches = atol(arg + 12); argi++; continue;
            }
            fprintf(stderr, "grep: unrecognized option '%s'\n", arg);
            return 2;
        }

        /* Short options */
        bool stop = false;
        for (const char *p = arg + 1; *p && !stop; p++) {
            switch (*p) {
                case 'i': case_insensitive = true; break;
                case 'v': invert           = true; break;
                case 'c': count_only       = true; break;
                case 'n': line_numbers     = true; break;
                case 'l': files_only       = true; break;
                case 'r': recursive        = true; break;
                case 'R': recursive        = true; break;
                case 'q': quiet            = true; break;
                case 'F': fixed_strings    = true; break;
                case 'E': extended_re      = true; break;
                case 'G': extended_re      = false; break;
                case 'w': word_match       = true; break;
                case 'x': line_match       = true; break;
                case 'o': only_matching    = true; break;
                case 'P':
                    fprintf(stderr, "grep: -P (Perl regex) not supported\n");
                    return 2;
                case 'm':
                    if (*(p + 1) != '\0') { max_matches = atol(p + 1); }
                    else if (++argi < argc) { max_matches = atol(argv[argi]); }
                    else { fprintf(stderr, "grep: option requires an argument -- 'm'\n"); return 2; }
                    stop = true;
                    break;
                default:
                    fprintf(stderr, "grep: invalid option -- '%c'\n", *p);
                    return 2;
            }
        }
        argi++;
    }

    if (argi >= argc) {
        fprintf(stderr, "Usage: grep [OPTION]... PATTERN [FILE]...\n");
        return 2;
    }

    const char *pattern = argv[argi++];

    /* ---- compile regex ---- */
    if (!fixed_strings) {
        std::string pat_str;
        std::regex_constants::syntax_option_type flags =
            std::regex_constants::ECMAScript |
            std::regex_constants::optimize;

        if (case_insensitive)
            flags |= std::regex_constants::icase;

        if (!extended_re) {
            /* BRE: convert \( \) \{ \} \+ \? to unescaped equivalents */
            pat_str = bre_to_ecma(pattern);
        } else {
            pat_str = pattern;
        }

        try {
            g_re = std::regex(pat_str, flags);
            g_re_compiled = true;
        } catch (const std::regex_error &e) {
            fprintf(stderr, "grep: invalid regex '%s': %s\n", pattern, e.what());
            return 2;
        }
    }

    /* ---- run ---- */
    int ret;
    if (argi >= argc) {
        grep_stream(pattern, stdin, NULL, false);
        ret = matched_any ? 0 : 1;
    } else {
        int nfiles = argc - argi;
        bool show_filename = (nfiles > 1) || recursive;
        for (int i = argi; i < argc; i++)
            grep_path(pattern, argv[i], show_filename);
        ret = matched_any ? 0 : 1;
    }

    return ret;
}
