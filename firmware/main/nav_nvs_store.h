/* NVS-backed trip storage slots for nav_trip_store (two blobs, "s0" and "s1"). */
#ifndef NAV_NVS_STORE_H
#define NAV_NVS_STORE_H
#include "nav_trip_store.h"
void nav_nvs_init(void);              /* call once at boot, before BLE */
extern const nav_store_ops_t nav_nvs_ops;

/* Backlight levels chosen in the phone app, plus the theme last shown (so the power-on logo suits it). */
typedef struct { uint8_t day_pct, night_pct, last_night, voice_on, metric, clock24, style, volume, touch_on; } nav_saved_settings_t;
void nav_nvs_load_settings(nav_saved_settings_t *out);      /* defaults if nothing saved: 100 / 40 / night, voice on, imperial, 12-hour, standard theme */
void nav_nvs_save_settings(const nav_saved_settings_t *in);

/* Whether tire monitoring was on at the phone's last word (the tire screen is in the strip only then). Default off. */
int nav_nvs_load_tires_on(void);
void nav_nvs_save_tires_on(int on);

/* The order of the screens (a permutation of 0..5). Returns 1 and fills out[] if a valid order was saved. */
int nav_nvs_load_layout(uint8_t out[6]);
void nav_nvs_save_layout(const uint8_t order[6]);
/* The screens the phone turned off (bit 1 << id). 0 if none were saved. */
uint8_t nav_nvs_load_screens_off(void);
void nav_nvs_save_screens_off(uint8_t mask);
#endif
