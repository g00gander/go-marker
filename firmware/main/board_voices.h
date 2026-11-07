#ifndef BOARD_VOICES_H
#define BOARD_VOICES_H
#include <stdint.h>
#define VOICE_RATE 24000
typedef struct { const int16_t *pcm; int samples; } voice_t;
extern const voice_t voice_power_on;
extern const voice_t voice_connected;
extern const voice_t voice_disconnected;
extern const voice_t voice_lets_go;
#endif
