/* Zelda 3 32X - sound (see z32_sound.h).
 *
 * Effects: tools/make_sound.py renders every effect the game can send (ports 1..3) with
 * zelda3's own SNES sound driver into sfx.bin, signed 8-bit at Z32_PWM_RATE, in the cartridge.
 * The master posts (port, id) into a mailbox; the slave SH2 mixes, in its H interrupt (one per
 * video line, crt0.s vector 69), as Rev32XCD / U-R-R does on hardware: samples are made only
 * while the PWM's FIFO has room, so the PWM's own clock sets the pace (the line rate only has
 * to be faster). Voices: 0 the ambience (port 1: rain, wind... - loops, fades out on 0x80+),
 * 1-2 port 2 and 3-4 port 3 effects, taking turns.
 *
 * Music: song n is CD audio track n (Zelda3_32X_CD.cue). The 68000 plays it through a Sega CD in
 * Mode 1 (cart/cart_md.s command 62): repeating if the song loops, else once; 0xF1 fades out
 * (volume 0), 0xF2 lowers it (the SNES driver's 64 / 255), 0xF3 back up; a new song starts at
 * full volume. Without a Sega CD the 68000 skips it. */
#include "z32_sound.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define SYS_INTMSK    REG16(0x20004000)
#define SH2_HCOUNT    REG16(0x20004004)
#define SH2_HINT_CLR  REG16(0x20004018)
#define COMM0         REG16(0x20004020)
#define COMM2         REG16(0x20004022)
#define COMM8         REG16(0x20004028)
#define PWM_CTRL      REG16(0x20004030)
#define PWM_CYCLE     REG16(0x20004032)
#define PWM_MONO      REG16(0x20004038)
#define PWM_MID       (Z32_PWM_CYCLE / 2)
#define THRU(v)       (*(volatile __typeof__(v) *)((uintptr_t)&(v) | 0x20000000))

extern "C" const uint8_t z32_sfx[];               /* sfx.bin (z32_data.s) */

/* ---- master ---------------------------------------------------------------------------- */
static uint8_t z32_mail_port[16], z32_mail_id[16];
static uint8_t z32_mail_w;                       /* written by the master only (cache-through) */
static uint8_t last_value[4];

void z32_sound_port(int port, uint8_t value) {
  /* the SNES driver starts a sound when a port's value changes (the game writes it for one
   * frame, then 0) */
  if (value == last_value[port])
    return;
  last_value[port] = value;
  if (!value)
    return;
  uint8_t w = THRU(z32_mail_w);
  ((volatile uint8_t *)((uintptr_t)z32_mail_port | 0x20000000))[w & 15] = (uint8_t)port;
  ((volatile uint8_t *)((uintptr_t)z32_mail_id | 0x20000000))[w & 15] = value;
  THRU(z32_mail_w) = (uint8_t)(w + 1);
}

static int music_mode, music_arg;                /* the pending command 62 (0: none) */
void z32_music(uint8_t ctrl) {
  if ((ctrl & 0xf0) != 0xf0) {
    uint8_t flags = ctrl < 64 ? z32_sfx[8 + ctrl] : 0;
    if (!(flags & 1))
      music_mode = 1, music_arg = 0;             /* not on the CD: stop */
    else
      music_mode = (flags & 2) ? 3 : 4, music_arg = ctrl;   /* repeat / once */
  } else if (ctrl >= 0xf1 && ctrl <= 0xf3) {
    static const uint16_t kVolume[3] = { 0, 0x100, 0x400 };
    music_mode = 5, music_arg = kVolume[ctrl - 0xf1];
  }
}

void z32_music_service(void) {
  if (!music_mode)
    return;
  static const uint8_t kComm2[6] = { 0, 0, 0, 2, 3, 4 };   /* command 62: 0 stop, 2 repeat, 3 once, 4 volume */
  COMM2 = kComm2[music_mode];
  COMM8 = (uint16_t)music_arg;
  COMM0 = 62;
  for (uint32_t n = 0; n < 4000000 && COMM0 != 0; n++) { }  /* the 68000 answers when the Sega CD has */
  music_mode = 0;
}

/* ---- slave: the mixer -------------------------------------------------------------------- */
struct Voice {
  const int8_t *p, *end, *loop;                  /* loop: where to go on at end (0: stop) */
  int32_t fade;                                  /* > 0: fading out, samples left (of kFade) */
};
enum { kVoices = 5, kFade = Z32_PWM_RATE * 2 };  /* the SNES driver's 0x78-frame fade */
static Voice voices[kVoices];
static uint8_t mail_r, turn2, turn3;

static void start(Voice *v, int port, int id) {
  const uint8_t *e = z32_sfx + 72 + ((port - 1) * 64 + id) * 12;
  uint32_t off = (uint32_t)e[0] << 24 | e[1] << 16 | e[2] << 8 | e[3];
  uint32_t len = (uint32_t)e[4] << 24 | e[5] << 16 | e[6] << 8 | e[7];
  uint32_t loop = (uint32_t)e[8] << 24 | e[9] << 16 | e[10] << 8 | e[11];
  if (!len) { v->p = v->end = 0; return; }       /* no sound for this id (port 1: stops it) */
  v->p = (const int8_t *)z32_sfx + off;
  v->end = v->p + len;
  v->loop = loop == 0xffffffff ? 0 : v->p + loop;
  v->fade = 0;
}

static void take_mail(void) {
  uint8_t w = THRU(z32_mail_w);
  while (mail_r != w) {
    int port = ((volatile uint8_t *)((uintptr_t)z32_mail_port | 0x20000000))[mail_r & 15];
    int value = ((volatile uint8_t *)((uintptr_t)z32_mail_id | 0x20000000))[mail_r & 15];
    mail_r++;
    if (port == 1) {
      if (value & 0x80) {                        /* fade the ambience out */
        if (voices[0].p && !voices[0].fade) voices[0].fade = kFade;
      } else {
        start(&voices[0], 1, value & 0x3f);
      }
    } else if (port == 2) {
      start(&voices[1 + (turn2++ & 1)], 2, value & 0x3f);   /* bits 6-7: the pan (mono here) */
    } else {
      start(&voices[3 + (turn3++ & 1)], 3, value & 0x3f);
    }
  }
}

extern "C" void slave_hint_c(void) {
  SH2_HINT_CLR = 0;                              /* acknowledge first */
  take_mail();
  for (int k = 0; k < 3 && !(PWM_MONO & 0x8000); k++) {   /* while the FIFO has room */
    int mix = 0;
    for (int i = 0; i < kVoices; i++) {
      Voice *v = &voices[i];
      if (!v->p)
        continue;
      int s = *v->p++;
      if (v->fade) {
        s = s * v->fade / kFade;
        if (--v->fade == 0) { v->p = 0; continue; }
      }
      mix += s;
      if (v->p == v->end)
        v->p = v->loop;                          /* the ambience loops; an effect ends */
    }
    mix = PWM_MID + mix * 2;
    if (mix < 1) mix = 1;
    if (mix > Z32_PWM_CYCLE - 2) mix = Z32_PWM_CYCLE - 2;
    PWM_MONO = (uint16_t)mix;
  }
  (void)SH2_HINT_CLR;                            /* let the clear land before rte */
}

void z32_sound_slave_init(void) {
  for (int i = 0; i < kVoices; i++) voices[i].p = 0;
  mail_r = THRU(z32_mail_w);
  PWM_MONO = 1; PWM_MONO = 1; PWM_MONO = 1;      /* prime the FIFO, then the cycle, then the control */
  PWM_CYCLE = Z32_PWM_CYCLE;
  PWM_CTRL = 0x0185;
  SH2_HCOUNT = 0;                                /* an H interrupt on every line */
  SYS_INTMSK = (uint16_t)((SYS_INTMSK & 0x8000) | 0x0080 | 0x0004);   /* HEN (in V-blank too) + H */
  __asm__ volatile ("ldc %0, sr" : : "r" (0x90));                     /* accept level 10 and up */
}
