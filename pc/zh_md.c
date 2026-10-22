/* Zelda 3 32X - the PC check of the "rip and tear" picture (md_planes.c): a Genesis video chip
 * model (VRAM, CRAM, VSRAM, the two planes) fed with MdPlanes' ops, and the 32X layer on top with
 * its through bits - composed as the 32X does (ares md/m32x/vdp.cpp, PRI = 1), every pixel turned
 * back into the CGRAM index it shows, and compared with the 32X's whole-frame 8-bit picture.
 *   ZH_MDCHECK=1     every frame: the Genesis + 32X picture against the 8-bit one; the counts at
 *                    the end (frames shown with the planes, frames that differ, why the others
 *                    were not)
 *   ZH_MDBAD=dir     the first frames that differ: both pictures as PPM
 *   ZH_VRAMCHECK=1   every frame: each VRAM block that changed was marked dirty (a writer that
 *                    does not mark would leave the Genesis's copy stale) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../zelda3/src/types.h"
#include "../zelda3/src/zelda_rtl.h"
#include "../zelda3/snes/ppu.h"
#include "../zelda3/snes/md_planes.h"

typedef struct MdSim {
  uint8 vram[0x10000];
  uint16 cram[64], vsram[40];
  uint8 reg[24];
} MdSim;
static MdSim g_sim;
static MdPlanes g_md;

static void sim_apply(MdSim *s, const uint16 *op, int n) {
  const uint16 *end = op + n;
  while (op < end) {
    uint32 hi = op[0], lo = op[1], cnt = op[2];
    op += 3;
    if (cnt == 0) { s->reg[(hi >> 8) & 0x1f] = (uint8)hi; if (getenv("ZH_MDREG")) fprintf(stderr, "reg %04x\n", hi); continue; }
    if ((hi & 0xc000) == 0xc000) {
      uint32 a = (hi & 0x7f) >> 1;
      for (uint32 k = 0; k < cnt; k++) s->cram[(a + k * (s->reg[15] >> 1)) & 63] = op[k];
    } else if (lo & 0x10) {
      uint32 a = (hi & 0x7f) >> 1;
      for (uint32 k = 0; k < cnt; k++) s->vsram[(a + k * (s->reg[15] >> 1)) % 40] = op[k];
    } else {
      uint32 a = (hi & 0x3fff) | (lo & 3) << 14;
      for (uint32 k = 0; k < cnt; k++, a += s->reg[15]) { s->vram[a & 0xffff] = (uint8)(op[k] >> 8); s->vram[(a + 1) & 0xffff] = (uint8)op[k]; }
    }
    op += cnt;
  }
}

/* the Genesis plane pixel at (sx, sy): 0 none, else 0x100 | line << 4 | value, priority in bit 9 */
static uint32 sim_plane(const MdSim *s, uint32 base, uint32 hs, uint32 vs, int sx, int sy) {
  uint32 w = (s->reg[16] & 3) ? 64 : 32, hh = (s->reg[16] & 0x30) ? 64 : 32;   /* the plane size */
  uint32 px = (uint32)(sx - (int)hs) & (w * 8 - 1), py = (uint32)(sy + (int)vs) & (hh * 8 - 1);
  const uint8 *c = &s->vram[base + ((py >> 3) * w + (px >> 3)) * 2];
  uint32 cell = c[0] << 8 | c[1];
  uint32 t = cell & 0x7ff, row = py & 7, col = px & 7;
  if (cell & 0x1000) row = 7 - row;
  if (cell & 0x0800) col = 7 - col;
  uint8 b = s->vram[(t * 32 + row * 4 + col / 2) & 0xffff];
  uint32 v = col & 1 ? b & 15 : b >> 4;
  if (!v) return 0;
  return 0x100 | ((cell >> 13) & 3) << 4 | v | (cell >> 15) << 9;
}

void zh_md_reset(void) {
  MdPlanes_Reset(&g_md);
  memset(&g_sim, 0, sizeof g_sim);
  g_sim.reg[2] = kMd_PlaneOff >> 10, g_sim.reg[4] = kMd_PlaneOff >> 13;   /* as the 68000 sets them up */
  g_sim.reg[15] = 2, g_sim.reg[16] = 0x11;
  for (int i = 0; i < 0x1000; i++) g_sim.vram[kMd_PlaneOff + i * 2] = kMd_BlankTile >> 8, g_sim.vram[kMd_PlaneOff + i * 2 + 1] = kMd_BlankTile & 0xff;
}

static long g_ops_frame;
static void sim_flush(MdSink *k) { sim_apply(&g_sim, k->buf, k->n); g_ops_frame += k->n; }
static MdSink g_sink = { {0}, 0, 0, 0, sim_flush };
static MdHybrid g_h;
static long g_tint, g_flip, g_cost_max, g_cost_over;
static long g_frames, g_hybrid, g_bad, g_why[kMdWhy_Count], g_ops_words, g_ops_max, g_bad_dumps;
static void dump(const char *fn, const uint8 *idx, const uint16 *cgram) {
  FILE *o = fopen(fn, "wb"); if (!o) return;
  fprintf(o, "P6\n256 224\n255\n");
  for (int i = 0; i < 256 * 224; i++) { uint16 c = cgram[idx[i]];
    uint8 rgb[3] = { (uint8)((c & 31) << 3), (uint8)((c >> 5 & 31) << 3), (uint8)((c >> 10 & 31) << 3) }; fwrite(rgb, 1, 3, o); }
  fclose(o);
}

/* one frame: the ops, then both pictures (ZH_SPLIT's way: a copy of the context draws the Genesis
 * frame, the game's context the reference, one FrameEnd), composed, compared */
void zh_md_frame(int f, uint8 *ref, uint32 dflags) {
  static uint8 hyb[256 * 224];
  static Ppu other;
  MdHybrid *h = &g_h;
  g_ops_frame = 0, g_sink.total = 0, g_sink.cost = 0;
  if (getenv("ZH_BG3LOG") && f >= atoi(getenv("ZH_BG3LOG")) && f < atoi(getenv("ZH_BG3LOG")) + 12) {
    fprintf(stderr, "frame %d dirty:", f);
    for (int b = 0x400; b < 0x800; b++) if (g_vram_dirty[b >> 3] >> (b & 7) & 1) fprintf(stderr, " %03x", b);
    fprintf(stderr, "\n");
  }
  MdPlanes_Prepare(&g_md, g_zenv.ppu, h, kMd_CommitMax - g_sink.total);
  MdPlanes_Send(&g_md, g_zenv.ppu, h, &g_sink);
  g_frames++;
  ZeldaDrawCtx ctx, ctx2;
  if (h->why) {
    g_why[h->why]++;
    if (getenv("ZH_MATHLOG") && h->why == kMdWhy_Math) {
      Ppu *q = g_zenv.ppu;
      fprintf(stderr, "M mod=%02x sub=%02x in=%d area=%02x room=%02x me=%02x as=%d sb=%d half=%d fix=%02x%02x%02x ms=%02x ss=%02x clip=%d prev=%d c0=%04x bg1=%d bg2=%d\n",
        g_zenv.ram[0x10], g_zenv.ram[0x11], g_zenv.ram[0x1b], g_zenv.ram[0x8a], g_zenv.ram[0xa0], q->mathEnabled, q->addSubscreen, q->subtractColor, q->halfColor,
        q->fixedColorR, q->fixedColorG, q->fixedColorB, q->screenEnabled[0], q->screenEnabled[1], q->clipMode, q->preventMathMode, q->cgram[0], MdPlanes_LayerCells(&g_md, q, 0), MdPlanes_LayerCells(&g_md, q, 1));
    }
    MdPlanes_Show(&g_md, 0, &g_sink);
    g_ops_words += g_ops_frame;
    if (g_sink.cost > g_cost_max) g_cost_max = g_sink.cost;
    if (g_sink.cost > 14000 + 400) g_cost_over++;
    if (g_ops_frame > g_ops_max) g_ops_max = g_ops_frame;
    ZeldaDrawPpuFrame(ref, 256, dflags);
    return;
  }
  ZeldaDrawPpuFrameBegin(&ctx, ref, 256, dflags);
  ZeldaDrawCtxCopy(&ctx2, &other, &ctx);
  /* flipped: the main screen's indices are not the picture (the colour math adds BG1): the
   * reference is the colour picture */
  static uint16 rgb_ref[256 * 224];
  static Ppu third;
  ZeldaDrawCtx ctx3;
  if (h->flip || h->tint) {
    ZeldaDrawCtxCopy(&ctx3, &third, &ctx);
    third.renderFlags = (uint8)(kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Rgb15);
    third.renderBuffer = (uint8 *)rgb_ref, third.renderPitch = 512;
    ZeldaDrawPpuLines(&ctx3, 0, ctx.height, ctx.height);
  }
  other.renderFlags = (uint8)(kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Hybrid);
  other.renderBuffer = hyb, other.renderPitch = 256;
  other.hybrid = h;
  other.hybridFail = 0;
  other.hybridKey[5] = 0;
  other.regsChanged = 3;
  memset(other.lineBuf, 0, 256);
  h->fb = 0;
  ZeldaDrawPpuLines(&ctx2, 0, ctx.height, ctx.height);
  ZeldaDrawPpuLines(&ctx, 0, ctx.height, ctx.height);
  ZeldaDrawPpuFrameEnd(&ctx);
  MdPlanes_Show(&g_md, other.hybridFail ? 0 : (h->layers | (h->flip == 1) << 2), &g_sink);
  g_ops_words += g_ops_frame;
  if (g_sink.cost > g_cost_max) g_cost_max = g_sink.cost;
  if (g_sink.cost > 14000 + 400) g_cost_over++;
  if (g_ops_frame > g_ops_max) g_ops_max = g_ops_frame;
  if (other.hybridFail) { g_why[kMdWhy_LineCheck]++; return; }
  g_hybrid++;
  g_flip += h->flip != 0, g_tint += h->tint != 0;
  uint16 pal[256];
  MdPlanes_Palette32x(h, g_zenv.ppu->cgram, g_zenv.ppu->brightness, pal);
  uint8 back[256];                              /* 32X index -> the CGRAM index it stands for */
  for (int i = 0; i < 256; i++) back[i] = (uint8)i;
  back[0] = 0;
  for (int i = 33; i < 64; i++) back[i] = (uint8)(i - 32);
  for (int p = 0; p < 8; p++) if (h->spr_slot[p] != 0xff) for (int j = 0; j < 16; j++) back[64 + h->spr_slot[p] * 16 + j] = (uint8)(128 + p * 16 + j);
  for (int p = 0; p < 8; p++) if (h->off_base[p]) for (int j = 1; j < 16; j++) back[h->off_base[p] + j] = (uint8)(p * 16 + j);
  uint32 hsA = (g_sim.vram[kMd_HScroll] << 8 | g_sim.vram[kMd_HScroll + 1]) & 0x3ff;
  uint32 hsB = (g_sim.vram[kMd_HScroll + 2] << 8 | g_sim.vram[kMd_HScroll + 3]) & 0x3ff;
  uint32 vsA = g_sim.vsram[0] & 0x3ff, vsB = g_sim.vsram[1] & 0x3ff;
  static uint8 out[256 * 224];
  long diff = 0;
  for (int y = 0; y < 224; y++) {
    for (int x = 0; x < 256; x++) {
      uint8 i32 = hyb[y * 256 + x];
      uint8 v = back[i32];
      int src = i32 == 0 ? 0 : i32 < 64 ? 3 : v >= 128 ? 4 : 1;   /* the layer: backdrop, BG3, BG, sprite */
      if (pal[i32] & 0x8000) {                  /* behind: the Genesis's pixel if it has one */
        uint32 a = sim_plane(&g_sim, (g_sim.reg[2] & 0x38) << 10, hsA, vsA, x + 32, y);
        uint32 b = sim_plane(&g_sim, (g_sim.reg[4] & 7) << 13, hsB, vsB, x + 32, y);
        uint32 top = 0;
        if (b && !(b & 0x200)) top = b;
        if (a && !(a & 0x200)) top = a;
        if (b && (b & 0x200)) top = b;
        if (a && (a & 0x200)) top = a;
        if (top) {
          uint32 p = g_md.pal_of_line[top >> 4 & 3];
          v = p == 0xff ? 0xff : (uint8)(p * 16 + (top & 15));
          src = 1;
        }
      }
      out[y * 256 + x] = v;
      if (h->flip || h->tint) {                 /* the colours (with the brightness) */
        const uint8 *bm = g_zenv.ppu->bright5;
        uint16 c = v == 0xff ? 0xffff : g_zenv.ppu->cgram[v];
        uint32 t = h->tint, tbg = !t ? 0 : (h->layers & 1) ? t & 1 : t >> 1 & 1;
        bool on = src == 0 ? (t & 0x20) != 0 : src == 3 ? (t & 4) != 0 : src == 1 ? tbg != 0 : (v >= 192 && (t & 0x10));
        if (c != 0xffff && t && on) c = MdPlanes_Tint(c, h);
        uint16 cc = c == 0xffff ? 0xffff : (uint16)(bm[c & 31] | bm[c >> 5 & 31] << 5 | bm[c >> 10 & 31] << 10);
        uint16 r = rgb_ref[y * 256 + x] & 0x7fff;
        if (cc != r && diff < 3 && getenv("ZH_MDWHY"))
          fprintf(stderr, "  flip (%d,%d): ref %04x gen %04x (idx %02x 32x %02x)\n", x, y, r, cc, v, i32);
        diff += cc != r;
        continue;
      }
      if (v != ref[y * 256 + x] && diff < 3 && getenv("ZH_MDWHY"))
        fprintf(stderr, "  (%d,%d): ref %02x gen %02x (32x %02x pal %04x) md_pal %02x lines %02x %02x %02x %02x layers %d regs %02x %02x on %d\n", x, y, ref[y * 256 + x], v, i32, pal[i32],
                h->md_pal, g_md.pal_of_line[0], g_md.pal_of_line[1], g_md.pal_of_line[2], g_md.pal_of_line[3], h->layers, g_sim.reg[2], g_sim.reg[4], g_md.plane_on);
      diff += v != ref[y * 256 + x];
    }
  }
  if (diff && g_bad < 3) {                     /* where the Genesis's copy is stale */
    int nt = 0, nc = 0;
    for (uint32 t = 0; t < 512; t++) {
      const uint8 *w = (const uint8 *)&g_zenv.ppu->vram[kMd_TileBase + t * 16];
      for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) {
        int bit = 7 - c;
        uint32 v = (w[r * 2] >> bit & 1) | (w[r * 2 + 1] >> bit & 1) << 1 | (w[r * 2 + 16] >> bit & 1) << 2 | (w[r * 2 + 17] >> bit & 1) << 3;
        uint8 bb = g_sim.vram[t * 32 + r * 4 + c / 2];
        uint32 m = c & 1 ? bb & 15 : bb >> 4;
        if (m != v && nt++ < 5) fprintf(stderr, "  tile %u row %d col %d: snes %u md %u (dirty %d)\n", t, r, c, v, m, (g_vram_dirty[(0x200 + t) >> 3] >> ((0x200 + t) & 7)) & 1);
      }
    }
    for (uint32 o = 0; o < 0x2000; o++) {
      uint32 e = g_zenv.ppu->vram[o];
      uint32 base = o < 0x1000 ? kMd_PlaneB : kMd_PlaneA, oo = o & 0xfff;
      uint32 q = oo >> 10, r = oo >> 5 & 31, c = oo & 31;
      uint32 a = base + ((r + (q >> 1) * 32) * 64 + c + (q & 1) * 32) * 2;
      uint32 cell = g_sim.vram[a] << 8 | g_sim.vram[a + 1];
      if (g_md.line_of_pal[e >> 10 & 7] != 0xff && (cell & 0x3ff) != (e & 0x3ff) && nc++ < 5) fprintf(stderr, "  map %04x: snes %04x md %04x\n", o, e, cell);
    }
    fprintf(stderr, "  frame %d: %d tile pixels, %d cells stale\n", f, nt, nc);
  }
  if (diff) {
    g_bad++;
    if (g_bad <= 20) fprintf(stderr, "frame %d: %ld pixels differ\n", f, diff);
    if (getenv("ZH_MDBAD") && g_bad_dumps < 6) {
      char fn[512];
      snprintf(fn, sizeof fn, "%s/md%06d_ref.ppm", getenv("ZH_MDBAD"), f); dump(fn, ref, g_zenv.ppu->cgram);
      snprintf(fn, sizeof fn, "%s/md%06d_gen.ppm", getenv("ZH_MDBAD"), f); dump(fn, out, g_zenv.ppu->cgram);
      g_bad_dumps++;
    }
  }
}

extern long g_hyb_count[4];
void zh_md_report(void) {
  fprintf(stderr, "md lines: %ld drawn, %ld skipped as before, %ld with BG3, %ld with sprites\n",
          g_hyb_count[0], g_hyb_count[1], g_hyb_count[2], g_hyb_count[3]);
  static const char *kWhy[] = { "shown", "blank", "mode", "layout", "mosaic", "window", "math", "palettes>4",
                                "tiles>511", "sprite palettes", "not synced", "line check" };
  fprintf(stderr, "md: %ld frames, %ld on the Genesis planes (%.1f%%), %ld differ; ops %.1f words/frame, max %ld\n",
          g_frames, g_hybrid, g_frames ? 100.0 * g_hybrid / g_frames : 0.0, g_bad, g_frames ? (double)g_ops_words / g_frames : 0.0, g_ops_max);
  fprintf(stderr, "  (tinted: %ld; flipped: %ld of the frames shown; 68000 cycles a commit: max %ld, %ld over)\n", g_tint, g_flip, g_cost_max, g_cost_over);
  for (int i = 1; i < kMdWhy_Count; i++) if (g_why[i]) fprintf(stderr, "  %-16s %ld\n", kWhy[i], g_why[i]);
}

/* ZH_VRAMCHECK: a VRAM block that changed without its dirty bit */
void zh_vram_check(int f) {
  static uint8 copy[0x10000];
  static bool have;
  static long missed;
  const uint8 *v = (const uint8 *)g_zenv.ppu->vram;
  if (have) {
    for (int b = 0; b < 0x800; b++) {
      if (memcmp(copy + b * 32, v + b * 32, 32) && !((g_vram_dirty[b >> 3] >> (b & 7)) & 1)) {
        if (missed++ < 20) {
          int k = 0; while (copy[b * 32 + k] == v[b * 32 + k]) k++;
          fprintf(stderr, "frame %d: VRAM words %04x-%04x changed, not marked (byte %d: %02x -> %02x)\n", f, b * 16, b * 16 + 15, k, copy[b * 32 + k], v[b * 32 + k]);
        }
      } else if (b < 0x200 && (g_vram_fine[b * 2] | g_vram_fine[b * 2 + 1])) {   /* a map block: its words */
        for (int w = b * 16; w < b * 16 + 16; w++)
          if (memcmp(copy + w * 2, v + w * 2, 2) && !((g_vram_fine[w >> 3] >> (w & 7)) & 1) && missed++ < 20)
            fprintf(stderr, "frame %d: VRAM word %04x changed, its bit not marked\n", f, w);
      }
    }
  }
  memcpy(copy, v, sizeof copy);
  have = true;
}
