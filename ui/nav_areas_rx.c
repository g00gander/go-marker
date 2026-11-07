#include "nav_areas_rx.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

void nav_areas_rx_reset(nav_areas_rx_t *rx)
{
    rx->expected = 0;
    rx->received = 0;
    rx->raw_len = 0;
    rx->active = 0;
}

nav_rx_result_t nav_areas_rx_feed(nav_areas_rx_t *rx, const uint8_t *d, size_t n)
{
    if (n < 1) return NAV_RX_ERROR;

    switch (d[0]) {
    case NAV_AREAS_HEADER:
        if (n < 11) return NAV_RX_ERROR;
        nav_areas_rx_reset(rx);
        rx->expected = rd16(d + 1);
        rx->origin_lat_e7 = rd32(d + 3);
        rx->origin_lon_e7 = rd32(d + 7);
        if (rx->expected > NAV_MAX_POLYS) return NAV_RX_ERROR;
        rx->active = 1;
        return NAV_RX_PENDING;

    case NAV_AREAS_POLYS: {
        if (!rx->active || n < 3) return NAV_RX_ERROR;
        if (rd16(d + 1) != rx->received) { rx->active = 0; return NAV_RX_ERROR; }
        size_t off = 3;
        uint16_t got = rx->received;
        while (off < n) {
            if (got >= rx->expected || off + 2 > n) { rx->active = 0; return NAV_RX_ERROR; }
            uint8_t kind = d[off], nv = d[off + 1];
            size_t need = 2 + (size_t)nv * 4;
            if (nv < 3 || nv > NAV_POLY_MAXV || off + need > n || (size_t)rx->raw_len + need > NAV_AREAS_MAX_RAW) {
                rx->active = 0;
                return NAV_RX_ERROR;
            }
            nav_poly_t *p = &rx->polys[got];
            p->kind = kind;
            p->n = nv;
            for (int i = 0; i < nv; i++) {
                p->x[i] = (int16_t)rd16(d + off + 2 + i * 4);
                p->y[i] = (int16_t)rd16(d + off + 4 + i * 4);
            }
            memcpy(rx->raw + rx->raw_len, d + off, need);
            rx->raw_len = (uint16_t)(rx->raw_len + need);
            off += need;
            got++;
        }
        rx->received = got;
        return NAV_RX_PENDING;
    }

    case NAV_AREAS_END: {
        if (!rx->active || n < 3) return NAV_RX_ERROR;
        rx->active = 0;
        if (rx->received != rx->expected) return NAV_RX_ERROR;
        return rd16(d + 1) == nav_crc16(rx->raw, rx->raw_len) ? NAV_RX_READY : NAV_RX_ERROR;
    }

    default:
        return NAV_RX_ERROR;
    }
}
