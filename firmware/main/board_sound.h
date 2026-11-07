/* board_sound.h - the speaker (PCM5101 on I2S: DIN 47, LRCK 38, BCK 48); short tones played from a task. */
#ifndef BOARD_SOUND_H
#define BOARD_SOUND_H

typedef enum {
    SOUND_BOOT = 0,      /* power-on: says "Power on" */
    SOUND_CONNECT,       /* phone connected: says "Connected" */
    SOUND_DISCONNECT,    /* phone lost: says "Disconnected" */
    SOUND_LETS_GO,       /* a route has started: says "Let's go!" */
    SOUND_TURN,          /* a turn is coming: one short beep */
    SOUND_COUNT
} board_sound_t;

void board_sound_init(void);
void board_sound_play(board_sound_t s);      /* returns at once; ignored while another sound is playing */
void board_sound_set_enabled(int on);        /* 0 = silent (the setting from the phone app), 1 = speaks */
void board_sound_set_volume(int percent);    /* 0..100 (0 = muted) */
#endif
