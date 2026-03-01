#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Process escape sequences in a string, writing to stdout.
 * Returns a pointer past the last consumed character. */
static void print_escaped(const char *s) {
    for (; *s; s++) {
        if (*s != '\\') { putchar(*s); continue; }
        s++;
        switch (*s) {
            case 'n':  putchar('\n'); break;
            case 't':  putchar('\t'); break;
            case 'r':  putchar('\r'); break;
            case 'a':  putchar('\a'); break;
            case 'b':  putchar('\b'); break;
            case 'f':  putchar('\f'); break;
            case 'v':  putchar('\v'); break;
            case '\\': putchar('\\'); break;
            case '0': {
                /* octal: \0NNN */
                unsigned int val = 0;
                int i;
                for (i = 0; i < 3 && s[1] >= '0' && s[1] <= '7'; i++)
                    val = val * 8 + (*++s - '0');
                putchar((char)val);
                break;
            }
            case '\0': putchar('\\'); s--; break;
            default:   putchar('\\'); putchar(*s); break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: printf <format> [arg...]\n");
        return 1;
    }

    const char *fmt = argv[1];
    int argi = 2;   /* next argument to consume */

    for (const char *p = fmt; *p; p++) {
        if (*p == '\\') {
            /* Escape sequence */
            p++;
            switch (*p) {
                case 'n':  putchar('\n'); break;
                case 't':  putchar('\t'); break;
                case 'r':  putchar('\r'); break;
                case 'a':  putchar('\a'); break;
                case 'b':  putchar('\b'); break;
                case 'f':  putchar('\f'); break;
                case 'v':  putchar('\v'); break;
                case '\\': putchar('\\'); break;
                case '0': {
                    unsigned int val = 0;
                    int i;
                    for (i = 0; i < 3 && p[1] >= '0' && p[1] <= '7'; i++)
                        val = val * 8 + (*++p - '0');
                    putchar((char)val);
                    break;
                }
                case '\0': putchar('\\'); p--; break;
                default:   putchar('\\'); putchar(*p); break;
            }
        } else if (*p == '%') {
            p++;
            if (*p == '%') { putchar('%'); continue; }

            /* Build format spec: collect flags, width, precision, conversion */
            char spec[64] = "%";
            int  si = 1;

            /* Flags */
            while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') {
                if (si < 60) spec[si++] = *p;
                p++;
            }
            /* Width */
            while (*p >= '0' && *p <= '9') {
                if (si < 60) spec[si++] = *p;
                p++;
            }
            /* Precision */
            if (*p == '.') {
                if (si < 60) spec[si++] = *p++;
                while (*p >= '0' && *p <= '9') {
                    if (si < 60) spec[si++] = *p;
                    p++;
                }
            }

            char conv = *p;
            if (si < 62) { spec[si++] = conv; spec[si] = '\0'; }

            const char *arg = (argi < argc) ? argv[argi++] : "";

            switch (conv) {
                case 's':
                    printf(spec, arg);
                    break;
                case 'd': case 'i':
                    printf(spec, (int)strtol(arg, NULL, 0));
                    break;
                case 'u':
                    printf(spec, (unsigned int)strtoul(arg, NULL, 0));
                    break;
                case 'o':
                    printf(spec, (unsigned int)strtoul(arg, NULL, 0));
                    break;
                case 'x': case 'X':
                    printf(spec, (unsigned int)strtoul(arg, NULL, 0));
                    break;
                case 'f': case 'e': case 'E': case 'g': case 'G':
                    printf(spec, strtod(arg, NULL));
                    break;
                case 'c':
                    putchar(arg[0]);
                    break;
                case 'b':
                    /* %b: print with escape expansion */
                    print_escaped(arg);
                    break;
                default:
                    putchar('%');
                    putchar(conv);
                    break;
            }
        } else {
            putchar(*p);
        }
    }

    return 0;
}
