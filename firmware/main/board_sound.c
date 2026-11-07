#include "board_sound.h"
#include "board_voices.h"
#include <math.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define PIN_DIN  47
#define PIN_LRCK 38
#define PIN_BCK  48
#define RATE     24000

static const char *TAG = "sound";
static i2s_chan_handle_t tx;
static QueueHandle_t q;
static volatile int enabled = 1;
static volatile int volume = 75;     /* percent of a deliberately modest full scale */

typedef struct { int hz; int ms; } note_t;      /* hz 0 = a pause */
static const note_t TURN[]       = { {1568, 110}, {0, 0} };
static const note_t *const SOUNDS[SOUND_COUNT] = { NULL, NULL, NULL, NULL, TURN };     /* BOOT, CONNECT, DISCONNECT and LETS_GO are spoken */

/* One note: a sine with a short fade in and out so it does not click. */
static void play_note(const note_t *n)
{
    int total = RATE * n->ms / 1000;
    int16_t buf[256 * 2];
    int done = 0;
    float phase = 0, step = 2.0f * (float)M_PI * (float)n->hz / RATE;
    const int fade = RATE / 200;                    /* 5 ms */
    while (done < total) {
        int chunk = total - done < 256 ? total - done : 256;
        for (int i = 0; i < chunk; i++) {
            int pos = done + i;
            float env = 1.0f;
            if (pos < fade) env = (float)pos / fade;
            else if (total - pos < fade) env = (float)(total - pos) / fade;
            float v = n->hz ? sinf(phase) * env : 0.0f;
            phase += step;
            if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
            int16_t s = (int16_t)(v * 12000.0f * volume / 100.0f);
            buf[2 * i] = s;
            buf[2 * i + 1] = s;
        }
        size_t w;
        i2s_channel_write(tx, buf, chunk * 4, &w, portMAX_DELAY);
        done += chunk;
    }
}

/* A recorded phrase (see voices/make_voices.py), scaled by the volume. */
static void play_voice(const voice_t *v)
{
    int16_t buf[256 * 2];
    for (int done = 0; done < v->samples;) {
        int chunk = v->samples - done < 256 ? v->samples - done : 256;
        for (int i = 0; i < chunk; i++) {
            int16_t s = (int16_t)(v->pcm[done + i] * volume / 100);
            buf[2 * i] = s;
            buf[2 * i + 1] = s;
        }
        size_t w;
        i2s_channel_write(tx, buf, chunk * 4, &w, portMAX_DELAY);
        done += chunk;
    }
}

static void sound_task(void *arg)
{
    board_sound_t s;
    for (;;) {
        if (xQueueReceive(q, &s, portMAX_DELAY) != pdTRUE || s >= SOUND_COUNT || volume <= 0 || !enabled) continue;
        i2s_channel_enable(tx);
        if (s == SOUND_BOOT) play_voice(&voice_power_on);
        else if (s == SOUND_CONNECT) play_voice(&voice_connected);
        else if (s == SOUND_DISCONNECT) play_voice(&voice_disconnected);
        else if (s == SOUND_LETS_GO) play_voice(&voice_lets_go);
        else for (const note_t *n = SOUNDS[s]; n->ms; n++) play_note(n);
        int16_t silence[64 * 2] = {0};                /* let the chip settle on silence, then stop the clocks (no hiss) */
        size_t w;
        i2s_channel_write(tx, silence, sizeof silence, &w, portMAX_DELAY);
        i2s_channel_disable(tx);
    }
}

void board_sound_init(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    if (i2s_new_channel(&cc, &tx, NULL) != ESP_OK) { ESP_LOGW(TAG, "no I2S channel; speaker off"); return; }
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PIN_BCK, .ws = PIN_LRCK, .dout = PIN_DIN, .din = I2S_GPIO_UNUSED },
    };
    if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK) { ESP_LOGW(TAG, "I2S init failed; speaker off"); return; }
    q = xQueueCreate(2, sizeof(board_sound_t));
    xTaskCreate(sound_task, "sound", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "speaker ready (I2S %d Hz, volume %d%%)", RATE, volume);
}

void board_sound_play(board_sound_t s)
{
    if (q) xQueueSend(q, &s, 0);
}

void board_sound_set_enabled(int on)
{
    enabled = on != 0;
}

void board_sound_set_volume(int percent)
{
    volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
}
