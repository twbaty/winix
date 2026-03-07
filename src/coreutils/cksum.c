/*
 * cksum — compute and print CRC-32 checksum and byte count
 *
 * Usage: cksum [FILE ...]
 *   With no FILE or FILE is -, reads stdin.
 *   Output: CRC32  BYTECOUNT  FILENAME
 *
 * Uses the standard POSIX CRC-32 polynomial (IEEE 802.3).
 * --version / --help
 * Exit: 0 = success, 1 = error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define VERSION "1.0"
#define BUFSZ   65536

/* ── CRC-32 table (IEEE 802.3 polynomial 0xEDB88320) ──────── */

static uint32_t crc_table[256];
static int      crc_init_done = 0;

static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_init_done = 1;
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *buf, size_t n) {
    for (size_t i = 0; i < n; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

/*
 * POSIX cksum appends the byte count to the data before finalizing,
 * encoded as a sequence of bytes (length in binary, LSB first run
 * through the CRC).  This matches GNU coreutils behaviour.
 */
static uint32_t finalize_crc(uint32_t crc, unsigned long long len) {
    unsigned long long l = len;
    while (l != 0) {
        unsigned char c = (unsigned char)(l & 0xFF);
        crc = crc_table[(crc ^ c) & 0xFF] ^ (crc >> 8);
        l >>= 8;
    }
    return ~crc;
}

static int do_cksum(FILE *fp, const char *name) {
    unsigned char *buf = (unsigned char *)malloc(BUFSZ);
    if (!buf) { fprintf(stderr, "cksum: out of memory\n"); return 1; }

    uint32_t crc = 0xFFFFFFFFu;
    unsigned long long total = 0;
    size_t n;

    while ((n = fread(buf, 1, BUFSZ, fp)) > 0) {
        crc = crc32_update(crc, buf, n);
        total += n;
    }
    free(buf);

    if (ferror(fp)) {
        fprintf(stderr, "cksum: read error: %s\n", name);
        return 1;
    }

    crc = finalize_crc(crc, total);

    if (name)
        printf("%u %llu %s\n", crc, total, name);
    else
        printf("%u %llu\n", crc, total);
    return 0;
}

int main(int argc, char *argv[]) {
    if (!crc_init_done) crc_init();

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("cksum %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[i], "--help")) {
            fprintf(stderr,
                "usage: cksum [FILE ...]\n\n"
                "Print CRC-32 checksum and byte count of each FILE.\n"
                "With no FILE or FILE is -, reads stdin.\n\n"
                "Output format:  CRC32  BYTES  FILENAME\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
    }

    int ret = 0;

    if (argc < 2) {
        ret = do_cksum(stdin, NULL);
    } else {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-")) {
                ret |= do_cksum(stdin, NULL);
                continue;
            }
            FILE *fp = fopen(argv[i], "rb");
            if (!fp) { perror(argv[i]); ret = 1; continue; }
            ret |= do_cksum(fp, argv[i]);
            fclose(fp);
        }
    }
    return ret;
}
