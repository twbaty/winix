#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p, 0755)
#endif

static int verbose = 0, force = 0, recursive = 0;

/* ------------------------------------------------------------------ */
/* Copy a single regular file src -> dst                               */
/* ------------------------------------------------------------------ */
static int copy_file(const char *src, const char *dst) {
    if (!force) {
        FILE *chk = fopen(dst, "rb");
        if (chk) {
            fclose(chk);
            fprintf(stderr, "cp: '%s' already exists (use -f to overwrite)\n", dst);
            return 1;
        }
    }

    FILE *in = fopen(src, "rb");
    if (!in) {
        fprintf(stderr, "cp: cannot open '%s': %s\n", src, strerror(errno));
        return 1;
    }

    FILE *out = fopen(dst, "wb");
    if (!out) {
        fprintf(stderr, "cp: cannot create '%s': %s\n", dst, strerror(errno));
        fclose(in);
        return 1;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fprintf(stderr, "cp: write error on '%s': %s\n", dst, strerror(errno));
            fclose(in);
            fclose(out);
            return 1;
        }
    }

    fclose(in);
    fclose(out);

    if (verbose)
        printf("'%s' -> '%s'\n", src, dst);

    return 0;
}

/* forward declaration */
static int copy_entry(const char *src, const char *dst);

/* ------------------------------------------------------------------ */
/* Recursively copy directory src -> dst                               */
/* ------------------------------------------------------------------ */
static int copy_dir(const char *src, const char *dst) {
    if (make_dir(dst) != 0 && errno != EEXIST) {
        fprintf(stderr, "cp: cannot create directory '%s': %s\n", dst, strerror(errno));
        return 1;
    }

    if (verbose)
        printf("'%s' -> '%s'\n", src, dst);

    DIR *d = opendir(src);
    if (!d) {
        fprintf(stderr, "cp: cannot open directory '%s': %s\n", src, strerror(errno));
        return 1;
    }

    int ret = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char src_path[4096], dst_path[4096];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, ent->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, ent->d_name);
        ret |= copy_entry(src_path, dst_path);
    }

    closedir(d);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Dispatch: stat src and copy as file or directory                    */
/* ------------------------------------------------------------------ */
static int copy_entry(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) {
        fprintf(stderr, "cp: cannot stat '%s': %s\n", src, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            fprintf(stderr, "cp: '%s' is a directory (use -r)\n", src);
            return 1;
        }
        return copy_dir(src, dst);
    }

    return copy_file(src, dst);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    int argi = 1;

    /* Parse flags — supports combined flags like -rf, -rfv */
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (const char *p = argv[argi] + 1; *p; p++) {
            if      (*p == 'v')           verbose   = 1;
            else if (*p == 'f')           force     = 1;
            else if (*p == 'r' || *p == 'R') recursive = 1;
            else {
                fprintf(stderr, "cp: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
        argi++;
    }

    if (argc - argi < 2) {
        fprintf(stderr, "Usage: cp [-rfv] <source>... <destination>\n");
        return 1;
    }

    const char *dst = argv[argc - 1];

    /* Is the destination an existing directory? */
    struct stat dst_st;
    int dst_is_dir = (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode));

    /* Multiple sources require the destination to be a directory */
    if (argc - argi > 2 && !dst_is_dir) {
        fprintf(stderr, "cp: target '%s' is not a directory\n", dst);
        return 1;
    }

    int ret = 0;
    for (int i = argi; i < argc - 1; i++) {
        const char *src = argv[i];

        if (dst_is_dir) {
            /* Derive basename of src and place it inside dst */
            const char *base = strrchr(src, '/');
            if (!base) base = strrchr(src, '\\');
            base = base ? base + 1 : src;

            char dst_path[4096];
            snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, base);
            ret |= copy_entry(src, dst_path);
        } else {
            ret |= copy_entry(src, dst);
        }
    }

    return ret;
}
