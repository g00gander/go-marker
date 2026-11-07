#include "I2C_Driver.h"
#include <string.h>

static const char *I2C_TAG = "I2C";
i2c_master_bus_handle_t nav_i2c_bus = NULL;

#define MAX_DEVICES 8
static struct { uint8_t addr; i2c_master_dev_handle_t dev; } devices[MAX_DEVICES];
static int ndevices;

void I2C_Init(void)
{
    if (nav_i2c_bus) return;
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &nav_i2c_bus));
    ESP_LOGI(I2C_TAG, "I2C initialized successfully");
}

/* one device handle per address, made the first time the address is used */
static i2c_master_dev_handle_t dev_for(uint8_t addr)
{
    for (int i = 0; i < ndevices; i++) if (devices[i].addr == addr) return devices[i].dev;
    if (ndevices >= MAX_DEVICES || !nav_i2c_bus) return NULL;
    const i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = I2C_MASTER_FREQ_HZ };
    i2c_master_dev_handle_t d = NULL;
    if (i2c_master_bus_add_device(nav_i2c_bus, &dc, &d) != ESP_OK) return NULL;
    devices[ndevices].addr = addr; devices[ndevices].dev = d; ndevices++;
    return d;
}

esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
    i2c_master_dev_handle_t d = dev_for(Driver_addr);
    if (!d) return ESP_ERR_INVALID_STATE;
    uint8_t buf[Length + 1];
    buf[0] = Reg_addr;
    memcpy(&buf[1], Reg_data, Length);
    return i2c_master_transmit(d, buf, Length + 1, I2C_MASTER_TIMEOUT_MS);
}

esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
    i2c_master_dev_handle_t d = dev_for(Driver_addr);
    if (!d) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(d, &Reg_addr, 1, Reg_data, Length, I2C_MASTER_TIMEOUT_MS);
}
