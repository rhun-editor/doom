#include "platform.h"
#include <string.h>
#include <zlib.h>

static void be32(unsigned char *p, uint32_t n) {
    p[0] = n >> 24;
    p[1] = n >> 16;
    p[2] = n >> 8;
    p[3] = n;
}
static size_t chunk(unsigned char *p, const char *name, const void *data, size_t n) {
    be32(p, (uint32_t)n);
    memcpy(p + 4, name, 4);
    if (n)
        memcpy(p + 8, data, n);
    be32(p + 8 + n, (uint32_t)crc32(0, p + 4, (uInt)n + 4));
    return n + 12;
}
size_t doom_png(const unsigned char *pixels, const unsigned char palette[768], unsigned char *out,
                size_t cap) {
    unsigned char raw[201 * 320], packed[66000];
    for (int y = 0; y < 200; y++) {
        raw[y * 321] = 0;
        memcpy(raw + y * 321 + 1, pixels + y * 320, 320);
    }
    uLongf n = sizeof packed;
    if (compress2(packed, &n, raw, 200 * 321, 1) != Z_OK || cap < n + 837)
        return 0;
    memcpy(out, "\x89PNG\r\n\x1a\n", 8);
    unsigned char ihdr[13] = {0};
    be32(ihdr, 320);
    be32(ihdr + 4, 200);
    ihdr[8] = 8;
    ihdr[9] = 3;
    size_t at = 8;
    at += chunk(out + at, "IHDR", ihdr, sizeof ihdr);
    at += chunk(out + at, "PLTE", palette, 768);
    at += chunk(out + at, "IDAT", packed, n);
    at += chunk(out + at, "IEND", NULL, 0);
    return at;
}
