/* nav_trip.h - trip statistics (no LVGL dependency, easy to unit test). */
#ifndef NAV_TRIP_H
#define NAV_TRIP_H

typedef struct {
    double dist_m;        /* distance travelled */
    double moving_s;      /* time spent moving (> ~3.6 km/h) */
    double total_s;       /* time since reset */
    float max_kmh;
} nav_trip_t;

void nav_trip_reset(nav_trip_t *t);

/* Call every frame. */
void nav_trip_tick(nav_trip_t *t, float dt, float speed_ms);

#endif
