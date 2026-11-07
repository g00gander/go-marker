/* Silent placeholder. The spoken phrases ("Power on", "Connected", "Disconnected", "Let's go!") are recordings made with a macOS
 * voice, and are not included in this repository. To get them, on a Mac run:
 *     python3 firmware/main/voices/make_voices.py            (uses the voice "Samantha"; `say -v '?'` lists others)
 * which overwrites this file and board_voices.h. Without it the display simply stays quiet. */
#include "board_voices.h"

static const int16_t silence[1] = {0};
const voice_t voice_power_on = { silence, 0 };
const voice_t voice_connected = { silence, 0 };
const voice_t voice_disconnected = { silence, 0 };
const voice_t voice_lets_go = { silence, 0 };
