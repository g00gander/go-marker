/* lvgl_port.h - LVGL 9 <-> esp_lcd glue for the SPD2010 panel (the vendor LVGL 8 demo in vendor_waveshare/ is reference only). */
#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include <stdint.h>

/* Brings up the panel and touch, then creates the LVGL display and input device; call once after I2C_Init()/EXIO_Init().
 * Returns the active screen, ready for nav_ui_init(). */
struct _lv_obj_t *lvgl_port_init(void);

/* Call periodically (e.g. every ~10 ms) from app_main's loop. */
uint32_t lvgl_port_task_handler(void);
void lvgl_port_set_touch(int on);                  /* 1 = the touch screen answers (default), 0 = it is ignored */
int lvgl_port_touch_enabled(void);

#endif
