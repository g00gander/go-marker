#ifndef NAV_RASTER_H
#define NAV_RASTER_H

/* Exact-edge coverage masks for the map's streets and route. Header only, so the firmware, simulator, and tests all build it. */

#include <math.h>
#include <stdint.h>
#include <stddef.h>

/* The span [*xa, *xb] of row `cy` that a segment's capsule of radius `r` can touch; returns 0 if the row is out of reach. */
static inline int nav_seg_row_span(float ax, float ay, float bx, float by, float vx, float vy, float cy, float r, float *xa, float *xb)
{
    if (fabsf(vy) < 1e-3f) {
        if (cy < ay - r || cy > ay + r) return 0;
        *xa = (ax < bx ? ax : bx) - r; *xb = (ax > bx ? ax : bx) + r;
        return 1;
    }
    float tl = (cy - r - ay) / vy, th = (cy + r - ay) / vy;
    if (tl > th) { const float t = tl; tl = th; th = t; }
    if (th < 0 || tl > 1) return 0;
    if (tl < 0) tl = 0;
    if (th > 1) th = 1;
    const float x1 = ax + tl * vx, x2 = ax + th * vx;
    *xa = (x1 < x2 ? x1 : x2) - r; *xb = (x1 > x2 ? x1 : x2) + r;
    (void)bx; (void)by;
    return 1;
}

/* floor and ceiling by integer cast: the chip has no rounding instruction, so floorf/ceilf are library calls (values here are small). */
static inline int nav_ifloor(float x) { const int i = (int)x; return i - (x < (float)i); }
static inline int nav_iceil(float x) { const int i = (int)x; return i + (x > (float)i); }

/* The k in [0, n-1] with |c + k * s| <= t as [*lo, *hi] (empty when *lo > *hi); `is` is 1 / s, or 0 when s is too small to divide by. */
static inline void nav_raster_range(float c, float s, float is, float t, int n, int *lo, int *hi)
{
    if (t < 0) { *lo = 1; *hi = 0; return; }
    if (is == 0) {
        if (fabsf(c) <= t) { *lo = 0; *hi = n - 1; } else { *lo = 1; *hi = 0; }
        return;
    }
    float a = (-t - c) * is, b = (t - c) * is;
    if (a > b) { const float x = a; a = b; b = x; }
    if (a < 0) a = 0;
    if (b > (float)(n - 1)) b = (float)(n - 1);
    (void)s;
    *lo = nav_iceil(a);
    *hi = nav_ifloor(b);
}

/* nav_seg_row_span with the divide by vy done once (ivy = 1 / vy, or 0 for a horizontal segment). */
static inline int nav_seg_row_span_fast(float ax, float ay, float bx, float vx, float vy, float ivy, float cy, float r, float *xa, float *xb)
{
    if (ivy == 0) {
        if (cy < ay - r || cy > ay + r) return 0;
        *xa = (ax < bx ? ax : bx) - r; *xb = (ax > bx ? ax : bx) + r;
        return 1;
    }
    float tl = (cy - r - ay) * ivy, th = (cy + r - ay) * ivy;
    if (tl > th) { const float t = tl; tl = th; th = t; }
    if (th < 0 || tl > 1) return 0;
    if (tl < 0) tl = 0;
    if (th > 1) th = 1;
    const float x1 = ax + tl * vx, x2 = ax + th * vx;
    *xa = (x1 < x2 ? x1 : x2) - r; *xb = (x1 > x2 ? x1 : x2) + r;
    (void)vy;
    return 1;
}

/* One pixel, the general way (round ends included): `d` from the segment, then the coverage for each mask. `dx0`/`dy0` are from the segment's start. */
static inline void nav_raster_px(uint8_t *r0, uint8_t *r1, int32_t x, float dot, float cross, float dx0, float dy0,
                                 float vx, float vy, float len2, float inv, float lim, float h0, float h1)
{
    float d;
    if (dot <= 0) {
        const float d2 = dx0 * dx0 + dy0 * dy0;
        if (d2 >= lim) return;
        d = sqrtf(d2);
    } else if (dot >= len2) {
        const float ex = dx0 - vx, ey = dy0 - vy, d2 = ex * ex + ey * ey;
        if (d2 >= lim) return;
        d = sqrtf(d2);
    } else {
        d = fabsf(cross) * inv;
        if (d >= h0 + 1.0f) return;
    }
    const float c0 = h0 + 0.5f - d;
    if (c0 <= 0) return;
    const uint8_t v0 = (uint8_t)((c0 > 1 ? 1 : c0) * 255.0f + 0.5f);
    if (v0 > r0[x]) r0[x] = v0;
    if (r1) {
        const float c1 = h1 + 0.5f - d;
        if (c1 > 0) { const uint8_t v1 = (uint8_t)((c1 > 1 ? 1 : c1) * 255.0f + 0.5f); if (v1 > r1[x]) r1[x] = v1; }
    }
}

/* Fills one segment's coverage into one or two 8-bit masks (keeping the larger value where pieces overlap): `m0` a line of half width h0, `m1` (if not NULL) h1 <= h0.
 * Only the box x1..x2, y1..y2 is touched; matches nav_seg_cover_ref to within a level. */
static inline void nav_seg_cover(uint8_t *m0, uint8_t *m1, int stride, int32_t mx0, int32_t my0, int32_t x1, int32_t x2, int32_t y1, int32_t y2,
                                 float ax, float ay, float bx, float by, float h0, float h1)
{
    const float vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
    const float inv = len2 > 0 ? 1.0f / sqrtf(len2) : 0;
    const float lim = (h0 + 1.0f) * (h0 + 1.0f);
    /* coverage in levels (0..255) as a function of the cross product: v = A - |cross| * K */
    const float K = inv * 255.0f, A0 = (h0 + 0.5f) * 255.0f, A1 = (h1 + 0.5f) * 255.0f;
    const float full_t = (m1 ? h1 : h0) - 0.5f - 0.002f;       /* |distance| up to this is fully covered (in every mask asked for) */
    /* the divides, once for the whole segment */
    const float ivy = fabsf(vy) < 1e-3f ? 0.0f : 1.0f / vy;                                   /* (same cut-off as nav_seg_row_span) */
    const float ivx = fabsf(vx) < 1e-6f ? 0.0f : 1.0f / vx;
    const float svy = vy * inv;
    const float isvy = fabsf(svy) < 1e-6f ? 0.0f : 1.0f / svy;
    for (int32_t y = y1; y <= y2; y++) {
        const float cy = (float)y + 0.5f;
        float fxa, fxb;
        if (!nav_seg_row_span_fast(ax, ay, bx, vx, vy, ivy, cy, h0 + 1.0f, &fxa, &fxb)) continue;
        int32_t xs = nav_ifloor(fxa) - 1, xe = nav_iceil(fxb) + 1;
        if (xs < x1) xs = x1;
        if (xe > x2) xe = x2;
        if (xs > xe) continue;
        const int n = xe - xs + 1;
        uint8_t *r0 = m0 + (size_t)(y - my0) * stride - mx0;
        uint8_t *r1 = m1 ? m1 + (size_t)(y - my0) * stride - mx0 : NULL;
        const float rx = ((float)xs + 0.5f) - ax, ry = cy - ay;
        const float dot0 = rx * vx + ry * vy, cross0 = rx * vy - ry * vx;

        /* the pixels along the body of the segment (not in a round end), and among them the fully covered ones */
        int ml, mr, fl, fr;
        nav_raster_range(dot0 - len2 * 0.5f, vx, ivx, len2 * 0.5f - 0.01f, n, &ml, &mr);
        if (ml > mr || len2 <= 0) { ml = n; mr = n - 1; fl = n; fr = n - 1; }
        else {
            nav_raster_range(cross0 * inv, svy, isvy, full_t, n, &fl, &fr);
            if (fl < ml) fl = ml;
            if (fr > mr) fr = mr;
        }

        float dot = dot0, cross = cross0, dx0 = rx;
        int k = 0;
        for (; k < ml; k++, dot += vx, cross += vy, dx0 += 1.0f)         /* before the body: a round end, or nothing */
            nav_raster_px(r0, r1, xs + k, dot, cross, dx0, ry, vx, vy, len2, inv, lim, h0, h1);
        if (fl <= fr) {
            /* body, soft edge before the full part */
            float t = cross0 + (float)k * vy;
            for (; k < fl; k++, t += vy) {
                const float c0 = A0 - fabsf(t) * K;
                if (c0 > 0) { const uint8_t v = (uint8_t)((c0 > 255.0f ? 255.0f : c0) + 0.5f); if (v > r0[xs + k]) r0[xs + k] = v; }
                if (r1) { const float c1 = A1 - fabsf(t) * K; if (c1 > 0) { const uint8_t v = (uint8_t)((c1 > 255.0f ? 255.0f : c1) + 0.5f); if (v > r1[xs + k]) r1[xs + k] = v; } }
            }
            for (int q = fl; q <= fr; q++) { r0[xs + q] = 255; if (r1) r1[xs + q] = 255; }     /* the solid middle */
            k = fr + 1;
            t = cross0 + (float)k * vy;
            for (; k <= mr; k++, t += vy) {
                const float c0 = A0 - fabsf(t) * K;
                if (c0 > 0) { const uint8_t v = (uint8_t)((c0 > 255.0f ? 255.0f : c0) + 0.5f); if (v > r0[xs + k]) r0[xs + k] = v; }
                if (r1) { const float c1 = A1 - fabsf(t) * K; if (c1 > 0) { const uint8_t v = (uint8_t)((c1 > 255.0f ? 255.0f : c1) + 0.5f); if (v > r1[xs + k]) r1[xs + k] = v; } }
            }
        } else {
            float t = cross0 + (float)k * vy;
            for (; k <= mr; k++, t += vy) {            /* body, no fully covered pixel in this row (a thin or shallow crossing) */
                const float c0 = A0 - fabsf(t) * K;
                if (c0 > 0) { const uint8_t v = (uint8_t)((c0 > 255.0f ? 255.0f : c0) + 0.5f); if (v > r0[xs + k]) r0[xs + k] = v; }
                if (r1) { const float c1 = A1 - fabsf(t) * K; if (c1 > 0) { const uint8_t v = (uint8_t)((c1 > 255.0f ? 255.0f : c1) + 0.5f); if (v > r1[xs + k]) r1[xs + k] = v; } }
            }
        }
        if (k <= mr) k = mr + 1;
        dot = dot0 + (float)k * vx; cross = cross0 + (float)k * vy; dx0 = rx + (float)k;
        for (; k < n; k++, dot += vx, cross += vy, dx0 += 1.0f)          /* after the body: the other round end */
            nav_raster_px(r0, r1, xs + k, dot, cross, dx0, ry, vx, vy, len2, inv, lim, h0, h1);
    }
}


/* Blends one color over `n` pixels of a 24-bit (B G R) row at opacity a (0..255), four at a time; `d` must be 4-byte aligned.
 * Leftover pixels are blended singly; returns how many went the fast way. */
static inline int nav_row_blend_bgr(uint8_t *d, int n, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    const uint32_t sa = (a * 257 + 128) >> 8, ia = 256 - sa;           /* opacity out of 256, so the result is a plain shift */
    const uint32_t cb = b * sa + 128, cg = g * sa + 128, cr = r * sa + 128;
    /* the twelve bytes of four pixels are B G R B | G R B G | R B G R: even bytes and odd bytes of each word, as two 16-bit lanes */
    const uint32_t ce0 = cb | (cr << 16), co0 = cg | (cb << 16);
    const uint32_t ce1 = cg | (cb << 16), co1 = cr | (cg << 16);
    const uint32_t ce2 = cr | (cg << 16), co2 = cb | (cr << 16);
    uint32_t *w = (uint32_t *)(void *)d;
    int k = 0;
    for (; k + 4 <= n; k += 4, w += 3) {
        uint32_t v, e, o;
        v = w[0]; e = ((v & 0x00FF00FFu) * ia + ce0) >> 8; o = (((v >> 8) & 0x00FF00FFu) * ia + co0) >> 8;
        w[0] = (e & 0x00FF00FFu) | ((o & 0x00FF00FFu) << 8);
        v = w[1]; e = ((v & 0x00FF00FFu) * ia + ce1) >> 8; o = (((v >> 8) & 0x00FF00FFu) * ia + co1) >> 8;
        w[1] = (e & 0x00FF00FFu) | ((o & 0x00FF00FFu) << 8);
        v = w[2]; e = ((v & 0x00FF00FFu) * ia + ce2) >> 8; o = (((v >> 8) & 0x00FF00FFu) * ia + co2) >> 8;
        w[2] = (e & 0x00FF00FFu) | ((o & 0x00FF00FFu) << 8);
    }
    return k;
}

#ifdef NAV_RASTER_WITH_REF
/* The straightforward version, one distance per pixel: the reference the fast one is tested against. */
static inline void nav_seg_cover_ref(uint8_t *m0, uint8_t *m1, int stride, int32_t mx0, int32_t my0, int32_t x1, int32_t x2, int32_t y1, int32_t y2,
                                     float ax, float ay, float bx, float by, float h0, float h1)
{
    const float vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
    const float inv = len2 > 0 ? 1.0f / sqrtf(len2) : 0;
    const float lim = (h0 + 1.0f) * (h0 + 1.0f);
    for (int32_t y = y1; y <= y2; y++) {
        const float cy = (float)y + 0.5f;
        float fxa, fxb;
        if (!nav_seg_row_span(ax, ay, bx, by, vx, vy, cy, h0 + 1.0f, &fxa, &fxb)) continue;
        int32_t xs = (int32_t)floorf(fxa) - 1, xe = (int32_t)ceilf(fxb) + 1;
        if (xs < x1) xs = x1;
        if (xe > x2) xe = x2;
        if (xs > xe) continue;
        uint8_t *r0 = m0 + (size_t)(y - my0) * stride - mx0;
        uint8_t *r1 = m1 ? m1 + (size_t)(y - my0) * stride - mx0 : NULL;
        const float rx = ((float)xs + 0.5f) - ax, ry = cy - ay;
        float dot = rx * vx + ry * vy, cross = rx * vy - ry * vx;
        float dx0 = rx;
        for (int32_t x = xs; x <= xe; x++, dot += vx, cross += vy, dx0 += 1.0f)
            nav_raster_px(r0, r1, x, dot, cross, dx0, ry, vx, vy, len2, inv, lim, h0, h1);
    }
}
#endif

#endif
