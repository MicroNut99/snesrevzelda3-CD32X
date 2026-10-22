/* Zelda 3 32X - SPEED / CORRECTNESS TEST (stage 1).
 * The master SH2 replays chapter 1 of zelda3's reference recordings from power-on (the game's own
 * replay code, the inputs and RAM patches in the recording), and after every frame:
 *   - hashes the SNES RAM and compares it with the PC's hash of the same frame (z32_expect, made
 *     by pc/zh); the first frame that differs is kept;
 *   - times the game logic (ZeldaRunFrame) and the SNES picture (ZeldaDrawPpuFrame, 8-bit CGRAM
 *     indices straight into the 32X frame buffer) with the master's FRT at phi/128;
 *   - copies the CGRAM into the 32X palette (the same 15-bit format).
 * Results in z32_stats (read from SDRAM by the emulator: ares-headless --peek). */
#include <stdint.h>
#include <string.h>
#include "../zelda3/src/types.h"
#include "../zelda3/src/zelda_rtl.h"
#include "../zelda3/src/assets.h"
#include "../zelda3/src/variables.h"
#include "../zelda3/snes/ppu.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG8(a)  (*(volatile uint8_t *)(a))
#define VDP_DISPMODE REG16(0x20004100)
#define VDP_FBCTL    REG16(0x2000410A)
#define CRAM         ((volatile uint16_t *)0x20004200)
#define FB16         ((volatile uint16_t *)0x24000000)
#define FB8          ((uint8_t *)0x24000200)
#define COMM0        REG16(0x20004020)
#define COMM8        REG16(0x20004028)
#define COMM10       REG16(0x2000402A)
#define COMM12       REG16(0x2000402C)
#define COMM14       REG16(0x2000402E)
#define FRT_TCSR     REG8(0xFFFFFE11)
#define FRT_FRCH     REG8(0xFFFFFE12)
#define FRT_FRCL     REG8(0xFFFFFE13)
#define FRT_TCR      REG8(0xFFFFFE16)

extern "C" {
extern const uint8_t z32_assets[], z32_assets_end[];      /* zelda3_assets.dat (z32_data.s) */
extern const uint8_t z32_replay[], z32_replay_end[];      /* chapter 1: header + input log */
extern const uint8_t z32_expect[], z32_expect_end[];      /* the PC's RAM hash per frame (BE) */
extern const uint8_t z32_expect_px[], z32_expect_px_end[];   /* the PC's picture hash per frame (BE) */
void secondary(void);
void slave_hint_c(void);
int main(void);
volatile uint32_t slave_go_flag;
}

const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];
MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
void NORETURN Die(const char *error) { (void)error; COMM12 = 0xDEAD; for (;;) { } }

/* the results: a fixed layout, peeked by the emulator (sh-elf-nm z32.elf | grep z32_stats) */
struct Z32Stats {
  uint32 magic;                 /* 'Z32S' */
  uint32 frames;                /* frames run */
  uint32 checked;               /* frames whose RAM hash was compared */
  uint32 first_bad;             /* first frame whose RAM differs from the PC's (0xFFFFFFFF: none) */
  uint32 logic_sum, draw_sum;   /* FRT ticks (phi/128 = 179.8 kHz) */
  uint32 logic_max, draw_max;
  uint32 heap_peak;             /* malloc high-water mark */
  uint32 bad_hash, want_hash;   /* at first_bad */
  uint32 hash_sum, flip_sum, pal_sum;   /* FRT ticks: the RAM hash (test only), the swap wait, the palette */
  uint32 v_logic, v_draw, v_hash, v_total;   /* the same in 68000 frames (V-blanks): no wrap */
  uint32 px_checked, px_first_bad;           /* the picture (8-bit FB + CGRAM) against the PC's */
  uint32 split;                              /* the line the slave starts at (last frame) */
  uint32 slave_sum, wait_sum;                /* FRT ticks: the slave's drawing, the master's wait for it */
};
extern "C" { Z32Stats z32_stats; extern char *z32_heap_peak; }

/* the 68000's frame counter (cart/cart_md.s command 48: pad in COMM8, V-blanks in COMM10) */
static void comm_pause(void) { for (volatile int k = 0; k < 64; k++) { } }
static uint16 md_frames(void) {
  COMM0 = 48;
  for (uint32 n = 0; n < 40000 && COMM0 != 0; n++) comm_pause();
  return COMM10;
}
static uint16 frt(void) { uint8 h = FRT_FRCH; uint8 l = FRT_FRCL; return (uint16)(h << 8 | l); }

/* ---- the slave SH2 draws the lower part of the picture (ZeldaDrawPpuLines, zelda_rtl.c) ----
 * The master readies the frame (ZeldaDrawPpuFrameBegin), copies the drawing context for the
 * slave, posts the first line and a command number; both draw; the master waits. The flags are
 * read and written through the cache-through alias (+0x20000000); the slave purges its cache
 * before each frame, so it sees what the master wrote (the SH2 caches are write-through). */
static Ppu z32_slave_ppu;
static ZeldaDrawCtx z32_slave_ctx;
static volatile uint32 z32_slave_cmd, z32_slave_from, z32_slave_done, z32_slave_ticks;
#define THRU(v) (*(volatile uint32 *)((uintptr_t)&(v) | 0x20000000))
#define CCR REG8(0xFFFFFE92)

void secondary(void) {
  FRT_TCR = 0x02;                                /* phi/128, free-running: the slave's own clock */
  FRT_TCSR = 0x00;
  uint32 seen = 0;
  for (;;) {
    uint32 cmd;
    while ((cmd = THRU(z32_slave_cmd)) == seen) { }
    seen = cmd;
    CCR = 0x11;                                  /* purge: drop what the master changed since */
    uint16 t0 = frt();
    ZeldaDrawPpuLines(&z32_slave_ctx, (int)THRU(z32_slave_from), z32_slave_ctx.height);
    THRU(z32_slave_ticks) = (uint16)(frt() - t0);
    THRU(z32_slave_done) = cmd;
  }
}

static void load_assets(void) {
  const uint8 *data = z32_assets;
  uint32 offset = 88 + kNumberOfAssets * 4 + LE32(data + 84);
  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = LE32(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }
}

static void video_init(void) {
  VDP_DISPMODE = 0x0001;                         /* packed pixel (8 bpp), PRI clear: 32X in front */
  for (int fb = 0; fb < 2; fb++) {
    for (int i = 0; i < 256; i++) FB16[i] = (uint16_t)(0x100 + (i < 224 ? i : 0) * 160);   /* rows of 320 */
    volatile uint32 *p = (uint32 *)FB8;          /* words: the FB drops byte writes of 0 */
    for (int i = 0; i < 320 * 224 / 4; i++) p[i] = 0;
    VDP_FBCTL = (uint16_t)((VDP_FBCTL & 1) ^ 1);
    while (!(VDP_FBCTL & 0x8000)) { }            /* the swap happens in the V-blank */
  }
}

static void palette_from_cgram(void) {
  const uint16_t *cg = g_zenv.ppu->cgram;
  for (int i = 0; i < 256; i++) {
    uint16_t c = cg[i] & 0x7FFF;                 /* SNES BGR555 = 32X BGR555 */
    while (!(VDP_FBCTL & 0x2000)) { }            /* PEN: writable only in the blanking (Rev32XCD cram_put) */
    CRAM[i] = c ? c : 0x0400;                    /* pure black is see-through on the 32X */
  }
}

static void flip(void) {
  uint16_t cur = VDP_FBCTL & 1;
  VDP_FBCTL = (uint16_t)(cur ^ 1);
  while ((VDP_FBCTL & 1) == cur) { }
}

#ifdef Z32_STOP_ON_BAD
static void flip(void);
static void palette_from_cgram(void);
static void NORETURN z32_stop(void) {
  __asm__ volatile("" ::: "memory");             /* z32_stats is written before we hold */
  flip();                                        /* the frame just drawn, on screen */
  palette_from_cgram();
  COMM14 = 0x0BAD;
  for (;;) __asm__ volatile("" ::: "memory");
}
#endif

int main(void) {
  FRT_TCR = 0x02;                                /* phi/128; the master takes no interrupts, so */
  FRT_TCSR = 0x00;                               /* its FRT is free to count (crt0 made it toggle) */
  memset(&z32_stats, 0, sizeof z32_stats);
  z32_stats.magic = 0x5A333253;
  z32_stats.first_bad = 0xFFFFFFFF;
  z32_stats.px_first_bad = 0xFFFFFFFF;
  COMM10 = 0x0AAA;                               /* the 68000's start-up handshake (Kobo's) */
  while (COMM10 != 0x0BBB) comm_pause();
  COMM12 = 1;
  load_assets();
  ZeldaInitialize();
  g_zenv.ppu->extraLeftRight = 0;
  COMM12 = 2;
  if (!ZeldaStartReplayFromMemory(z32_replay, (size_t)(z32_replay_end - z32_replay))) Die("replay");
  video_init();
#ifdef Z32_PATTERN
  for (;;) {                                     /* display test: colour bars, no game */
    for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) FB8[y * 320 + x] = (uint8)(x / 20 + 1 + (y / 56) * 16);
    for (int i = 1; i < 256; i++) { while (!(VDP_FBCTL & 0x2000)) { } CRAM[i] = (uint16)((i * 2) & 31 | ((i * 5) & 31) << 5 | ((i * 11) & 31) << 10) | 0x0400; }
    flip();
    COMM14++;
  }
#endif
  COMM12 = 3;
  const uint32 n_expect = (uint32)(z32_expect_end - z32_expect) / 4;
  for (uint32 f = 0;; f++) {
    uint16 m0 = md_frames();
    uint16 t0 = frt();
    ZeldaRunFrame(0);                            /* replay: the recording gives the inputs */
    uint16 t1 = frt();
    uint16 m1 = md_frames();
#ifdef Z32_NO_CHECK
    if (0) {                                     /* speed build: no RAM / picture checks */
      uint32 h = 0;
#else
    uint32 h = 2166136261u;                      /* FNV-1a, as pc/zh_main.c */
    for (int i = 0; i < 0x20000; i++) h = (h ^ g_ram[i]) * 16777619u;
    if (f < n_expect) {
#endif
      uint32 want = (uint32)z32_expect[4 * f] << 24 | z32_expect[4 * f + 1] << 16 | z32_expect[4 * f + 2] << 8 | z32_expect[4 * f + 3];
      z32_stats.checked = f + 1;
      if (h != want && z32_stats.first_bad == 0xFFFFFFFF) {
        z32_stats.first_bad = f; z32_stats.bad_hash = h; z32_stats.want_hash = want;
#ifdef Z32_STOP_ON_BAD
        z32_stop();                              /* debug: hold g_ram as it is for a dump */
#endif
      }
    }
    uint16 m2 = md_frames();
    uint16 t2 = frt();
    z32_stats.hash_sum += (uint16)(t2 - t1);
#ifdef Z32_DRAW_TO_SDRAM
    static uint8 line_buf[320];                  /* test: every line into one SDRAM line, no FB writes */
    ZeldaDrawPpuFrame(line_buf, 0, kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Index8);
#else
    {                                            /* two SH2s: master lines 0..split-1, slave split.. */
      static uint32 seq, split = 112;
      ZeldaDrawCtx ctx;
      ZeldaDrawPpuFrameBegin(&ctx, FB8 + 32, 320, kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Index8);
      ZeldaDrawCtxCopy(&z32_slave_ctx, &z32_slave_ppu, &ctx);
      THRU(z32_slave_from) = split;
      THRU(z32_slave_cmd) = ++seq;
      uint16 ta = frt();
      ZeldaDrawPpuLines(&ctx, 0, (int)split - 1);
      uint16 tb = frt();
      while (THRU(z32_slave_done) != seq) { }
      uint16 tc = frt();
      ZeldaDrawPpuFrameEnd(&ctx);
      uint32 tm = (uint16)(tb - ta), ts = THRU(z32_slave_ticks);
      z32_stats.split = split; z32_stats.slave_sum += ts; z32_stats.wait_sum += (uint16)(tc - tb);
      /* balance: the per-line cost of each part; move the split toward equal times (a few
       * lines a frame, so one odd frame does not swing it) */
      uint32 ns = 225 - split;
      if (tm && ts) {
        uint32 want = (uint32)((uint64_t)225 * (ts * split) / (ts * split + tm * ns));
        if (want > split + 4) want = split + 4;
        if (want + 4 < split) want = split - 4;
        if (want < 16) want = 16;
        if (want > 208) want = 208;
        split = want;
      }
    }
#endif
    uint16 t3 = frt();
    uint16 m3 = md_frames();
#if !defined(Z32_DRAW_TO_SDRAM) && !defined(Z32_NO_CHECK)
    if (f < (uint32)(z32_expect_px_end - z32_expect_px) / 4) {   /* test only: the picture as the PC's */
      uint32 ph = 2166136261u;
      for (int y = 0; y < 224; y++) { const uint8 *row = FB8 + 32 + y * 320; for (int x = 0; x < 256; x++) ph = (ph ^ row[x]) * 16777619u; }
      for (int i = 0; i < 256; i++) { uint16 c = g_zenv.ppu->cgram[i]; ph = (ph ^ (c & 0xff)) * 16777619u; ph = (ph ^ (c >> 8)) * 16777619u; }
      const uint8 *e = z32_expect_px + 4 * f;
      z32_stats.px_checked = f + 1;
      if (ph != ((uint32)e[0] << 24 | e[1] << 16 | e[2] << 8 | e[3]) && z32_stats.px_first_bad == 0xFFFFFFFF) {
        z32_stats.px_first_bad = f;
#ifdef Z32_STOP_ON_BAD
        z32_stop();                              /* debug: show the bad picture, hold */
#endif
      }
    }
#endif
    z32_stats.v_logic += (uint16)(m1 - m0); z32_stats.v_hash += (uint16)(m2 - m1);
    z32_stats.v_draw += (uint16)(m3 - m2);
    uint32 dl = (uint16)(t1 - t0), dd = (uint16)(t3 - t2);
    z32_stats.logic_sum += dl; z32_stats.draw_sum += dd;
    if (dl > z32_stats.logic_max) z32_stats.logic_max = dl;
    if (dd > z32_stats.draw_max) z32_stats.draw_max = dd;
    z32_stats.frames = f + 1;
    z32_stats.heap_peak = (uint32)(z32_heap_peak - (char *)0x06000000);
    uint16 t4 = frt();
    flip();
    uint16 t5 = frt();
    palette_from_cgram();
    uint16 t6 = frt();
    z32_stats.flip_sum += (uint16)(t5 - t4);
    z32_stats.pal_sum += (uint16)(t6 - t5);
    z32_stats.v_total += (uint16)(md_frames() - m0);
    COMM14 = (uint16)f;
  }
}

void slave_hint_c(void) { }
