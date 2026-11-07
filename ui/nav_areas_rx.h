/* nav_areas_rx.h - reassembles park and water polygons sent as AREAS chunks. No LVGL dependency. */
#ifndef NAV_AREAS_RX_H
#define NAV_AREAS_RX_H

#include <stddef.h>
#include <stdint.h>
#include "nav_protocol.h"
#include "nav_route_rx.h"     /* nav_rx_result_t, nav_crc16 */

#define NAV_AREAS_MAX_RAW (NAV_MAX_POLYS * (2 + 4 * NAV_POLY_MAXV))

typedef struct {
    nav_poly_t polys[NAV_MAX_POLYS];
    uint16_t expected, received;
    int32_t origin_lat_e7, origin_lon_e7;
    uint8_t active;
    uint16_t raw_len;
    uint8_t raw[NAV_AREAS_MAX_RAW];
} nav_areas_rx_t;

void nav_areas_rx_reset(nav_areas_rx_t *rx);
nav_rx_result_t nav_areas_rx_feed(nav_areas_rx_t *rx, const uint8_t *data, size_t len);

#endif
