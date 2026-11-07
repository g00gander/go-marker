/* Checks the fast street/route rasterizer (ui/nav_raster.h) against the straightforward one, on thousands of random segments: lines of every
 * angle and length, thin and wide, one or two masks, partly off the box. Pass = never more than one level apart.
 * Build and run: cc -O2 -DNAV_RASTER_WITH_REF -I ui tests/raster_test.c -lm -o /tmp/raster_test && /tmp/raster_test */
#include "nav_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 360
#define H 40

static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

int main(void)
{
    static uint8_t a0[W * H], a1[W * H], b0[W * H], b1[W * H];
    int worst = 0;
    long differ = 0, total_px = 0;
    srand(12345);
    for (int trial = 0; trial < 20000; trial++) {
        memset(a0, 0, sizeof a0); memset(a1, 0, sizeof a1); memset(b0, 0, sizeof b0); memset(b1, 0, sizeof b1);
        const int two = trial & 1;
        const int pieces = 1 + rand() % 4;
        for (int p = 0; p < pieces; p++) {
            float ax = frand(-60, W + 60), ay = frand(-30, H + 30), bx, by;
            switch (rand() % 5) {
            case 0: bx = ax + frand(-3, 3); by = ay + frand(-300, 300); break;        /* near vertical */
            case 1: bx = ax + frand(-300, 300); by = ay + frand(-3, 3); break;        /* near horizontal */
            case 2: bx = ax; by = ay; break;                                          /* a point */
            case 3: bx = ax + frand(-6, 6); by = ay + frand(-6, 6); break;            /* very short */
            default: bx = ax + frand(-300, 300); by = ay + frand(-300, 300); break;
            }
            const float h0 = frand(1.0f, 7.0f), h1 = two ? frand(0.5f, h0) : 0;
            int32_t x1 = (int32_t)floorf((ax < bx ? ax : bx) - h0 - 2), x2 = (int32_t)ceilf((ax > bx ? ax : bx) + h0 + 2);
            int32_t y1 = (int32_t)floorf((ay < by ? ay : by) - h0 - 2), y2 = (int32_t)ceilf((ay > by ? ay : by) + h0 + 2);
            if (x1 < 0) x1 = 0;
            if (x2 > W - 1) x2 = W - 1;
            if (y1 < 0) y1 = 0;
            if (y2 > H - 1) y2 = H - 1;
            if (x1 > x2 || y1 > y2) continue;
            nav_seg_cover_ref(a0, two ? a1 : NULL, W, 0, 0, x1, x2, y1, y2, ax, ay, bx, by, h0, h1);
            nav_seg_cover(b0, two ? b1 : NULL, W, 0, 0, x1, x2, y1, y2, ax, ay, bx, by, h0, h1);
        }
        for (int i = 0; i < W * H; i++) {
            int d = abs((int)a0[i] - (int)b0[i]);
            if (two) { int e = abs((int)a1[i] - (int)b1[i]); if (e > d) d = e; }
            if (d) differ++;
            if (d > worst) worst = d;
            total_px++;
        }
    }
    printf("pixels compared %ld, differing %ld, worst difference %d level(s)\n", total_px, differ, worst);

    /* speed on this machine (not the board): a street-sized batch of diagonal segments */
    const int reps = 3000;
    clock_t t0 = clock();
    for (int r = 0; r < reps; r++) for (int s = 0; s < 40; s++) nav_seg_cover_ref(a0, NULL, W, 0, 0, 0, W - 1, 0, H - 1, 10.0f + s * 8, 0, 90.0f + s * 8, 38, 2.1f, 0);
    clock_t t1 = clock();
    for (int r = 0; r < reps; r++) for (int s = 0; s < 40; s++) nav_seg_cover(b0, NULL, W, 0, 0, 0, W - 1, 0, H - 1, 10.0f + s * 8, 0, 90.0f + s * 8, 38, 2.1f, 0);
    clock_t t2 = clock();
    printf("time: reference %.1f ms, fast %.1f ms\n", 1000.0 * (t1 - t0) / CLOCKS_PER_SEC, 1000.0 * (t2 - t1) / CLOCKS_PER_SEC);
    if (worst > 1) { printf("FAIL: more than one level apart\n"); return 1; }
    printf("PASS\n");
    return 0;
}
