/* nav_route_rx.h - reassembles a route sent as ROUTE chunks. No LVGL dependency. */
#ifndef NAV_ROUTE_RX_H
#define NAV_ROUTE_RX_H

#include <stddef.h>
#include <stdint.h>
#include "nav_protocol.h"

#define NAV_RX_MAX_POINTS 600

typedef struct {
    nav_pt_t pts[NAV_RX_MAX_POINTS];
    uint16_t expected;       /* points announced by the header */
    uint16_t received;       /* points stored so far */
    int32_t  origin_lat_e7;
    int32_t  origin_lon_e7;
    uint8_t  active;         /* a header has been seen */
} nav_route_rx_t;

typedef enum {
    NAV_RX_PENDING = 0,      /* chunk accepted, route not finished */
    NAV_RX_READY,            /* whole route received and the CRC matched */
    NAV_RX_ERROR,            /* bad chunk, out of order, too long, or CRC mismatch */
} nav_rx_result_t;

uint16_t nav_crc16(const uint8_t *data, size_t len);   /* CRC-16/CCITT-FALSE */

void nav_route_rx_reset(nav_route_rx_t *rx);
nav_rx_result_t nav_route_rx_feed(nav_route_rx_t *rx, const uint8_t *data, size_t len);

#endif
