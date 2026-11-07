/* Unit test for trip persistence, including power cuts in the middle of a save.
 *   cc -I../ui -I../proto -o /tmp/trip_store_test trip_store_test.c ../ui/nav_trip_store.c ../ui/nav_trip.c ../ui/nav_route_rx.c -lm && /tmp/trip_store_test */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "nav_trip_store.h"

static uint8_t slots[2][NAV_TRIP_RECORD_SIZE];
static int slot_len[2];
static int fail_next_write_partially;      /* simulate a power cut halfway through a write */

static int rd(int s, uint8_t *b, size_t n) { if (slot_len[s] <= 0) return -1; memcpy(b, slots[s], (size_t)slot_len[s] < n ? (size_t)slot_len[s] : n); return slot_len[s]; }
static int wr(int s, const uint8_t *b, size_t n)
{
    if (fail_next_write_partially) {           /* half the new bytes land, the rest is whatever was there */
        fail_next_write_partially = 0;
        memcpy(slots[s], b, n / 2);
        slot_len[s] = (int)n;
        return -1;
    }
    memcpy(slots[s], b, n);
    slot_len[s] = (int)n;
    return 0;
}
static const nav_store_ops_t ops = {rd, wr};

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void reset_media(void) { memset(slots, 0, sizeof slots); slot_len[0] = slot_len[1] = 0; nav_store_init(&ops); }
static nav_trip_t make(double dist, double moving, double total, float mx)
{
    nav_trip_t t = {dist, moving, total, mx};
    return t;
}

int main(void)
{
    nav_trip_t t, got;

    /* nothing saved yet */
    reset_media();
    CHECK(nav_trip_load(&got) == 0);

    /* round trip */
    t = make(12345.6, 900, 1200, 87.3f);
    CHECK(nav_trip_save(&t) == 0);
    nav_store_init(&ops);                       /* "reboot" */
    CHECK(nav_trip_load(&got) == 1);
    CHECK(fabs(got.dist_m - 12345.6) < 0.11);
    CHECK(got.moving_s == 900 && got.total_s == 1200);
    CHECK(fabsf(got.max_kmh - 87.3f) < 0.11f);

    /* newest of two valid slots wins, and saves alternate */
    reset_media();
    for (int i = 1; i <= 5; i++) { t = make(i * 1000, i, i, 10); nav_trip_save(&t); }
    nav_store_init(&ops);
    CHECK(nav_trip_load(&got) == 1 && got.dist_m == 5000);
    CHECK(slot_len[0] > 0 && slot_len[1] > 0);

    /* power cut during a save: the older good record survives */
    t = make(6000, 6, 6, 10);
    fail_next_write_partially = 1;
    CHECK(nav_trip_save(&t) != 0);
    nav_store_init(&ops);
    CHECK(nav_trip_load(&got) == 1);
    CHECK(got.dist_m == 5000);                  /* still the last complete save */

    /* after that, saving again works and becomes the newest */
    nav_trip_save(&t);
    nav_store_init(&ops);
    CHECK(nav_trip_load(&got) == 1 && got.dist_m == 6000);

    /* a flipped bit invalidates a record; the other slot is used */
    slots[0][10] ^= 0x40;
    slots[1][10] ^= 0x40;
    nav_store_init(&ops);
    CHECK(nav_trip_load(&got) == 0);            /* both damaged: start fresh, do not crash */

    /* wrong version or magic is ignored */
    reset_media();
    t = make(1, 1, 1, 1);
    nav_trip_save(&t);
    slots[1][4] = 99; slots[0][4] = 99;
    nav_store_init(&ops);
    CHECK(nav_trip_load(&got) == 0);

    /* persistence off */
    nav_store_init(NULL);
    CHECK(nav_trip_load(&got) == 0 && nav_trip_save(&t) != 0);

    printf(failures ? "%d FAILURES\n" : "trip store: all checks passed\n", failures);
    return failures ? 1 : 0;
}
