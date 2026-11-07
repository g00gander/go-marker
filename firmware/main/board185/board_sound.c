/* Speaker of the Waveshare ESP32-S3-Touch-LCD-1.85B: an ES8311 audio chip (set up over I2C) fed by I2S, driving a small amplifier.
 * Same behavior as the 1.46's board_sound.c: spoken phrases from board_voices.c, plus a short beep for a turn.
 * The bring-up follows Waveshare's board support package (waveshareteam/ESP32-S3-Touch-LCD-1.85B). */
#include "board_sound.h"
#include "board_voices.h"
#include "I2C_Driver.h"
#include <math.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define PIN_MCLK 2
#define PIN_BCLK 48
#define PIN_WS   38
#define PIN_DOUT 47
#define PIN_DIN  39
#define PIN_PA   9          /* amplifier enable */
#define RATE     24000

static const char *TAG = "sound";

/* esp_codec_dev switches the I2S channel off before it switches it on, and the channel is sometimes already off, so the I2S driver
 * prints "channel has not been enabled yet" although nothing is wrong. Those messages are hidden only while the codec library runs. */
#define QUIET_I2S_BEGIN() esp_log_level_set("i2s_common", ESP_LOG_NONE)
#define QUIET_I2S_END()   esp_log_level_set("i2s_common", ESP_LOG_ERROR)
static i2s_chan_handle_t tx_chan;
static esp_codec_dev_handle_t spk;
static QueueHandle_t q;
static volatile int enabled = 1;
static volatile int volume = 75;     /* percent */

typedef struct { int hz; int ms; } note_t;      /* hz 0 = a pause */
static const int16_t silence[RATE / 20];      /* 50 ms, all zero (lives in flash, not on a stack) */
static const note_t TURN[]       = { {1568, 110}, {0, 0} };
static const note_t *const SOUNDS[SOUND_COUNT] = { NULL, NULL, NULL, NULL, TURN };     /* BOOT, CONNECT, DISCONNECT and LETS_GO are spoken */

/* One note: a sine with a short fade in and out so it does not click (16-bit mono). */
static void play_note(const note_t *n)
{
    int total = RATE * n->ms / 1000;
    int16_t buf[256];
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
            buf[i] = (int16_t)(v * 12000.0f);
        }
        esp_codec_dev_write(spk, buf, chunk * 2);
        done += chunk;
    }
}

static void play_voice(const voice_t *v)
{
    int16_t buf[256];
    for (int done = 0; done < v->samples;) {
        int chunk = v->samples - done < 256 ? v->samples - done : 256;
        for (int i = 0; i < chunk; i++) buf[i] = v->pcm[done + i];
        esp_codec_dev_write(spk, buf, chunk * 2);
        done += chunk;
    }
}

static void sound_task(void *arg)
{
    board_sound_t s;
    for (;;) {
        if (xQueueReceive(q, &s, portMAX_DELAY) != pdTRUE || s >= SOUND_COUNT || volume <= 0 || !enabled) continue;
        esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = RATE };
        QUIET_I2S_BEGIN();
        int opened = esp_codec_dev_open(spk, &fs);
        QUIET_I2S_END();
        if (opened != ESP_CODEC_DEV_OK) { ESP_LOGW(TAG, "codec did not open"); continue; }
        esp_codec_dev_set_out_vol(spk, volume);
        esp_codec_dev_write(spk, (void *)silence, sizeof silence);      /* 50 ms of silence first, so the amplifier is awake before the first word */
        if (s == SOUND_BOOT) play_voice(&voice_power_on);
        else if (s == SOUND_CONNECT) play_voice(&voice_connected);
        else if (s == SOUND_DISCONNECT) play_voice(&voice_disconnected);
        else if (s == SOUND_LETS_GO) play_voice(&voice_lets_go);
        else for (const note_t *n = SOUNDS[s]; n->ms; n++) play_note(n);
        esp_codec_dev_write(spk, (void *)silence, sizeof silence);
        QUIET_I2S_BEGIN();
        esp_codec_dev_close(spk);
        QUIET_I2S_END();
    }
}

void board_sound_init(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    if (i2s_new_channel(&cc, &tx_chan, NULL) != ESP_OK) { ESP_LOGW(TAG, "no I2S channel; speaker off"); return; }
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = PIN_DIN },
    };
    if (i2s_channel_init_std_mode(tx_chan, &sc) != ESP_OK) { ESP_LOGW(TAG, "I2S init failed; speaker off"); return; }
    i2s_channel_enable(tx_chan);          /* the audio chip needs its clock (MCLK) running before it will answer on I2C */

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .tx_handle = tx_chan, .rx_handle = NULL };
    QUIET_I2S_BEGIN();
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    QUIET_I2S_END();
    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_MASTER_NUM, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = nav_i2c_bus };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!data_if || !ctrl_if || !gpio_if) { ESP_LOGW(TAG, "codec interfaces failed; speaker off"); return; }

    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl_if, .gpio_if = gpio_if, .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PIN_PA, .pa_reverted = false, .master_mode = false, .use_mclk = true, .digital_mic = false,
        .invert_mclk = false, .invert_sclk = false, .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es);
    if (!codec) { ESP_LOGW(TAG, "ES8311 not answering; speaker off"); return; }
    esp_codec_dev_cfg_t dev_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data_if };
    QUIET_I2S_BEGIN();
    spk = esp_codec_dev_new(&dev_cfg);
    QUIET_I2S_END();
    if (!spk) { ESP_LOGW(TAG, "codec device failed; speaker off"); return; }

    q = xQueueCreate(2, sizeof(board_sound_t));
    xTaskCreate(sound_task, "sound", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "speaker ready (ES8311, %d Hz mono, volume %d%%); free internal RAM %u bytes", RATE, volume, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void board_sound_play(board_sound_t s) { if (q) xQueueSend(q, &s, 0); }
void board_sound_set_enabled(int on) { enabled = on != 0; }
void board_sound_set_volume(int percent) { volume = percent < 0 ? 0 : percent > 100 ? 100 : percent; }
