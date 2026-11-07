/* nav_trip_store.h - trip statistics across power cycles, in two alternating slots with a sequence number and CRC,
 * so a power cut mid-write leaves the other slot intact. No LVGL dependency. */
#ifndef NAV_TRIP_STORE_H
#define NAV_TRIP_STORE_H

#include <stddef.h>
#include <stdint.h>
#include "nav_trip.h"

typedef struct {
    /* Read up to n bytes of slot 0/1 into buf. Return bytes read, or < 0 if the slot is empty. */
    int (*read)(int slot, uint8_t *buf, size_t n);
    /* Replace the contents of slot 0/1. Return 0 on success. */
    int (*write)(int slot, const uint8_t *buf, size_t n);
} nav_store_ops_t;

#define NAV_TRIP_RECORD_SIZE 25

/* Pass NULL to turn persistence off (screenshot runs, unit tests). */
void nav_store_init(const nav_store_ops_t *ops);

/* Restore the newest valid record into *t. Returns 1 if one was found, else 0 (t untouched). */
int nav_trip_load(nav_trip_t *t);

/* Write the trip to the older slot. Returns 0 on success. */
int nav_trip_save(const nav_trip_t *t);

#endif
