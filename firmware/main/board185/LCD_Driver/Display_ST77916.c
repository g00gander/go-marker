/* Screen of the Waveshare ESP32-S3-Touch-LCD-1.85B: ST77916 panel, 360 x 360, over QSPI.
 * The bring-up follows Waveshare's own board support package (waveshareteam/ESP32-S3-Touch-LCD-1.85B). */
#include "Display_ST77916.h"
#include "Touch_CST816S.h"
#include "I2C_Driver.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st77916.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "st77916_init_tables.h"

static const char *TAG = "lcd";

#define LCD_CS        21
#define LCD_PCLK      40
#define LCD_D0        46
#define LCD_D1        45
#define LCD_D2        42
#define LCD_D3        41
#define LCD_RST       3
#define LCD_BACKLIGHT 5
#define LCD_SPI       SPI2_HOST
#define LCD_MAX_TRANSFER (EXAMPLE_LCD_WIDTH * 40 * 3 + 64)  /* bigger than one LVGL draw buffer (1/10 of a frame at 3 bytes a pixel; see lvgl_port.c), so a flush is never split in two */

esp_lcd_panel_handle_t panel_handle = NULL;
static SemaphoreHandle_t color_done;

/* esp_lcd queues the pixel transfer and returns at once; this fires when the DMA has finished reading the buffer. */
static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(color_done, &woken);
    return woken == pdTRUE;
}
void LCD_ColorBegin(void) { xSemaphoreTake(color_done, 0); }
int LCD_ColorWait(void) { return xSemaphoreTake(color_done, pdMS_TO_TICKS(200)) == pdTRUE; }      /* 0: the "transfer finished" signal never came (timed out after 200 ms) */

static void backlight_init(void)
{
    const ledc_timer_config_t timer = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT, .timer_num = LEDC_TIMER_1,
                                        .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    const ledc_channel_config_t ch = { .gpio_num = LCD_BACKLIGHT, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_1,
                                       .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER_1, .duty = 0, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

void Set_Backlight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (1023u * percent) / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

void LCD_Init(void)
{
    color_done = xSemaphoreCreateBinary();
    backlight_init();

    /* reset the panel */
    gpio_config_t rst = { .pin_bit_mask = 1ULL << LCD_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&rst);
    gpio_set_level(LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));

    const spi_bus_config_t bus = {
        .data0_io_num = LCD_D0, .data1_io_num = LCD_D1, .sclk_io_num = LCD_PCLK, .data2_io_num = LCD_D2, .data3_io_num = LCD_D3,
        .data4_io_num = -1, .data5_io_num = -1, .data6_io_num = -1, .data7_io_num = -1,
        .max_transfer_sz = LCD_MAX_TRANSFER, .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS, .dc_gpio_num = -1, .spi_mode = 0, .pclk_hz = 3 * 1000 * 1000, .trans_queue_depth = 10, .on_color_trans_done = on_color_done,
        .lcd_cmd_bits = 32, .lcd_param_bits = 8, .flags = { .quad_mode = 1 },
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI, &io_cfg, &io));

    /* the panel comes in two hardware versions: read its ID (slowly), then pick the matching init table */
    uint8_t id[4] = {0};
    const int read_cmd = (0x0B << 24) | (0x04 << 8);
    esp_err_t e = esp_lcd_panel_io_rx_param(io, read_cmd, id, sizeof id);
    ESP_LOGI(TAG, "panel ID register 0x04: %02x %02x %02x %02x (%s)", id[0], id[1], id[2], id[3], esp_err_to_name(e));
    ESP_ERROR_CHECK(esp_lcd_panel_io_del(io));
    io_cfg.pclk_hz = 80 * 1000 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI, &io_cfg, &io));

    st77916_vendor_config_t vendor = { .flags = { .use_qspi_interface = 1 } };
    if (id[0] == 0x00 && id[1] == 0x7F && id[2] == 0x7F && id[3] == 0x7F) {
        vendor.init_cmds = vendor_specific_init_version_1;
        vendor.init_cmds_size = sizeof(vendor_specific_init_version_1) / sizeof(st77916_lcd_init_cmd_t);
        ESP_LOGI(TAG, "init table 1");
    } else if (id[0] == 0x00 && id[1] == 0x02 && id[2] == 0x7F && id[3] == 0x7F) {
        vendor.init_cmds = vendor_specific_init_version_2;
        vendor.init_cmds_size = sizeof(vendor_specific_init_version_2) / sizeof(st77916_lcd_init_cmd_t);
        ESP_LOGI(TAG, "init table 2");
    } else {
        ESP_LOGW(TAG, "unknown panel ID: using the driver's default init");
    }

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,   /* LVGL's 24-bit pixels are stored blue, green, red: the panel reads them as they are */ .bits_per_pixel = 18,
        .flags = { .reset_active_high = 0 }, .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(io, &panel_cfg, &panel_handle));
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);

    /* The panel's memory holds noise when it powers up. Paint it black before the picture is switched on, and leave the backlight off:
     * app_main turns it on once the first real picture (the logo) has been drawn. */
    {
        const int rows = 10;
        uint8_t *black = heap_caps_calloc(1, (size_t)EXAMPLE_LCD_WIDTH * rows * 3, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (black) {
            for (int y = 0; y < EXAMPLE_LCD_HEIGHT; y += rows) {
                LCD_ColorBegin();
                esp_lcd_panel_draw_bitmap(panel_handle, 0, y, EXAMPLE_LCD_WIDTH, y + rows, black);
                LCD_ColorWait();
            }
            heap_caps_free(black);
        }
    }
    esp_lcd_panel_disp_on_off(panel_handle, true);

    Touch_Init();
    Set_Backlight(0);
}
