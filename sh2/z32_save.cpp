/* Zelda 3 32X - the save file (zelda3's 8 KB SRAM) in the cartridge's backup SRAM.
 *
 * The SH2s cannot reach the cartridge SRAM: it sits at 0x200000, where the 4 MB ROM's upper half
 * is, and only appears there while the 68000 sets 0xA130F1. So the 68000 keeps an 8 KB mirror of
 * it in its own RAM (cart/cart_md.s):
 *   60  SRAM -> mirror      61  mirror -> SRAM      (the cartridge is moved for ~35 ms: RV)
 *   65  mirror long COMM2 -> COMM8:COMM10           66  COMM8:COMM10 -> mirror long COMM2
 * While 60 / 61 run, neither SH2 may touch the ROM (their code and the sound effects live there):
 * the master waits in a loop placed in SDRAM, the slave is parked in SDRAM with its interrupts
 * (the PWM mixer) off - a ~35 ms gap in the sound effects, only when the game saves.
 *
 * Load: at power-on (z32_save_load, Z32_PLAY builds; the replays start from an empty SRAM as on
 * the PC). Save: zelda3 calls ZeldaWriteSram when it saves (file select, save & quit, death...):
 * only the 64-byte chunks that changed since the last load / save go over COMM, then 61. */
#include <stdint.h>
#include <string.h>
#include "../zelda3/src/types.h"
#include "../zelda3/src/zelda_rtl.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))
#define COMM0  REG16(0x20004020)
#define COMM2  REG16(0x20004022)
#define COMM8L REG32(0x20004028)
#define THRU(v) (*(volatile uint32_t *)((uintptr_t)&(v) | 0x20000000))
#define SDRAM_CODE __attribute__((section(".sdram"), noinline))

extern "C" {
uint32_t z32_park_req, z32_parked;       /* master -> slave: park; slave -> master: parked */
}
/* what the 68000's mirror holds, as a checksum per 64 bytes (no 8 KB copy: the SDRAM is full) */
static uint32 z32_sram_sums[0x2000 / 64];
static uint32 chunk_sum(const uint8 *p) {
  uint32 h = 2166136261u;                /* FNV-1a */
  for (int i = 0; i < 64; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

/* a 68000 command; the wait is in SDRAM (the master's code is in the ROM) */
SDRAM_CODE static void md_cmd_sdram(uint16 cmd) {
  COMM0 = cmd;
  for (uint32 n = 0; n < 2000000; n++) {
    for (volatile int k = 0; k < 64; k++) { }
    if (COMM0 == 0) break;
  }
}
static void md_cmd(uint16 cmd) { md_cmd_sdram(cmd); }

/* slave: called from its idle loop (z32_main.cpp secondary) */
extern "C" SDRAM_CODE void z32_slave_park(void) {
  uint32 sr;
  __asm__ volatile ("stc sr, %0" : "=r" (sr));
  __asm__ volatile ("ldc %0, sr" : : "r" (0xF0));          /* no interrupts: the mixer is in ROM */
  THRU(z32_parked) = 1;
  while (THRU(z32_park_req)) { }
  THRU(z32_parked) = 0;
  __asm__ volatile ("ldc %0, sr" : : "r" (sr));
}

/* the SRAM <-> mirror copy with both SH2s off the ROM */
static void sram_copy(uint16 cmd) {
  THRU(z32_park_req) = 1;
  for (uint32 n = 0; n < 4000000 && !THRU(z32_parked); n++) { }
  md_cmd_sdram(cmd);
  THRU(z32_park_req) = 0;
  for (uint32 n = 0; n < 4000000 && THRU(z32_parked); n++) { }
}

void z32_save_load(void) {
  sram_copy(60);
  for (int i = 0; i < 0x800; i++) {
    COMM2 = (uint16)i;
    md_cmd(65);
    uint32 v = COMM8L;                   /* big-endian: byte 0 of the long = SRAM byte 4i */
    uint8 *p = g_zenv.sram + i * 4;
    p[0] = (uint8)(v >> 24); p[1] = (uint8)(v >> 16); p[2] = (uint8)(v >> 8); p[3] = (uint8)v;
  }
  for (int c = 0; c < 0x2000 / 64; c++) z32_sram_sums[c] = chunk_sum(g_zenv.sram + c * 64);
}

void ZeldaReadSram() { z32_save_load(); }

void ZeldaWriteSram() {
#ifdef Z32_PLAY
  int changed = 0;
  for (int c = 0; c < 0x2000 / 64; c++) {   /* the 64-byte chunks that changed, whole */
    uint32 h = chunk_sum(g_zenv.sram + c * 64);
    if (h == z32_sram_sums[c])
      continue;
    for (int i = c * 16; i < c * 16 + 16; i++) {
      const uint8 *p = g_zenv.sram + i * 4;
      COMM8L = (uint32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
      COMM2 = (uint16)i;
      md_cmd(66);
    }
    z32_sram_sums[c] = h;
    changed++;
  }
  if (changed)
    sram_copy(61);
#endif
}

#ifdef Z32_SAVE_TEST
/* test: the counter saved by the last run, a pattern written, read back from the SRAM.
 * z32_save_test = run counter << 16 | 1 if last run's pattern was found | 2 if this one read back */
extern "C" { uint32_t z32_save_test; }
void z32_save_selftest(void) {
  uint8 *s = g_zenv.sram;
  uint16 n = (uint16)(s[0x1ff0] | s[0x1ff1] << 8);
  int ok_old = n != 0;
  for (int i = 0x1000; i < 0x1ff0 && ok_old; i++) if (s[i] != (uint8)(i * 7 + n)) ok_old = 0;
  n++;
  s[0x1ff0] = (uint8)n; s[0x1ff1] = (uint8)(n >> 8);
  for (int i = 0x1000; i < 0x1ff0; i++) s[i] = (uint8)(i * 7 + n);
  ZeldaWriteSram();
  memset(g_zenv.sram, 0, 0x2000);
  z32_save_load();
  int ok_new = 1;
  for (int i = 0x1000; i < 0x1ff0; i++) if (s[i] != (uint8)(i * 7 + n)) ok_new = 0;
  z32_save_test = (uint32)n << 16 | ok_old | ok_new << 1;
  memset(g_zenv.sram, 0, 0x2000);
  for (int c = 0; c < 0x2000 / 64; c++) z32_sram_sums[c] = chunk_sum(g_zenv.sram + c * 64);
}
#endif
