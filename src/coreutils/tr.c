#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

/* ---------------------------------------------------------------
 * SET expansion
 * --------------------------------------------------------------- */

/*
 * Parse one escape sequence starting at *p (which points at the char
 * after the backslash).  Advance *p past the consumed chars and return
 * the decoded byte value.
 */
static unsigned char parse_escape(const char **p)
{
    char c = **p;
    (*p)++;
    switch (c) {
        case 'a':  return '\a';
        case 'b':  return '\b';
        case 'f':  return '\f';
        case 'n':  return '\n';
        case 'r':  return '\r';
        case 't':  return '\t';
        case 'v':  return '\v';
        case '\\': return '\\';
        case '0':  return '\0';
        case 'x': {
            /* \xHH — up to two hex digits */
            unsigned int val = 0;
            int digits = 0;
            while (digits < 2 && isxdigit((unsigned char)**p)) {
                char h = **p;
                (*p)++;
                val <<= 4;
                if      (h >= '0' && h <= '9') val |= (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') val |= (unsigned)(h - 'a' + 10);
                else                            val |= (unsigned)(h - 'A' + 10);
                digits++;
            }
            return (unsigned char)val;
        }
        default:
            /* Unknown escape — return the literal character */
            return (unsigned char)c;
    }
}

/*
 * Expand a POSIX character class name (everything between [: and :]).
 * Write matching bytes into bitset[256].  Return true on success.
 */
static bool expand_class(const char *name, bool bitset[256])
{
    for (int i = 0; i < 256; i++) {
        unsigned char c = (unsigned char)i;
        bool match = false;
        if      (strcmp(name, "alpha")  == 0) match = isalpha(c)  != 0;
        else if (strcmp(name, "lower")  == 0) match = islower(c)  != 0;
        else if (strcmp(name, "upper")  == 0) match = isupper(c)  != 0;
        else if (strcmp(name, "digit")  == 0) match = isdigit(c)  != 0;
        else if (strcmp(name, "space")  == 0) match = isspace(c)  != 0;
        else if (strcmp(name, "alnum")  == 0) match = isalnum(c)  != 0;
        else if (strcmp(name, "blank")  == 0) match = (c == ' ' || c == '\t');
        else if (strcmp(name, "punct")  == 0) match = ispunct(c)  != 0;
        else if (strcmp(name, "print")  == 0) match = isprint(c)  != 0;
        else if (strcmp(name, "cntrl")  == 0) match = iscntrl(c)  != 0;
        else {
            fprintf(stderr, "tr: invalid character class '%s'\n", name);
            return false;
        }
        if (match) bitset[i] = true;
    }
    return true;
}

/*
 * Expand SET string into an ordered array of byte values.
 * arr[]  — caller-supplied buffer; must hold at least maxn entries.
 * Returns the number of entries written, or -1 on error.
 */
static int expand_set_to_array(const char *s, unsigned char *arr, int maxn)
{
    int count = 0;
    const char *p = s;

    while (*p) {
        /* POSIX class: [: ... :] */
        if (p[0] == '[' && p[1] == ':') {
            const char *start = p + 2;
            const char *end   = strstr(start, ":]");
            if (!end) {
                fprintf(stderr, "tr: missing ':]' in character class\n");
                return -1;
            }
            /* Copy class name into a small buffer */
            char classname[32];
            size_t namelen = (size_t)(end - start);
            if (namelen >= sizeof(classname)) {
                fprintf(stderr, "tr: character class name too long\n");
                return -1;
            }
            memcpy(classname, start, namelen);
            classname[namelen] = '\0';

            /* Expand into a temporary bitset then append in order */
            bool cls[256];
            memset(cls, 0, sizeof(cls));
            if (!expand_class(classname, cls)) return -1;

            for (int i = 0; i < 256; i++) {
                if (cls[i]) {
                    if (count >= maxn) {
                        fprintf(stderr, "tr: set too large\n");
                        return -1;
                    }
                    arr[count++] = (unsigned char)i;
                }
            }
            p = end + 2;  /* skip past :] */
            continue;
        }

        /* Escape sequence */
        unsigned char ch;
        if (*p == '\\') {
            p++;
            if (!*p) {
                /* Trailing backslash — treat as literal backslash */
                ch = '\\';
            } else {
                ch = parse_escape(&p);
            }
        } else {
            ch = (unsigned char)*p;
            p++;
        }

        /* Range: X-Y  (peek ahead) */
        if (*p == '-' && *(p + 1) != '\0') {
            p++;  /* skip '-' */
            unsigned char hi;
            if (*p == '\\') {
                p++;
                hi = parse_escape(&p);
            } else {
                hi = (unsigned char)*p;
                p++;
            }
            if (hi < ch) {
                fprintf(stderr, "tr: range-endpoints of '%c'-'%c' are in reverse order\n",
                        (int)ch, (int)hi);
                return -1;
            }
            for (unsigned char rc = ch; rc <= hi; rc++) {
                if (count >= maxn) {
                    fprintf(stderr, "tr: set too large\n");
                    return -1;
                }
                arr[count++] = rc;
                if (rc == 255) break;  /* avoid wrap-around */
            }
        } else {
            if (count >= maxn) {
                fprintf(stderr, "tr: set too large\n");
                return -1;
            }
            arr[count++] = ch;
        }
    }
    return count;
}

/*
 * Expand SET string into a boolean membership array (bitset[256]).
 * Returns true on success.
 */
static bool expand_set_to_bitset(const char *s, bool bitset[256])
{
    /* Re-use expand_set_to_array with a temporary buffer */
    unsigned char tmp[256 * 4];  /* generous; duplicate entries are fine */
    int n = expand_set_to_array(s, tmp, (int)(sizeof(tmp)));
    if (n < 0) return false;
    for (int i = 0; i < n; i++) bitset[(unsigned char)tmp[i]] = true;
    return true;
}

/* ---------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------- */

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: tr [OPTION]... SET1 [SET2]\n"
        "Translate, squeeze, or delete characters from stdin to stdout.\n"
        "\n"
        "Options:\n"
        "  -c, -C        complement SET1\n"
        "  -d            delete chars in SET1\n"
        "  -s            squeeze consecutive repeated chars\n"
        "  --version     print version and exit\n"
        "  --help        print this help and exit\n"
        "\n"
        "SET syntax:\n"
        "  Literal chars, ranges (a-z), escapes (\\n \\t \\r \\\\ \\a \\0 \\xHH),\n"
        "  POSIX classes: [:alpha:] [:lower:] [:upper:] [:digit:] [:space:]\n"
        "                 [:alnum:] [:blank:] [:punct:] [:print:] [:cntrl:]\n"
    );
}

/* ---------------------------------------------------------------
 * main
 * --------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    bool opt_complement = false;
    bool opt_delete     = false;
    bool opt_squeeze    = false;

    int argi = 1;

    /* --- Parse options --- */
    while (argi < argc) {
        const char *arg = argv[argi];

        if (strcmp(arg, "--version") == 0) {
            printf("tr 1.0 (Winix 1.0)\n");
            return 0;
        }
        if (strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(arg, "--") == 0) {
            argi++;
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            for (const char *p = arg + 1; *p; p++) {
                if      (*p == 'c' || *p == 'C') opt_complement = true;
                else if (*p == 'd')               opt_delete     = true;
                else if (*p == 's')               opt_squeeze    = true;
                else {
                    fprintf(stderr, "tr: invalid option -- '%c'\n", *p);
                    return 1;
                }
            }
            argi++;
        } else {
            break;  /* first non-option argument */
        }
    }

    /* --- Collect SET1 / SET2 --- */
    const char *set1_str = NULL;
    const char *set2_str = NULL;

    if (argi < argc) { set1_str = argv[argi++]; }
    if (argi < argc) { set2_str = argv[argi++]; }

    /* Validation */
    if (!set1_str) {
        fprintf(stderr, "tr: missing operand\n");
        usage(stderr);
        return 1;
    }
    if (!opt_delete && !set2_str && !opt_squeeze) {
        fprintf(stderr, "tr: missing operand after '%s'\n", set1_str);
        usage(stderr);
        return 1;
    }
    /* -d -s requires SET2 for the squeeze phase */
    if (opt_delete && opt_squeeze && !set2_str) {
        fprintf(stderr, "tr: option -s with -d requires SET2\n");
        return 1;
    }

    /* ---------------------------------------------------------------
     * Build working tables
     * --------------------------------------------------------------- */

    /* del_set: chars to delete (SET1, possibly complemented) */
    bool del_set[256];
    memset(del_set, 0, sizeof(del_set));

    /* sq_set: chars that trigger squeeze */
    bool sq_set[256];
    memset(sq_set, 0, sizeof(sq_set));

    /* xlat[]: translation table */
    unsigned char xlat[256];
    for (int i = 0; i < 256; i++) xlat[i] = (unsigned char)i;

    if (opt_delete) {
        /* --- Delete mode --- */
        bool raw1[256];
        memset(raw1, 0, sizeof(raw1));
        if (!expand_set_to_bitset(set1_str, raw1)) return 1;

        if (opt_complement) {
            for (int i = 0; i < 256; i++) del_set[i] = !raw1[i];
        } else {
            memcpy(del_set, raw1, sizeof(del_set));
        }

        if (opt_squeeze && set2_str) {
            /* After deletion, squeeze chars in SET2 */
            if (!expand_set_to_bitset(set2_str, sq_set)) return 1;
        }

    } else {
        /* --- Translate mode (with optional squeeze) --- */
        unsigned char arr1[256 * 4];
        unsigned char arr2[256 * 4];

        int n1 = expand_set_to_array(set1_str, arr1, (int)sizeof(arr1));
        if (n1 < 0) return 1;

        if (opt_complement) {
            /* Build complemented ordered array: all bytes NOT in arr1 */
            bool in1[256];
            memset(in1, 0, sizeof(in1));
            for (int i = 0; i < n1; i++) in1[arr1[i]] = true;
            unsigned char comp[256];
            int nc = 0;
            for (int i = 0; i < 256; i++) {
                if (!in1[i]) comp[nc++] = (unsigned char)i;
            }
            /* Replace arr1 with the complement */
            if (nc > (int)sizeof(arr1)) nc = (int)sizeof(arr1);
            memcpy(arr1, comp, (size_t)nc);
            n1 = nc;
        }

        int n2 = 0;
        if (set2_str) {
            n2 = expand_set_to_array(set2_str, arr2, (int)sizeof(arr2));
            if (n2 < 0) return 1;
        }

        if (opt_squeeze && !set2_str) {
            /* tr -s SET1: squeeze only, no translate */
            for (int i = 0; i < n1; i++) sq_set[arr1[i]] = true;
        } else {
            /* Build xlat table */
            if (n2 == 0 && set2_str) {
                /* SET2 expanded to empty — nothing to map to */
                fprintf(stderr, "tr: SET2 expanded to empty string\n");
                return 1;
            }
            if (set2_str) {
                /* Pad SET2 by repeating its last char */
                unsigned char pad = arr2[n2 - 1];
                for (int i = 0; i < n1; i++) {
                    unsigned char dst = (i < n2) ? arr2[i] : pad;
                    xlat[(unsigned char)arr1[i]] = dst;
                }
                if (opt_squeeze) {
                    /* Squeeze chars in SET2 */
                    for (int i = 0; i < n2; i++) sq_set[arr2[i]] = true;
                }
            }
        }
    }

    /* ---------------------------------------------------------------
     * Main processing loop
     * --------------------------------------------------------------- */
    int c;
    int last_out = -1;  /* last character written (for squeeze) */

    while ((c = getchar()) != EOF) {
        unsigned char uc = (unsigned char)c;

        /* Step 1: delete */
        if (opt_delete && del_set[uc]) continue;

        /* Step 2: translate */
        unsigned char out_c = xlat[uc];

        /* Step 3: squeeze */
        if (opt_squeeze && sq_set[out_c] && (int)out_c == last_out) continue;

        putchar((int)out_c);
        last_out = (int)out_c;
    }

    return 0;
}
