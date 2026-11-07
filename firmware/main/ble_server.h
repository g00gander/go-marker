/* ble_server.h - NimBLE GATT server for the phone link (layout in proto/nav_protocol.h). */
#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <stdint.h>

void ble_server_start(void);
/* Deliver queued writes to nav_link. Returns how many were handled. */
int ble_server_poll(void);
int ble_server_connected(void);
/* The 6-digit pairing code to show on screen; 0 = nothing to show. */
uint32_t ble_server_pairing_code(void);
/* 1 once the phone on the link has paired (the link is encrypted and authenticated), so it may send data. */
int ble_server_secure(void);
/* Send one packet of motion readings; returns 0 if sent, else an error (not paired, not subscribed, no buffers). */
int ble_server_notify_motion(const uint8_t *data, unsigned len);
void ble_server_set_page(uint8_t page);     /* the page on screen; notified to the phone when it changes */

#endif
