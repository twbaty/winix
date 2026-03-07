/*
 * numfmt — convert numbers to/from human-readable formats
 *
 * Usage: numfmt [OPTIONS] [NUMBER ...]
 *   --to=si       convert to SI  (1K=1000)   e.g. 1500 -> 1.5K
 *   --to=iec      convert to IEC (1K=1024)   e.g. 1536 -> 1.5Ki
 *   --to=iec-i    same as iec (alias)
 *   --from=si     parse SI  units in input
 *   --from=iec    parse IEC units in input
 *   --from=auto   detect SI or IEC suffix automatically
 *   --suffix=X    append X after the formatted number
 *   --field=N     which whitespace-delimited field to format (default: 1)
 *   --padding=N   pad result to N chars (negative = left-align)
 *   --round=up|down|nearest (default: nearest)
 *   --header[=N]  pass N header lines unchanged (default 1)
 *   --invalid=fail|warn|ignore  (default warn)
 *   --version / --help
 *
 * Reads from FILE args or stdin line-by-line when no NUMBERs given.
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#define VERSION "1.0"

typedef enum { TO_NONE, TO_SI, TO_IEC } ToFmt;
typedef enum { FROM_NONE, FROM_SI, FROM_IEC, FROM_AUTO } FromFmt;
typedef enum { ROUND_NEAREST, ROUND_UP, ROUND_DOWN } RoundMode;
typedef enum { INV_FAIL, INV_WARN, INV_IGNORE } InvMode;

static ToFmt    g_to      = TO_NONE;
static FromFmt  g_from    = FROM_NONE;
static char    *g_suffix  = NULL;
static int      g_field   = 1;
static int      g_padding = 0;
static RoundMode g_round  = ROUND_NEAREST;
static InvMode  g_invalid = INV_WARN;
static int      g_header  = 0;

static const double SI_UNITS[]  = { 1e15, 1e12, 1e9, 1e6, 1e3, 1 };
static const char  SI_CHARS[]   = { 'P',  'T',  'G',  'M',  'K', '\0' };
static const double IEC_UNITS[] = {
    1125899906842624.0, /* Pi */
    1099511627776.0,    /* Ti */
    1073741824.0,       /* Gi */
    1048576.0,          /* Mi */
    1024.0,             /* Ki */
    1.0
};
static const char *IEC_SUFFIXES[] = { "Pi","Ti","Gi","Mi","Ki","" };

/* Parse a number that may have an SI or IEC suffix */
static int parse_number(const char *s, double *out, FromFmt from) {
    char *end;
    double v = strtod(s, &end);
    if (end == s) return 0;

    if (*end && from != FROM_NONE) {
        char su = (char)toupper((unsigned char)*end);
        double mult = 1.0;
        int is_iec  = (end[1] == 'i' || end[1] == 'I');
        double base = (from == FROM_IEC || (from == FROM_AUTO && is_iec)) ? 1024.0 : 1000.0;

        switch (su) {
            case 'K': mult = base;                        break;
            case 'M': mult = base * base;                 break;
            case 'G': mult = base * base * base;          break;
            case 'T': mult = base * base * base * base;   break;
            case 'P': mult = base * base * base * base * base; break;
            default: return 0;
        }
        v *= mult;
    } else if (*end && from == FROM_NONE) {
        return 0;  /* trailing garbage with no from mode */
    }
    *out = v;
    return 1;
}

/* Format a number to human-readable */
static void format_number(double v, char *buf, size_t bufsz) {
    if (g_to == TO_NONE) {
        /* Just round to integer */
        snprintf(buf, bufsz, "%.0f", v);
        return;
    }

    int negative = v < 0;
    if (negative) v = -v;

    const double *units;
    const char   *suffixes[6];
    int nlevels = 6;

    if (g_to == TO_SI) {
        units = SI_UNITS;
        for (int i = 0; i < 5; i++) {
            char tmp[3] = { SI_CHARS[i], '\0', '\0' };
            suffixes[i] = strdup(tmp);
        }
        suffixes[5] = "";
    } else {
        units = IEC_UNITS;
        for (int i = 0; i < 6; i++) suffixes[i] = IEC_SUFFIXES[i];
    }

    for (int i = 0; i < nlevels - 1; i++) {
        if (v >= units[i] * 0.9995) {
            double scaled = v / units[i];
            /* Round */
            double disp;
            if (g_round == ROUND_UP)       disp = ceil(scaled * 10.0) / 10.0;
            else if (g_round == ROUND_DOWN) disp = floor(scaled * 10.0) / 10.0;
            else                            disp = round(scaled * 10.0) / 10.0;

            char sign[2] = { negative ? '-' : '\0', '\0' };
            if (disp >= 10.0)
                snprintf(buf, bufsz, "%s%.0f%s", sign, disp, suffixes[i]);
            else
                snprintf(buf, bufsz, "%s%.1f%s", sign, disp, suffixes[i]);
            goto done;
        }
    }
    snprintf(buf, bufsz, "%s%.0f", negative ? "-" : "", v);

done:
    if (g_to == TO_SI)
        for (int i = 0; i < 5; i++) free((char *)suffixes[i]);
}

static void process_number(const char *tok) {
    double v;
    if (!parse_number(tok, &v, g_from)) {
        switch (g_invalid) {
            case INV_FAIL:
                fprintf(stderr, "numfmt: invalid number '%s'\n", tok); exit(1);
            case INV_WARN:
                fprintf(stderr, "numfmt: invalid number '%s'\n", tok);
                /* fall through: print as-is */
            case INV_IGNORE:
                printf("%s", tok); return;
        }
    }
    char buf[64];
    format_number(v, buf, sizeof(buf));
    if (g_padding != 0) {
        int w = abs(g_padding);
        printf(g_padding < 0 ? "%-*s" : "%*s", w, buf);
    } else {
        printf("%s", buf);
    }
    if (g_suffix) printf("%s", g_suffix);
}

static void process_line(char *line) {
    /* strip trailing newline */
    size_t n = strlen(line);
    while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';

    if (g_field <= 0) { printf("%s\n", line); return; }

    /* Split into fields, transform g_field-th one */
    char *p   = line;
    int   fi  = 0;
    char *out = (char *)malloc(strlen(line) * 4 + 64);
    char *op  = out;

    while (*p) {
        /* leading whitespace before field */
        while (*p == ' ' || *p == '\t') *op++ = *p++;
        if (!*p) break;

        fi++;
        char *tok_start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        int   tok_len = (int)(p - tok_start);
        char  tok[256];
        if (tok_len >= (int)sizeof(tok)) tok_len = (int)sizeof(tok) - 1;
        memcpy(tok, tok_start, (size_t)tok_len);
        tok[tok_len] = '\0';

        if (fi == g_field) {
            /* format this field, capture to out */
            char buf[64] = "";
            double v;
            if (parse_number(tok, &v, g_from)) {
                format_number(v, buf, sizeof(buf));
                if (g_suffix) strcat(buf, g_suffix);
            } else {
                strcpy(buf, tok);
            }
            int blen = (int)strlen(buf);
            memcpy(op, buf, (size_t)blen);
            op += blen;
        } else {
            memcpy(op, tok, (size_t)tok_len);
            op += tok_len;
        }
    }
    *op = '\0';
    printf("%s\n", out);
    free(out);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] [NUMBER ...]\n\n"
        "Convert numbers to/from human-readable format.\n\n"
        "  --to=si|iec       format output with SI (1K=1000) or IEC (1Ki=1024) suffixes\n"
        "  --from=si|iec|auto parse SI or IEC suffixes in input\n"
        "  --suffix=X        append X to output\n"
        "  --field=N         field to reformat (default 1; use with stdin lines)\n"
        "  --padding=N       pad to N chars (negative = left-align)\n"
        "  --round=up|down|nearest\n"
        "  --header[=N]      skip N header lines (default 1)\n"
        "  --invalid=fail|warn|ignore\n"
        "      --version\n"
        "      --help\n",
        prog);
}

int main(int argc, char *argv[]) {
    int argi = 1;

    for (; argi < argc && argv[argi][0] == '-'; argi++) {
        const char *a = argv[argi];
        if (!strcmp(a, "--version")) { printf("numfmt %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(a, "--help"))    { usage(argv[0]); return 0; }
        if (!strcmp(a, "--"))        { argi++; break; }

        if (!strncmp(a, "--to=", 5)) {
            const char *v = a + 5;
            if (!strcmp(v,"si"))           g_to = TO_SI;
            else if (!strncmp(v,"iec",3))  g_to = TO_IEC;
            else { fprintf(stderr, "numfmt: unknown --to value '%s'\n", v); return 1; }
            continue;
        }
        if (!strncmp(a, "--from=", 7)) {
            const char *v = a + 7;
            if (!strcmp(v,"si"))        g_from = FROM_SI;
            else if (!strcmp(v,"iec"))  g_from = FROM_IEC;
            else if (!strcmp(v,"auto")) g_from = FROM_AUTO;
            else { fprintf(stderr, "numfmt: unknown --from value '%s'\n", v); return 1; }
            continue;
        }
        if (!strncmp(a, "--suffix=", 9))  { g_suffix  = (char *)(a + 9); continue; }
        if (!strncmp(a, "--field=", 8))   { g_field   = atoi(a + 8);    continue; }
        if (!strncmp(a, "--padding=", 10)){ g_padding = atoi(a + 10);   continue; }
        if (!strncmp(a, "--header", 8)) {
            g_header = (a[8] == '=') ? atoi(a + 9) : 1; continue;
        }
        if (!strncmp(a, "--round=", 8)) {
            const char *v = a + 8;
            if (!strcmp(v,"up"))          g_round = ROUND_UP;
            else if (!strcmp(v,"down"))   g_round = ROUND_DOWN;
            else                          g_round = ROUND_NEAREST;
            continue;
        }
        if (!strncmp(a, "--invalid=", 10)) {
            const char *v = a + 10;
            if (!strcmp(v,"fail"))        g_invalid = INV_FAIL;
            else if (!strcmp(v,"ignore")) g_invalid = INV_IGNORE;
            else                          g_invalid = INV_WARN;
            continue;
        }
        fprintf(stderr, "numfmt: invalid option '%s'\n", a);
        return 1;
    }

    if (argi < argc) {
        /* Numbers on command line */
        for (int i = argi; i < argc; i++) {
            process_number(argv[i]);
            putchar('\n');
        }
    } else {
        /* Read from stdin line by line */
        char line[4096];
        int lno = 0;
        while (fgets(line, sizeof(line), stdin)) {
            lno++;
            if (lno <= g_header) { fputs(line, stdout); continue; }
            process_line(line);
        }
    }
    return 0;
}
