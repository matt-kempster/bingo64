/* Pixel-compare every PNG in dir A against the same path in dir B, and
 * byte-compare non-PNG files. Used to prove the extractor's output matches
 * the basepack the build system produces.
 *
 * usage: verify <dirA (reference)> <dirB (extracted)> <listfile>
 * listfile: one relative path per line.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../sm64tools/stb/stb_image.h"

static uint8_t *read_all(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *len = ftell(f); rewind(f);
    uint8_t *buf = malloc(*len);
    if (fread(buf, 1, *len, f) != (size_t) *len) { fclose(f); free(buf); return NULL; }
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s dirA dirB listfile\n", argv[0]); return 2; }
    FILE *list = fopen(argv[3], "r");
    if (!list) { perror(argv[3]); return 2; }
    char rel[1024], pa[2048], pb[2048];
    int checked = 0, bad = 0, missing = 0;
    while (fgets(rel, sizeof(rel), list)) {
        rel[strcspn(rel, "\r\n")] = 0;
        if (!rel[0]) continue;
        snprintf(pa, sizeof(pa), "%s/%s", argv[1], rel);
        snprintf(pb, sizeof(pb), "%s/%s", argv[2], rel);
        size_t n = strlen(rel);
        if (n > 4 && strcmp(rel + n - 4, ".png") == 0) {
            int wa, ha, wb, hb, ca, cb;
            uint8_t *ia = stbi_load(pa, &wa, &ha, &ca, 4);
            uint8_t *ib = stbi_load(pb, &wb, &hb, &cb, 4);
            if (!ia || !ib) {
                printf("MISSING %s (%s)\n", rel, !ia ? "ref" : "extracted");
                missing++;
            } else if (wa != wb || ha != hb || memcmp(ia, ib, (size_t) wa * ha * 4) != 0) {
                printf("DIFF    %s (%dx%d vs %dx%d)\n", rel, wa, ha, wb, hb);
                bad++;
            }
            free(ia); free(ib);
        } else {
            long la, lb;
            uint8_t *da = read_all(pa, &la);
            uint8_t *db = read_all(pb, &lb);
            if (!da || !db) { printf("MISSING %s\n", rel); missing++; }
            else if (la != lb || memcmp(da, db, la) != 0) {
                printf("DIFF    %s (%ld vs %ld bytes)\n", rel, la, lb);
                bad++;
            }
            free(da); free(db);
        }
        checked++;
    }
    printf("checked %d files: %d diffs, %d missing\n", checked, bad, missing);
    return (bad || missing) ? 1 : 0;
}
