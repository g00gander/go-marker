#include "nav_trip_store.h"

#include <string.h>

#include "nav_route_rx.h"     /* nav_crc16 */

/* Record layout, little-endian:
 *   0  u32 magic 'NTRP'   4  u8 version   5  u32 sequence
 *   9  u32 distance (decimeters)   13 u32 moving seconds   17 u32 total seconds
 *   21 u16 max speed (km/h x 10)   23 u16 crc16 of bytes 0..22 */
#define MAGIC 0x5052544Eu
#define VERSION 1

static const nav_store_ops_t *ops;
static uint32_t seq;          /* sequence number of the newest record we know about */

void nav_store_init(const nav_store_ops_t *o)
{
    ops = o;
    seq = 0;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t clamp_u32(double v) { return v <= 0 ? 0 : (v >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)v); }

static void encode(uint8_t *r, const nav_trip_t *t, uint32_t sequence)
{
    put32(r + 0, MAGIC);
    r[4] = VERSION;
    put32(r + 5, sequence);
    put32(r + 9, clamp_u32(t->dist_m * 10.0));
    put32(r + 13, clamp_u32(t->moving_s));
    put32(r + 17, clamp_u32(t->total_s));
    double mx = t->max_kmh * 10.0;
    put16(r + 21, (uint16_t)(mx <= 0 ? 0 : (mx >= 65535.0 ? 65535 : mx)));
    put16(r + 23, nav_crc16(r, 23));
}

/* Returns 1 and fills *t / *sequence if the record is valid. */
static int decode(const uint8_t *r, size_t n, nav_trip_t *t, uint32_t *sequence)
{
    if (n < NAV_TRIP_RECORD_SIZE) return 0;
    if (get32(r) != MAGIC || r[4] != VERSION) return 0;
    if (get16(r + 23) != nav_crc16(r, 23)) return 0;
    *sequence = get32(r + 5);
    memset(t, 0, sizeof *t);
    t->dist_m = get32(r + 9) / 10.0;
    t->moving_s = get32(r + 13);
    t->total_s = get32(r + 17);
    t->max_kmh = get16(r + 21) / 10.0f;
    return 1;
}

int nav_trip_load(nav_trip_t *out)
{
    if (!ops) return 0;
    int found = 0;
    uint32_t best_seq = 0;
    for (int slot = 0; slot < 2; slot++) {
        uint8_t buf[NAV_TRIP_RECORD_SIZE];
        int n = ops->read(slot, buf, sizeof buf);
        nav_trip_t t;
        uint32_t s;
        if (n > 0 && decode(buf, (size_t)n, &t, &s) && (!found || s > best_seq)) {
            best_seq = s;
            *out = t;
            found = 1;
        }
    }
    if (found) seq = best_seq;
    return found;
}

int nav_trip_save(const nav_trip_t *t)
{
    if (!ops) return -1;
    uint32_t next = seq + 1;
    uint8_t rec[NAV_TRIP_RECORD_SIZE];
    encode(rec, t, next);
    int rc = ops->write((int)(next & 1), rec, sizeof rec);   /* always the older slot */
    if (rc == 0) seq = next;
    return rc;
}
