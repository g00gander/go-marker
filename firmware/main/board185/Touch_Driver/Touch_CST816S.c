#include "Touch_CST816S.h"
#include "I2C_Driver.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "Display_ST77916.h"

#define TOUCH_RST_GPIO  1
#define TOUCH_INT_GPIO  4

static const char *TAG = "touch";
static esp_lcd_touch_handle_t tp;
static volatile bool touch_event;        /* set by the INT pin's interrupt */

static void IRAM_ATTR touch_isr(esp_lcd_touch_handle_t handle) { (void)handle; touch_event = true; }

void Touch_Init(void)
{
    const esp_lcd_touch_config_t cfg = {
        .x_max = EXAMPLE_LCD_WIDTH,
        .y_max = EXAMPLE_LCD_HEIGHT,
        .rst_gpio_num = TOUCH_RST_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels = { .reset = 0, .interrupt = 0 },
        .interrupt_callback = touch_isr,
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    io_cfg.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(nav_i2c_bus, &io_cfg, &io));
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(io, &cfg, &tp));
    esp_log_level_set("CST816S", ESP_LOG_NONE);          /* reads fail while the chip sleeps; that is normal */
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    ESP_LOGI(TAG, "CST816S touch ready");
}

/* The CST816S sleeps between touches: it does not answer on I2C then, and it pulls INT low in short pulses when there is something to read.
 * A poll every 33 ms can miss a pulse, so the interrupt sets a flag. Read when the flag is set, while a finger was down on the last
 * read, and now and then as a fallback. The failed reads while it sleeps are expected, so their error messages are silenced. */
bool Touch_Get_xy(uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *count, uint8_t max)
{
    static bool touching;
    static int64_t last_poll;
    *count = 0;
    if (!tp) return false;
    int64_t now = esp_timer_get_time();
    if (!touching && !touch_event && now - last_poll < 100 * 1000) return false;
    last_poll = now;
    touch_event = false;
    esp_lcd_touch_read_data(tp);
    bool was = touching;
    touching = esp_lcd_touch_get_coordinates(tp, x, y, strength, count, max) && *count > 0;
    if (touching != was) {
        if (touching) ESP_LOGI(TAG, "touch down at %u,%u", x[0], y[0]); else ESP_LOGI(TAG, "touch up");
    }
    return touching;
}
