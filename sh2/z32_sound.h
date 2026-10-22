/* Zelda 3 32X - sound: the effects on the 32X PWM (mixed by the slave SH2), the music from a Sega
 * CD (CD audio, played by the 68000 through the Sega CD - cart/cd.s). */
#pragma once
#include <stdint.h>

/* The PWM's sample rate. tools/make_sound.py resamples the effects to exactly this rate (its
 * PWM_RATE): change both together. The cycle register follows Chilly Willy's 32X code: the
 * period is (cycle - 1) clocks of 23.01 MHz (NTSC), so cycle = clock / rate + 1. If the effects
 * play too low or too high on a console, this is the one number to correct. */
#define Z32_PWM_RATE   15720
#define Z32_PWM_CLOCK  23011361
#define Z32_PWM_CYCLE  ((Z32_PWM_CLOCK + Z32_PWM_RATE / 2) / Z32_PWM_RATE + 1)

#ifdef __cplusplus
extern "C" {
#endif
/* master: what the game sends the SNES sound chip (sh2/z32_stubs.cpp) */
void z32_sound_port(int port, uint8_t value);        /* ports 1..3: effects and ambience */
void z32_music(uint8_t music_ctrl);                  /* port 0: songs 1.., 0xF1 fade out, 0xF2 half, 0xF3 full */
/* master: send a pending music change to the 68000 (between game frames) */
void z32_music_service(void);
/* slave: the PWM and its H interrupt (once, before its work loop) */
void z32_sound_slave_init(void);
#ifdef __cplusplus
}
#endif
