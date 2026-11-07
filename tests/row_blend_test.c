/* Checks the four-pixels-at-a-time row blend (ui/nav_raster.h) against the exact per-pixel blend, over every opacity and many colors. Pass = never more
 * than one level apart. Build and run: cc -O2 -I ui tests/row_blend_test.c -o /tmp/row_blend_test && /tmp/row_blend_test */
#include "nav_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t div255(uint32_t v) { v += 128; return (v + (v >> 8)) >> 8; }

int main(void)
{
    static uint32_t buf[3 * 64];            /* aligned: 256 pixels */
    uint8_t *row = (uint8_t *)buf, ref[768];
    int worst = 0;
    srand(7);
    for (int trial = 0; trial < 200000; trial++) {
        for (int i = 0; i < 768; i++) row[i] = (uint8_t)rand();
        memcpy(ref, row, 768);
        const uint32_t r = rand() & 255, g = rand() & 255, b = rand() & 255, a = (trial % 256);
        const int n = 4 + rand() % 250;
        const int done = nav_row_blend_bgr(row, n, r, g, b, a);
        for (int k = 0; k < done; k++) {
            ref[k * 3] = (uint8_t)div255(ref[k * 3] * (255 - a) + b * a);
            ref[k * 3 + 1] = (uint8_t)div255(ref[k * 3 + 1] * (255 - a) + g * a);
            ref[k * 3 + 2] = (uint8_t)div255(ref[k * 3 + 2] * (255 - a) + r * a);
        }
        for (int i = 0; i < 768; i++) { int d = abs((int)row[i] - (int)ref[i]); if (d > worst) worst = d; }
        if (done > n || (done & 3)) { printf("FAIL: did %d of %d\n", done, n); return 1; }
    }
    printf("worst difference %d level(s)\n", worst);
    if (worst > 1) { printf("FAIL\n"); return 1; }
    printf("PASS\n");
    return 0;
}
