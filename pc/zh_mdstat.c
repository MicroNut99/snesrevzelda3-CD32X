/* Zelda 3 32X - PC analysis for the "rip and tear" plan (BG1 / BG2 on the Genesis planes, the
 * 32X drawing the sprites and BG3). Built only into pc/zhx_md (-DZH_MDSTAT, pc/Makefile.cpp);
 * zh_main.c calls it when ZH_MDSTAT=<file> is set. One line per frame:
 *   f fb mode direct var hdma tm tmw win mos | npal cells out4 p0..p7 | vtot vchar vmap |
 *   bg1(map w h tiles h v) bg2(...) | spr n01 n2 n3 px01 px2 px3 npal01 |
 *   clip prevent mathEnabled addSub half subtract fixedR fixedG fixedB TS TSW (after the frame) |
 *   mathpix: pixels where the direct-colour picture differs from the 8-bit one (the math shows)
 * var: what changed between lines (bit 0 BG1 HOFS, 1 BG2 HOFS, 2 BG1 VOFS, 3 BG2 VOFS,
 *   4 BG1/2 setup (mode, TM bits 0-1, map / tile bases, mosaic), 5 window registers,
 *   6 colour math, 7 brightness / forced blank, 8 BG3 scroll, 9 other TM / TS bits)
 * win: bit l = layer l's main-screen window masks something on some line
 * hdma: channel 6 / 7 target register and mode (ff = off)
 * vtot / vchar / vmap: VRAM bytes changed since the frame's logic began: all, in the tiles
 *   BG1/2's whole maps use, in BG1/2's maps
 * out4: visible non-empty BG1/2 cells whose palette is not among the 4 most used */
#include <stdio.h>
#include <string.h>
#include "../zelda3/src/types.h"
#include "../zelda3/src/zelda_rtl.h"
#include "../zelda3/snes/ppu.h"

extern void (*g_zh_line_hook)(Ppu *ppu, int line);

typedef struct LineState {
  uint16 h[3], v[3];
  uint32 cfg, win, math, bright, tmo;
} LineState;

static LineState first;
static uint32 var;
static int nlines;
static uint8 win_eff;
static uint32 stamp[2][0x8000];
static uint32 gen;
static uint32 pal_cells[8], cells;

static uint32 hash_bytes(const void *p, size_t n, uint32 h) {
  const uint8 *b = (const uint8 *)p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}

static void snap(Ppu *q, LineState *s) {
  for (int l = 0; l < 3; l++) s->h[l] = q->bgLayer[l].hScroll, s->v[l] = q->bgLayer[l].vScroll;
  uint32 h = 2166136261u;
  uint8 m = q->mode, tm = q->screenEnabled[0] & 3, mos = q->mosaicEnabled & 3;
  h = hash_bytes(&m, 1, h); h = hash_bytes(&tm, 1, h); h = hash_bytes(&mos, 1, h);
  for (int l = 0; l < 2; l++) {
    BgLayer *b = &q->bgLayer[l];
    h = hash_bytes(&b->tilemapAdr, 2, h); h = hash_bytes(&b->tileAdr, 2, h);
    uint8 wh = b->tilemapWider | b->tilemapHigher << 1; h = hash_bytes(&wh, 1, h);
  }
  s->cfg = h;
  h = 2166136261u;
  h = hash_bytes(&q->screenWindowed[0], 2, h); h = hash_bytes(&q->windowsel, 4, h);
  h = hash_bytes(&q->window1left, 1, h); h = hash_bytes(&q->window1right, 1, h);
  h = hash_bytes(&q->window2left, 1, h); h = hash_bytes(&q->window2right, 1, h);
  s->win = h;
  h = 2166136261u;
  h = hash_bytes(&q->clipMode, 1, h); h = hash_bytes(&q->preventMathMode, 1, h);
  h = hash_bytes(&q->addSubscreen, 1, h); h = hash_bytes(&q->subtractColor, 1, h);
  h = hash_bytes(&q->halfColor, 1, h); h = hash_bytes(&q->mathEnabled, 1, h);
  h = hash_bytes(&q->fixedColorR, 3, h);
  s->math = h;
  s->bright = q->brightness | q->forcedBlank << 8;
  s->tmo = (q->screenEnabled[0] & ~3) | q->screenEnabled[1] << 8;
}

/* does layer l's main-screen window hide anything on this line */
static bool window_masks(Ppu *q, int l) {
  if (!(q->screenWindowed[0] & (1 << l))) return false;
  uint32 sel = (q->windowsel >> (4 * l)) & 15;
  bool any = false;
  if (sel & 2) { bool empty = q->window1left > q->window1right; bool inv = sel & 1;
    if (inv ? !(q->window1left == 0 && q->window1right == 255) : !empty) any = true; }
  if (sel & 8) { bool empty = q->window2left > q->window2right; bool inv = sel & 4;
    if (inv ? !(q->window2left == 0 && q->window2right == 255) : !empty) any = true; }
  return any;
}

static void line_hook(Ppu *q, int line) {
  if (line < 1 || line > 224) return;
  LineState s;
  snap(q, &s);
  if (nlines++ == 0) first = s;
  else {
    for (int l = 0; l < 2; l++) {
      if (s.h[l] != first.h[l]) var |= 1 << l;
      if (s.v[l] != first.v[l]) var |= 4 << l;
    }
    if (s.cfg != first.cfg) var |= 16;
    if (s.win != first.win) var |= 32;
    if (s.math != first.math) var |= 64;
    if (s.bright != first.bright) var |= 128;
    if (s.h[2] != first.h[2] || s.v[2] != first.v[2]) var |= 256;
    if (s.tmo != first.tmo) var |= 512;
  }
  if (q->forcedBlank || q->mode != 1) return;
  for (int l = 0; l < 2; l++) {
    if (!(q->screenEnabled[0] & (1 << l))) continue;
    if (window_masks(q, l)) win_eff |= 1 << l;
    BgLayer *b = &q->bgLayer[l];
    int y = (line + b->vScroll) & 0x3ff;
    for (int cx = 0; cx < 33; cx++) {
      int x = ((b->hScroll & ~7) + cx * 8) & 0x3ff;
      if (cx == 32 && !(b->hScroll & 7)) break;
      uint16 a = b->tilemapAdr + (((y >> 3) & 0x1f) << 5 | ((x >> 3) & 0x1f));
      if ((x & 0x100) && b->tilemapWider) a += 0x400;
      if ((y & 0x100) && b->tilemapHigher) a += b->tilemapWider ? 0x800 : 0x400;
      a &= 0x7fff;
      if (stamp[l][a] == gen) continue;
      stamp[l][a] = gen;
      uint16 e = q->vram[a];
      const uint16 *t = (const uint16 *)&q->vram[(b->tileAdr + (e & 0x3ff) * 16) & 0x7fff];
      bool empty = true;
      for (int i = 0; i < 16; i++) if (t[i]) { empty = false; break; }
      if (empty) continue;
      pal_cells[(e >> 10) & 7]++;
      cells++;
    }
  }
}

static uint16 vram_prev[0x8000];
void zh_mdstat_before_logic(void) { memcpy(vram_prev, g_zenv.ppu->vram, sizeof vram_prev); }

static const uint8 kSizes[8][2] = { {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32} };

/* the frame's picture, drawn as the game does (ZeldaDrawPpuFrame), with the line hook */
void zh_mdstat_draw(FILE *out, int f, uint8 *buf, size_t pitch, uint32 flags) {
  Ppu *q = g_zenv.ppu;
  ZeldaDrawCtx ctx;
  gen++;
  var = 0; nlines = 0; win_eff = 0; cells = 0;
  memset(pal_cells, 0, sizeof pal_cells);
  bool direct = PpuNeedsDirectColour(q);
  (void)flags; (void)pitch;
  ZeldaDrawPpuFrameBegin(&ctx, buf, 256, kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Index8);
  /* the same frame in direct colour (colour math, the colour window's clip), on a copy of the
   * context: where it differs from the 8-bit picture (CGRAM + brightness), the math shows */
  static Ppu q2;
  static uint16 rgb[256 * 240];
  ZeldaDrawCtx ctx2;
  ZeldaDrawCtxCopy(&ctx2, &q2, &ctx);
  PpuBeginDrawing(&q2, (uint8 *)rgb, 512, kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Rgb15);
  ZeldaDrawPpuLines(&ctx2, 0, ctx2.height, ctx2.height);
  int hd[2];
  for (int k = 0; k < 2; k++) hd[k] = ctx.hdma[k].table ? (ctx.hdma[k].ppu_addr << 8 | ctx.hdma[k].mode) : 0xffff;
  /* sprites: on screen, by priority (OAM does not change during the frame) */
  uint32 n[4] = { 0 }, px[4] = { 0 }, pal01 = 0;
  for (int k = 0; k < q->objCount; k++) {
    int i = q->objList[k];
    int hi = q->objHigh[k];
    int sz = kSizes[q->objSize][(hi >> 1) & 1];
    int yy = q->oam[i * 2] >> 8;
    int x = (q->oam[i * 2] & 0xff) + (hi & 1) * 256;
    if (x >= 256) x -= 512;
    int rows = 0;
    for (int r = 0; r < sz; r++) { int line = (yy + r) & 0xff; if (line < 224) rows++; }
    int x0 = x < 0 ? 0 : x, x1 = x + sz > 256 ? 256 : x + sz;
    if (!rows || x1 <= x0) continue;
    int attr = q->oam[i * 2 + 1] >> 8;
    int p = (attr >> 4) & 3;
    n[p]++; px[p] += rows * (x1 - x0);
    if (p < 2) pal01 |= 1 << ((attr >> 1) & 7);
  }
  LineState s0; snap(q, &s0);
  BgLayer b0[2] = { q->bgLayer[0], q->bgLayer[1] };
  g_zh_line_hook = line_hook;
  ZeldaDrawPpuLines(&ctx, 0, ctx.height, ctx.height);
  g_zh_line_hook = 0;
  ZeldaDrawPpuFrameEnd(&ctx);
  uint32 mathpix = 0;
  if (!q->forcedBlank) {
    for (int i = 0; i < 256 * 224; i++) {
      uint16 c = q->cgram[buf[i]];
      uint16 c8 = (uint16)(q2.bright5[c & 31] | q2.bright5[c >> 5 & 31] << 5 | q2.bright5[c >> 10 & 31] << 10);
      if ((rgb[i] & 0x7fff) != c8) mathpix++;
    }
  }
  /* palettes: the four most used, the rest */
  int npal = 0; uint32 sorted[8];
  for (int i = 0; i < 8; i++) { sorted[i] = pal_cells[i]; if (pal_cells[i]) npal++; }
  for (int i = 0; i < 8; i++) for (int j = i + 1; j < 8; j++) if (sorted[j] > sorted[i]) { uint32 t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }
  uint32 out4 = 0; for (int i = 4; i < 8; i++) out4 += sorted[i];
  /* VRAM changed since the frame's logic began; in BG1/2's tile and map areas */
  static uint8 area[0x8000];
  memset(area, 0, sizeof area);
  for (int l = 0; l < 2; l++) {
    /* the tiles the layer's whole map uses (what a Genesis mirror of it needs), and the map */
    int msz = 0x400 * (1 + b0[l].tilemapWider) * (1 + b0[l].tilemapHigher);
    for (int i = 0; i < msz; i++) {
      uint16 e = q->vram[(b0[l].tilemapAdr + i) & 0x7fff];
      for (int k = 0; k < 16; k++) area[(b0[l].tileAdr + (e & 0x3ff) * 16 + k) & 0x7fff] |= 1;
    }
    for (int i = 0; i < msz; i++) area[(b0[l].tilemapAdr + i) & 0x7fff] |= 2;
  }
  uint32 vt = 0, vc = 0, vm = 0;
  for (int i = 0; i < 0x8000; i++) {
    if (vram_prev[i] != (uint16)q->vram[i]) {
      vt += 2;
      if (area[i] & 2) vm += 2; else if (area[i] & 1) vc += 2;
    }
  }
  int npal01 = __builtin_popcount(pal01);
  fprintf(out, "%d %d %d %d %x %04x %04x %02x %02x %d %d | %d %u %u", f, q->forcedBlank, q->mode, direct, var, hd[0], hd[1],
          q->screenEnabled[0], q->screenWindowed[0], win_eff, q->mosaicEnabled & 3, npal, cells, out4);
  for (int i = 0; i < 8; i++) fprintf(out, " %u", pal_cells[i]);
  fprintf(out, " | %u %u %u", vt, vc, vm);
  for (int l = 0; l < 2; l++)
    fprintf(out, " | %04x %d %d %04x %d %d", b0[l].tilemapAdr, b0[l].tilemapWider, b0[l].tilemapHigher, b0[l].tileAdr, s0.h[l], s0.v[l]);
  fprintf(out, " | %u %u %u %u %u %u %d", n[0] + n[1], n[2], n[3], px[0] + px[1], px[2], px[3], npal01);
  fprintf(out, " | %d %d %02x %d %d %d %d %d %d %02x %02x | %u\n", q->clipMode, q->preventMathMode, q->mathEnabled, q->addSubscreen,
          q->halfColor, q->subtractColor, q->fixedColorR, q->fixedColorG, q->fixedColorB, q->screenEnabled[1], q->screenWindowed[1], mathpix);
}
