#include "nav_trip.h"

#include <string.h>

void nav_trip_reset(nav_trip_t *t)
{
    memset(t, 0, sizeof(*t));
}

void nav_trip_tick(nav_trip_t *t, float dt, float speed_ms)
{
    t->total_s += dt;
    if (speed_ms > 1.0f) {
        t->dist_m += speed_ms * dt;
        t->moving_s += dt;
    }
    float kmh = speed_ms * 3.6f;
    if (kmh > t->max_kmh) t->max_kmh = kmh;
}
