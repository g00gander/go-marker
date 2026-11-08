#include "nav_nvs_store.h"

#include "nvs.h"
#include "nvs_flash.h"

static int rd(int slot, uint8_t *buf, size_t n)
{
    nvs_handle_t h;
    if (nvs_open("trip", NVS_READONLY, &h) != ESP_OK) return -1;
    char key[4] = {'s', (char)('0' + slot), 0};
    size_t len = n;
    esp_err_t e = nvs_get_blob(h, key, buf, &len);
    nvs_close(h);
    return e == ESP_OK ? (int)len : -1;
}

static int wr(int slot, const uint8_t *buf, size_t n)
{
    nvs_handle_t h;
    if (nvs_open("trip", NVS_READWRITE, &h) != ESP_OK) return -1;
    char key[4] = {'s', (char)('0' + slot), 0};
    esp_err_t e = nvs_set_blob(h, key, buf, n);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
}

const nav_store_ops_t nav_nvs_ops = {rd, wr};

void nav_nvs_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

void nav_nvs_load_settings(nav_saved_settings_t *out)
{
    out->day_pct = 100; out->night_pct = 40; out->last_night = 1; out->voice_on = 1; out->metric = 0; out->clock24 = 0; out->style = 0; out->volume = 75; out->touch_on = 1;
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) != ESP_OK) return;
    nav_saved_settings_t s = *out;                 /* an older, shorter blob (3 or 4 bytes) keeps the defaults for the fields it lacks */
    size_t len = sizeof s;
    if (nvs_get_blob(h, "disp", &s, &len) == ESP_OK && len >= 3 && len <= sizeof s && s.day_pct >= 5 && s.day_pct <= 100 &&
        s.night_pct >= 5 && s.night_pct <= 100) {
        if (len < 4) s.voice_on = 1;
        if (len < 5) s.metric = 0;
        if (len < 6) s.clock24 = 0;
        if (len < 8) s.volume = 75;
        if (len < 9) s.touch_on = 1;
        if (len < 7) s.style = 0;
        s.voice_on = s.voice_on ? 1 : 0; s.metric = s.metric ? 1 : 0; s.clock24 = s.clock24 ? 1 : 0; s.style = s.style <= 8 ? s.style : 0; if (s.volume > 100) s.volume = 75; s.touch_on = s.touch_on ? 1 : 0;
        *out = s;
    }
    nvs_close(h);
}

void nav_nvs_save_settings(const nav_saved_settings_t *in)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "disp", in, sizeof *in);
    nvs_commit(h);
    nvs_close(h);
}

int nav_nvs_load_layout(uint8_t out[6])
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t b[6];
    size_t len = sizeof b;
    int ok = nvs_get_blob(h, "layout", b, &len) == ESP_OK && (len == 5 || len == 6);
    nvs_close(h);
    if (!ok) return 0;
    if (len == 5) b[5] = 5;                           /* saved before the tire page existed: it goes last */
    uint8_t seen = 0;
    for (int i = 0; i < 6; i++) { if (b[i] >= 6 || (seen & (1u << b[i]))) return 0; seen |= (uint8_t)(1u << b[i]); }
    for (int i = 0; i < 6; i++) out[i] = b[i];
    return 1;
}

void nav_nvs_save_layout(const uint8_t order[6])
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "layout", order, 6);
    nvs_commit(h);
    nvs_close(h);
}

uint8_t nav_nvs_load_screens_off(void)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t m = 0;
    if (nvs_get_u8(h, "screens_off", &m) != ESP_OK) m = 0;
    nvs_close(h);
    return m;
}

void nav_nvs_save_screens_off(uint8_t mask)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "screens_off", mask);
    nvs_commit(h);
    nvs_close(h);
}

int nav_nvs_load_tires_on(void)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t on = 0;
    if (nvs_get_u8(h, "tires_on", &on) != ESP_OK) on = 0;
    nvs_close(h);
    return on != 0;
}

void nav_nvs_save_tires_on(int on)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "tires_on", on ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}
