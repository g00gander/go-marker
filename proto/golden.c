/* Prints sample packets as hex so the Swift encoder can be checked against the C structs.
 * Build: cc -I. -o /tmp/golden golden.c && /tmp/golden */
#include <stdio.h>
#include <string.h>
#include "nav_protocol.h"

static void dump(const char *name, const void *p, size_t n)
{
    printf("%s ", name);
    for (size_t i = 0; i < n; i++) printf("%02x", ((const unsigned char *)p)[i]);
    printf("\n");
}

int main(void)
{
    nav_status_t s;
    memset(&s, 0, sizeof s);
    s.version = 1; s.maneuver = NAV_MAN_RIGHT; s.next_maneuver = NAV_MAN_SLIGHT_LEFT;
    s.flags = NAV_FLAG_NAVIGATING | NAV_FLAG_NIGHT;
    s.lat_e7 = 377749000; s.lon_e7 = -1224194000;
    s.heading_x10 = 2705; s.dist_turn_m = 180; s.dist_next_m = 450;
    s.remaining_m = 4200; s.eta_min = 8; s.speed_kmh_x10 = 453;
    strncpy(s.street, "Ring Road", NAV_STREET_LEN);
    dump("STATUS", &s, sizeof s);

    nav_weather_t w;
    memset(&w, 0, sizeof w);
    w.version = 1; w.condition = NAV_WX_RAIN; w.flags = NAV_WX_FLAG_NIGHT; w.hour0 = 15;
    w.temp_x10 = -35; w.feels_x10 = -80; w.hi_c = 22; w.lo_c = -5;
    w.wind_kmh = 19; w.gust_kmh = 32; w.wind_from_deg = 300; w.precip_pct = 80; w.uv = 4;
    for (int i = 0; i < 4; i++) {
        w.hourly[i].temp_c = (int8_t)(19 - 2 * i);
        w.hourly[i].condition = (uint8_t)(i + 1);
        w.hourly[i].precip_pct = (uint8_t)(20 * (i + 1));
    }
    dump("WEATHER", &w, sizeof w);
    return 0;
}
