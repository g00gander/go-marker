#pragma once
/* The 1.85B board has no power-latch circuit like the 1.46's, so these do nothing; they keep app_main.c identical on both boards. */
static inline void PWR_Init(void) {}
static inline void PWR_Loop(void) {}
