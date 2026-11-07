/* nav_roads_rx.h - reassembles background road segments sent as ROADS chunks. No LVGL dependency. */
#ifndef NAV_ROADS_RX_H
#define NAV_ROADS_RX_H

#include <stddef.h>
#include <stdint.h>
#include "nav_protocol.h"
#include "nav_route_rx.h"     /* nav_rx_result_t, nav_crc16 */

typedef struct {
    nav_seg_t segs[NAV_MAX_ROADS];
    uint16_t expected;
    uint16_t received;
    int32_t  origin_lat_e7;
    int32_t  origin_lon_e7;
    uint8_t  active;
    uint8_t  raw[NAV_MAX_ROADS * NAV_ROADS_REC];   /* wire bytes, kept for the CRC check */
} nav_roads_rx_t;

void nav_roads_rx_reset(nav_roads_rx_t *rx);
nav_rx_result_t nav_roads_rx_feed(nav_roads_rx_t *rx, const uint8_t *data, size_t len);

#endif
