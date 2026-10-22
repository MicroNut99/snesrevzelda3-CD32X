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
#include "../zelda3/snes/md_planes.h"
#include "z32_sound.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG8(a)  (*(volatile uint8_t *)(a))
#define VDP_DISPMODE REG16(0x20004100)
#define VDP_FBCTL    REG16(0x2000410A)
#define CRAM         ((volatile uint16_t *)0x20004200)
#define FB16         ((volatile uint16_t *)0x24000000)
#define FB8          ((uint8_t *)0x24000200)
#define COMM0        REG16(0x20004020)
#define COMM2        REG16(0x20004022)
#define COMM4L       (*(volatile uint32_t *)0x20004024)
#define COMM8L       (*(volatile uint32_t *)0x20004028)
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
void z32_slave_park(void);                /* z32_save.cpp: off the ROM while the 68000 moves it */
extern uint32_t z32_park_req;
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
  uint32 drawn, skipped;                     /* frames drawn; game frames run without a picture */
  uint32 direct, relayouts;                  /* frames drawn in direct colour; layout changes */
  uint32 direct_draw_sum;                    /* FRT ticks drawing the direct colour frames */
  uint32 hybrid, hybrid_fail;                /* frames shown with the Genesis planes; redrawn: a line failed */
  uint32 why[kMdWhy_Count];                  /* why the others were not (md_planes.h) */
  uint32 ops_words, ops_ticks;               /* words of ops sent to the 68000, FRT ticks it took */
  uint32 md_ticks, begin_ticks;              /* FRT ticks: MdPlanes_Prepare; FrameBegin + the copy */
};
extern "C" { Z32Stats z32_stats; extern char *z32_heap_peak; }

/* the 68000's frame counter (cart/cart_md.s command 48: pad in COMM8, V-blanks in COMM10) */
static void comm_pause(void) { for (volatile int k = 0; k < 64; k++) { } }
static uint16 z32_pad;                           /* the pad, as the 68000 last read it (SEGA_CTRL_*) */
static uint16 md_frames(void) {
  COMM0 = 48;
  for (uint32 n = 0; n < 40000 && COMM0 != 0; n++) comm_pause();
  z32_pad = COMM8;
  return COMM10;
}

/* the Genesis pad as the SNES joypad zelda3 reads (bit 0 B, 1 Y, 2 Select, 3 Start, 4 Up, 5 Down,
 * 6 Left, 7 Right, 8 A, 9 X, 10 L, 11 R). Bottom row A B C = Y (item) B (sword) A (action);
 * top row X Y Z = X (map) L R; Start = Start; Mode = Select. */
static int snes_input(uint16 pad) {
  if ((pad & 0xF000) == 0xF000)
    return 0;                                    /* no pad */
  static const struct { uint16 md; uint16 snes; } kMap[] = {
    { 0x0001, 1 << 4 }, { 0x0002, 1 << 5 }, { 0x0004, 1 << 6 }, { 0x0008, 1 << 7 },   /* d-pad */
    { 0x0040, 1 << 1 }, { 0x0010, 1 << 0 }, { 0x0020, 1 << 8 },                     /* A B C */
    { 0x0400, 1 << 9 }, { 0x0200, 1 << 10 }, { 0x0100, 1 << 11 },                   /* X Y Z */
    { 0x0080, 1 << 3 }, { 0x0800, 1 << 2 },                                         /* Start, Mode */
  };
  int in = 0;
  for (unsigned i = 0; i < sizeof kMap / sizeof kMap[0]; i++)
    if (pad & kMap[i].md)
      in |= kMap[i].snes;
  return in;
}
static uint16 frt(void) { uint8 h = FRT_FRCH; uint8 l = FRT_FRCL; return (uint16)(h << 8 | l); }
extern "C" uint16 z32_frt(void) { return frt(); }

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
  z32_sound_slave_init();                        /* the PWM effects, in its H interrupt */
  uint32 seen = 0;
  for (;;) {
    uint32 cmd;
    while ((cmd = THRU(z32_slave_cmd)) == seen) {
      if (THRU(z32_park_req)) z32_slave_park();
    }
    seen = cmd;
    CCR = 0x11;                                  /* purge: drop what the master changed since */
    uint16 t0 = frt();
    ZeldaDrawPpuLines(&z32_slave_ctx, (int)THRU(z32_slave_from), z32_slave_ctx.height, z32_slave_ctx.height);
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

/* The picture in the frame buffer, in one of three layouts, chosen per frame:
 * Genesis planes ("rip and tear", zelda3/snes/md_planes.h): 8 bits, the 32X's own palette
 *   (MdPlanes_Palette32x) - only the sprites and BG3 are drawn, BG1 / BG2 are the Genesis's
 *   planes, seen through index 0; the side borders are index 32 (black, in front).
 * The 32X's priority bit (DISPMODE bit 7) is set in every layout: a pixel is in front of the
 *   Genesis's unless its colour has the through bit (only the Genesis-planes palette has any).
 * direct colour (DISPMODE 2; 15 bits a pixel: the SNES colour math, the colour window's clip,
 *   the brightness) for the frames that need it (PpuNeedsDirectColour): a 32X line is 320 pixels
 *   = 640 bytes, 224 of them would not fit in the 128 KB, so the lines overlap: line y starts at
 *   0x200 + y * 576, 32 black pixels on each side; the right border of a line is the left border
 *   of the next. The SNES's 256 pixels start 64 bytes into a line. 129,600 bytes.
 * packed pixels (DISPMODE 1; 8 bits: the CGRAM index, the CGRAM as the 32X palette, brightness
 *   applied to it) for the others - half the frame buffer writes: lines of 320 bytes, the
 *   picture 32 bytes in.
 * Z32_INDEX8 / Z32_DIRECT: one layout for every frame (tests). */
struct Z32Layout { uint8 dispmode; uint16 step, offset; uint32 flags; };
static const Z32Layout kLayouts[3] = {
  { 0x81, 320, 32, kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Index8 },
  { 0x82, 576, 64, kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Rgb15 },
  { 0x81, 320, 32, kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Hybrid },
};
static int8 z32_fb_layout[2] = { -1, -1 };       /* per frame buffer (FBCTL bit 0 when it is drawn) */
static uint8 z32_shown_layout, z32_drawn_layout;

static void layout_frame_buffer(int l) {         /* the line table and black borders, for layout l */
  const Z32Layout *L = &kLayouts[l];
  for (int i = 0; i < 256; i++) FB16[i] = (uint16_t)((0x200 + (i < 224 ? i : 0) * L->step) / 2);
  volatile uint32 *p = (uint32 *)FB8;            /* words: the FB drops byte writes of 0 */
  if (l == 1) {
    for (int y = 0; y <= 224; y++)               /* 64 bytes at each line start = every border */
      for (int k = 0; k < 16; k++) p[(y * 576) / 4 + k] = 0;
  } else {
    uint32 b = l == 2 ? 0x20202020 : 0;          /* Genesis planes: index 32, black in front */
    for (int y = 0; y < 224; y++) {
      for (int k = 0; k < 8; k++) p[(y * 320) / 4 + k] = b;
      for (int k = 0; k < 8; k++) p[(y * 320 + 288) / 4 + k] = b;
    }
  }
}

static void video_init(void) {
  VDP_DISPMODE = 0x81;                           /* PRI: the 32X in front of the Genesis */
  for (int fb = 0; fb < 2; fb++) {
    volatile uint32 *p = (uint32 *)FB8;          /* words: the FB drops byte writes of 0 */
    for (int i = 0; i < (0x20000 - 0x200) / 4; i++) p[i] = 0;
    layout_frame_buffer(0);
    z32_fb_layout[VDP_FBCTL & 1] = 0;
    VDP_FBCTL = (uint16_t)((VDP_FBCTL & 1) ^ 1);
    while (!(VDP_FBCTL & 0x8000)) { }            /* the swap happens in the V-blank */
  }
}

static uint16 z32_palette[256];                  /* the 32X colours of the picture last drawn */
static MdHybrid z32_hybrid;                      /* its split (Genesis planes frames) */
static void snapshot_palette(int layout) {
  const uint16 *cg = g_zenv.ppu->cgram;
  uint8 b = g_zenv.ppu->brightness;
  if (layout == 2) {
    MdPlanes_Palette32x(&z32_hybrid, cg, b, z32_palette);
    return;
  }
  uint8 m[32];                                   /* the SNES brightness: each 5-bit level * b / 15 */
  for (int i = 0; i < 32; i++) m[i] = (uint8)(i * b / 15);
  for (int i = 0; i < 256; i++) {
    uint16_t c = cg[i] & 0x7FFF;                 /* SNES BGR555 = 32X BGR555 */
    if (b != 15)
      c = (uint16_t)(m[c & 31] | m[c >> 5 & 31] << 5 | m[c >> 10] << 10);
    z32_palette[i] = c ? c : 0x0400;             /* black as 1/31 blue: 0x0000 let the MD layer show (first tests) */
  }
}
static uint16 z32_cram[256];                     /* what the 32X's CRAM holds */
static bool z32_cram_valid;
static void palette_from_cgram(void) {           /* only the colours that changed */
  for (int i = 0; i < 256; i++) {
    if (z32_cram_valid && z32_cram[i] == z32_palette[i]) continue;
    while (!(VDP_FBCTL & 0x2000)) { }            /* PEN: writable only in the blanking (the 32X needs this) */
    CRAM[i] = z32_cram[i] = z32_palette[i];
  }
  z32_cram_valid = true;
}

static void flip(void) {
  uint16_t cur = VDP_FBCTL & 1;
  VDP_FBCTL = (uint16_t)(cur ^ 1);
  while ((VDP_FBCTL & 1) == cur) { }
}

#ifdef Z32_STOP_ON_BAD
static void z32_stop(void);
#endif
/* ---- test checks (off with Z32_NO_CHECK): the RAM after every game frame, the picture of
 * every drawn frame, against the PC's (expect.bin / expect_px.bin) ---- */
static void check_ram(uint32 f) {
#ifndef Z32_NO_CHECK
  if (f >= (uint32)(z32_expect_end - z32_expect) / 4)
    return;
  uint16 t1 = frt();
  uint32 h = 2166136261u;                        /* FNV-1a, as pc/zh_main.c */
  for (int i = 0; i < 0x20000; i++) h = (h ^ g_ram[i]) * 16777619u;
  const uint8 *e = z32_expect + 4 * f;
  uint32 want = (uint32)e[0] << 24 | e[1] << 16 | e[2] << 8 | e[3];
  z32_stats.checked = f + 1;
  z32_stats.hash_sum += (uint16)(frt() - t1);
  if (h != want && z32_stats.first_bad == 0xFFFFFFFF) {
    z32_stats.first_bad = f; z32_stats.bad_hash = h; z32_stats.want_hash = want;
#ifdef Z32_STOP_ON_BAD
    z32_stop();                                  /* debug: hold g_ram as it is for a dump */
#endif
  }
#else
  (void)f;
#endif
}

static void check_picture(uint32 f) {
#ifndef Z32_NO_CHECK
  if (f >= (uint32)(z32_expect_px_end - z32_expect_px) / 4)
    return;
  uint32 ph = 2166136261u;
  const Z32Layout *L = &kLayouts[z32_drawn_layout];
  const uint8 *pic = (const uint8 *)FB8 + L->offset;
  if (z32_drawn_layout == 0) {                   /* as pc/zh_main.c: indices + CGRAM */
    for (int y = 0; y < 224; y++) { const uint8 *row = pic + y * L->step; for (int x = 0; x < 256; x++) ph = (ph ^ row[x]) * 16777619u; }
    for (int i = 0; i < 256; i++) { uint16 c = g_zenv.ppu->cgram[i]; ph = (ph ^ (c & 0xff)) * 16777619u; ph = (ph ^ (c >> 8)) * 16777619u; }
  } else {                                       /* the 16-bit values (ZH_RGB15) */
    for (int y = 0; y < 224; y++) {
      const uint16 *row = (const uint16 *)(pic + y * L->step);
      for (int x = 0; x < 256; x++) { uint16 v = row[x]; ph = (ph ^ (v & 0xff)) * 16777619u; ph = (ph ^ (v >> 8)) * 16777619u; }
    }
  }
  const uint8 *e = z32_expect_px + 4 * f;
  z32_stats.px_checked++;
  if (ph != ((uint32)e[0] << 24 | e[1] << 16 | e[2] << 8 | e[3]) && z32_stats.px_first_bad == 0xFFFFFFFF) {
    z32_stats.px_first_bad = f;
#ifdef Z32_STOP_ON_BAD
    z32_stop();                                  /* debug: show the bad picture, hold */
#endif
  }
#else
  (void)f;
#endif
}

/* ---- the Genesis planes ("rip and tear", zelda3/snes/md_planes.h) ----
 * Once per drawn frame MdPlanes_Prepare splits the frame; MdPlanes_Send (while the slave draws)
 * sends the 68000 what changed in BG1 / BG2 (command 70, a
 * packet of 4 words at a time through COMM4-COMM10, COMM14 its number, COMM2 the 68000's answer);
 * after drawing, the planes to show; at the swap, command 71 commits it all: the 68000 applies
 * it in the next V-blank and counts it in COMM12, and the master swaps the 32X's frame buffer
 * right then - the same V-blank, so both halves of the picture change together.
 * Z32_NO_MD (and the test build, whose checks compare whole 8-bit pictures): never. */
#if !defined(Z32_NO_MD) && defined(Z32_NO_CHECK) && !defined(Z32_INDEX8) && !defined(Z32_DIRECT)
#define Z32_MD 1
#endif
static MdPlanes z32_md;
static uint16 z32_ops_t;
static void md_flush(MdSink *k) {
  uint16 t0 = frt();
  int n = k->n;
  COMM14 = 0;
  COMM2 = (uint16)n;
  COMM0 = 70;
  const uint16 *w = k->buf;
  for (int i = 0, seq = 1; i < n; i += 4, seq++) {
    uint16 a = w[i], b = i + 1 < n ? w[i + 1] : 0, c = i + 2 < n ? w[i + 2] : 0, d = i + 3 < n ? w[i + 3] : 0;
    COMM4L = (uint32)a << 16 | b;
    COMM8L = (uint32)c << 16 | d;
    COMM14 = (uint16)seq;
    while (COMM2 != (uint16)(seq | 0x8000)) { }
  }
  while (COMM0 != 0) { }
  z32_stats.ops_words += n;
  z32_ops_t += (uint16)(frt() - t0);
}
static MdSink z32_sink = { {0}, 0, 0, 0, md_flush };

/* the picture: two SH2s, master lines 0..split-1, slave split..; the split balanced each frame.
 * false: a line of a Genesis-planes frame could not be shown so (MdHybrid) - nothing to swap,
 * the next frame is drawn whole */
static bool draw_frame(void) {
  static uint32 seq, split = 112;
  static bool md_off;                            /* the last frame's lines failed: whole */
  ZeldaDrawCtx ctx;
#if defined(Z32_INDEX8)
  int l = 0;
#elif defined(Z32_DIRECT)
  int l = 1;
#else
  int l = PpuNeedsDirectColour(g_zenv.ppu) ? 1 : 0;
#endif
#ifdef Z32_MD
  z32_ops_t = 0;
  uint16 tm0 = frt();
  MdPlanes_Prepare(&z32_md, g_zenv.ppu, &z32_hybrid, kMd_CommitMax - z32_sink.total);
  z32_stats.md_ticks += (uint16)(frt() - tm0);
  if (!z32_hybrid.why && !md_off) l = 2;
  else z32_stats.why[md_off ? kMdWhy_LineCheck : z32_hybrid.why]++;
  md_off = false;
  g_zenv.ppu->hybrid = &z32_hybrid;
  g_zenv.ppu->hybridFail = 0;
  g_zenv.ppu->hybridKey[5] = 0;                  /* no line checked yet */
  g_zenv.ppu->regsChanged = 3;                   /* every line checked from the first */
  for (int i = 0; i < 64; i++) g_zenv.ppu->lineBuf[i] = 0;   /* the painter keeps it 0 between lines */
#endif
  int fb = VDP_FBCTL & 1;
  z32_hybrid.fb = (uint8)(fb ^ 1);               /* drawn into the buffer not shown */
  if (z32_fb_layout[fb] != l) {                  /* this frame buffer held the other layout */
    layout_frame_buffer(l);
    z32_fb_layout[fb] = (int8)l;
    z32_stats.relayouts++;
    PpuHybridSpansFull(z32_hybrid.fb);
  }
  if (l != 2) PpuHybridSpansFull(z32_hybrid.fb);   /* (a frame drawn whole: no line is as it was) */
  z32_drawn_layout = (uint8)l;
  z32_stats.direct += l == 1;
  uint16 tb0 = frt();
  ZeldaDrawPpuFrameBegin(&ctx, (uint8 *)FB8 + kLayouts[l].offset, kLayouts[l].step, kLayouts[l].flags);
  ZeldaDrawCtxCopy(&z32_slave_ctx, &z32_slave_ppu, &ctx);
  z32_stats.begin_ticks += (uint16)(frt() - tb0);
  CCR = 0x11;                                    /* purge: the slave drew some of our lines last time
                                                    (ppu.c's line tables; the slave purges too) */
  THRU(z32_slave_from) = split;
  THRU(z32_slave_cmd) = ++seq;
  uint16 ta = frt();
#ifdef Z32_MD
  MdPlanes_Send(&z32_md, g_zenv.ppu, &z32_hybrid, &z32_sink);   /* the slave draws meanwhile */
#endif
  ZeldaDrawPpuLines(&ctx, 0, (int)split - 1, ctx.height);   /* the events to the end: the PPU
                                                                 ends as after a whole frame */
  uint16 tb = frt();
  while (THRU(z32_slave_done) != seq) { }
  uint16 tc = frt();
  ZeldaDrawPpuFrameEnd(&ctx);
  bool shown = true;
#ifdef Z32_MD
  if (l == 2 && (g_zenv.ppu->hybridFail || z32_hybrid.why ||   /* (why: the ops did not fit) */
                 *(volatile uint8 *)((uintptr_t)&z32_slave_ppu.hybridFail | 0x20000000))) {
    md_off = true;                               /* not shown: the next one is drawn whole */
    shown = false;
    z32_stats.hybrid_fail++;
  } else {
    MdPlanes_Show(&z32_md, l == 2 ? (z32_hybrid.layers | (z32_hybrid.flip == 1) << 2) : 0, &z32_sink);
    z32_stats.hybrid += l == 2;
  }
  z32_stats.ops_ticks += z32_ops_t;
#endif
  if (shown) snapshot_palette(l);                /* shown with this picture */
  uint32 tm = (uint16)(tb - ta), ts = THRU(z32_slave_ticks);
  z32_stats.split = split; z32_stats.slave_sum += ts; z32_stats.wait_sum += (uint16)(tc - tb);
  /* balance: the per-line cost of each part; move the split toward equal times (a few lines a
   * frame, so one odd frame does not swing it) */
  uint32 ns = 225 - split;
  if (tm && ts) {
    uint32 want = (uint32)((uint64_t)225 * (ts * split) / (ts * split + tm * ns));
    if (want > split + 4) want = split + 4;
    if (want + 4 < split) want = split - 4;
    if (want < 16) want = 16;
    if (want > 208) want = 208;
    split = want;
  }
  return shown;
}

#ifdef Z32_STOP_ON_BAD
static void flip(void);
static void palette_from_cgram(void);
static void NORETURN z32_stop(void) {
  __asm__ volatile("" ::: "memory");             /* z32_stats is written before we hold */
  snapshot_palette(z32_drawn_layout);
  flip();                                        /* the frame just drawn, on screen */
  VDP_DISPMODE = kLayouts[z32_drawn_layout].dispmode;
  if (z32_drawn_layout != 1) palette_from_cgram();
  COMM14 = 0x0BAD;
  for (;;) __asm__ volatile("" ::: "memory");
}
#endif

/* the picture just drawn goes on screen at the next V-blank: right after drawing, the frame
 * buffer swap is asked for (outside a V-blank the 32X does it at the next one) and the Genesis's
 * ops committed (the 68000 applies them in that V-blank) - both halves change together, and the
 * master goes on with the game meanwhile. A change of the 32X's mode (direct colour <-> 8-bit)
 * waits for the V-blank instead (swap_pictures). */
static uint16 z32_swap_cur;
static bool z32_swap_async;
static void request_swap(void) {
  z32_swap_async = kLayouts[z32_drawn_layout].dispmode == kLayouts[z32_shown_layout].dispmode;
  if (!z32_swap_async) return;
#ifdef Z32_MD
  COMM0 = 71;                                    /* commit */
  for (uint32 n = 0; n < 4000000 && COMM0 != 0; n++) comm_pause();
  z32_sink.total = 0, z32_sink.cost = 0;                            /* the next commit's room */
#endif
  while (VDP_FBCTL & 0x8000) { }                 /* not in a V-blank: the swap waits for the next */
  z32_swap_cur = VDP_FBCTL & 1;
  VDP_FBCTL = (uint16_t)(z32_swap_cur ^ 1);
}
/* at the next pass (a V-blank has come): the swap done, the palette and mode of the picture */
static void swap_pictures(void) {
  uint16 t4 = frt();
  if (z32_swap_async) {
    while ((VDP_FBCTL & 1) == z32_swap_cur) { }
  } else {
#ifdef Z32_MD
  uint16 applied = COMM12;
  COMM0 = 71;
  for (uint32 n = 0; n < 4000000 && COMM0 != 0; n++) comm_pause();
  for (uint32 n = 0; n < 4000000 && COMM12 == applied; n++) { }
  uint16_t cur = VDP_FBCTL & 1;
  VDP_FBCTL = (uint16_t)(cur ^ 1);
  while ((VDP_FBCTL & 1) == cur) { }
  z32_sink.total = 0, z32_sink.cost = 0;                            /* the next commit's room */
#else
  flip();
#endif
  }
  uint16 t5 = frt();
  if (z32_shown_layout != z32_drawn_layout) {    /* in the V-blank the swap happened in */
    VDP_DISPMODE = kLayouts[z32_drawn_layout].dispmode;
    z32_shown_layout = z32_drawn_layout;
  }
  if (z32_drawn_layout != 1) palette_from_cgram();   /* direct colour: no palette */
  z32_stats.flip_sum += (uint16)(t5 - t4);
  z32_stats.pal_sum += (uint16)(frt() - t5);
}

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
  ZeldaSetLanguage(NULL);                        /* the game's text (main.c does this on the PC; without it every message is empty) */
  COMM12 = 2;
#ifdef Z32_PLAY
  ZeldaReadSram();                               /* the save file from the cartridge (z32_save.cpp) */
#ifdef Z32_SAVE_TEST
  { void z32_save_selftest(void); z32_save_selftest(); }
#endif
#endif
#ifndef Z32_PLAY                                 /* Z32_PLAY: no replay, the pad from power-on */
  if (!ZeldaStartReplayFromMemory(z32_replay, (size_t)(z32_replay_end - z32_replay))) Die("replay");
#endif
  video_init();
  MdPlanes_Reset(&z32_md);                       /* every tile and cell goes to the Genesis */
#ifdef Z32_PPU_TIMING
  { extern Ppu *g_ppu_time_ctx; g_ppu_time_ctx = g_zenv.ppu; }
#endif
#ifdef Z32_HYB_TIMING
  { extern Ppu *g_hyb_time_ctx; g_hyb_time_ctx = g_zenv.ppu; }
#endif
#ifdef Z32_PATTERN
  for (;;) {                                     /* display test: colour bars, no game */
    for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) FB8[y * 320 + x] = (uint8)(x / 20 + 1 + (y / 56) * 16);
    for (int i = 1; i < 256; i++) { while (!(VDP_FBCTL & 0x2000)) { } CRAM[i] = (uint16)((i * 2) & 31 | ((i * 5) & 31) << 5 | ((i * 11) & 31) << 10) | 0x0400; }
    flip();
    COMM14++;
  }
#endif
  COMM12 = 3;
  /* frame skip: the game logic runs at the SNES's 60 Hz, timed by the 68000's V-blank count;
   * each pass runs the frames that are due (at most Z32_MAX_SKIP: past that the game slows
   * down rather than jump), the ones before the last without a picture (ZeldaSkipPpuFrame:
   * the game RAM and the PPU end as if drawn), then draws once. Z32_NO_SKIP: every frame. */
#ifdef Z32_NO_SKIP
  enum { kMaxSkip = 1 };
#else
  enum { kMaxSkip = 6 };
#endif
  uint32 f = 0, clock = 0, credited = 0, clock_base = 0;
  uint16 last = md_frames();
  bool pending = false;                          /* a drawn picture waits for its V-blank */
  for (;;) {
    uint16 now;
    do {                                         /* wait until a frame is due */
      now = md_frames();
      clock += (uint16)(now - last), last = now;
    } while (clock <= credited && kMaxSkip > 1);
    if (pending) { swap_pictures(); pending = false; }   /* the V-blank of its swap */
    uint32 n = kMaxSkip > 1 ? clock - credited : 1;
    if (n < 1) n = 1;
    if (n > kMaxSkip) n = kMaxSkip;
    credited = kMaxSkip > 1 && clock - credited > kMaxSkip ? clock - kMaxSkip + n : credited + n;
    for (uint32 k = 0; k < n; k++, f++) {
      uint16 t0 = frt();
      ZeldaRunFrame(snes_input(z32_pad));        /* a replay ignores it; after one, the pad plays */
      uint32 dl = (uint16)(frt() - t0);
      z32_stats.logic_sum += dl;
      if (dl > z32_stats.logic_max) z32_stats.logic_max = dl;
      check_ram(f);
      if (k + 1 < n) {
        ZeldaSkipPpuFrame();
        z32_stats.skipped++;
      }
    }
    z32_stats.frames = f;
    z32_music_service();                         /* a song change to the 68000 / Sega CD */
#ifdef Z32_STATS_FROM                            /* test: the timings of one part of the game */
    static bool reset;
    if (!reset && f >= Z32_STATS_FROM) {
      reset = true;
      uint32 magic = z32_stats.magic;
      memset(&z32_stats, 0, sizeof z32_stats);
      z32_stats.magic = magic, z32_stats.first_bad = z32_stats.px_first_bad = 0xFFFFFFFF;
      clock_base = clock;
#ifdef Z32_PPU_TIMING
      extern uint32 g_ppu_t[6];
      memset(g_ppu_t, 0, sizeof g_ppu_t);
#endif
    }
#endif
    uint16 t2 = frt(), m2 = md_frames();
    bool shown = draw_frame();
    uint16 t3 = frt(), m3 = md_frames();
    check_picture(f - 1);
    uint32 dd = (uint16)(t3 - t2);
    z32_stats.draw_sum += dd;
    if (z32_drawn_layout == 1) z32_stats.direct_draw_sum += dd;
    if (dd > z32_stats.draw_max) z32_stats.draw_max = dd;
    z32_stats.v_draw += (uint16)(m3 - m2);
    z32_stats.drawn++;
    z32_stats.heap_peak = (uint32)(z32_heap_peak - (char *)0x06000000);
    pending = shown;
    if (shown) request_swap();
    z32_stats.v_total = clock - clock_base;
  }
}

