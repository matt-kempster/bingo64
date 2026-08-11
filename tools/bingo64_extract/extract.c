/* bingo64-extract: build the game's res/ data from the user's own SM64 ROM.
 *
 * Usage: bingo64-extract <baserom.us.z64> [outdir]
 *
 * Writes res/gfx/<...>.png (textures, skybox and cake tiles) and
 * res/sound/* (rebuilt via the recipe baked into manifest.inc). The result
 * is equivalent to the EXTERNAL_DATA basepack, so the game runs with
 * loose files under res/ next to the executable.
 *
 * Conversion logic is ported from tools/sm64tools/n64graphics.c,
 * tools/sm64tools/libmio0.c and tools/skyconv.c so output matches what the
 * build system produces from the same ROM.
 *
 * Accepts .z64 (big-endian) ROMs directly and auto-converts the .v64/.n64
 * byte orders. The ROM is verified against the known US sha1 before use.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../sm64tools/stb/stb_image_write.h"

struct TexEntry {
    const char *path;
    uint8_t fmt;    /* 0 = rgba, 1 = ia */
    uint8_t depth;  /* bits per texel */
    uint16_t w, h;
    uint32_t size;  /* raw byte length */
    int64_t mio0;   /* containing MIO0 block offset, or -1 for plain */
    uint32_t pos;   /* offset within block (or within ROM if mio0 < 0) */
};
struct SkyEntry {
    const char *name;
    uint32_t size;
    int64_t mio0;
    uint32_t pos;
    int cake;       /* 0 = skybox (8x8 of 32x32), 1 = US cake ending */
};

#include "manifest.inc"

#define ROM_SIZE 0x800000

typedef struct { uint8_t r, g, b, a; } rgba_t;

static uint8_t *sRom;

/* ------------------------------------------------------------------ */
/* SHA-1 (public-domain style compact implementation) */

static void sha1(const uint8_t *data, size_t len, char out_hex[41]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t total = (uint64_t) len * 8;
    size_t full = (len + 8) / 64 + 1;
    for (size_t blk = 0; blk < full; blk++) {
        uint8_t chunk[64] = {0};
        size_t base = blk * 64;
        for (int i = 0; i < 64; i++) {
            size_t p = base + i;
            if (p < len) chunk[i] = data[p];
            else if (p == len) chunk[i] = 0x80;
        }
        if (blk == full - 1) {
            for (int i = 0; i < 8; i++) chunk[56 + i] = (uint8_t)(total >> (56 - 8 * i));
        }
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t) chunk[i*4] << 24 | chunk[i*4+1] << 16 | chunk[i*4+2] << 8 | chunk[i*4+3];
        for (int i = 16; i < 80; i++) {
            uint32_t v = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (v << 1) | (v >> 31);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
            uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = ((b << 30) | (b >> 2)); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++)
        sprintf(out_hex + i * 8, "%08x", h[i]);
}

/* ------------------------------------------------------------------ */
/* MIO0 (decode ported from libmio0.c) */

static uint32_t be32(const uint8_t *p) {
    return (uint32_t) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static uint8_t *mio0_decode(const uint8_t *in, uint32_t *out_size) {
    if (memcmp(in, "MIO0", 4) != 0) return NULL;
    uint32_t dest_size = be32(in + 4);
    uint32_t comp_off = be32(in + 8);
    uint32_t uncomp_off = be32(in + 12);
    uint8_t *out = malloc(dest_size);
    if (!out) return NULL;
    uint32_t written = 0, bit_idx = 0, comp_idx = 0, uncomp_idx = 0;
    while (written < dest_size) {
        if (in[16 + bit_idx / 8] & (0x80u >> (bit_idx % 8))) {
            out[written++] = in[uncomp_off + uncomp_idx++];
        } else {
            const uint8_t *v = &in[comp_off + comp_idx];
            comp_idx += 2;
            int length = ((v[0] & 0xF0) >> 4) + 3;
            int idx = ((v[0] & 0x0F) << 8) + v[1] + 1;
            for (int i = 0; i < length; i++, written++)
                out[written] = out[written - idx];
        }
        bit_idx++;
    }
    *out_size = dest_size;
    return out;
}

/* cache of decompressed blocks */
#define MAX_BLOCKS 512
static struct { int64_t off; uint8_t *data; uint32_t size; } sBlocks[MAX_BLOCKS];
static int sNumBlocks;

static const uint8_t *segment_data(int64_t mio0, uint32_t *size) {
    if (mio0 < 0) { *size = ROM_SIZE; return sRom; }
    for (int i = 0; i < sNumBlocks; i++)
        if (sBlocks[i].off == mio0) { *size = sBlocks[i].size; return sBlocks[i].data; }
    if (sNumBlocks >= MAX_BLOCKS) { fprintf(stderr, "too many MIO0 blocks\n"); exit(1); }
    uint32_t sz;
    uint8_t *d = mio0_decode(sRom + mio0, &sz);
    if (!d) { fprintf(stderr, "bad MIO0 block at 0x%llx\n", (long long) mio0); exit(1); }
    sBlocks[sNumBlocks].off = mio0;
    sBlocks[sNumBlocks].data = d;
    sBlocks[sNumBlocks].size = sz;
    sNumBlocks++;
    *size = sz;
    return d;
}

/* ------------------------------------------------------------------ */
/* texel -> image conversion (ported from n64graphics.c) */

#define SCALE_5_8(v) (((v) * 0xFF) / 0x1F)
#define SCALE_4_8(v) (((v) * 0xFF) / 0x0F)
#define SCALE_3_8(v) (((v) * 0xFF) / 0x07)

static void raw2rgba(const uint8_t *raw, rgba_t *img, int count, int depth) {
    if (depth == 16) {
        for (int i = 0; i < count; i++) {
            img[i].r = SCALE_5_8((raw[i*2] & 0xF8) >> 3);
            img[i].g = SCALE_5_8(((raw[i*2] & 0x07) << 2) | ((raw[i*2+1] & 0xC0) >> 6));
            img[i].b = SCALE_5_8((raw[i*2+1] & 0x3E) >> 1);
            img[i].a = (raw[i*2+1] & 0x01) ? 0xFF : 0x00;
        }
    } else { /* 32 */
        for (int i = 0; i < count; i++) {
            img[i].r = raw[i*4]; img[i].g = raw[i*4+1];
            img[i].b = raw[i*4+2]; img[i].a = raw[i*4+3];
        }
    }
}

static void raw2ia(const uint8_t *raw, uint8_t *img /* i,a pairs */, int count, int depth) {
    switch (depth) {
    case 16:
        for (int i = 0; i < count; i++) { img[i*2] = raw[i*2]; img[i*2+1] = raw[i*2+1]; }
        break;
    case 8:
        for (int i = 0; i < count; i++) {
            img[i*2]   = SCALE_4_8((raw[i] & 0xF0) >> 4);
            img[i*2+1] = SCALE_4_8(raw[i] & 0x0F);
        }
        break;
    case 4:
        for (int i = 0; i < count; i++) {
            uint8_t bits = raw[i/2];
            bits = (i % 2) ? (bits & 0xF) : (bits >> 4);
            img[i*2]   = SCALE_3_8((bits >> 1) & 0x07);
            img[i*2+1] = (bits & 0x01) ? 0xFF : 0x00;
        }
        break;
    case 1:
        for (int i = 0; i < count; i++) {
            uint8_t bits = raw[i/8];
            bits = (bits & (1 << (7 - (i % 8)))) ? 0xFF : 0x00;
            img[i*2] = bits; img[i*2+1] = bits;
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* output helpers */

static void mkdirs_for(const char *path) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') { *p = 0; MKDIR(buf); *p = '/'; }
    }
}

static void write_png(const char *path, const void *data, int w, int h, int comp) {
    mkdirs_for(path);
    if (!stbi_write_png(path, w, h, comp, data, 0)) {
        fprintf(stderr, "failed to write %s\n", path);
        exit(1);
    }
}

/* ------------------------------------------------------------------ */
/* skybox / cake (ported from skyconv.c) */

/* Rebuild the combined 248x248 (skybox) image from the ROM blob:
 * up to 64 unique 32x32 rgba16 tiles followed by an 8x10 pointer table. */
static rgba_t *sky_combine(const uint8_t *blob, uint32_t size) {
    enum { W = 10, H = 8, W2 = 8 };
    uint32_t table_off = size - W * H * 4;
    if (table_off % (32 * 32 * 2) != 0) { fprintf(stderr, "bad skybox blob\n"); exit(1); }
    int ntiles = table_off / (32 * 32 * 2);
    rgba_t (*tiles)[32 * 32] = malloc(ntiles * sizeof(*tiles));
    for (int t = 0; t < ntiles; t++)
        raw2rgba(blob + t * 32 * 32 * 2, tiles[t], 32 * 32, 16);
    uint32_t table[W * H];
    for (int i = 0; i < W * H; i++) table[i] = be32(blob + table_off + i * 4);
    uint32_t base = table[0];
    rgba_t *combined = malloc(31 * H * 31 * W2 * sizeof(rgba_t));
    for (int i = 0; i < H; i++)
        for (int j = 0; j < W2; j++) {
            int index = (table[i * W + j] - base) / 0x800;
            if (index < 0 || index >= ntiles) { fprintf(stderr, "bad sky table\n"); exit(1); }
            for (int y = 0; y < 31; y++)
                for (int x = 0; x < 31; x++)
                    combined[(i*31 + y) * (31*W2) + (j*31 + x)] = tiles[index][y*32 + x];
        }
    free(tiles);
    return combined; /* 248 x 248 */
}

/* Split the combined image into expanded 32x32 tiles, dedupe, write pngs.
 * Mirrors skyconv's split path (--type sky, non-expanded input). */
static void sky_split_write(const char *outdir, const char *name, const rgba_t *image) {
    enum { COLS = 8, ROWS = 8, TW = 31, TH = 31, EW = 32, EH = 32, IMGW = 248 };
    static rgba_t tiles[ROWS * COLS][EW * EH];
    int pos[ROWS * COLS], useless[ROWS * COLS];
    memset(tiles, 0, sizeof(tiles));

    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++)
            for (int y = 0; y < TH; y++)
                for (int x = 0; x < TW; x++)
                    tiles[row * COLS + col][y * EW + x] =
                        image[(row * TH + y) * IMGW + (col * TW + x)];

    /* expand: right edge = next tile's left column (wrap) */
    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++) {
            int next = (col + 1) % COLS;
            for (int y = 0; y < EH - 1; y++)
                tiles[row * COLS + col][(EW - 1) + y * EW] = tiles[row * COLS + next][y * EW];
        }
    /* bottom edge = next row's top row; last row duplicates its own */
    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++)
            for (int x = 0; x < EW; x++)
                tiles[row * COLS + col][x + (EH - 1) * EW] =
                    (row < ROWS - 1) ? tiles[(row + 1) * COLS + col][x]
                                     : tiles[row * COLS + col][x + (EH - 2) * EW];

    int newPos = 0;
    for (int i = 0; i < ROWS * COLS; i++) {
        useless[i] = 0;
        for (int j = 0; j < i; j++)
            if (!useless[j] && memcmp(tiles[j], tiles[i], sizeof(tiles[i])) == 0) {
                useless[i] = 1; pos[i] = j; break;
            }
        if (!useless[i]) pos[i] = newPos++;
    }
    char path[1024];
    for (int i = 0; i < ROWS * COLS; i++) {
        if (useless[i]) continue;
        snprintf(path, sizeof(path), "%s/gfx/textures/skybox_tiles/%s.%d.rgba16.png",
                 outdir, name, pos[i]);
        write_png(path, tiles[i], EW, EH, 4);
    }
}

/* US cake: 12 rows x 4 cols of 20x80 rgba16 tiles; combined 316x228. */
static rgba_t *cake_combine(const uint8_t *blob) {
    enum { W = 4, H = 12, SH = 20, SW = 80 };
    rgba_t *combined = malloc((SH-1)*H * (SW-1)*W * sizeof(rgba_t));
    rgba_t tile[SH * SW];
    for (int i = 0; i < H; i++)
        for (int j = 0; j < W; j++) {
            raw2rgba(blob + (i * W + j) * SH * SW * 2, tile, SH * SW, 16);
            for (int y = 0; y < SH - 1; y++)
                for (int x = 0; x < SW - 1; x++)
                    combined[(i*(SH-1) + y) * (SW-1)*W + (j*(SW-1) + x)] = tile[y*SW + x];
        }
    return combined; /* 316 x 228 */
}

static void cake_split_write(const char *outdir, const rgba_t *image) {
    enum { COLS = 4, ROWS = 12, TW = 79, TH = 19, EW = 80, EH = 20, IMGW = 316 };
    static rgba_t tiles[ROWS * COLS][EW * EH];
    memset(tiles, 0, sizeof(tiles));
    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++)
            for (int y = 0; y < TH; y++)
                for (int x = 0; x < TW; x++)
                    tiles[row * COLS + col][y * EW + x] =
                        image[(row * TH + y) * IMGW + (col * TW + x)];
    /* no wrap: right edge from next col; last col duplicates its own */
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS - 1; col++)
            for (int y = 0; y < EH - 1; y++)
                tiles[row * COLS + col][(EW - 1) + y * EW] = tiles[row * COLS + col + 1][y * EW];
        for (int y = 0; y < EH - 1; y++)
            tiles[row * COLS + COLS - 1][(EW - 1) + y * EW] =
                tiles[row * COLS + COLS - 1][(EW - 2) + y * EW];
    }
    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++)
            for (int x = 0; x < EW; x++)
                tiles[row * COLS + col][x + (EH - 1) * EW] =
                    (row < ROWS - 1) ? tiles[(row + 1) * COLS + col][x]
                                     : tiles[row * COLS + col][x + (EH - 2) * EW];
    char path[1024];
    for (int i = 0; i < ROWS * COLS; i++) {  /* cake does not dedupe */
        snprintf(path, sizeof(path), "%s/gfx/textures/skybox_tiles/cake.%d.rgba16.png",
                 outdir, i);
        write_png(path, tiles[i], EW, EH, 4);
    }
}

/* ------------------------------------------------------------------ */

static void replay_sound_recipe(const char *outdir) {
    static const char *names[] = {"bank_sets", "sequences.bin",
                                  "sound_data.ctl", "sound_data.tbl"};
    const unsigned char *p = sSoundRecipe;
    const unsigned char *end = sSoundRecipe + sizeof(sSoundRecipe);
    for (int f = 0; f < 4; f++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/sound/%s", outdir, names[f]);
        mkdirs_for(path);
        FILE *out = fopen(path, "wb");
        if (!out) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
        while (p < end && *p != 0x00) {
            unsigned op = *p++;
            uint32_t a = (uint32_t) p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24;
            p += 4;
            if (op == 0x01) {
                uint32_t len = (uint32_t) p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24;
                p += 4;
                fwrite(sRom + a, 1, len, out);
            } else { /* 0x02 literal */
                fwrite(p, 1, a, out);
                p += a;
            }
        }
        p++; /* skip 0x00 terminator */
        fclose(out);
        printf("  sound/%s\n", names[f]);
    }
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <baserom.us.z64> [outdir]\n", argv[0]);
        return 1;
    }
    const char *outdir = argc > 2 ? argv[2] : "res";

    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    sRom = malloc(ROM_SIZE);
    size_t got = fread(sRom, 1, ROM_SIZE, f);
    fclose(f);
    if (got != ROM_SIZE) {
        fprintf(stderr, "ROM is %zu bytes, expected %d. This must be the 8MB US ROM.\n",
                got, ROM_SIZE);
        return 1;
    }

    /* accept byteswapped dumps */
    if (sRom[0] == 0x37 && sRom[1] == 0x80) {          /* .v64: swap u16 */
        for (size_t i = 0; i < ROM_SIZE; i += 2) {
            uint8_t t = sRom[i]; sRom[i] = sRom[i+1]; sRom[i+1] = t;
        }
        printf("(byteswapped .v64 ROM detected, converting)\n");
    } else if (sRom[0] == 0x40 && sRom[1] == 0x12) {   /* .n64: swap u32 */
        for (size_t i = 0; i < ROM_SIZE; i += 4) {
            uint8_t t0 = sRom[i], t1 = sRom[i+1];
            sRom[i] = sRom[i+3]; sRom[i+1] = sRom[i+2];
            sRom[i+2] = t1; sRom[i+3] = t0;
        }
        printf("(little-endian .n64 ROM detected, converting)\n");
    }

    char hex[41];
    sha1(sRom, ROM_SIZE, hex);
    if (strcmp(hex, sRomSha1) != 0) {
        fprintf(stderr, "This is not the US version of the ROM (sha1 %s,\n"
                        "expected %s). bingo64 needs the 8MB US ROM.\n", hex, sRomSha1);
        return 1;
    }
    printf("ROM verified (US). Extracting to %s/ ...\n", outdir);

    int ntex = (int) (sizeof(sTex) / sizeof(sTex[0]));
    for (int i = 0; i < ntex; i++) {
        const struct TexEntry *e = &sTex[i];
        uint32_t segsize;
        const uint8_t *seg = segment_data(e->mio0, &segsize);
        if (e->pos + e->size > segsize) {
            fprintf(stderr, "out of range: %s\n", e->path);
            return 1;
        }
        const uint8_t *raw = seg + e->pos;
        int count = e->w * e->h;
        char path[1024];
        snprintf(path, sizeof(path), "%s/gfx/%s", outdir, e->path);
        if (e->fmt == 0) {
            rgba_t *img = malloc(count * sizeof(rgba_t));
            raw2rgba(raw, img, count, e->depth);
            write_png(path, img, e->w, e->h, 4);
            free(img);
        } else {
            uint8_t *img = malloc(count * 2);
            raw2ia(raw, img, count, e->depth);
            write_png(path, img, e->w, e->h, 2);
            free(img);
        }
        if (i % 200 == 0) printf("  textures: %d/%d\n", i, ntex);
    }
    printf("  textures: %d/%d\n", ntex, ntex);

    int nsky = (int) (sizeof(sSky) / sizeof(sSky[0]));
    for (int i = 0; i < nsky; i++) {
        const struct SkyEntry *e = &sSky[i];
        uint32_t segsize;
        const uint8_t *seg = segment_data(e->mio0, &segsize);
        const uint8_t *blob = seg + e->pos;
        /* assets.json sizes can exceed the decompressed segment; python's
         * slicing truncates silently, so mirror that. */
        uint32_t size = e->size;
        if (e->pos + size > segsize) size = segsize - e->pos;
        char path[1024];
        if (e->cake) {
            rgba_t *combined = cake_combine(blob);
            snprintf(path, sizeof(path), "%s/gfx/levels/ending/cake.png", outdir);
            write_png(path, combined, 316, 228, 4);
            cake_split_write(outdir, combined);
            free(combined);
        } else {
            rgba_t *combined = sky_combine(blob, size);
            snprintf(path, sizeof(path), "%s/gfx/textures/skyboxes/%s.png", outdir, e->name);
            write_png(path, combined, 248, 248, 4);
            sky_split_write(outdir, e->name, combined);
            free(combined);
        }
        printf("  skybox: %s\n", e->name);
    }

    printf("  sound:\n");
    replay_sound_recipe(outdir);

    printf("Done. Put the res/ folder next to the game executable.\n");
    return 0;
}
