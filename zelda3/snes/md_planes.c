// Z32X "rip and tear": BG1 / BG2 on the Genesis planes - see md_planes.h.
#include <string.h>
#include "md_planes.h"
#include "src/types.h"

extern uint8 g_vram_dirty[0x8000 / 16 / 8];   // src/zelda_rtl.c
#if defined(Z32X) && defined(Z32_MD_PROF)        // test: FRT ticks per stage (g_md_prof)
extern "C" uint16 z32_frt(void);
uint32 g_md_prof[8];
#define MD_T(k) do { uint16 t_ = z32_frt(); g_md_prof[k] += (uint16)(t_ - pt_); pt_ = t_; } while (0)
#define MD_T0 uint16 pt_ = z32_frt()
#else
#define MD_T(k) do { } while (0)
#define MD_T0 do { } while (0)
#endif

// the SNES regions the Genesis copies, in 16-word blocks
enum {
  kBlk_MapB = 0x000,           // SNES words 0x0000-0x0FFF: BG2's 64x64 map -> plane B
  kBlk_MapA = 0x100,           // 0x1000-0x1FFF: BG1's map -> plane A
  kBlk_Tiles = 0x200,          // 0x2000-0x3FFF: tiles 0-511 -> Genesis tiles 0-511
  kBlk_End = 0x400,
};

// (the SH2 has no shift by a variable amount - GCC calls a routine: bits come from a table)
static const uint8 kBit8[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
static inline bool BlockDirty(uint32 b) { return (g_vram_dirty[b >> 3] & kBit8[b & 7]) != 0; }
static inline void BlockClean(uint32 b) { g_vram_dirty[b >> 3] &= (uint8)~kBit8[b & 7]; }
static inline void BlockMark(uint32 b) { g_vram_dirty[b >> 3] |= kBit8[b & 7]; }
static inline bool Bit(const uint8 *a, uint32 i) { return (a[i >> 3] & kBit8[i & 7]) != 0; }

void MdPlanes_Reset(MdPlanes *m) {
  memset(m, 0, sizeof(*m));
  for (int p = 0; p < 8; p++) m->line_of_pal[p] = 0xff;
  for (int l = 0; l < 4; l++) m->pal_of_line[l] = 0xff;
  for (int b = 0; b < kBlk_End; b++) BlockMark(b);   // send it all
  m->bg3_map = m->bg3_tiles = 0xffff;
  m->gen = 1;
}

// ---- BG3: empty tiles (2 bits a pixel, 8 words), rows of the map that hold a non-empty one ----
static inline uint32 Word(const uint8 *v, uint32 w) { w &= 0x7fff; return v[w * 2] | v[w * 2 + 1] << 8; }
static bool Bg3Row(MdPlanes *m, const uint8 *v, uint32 map, uint32 r, uint32 cols, uint32 rows) {
  // map row r: 32-cell halves side by side (0x400 words a 32x32 screen), the lower screens after
  uint32 base = map + (r & 31) * 32 + (r >= 32 ? (cols == 64 ? 0x800 : 0x400) : 0);
  uint32 mask[2] = { 0, 0 };
  for (uint32 half = 0; half < (cols >> 5); half++) {
    uint32 bit = 1, mk = 0;
    for (uint32 c = 0; c < 32; c++, bit <<= 1) {
      uint32 t = Word(v, base + c + half * 0x400) & 0x3ff;
      if (t >= 512 || !Bit(m->bg3_empty, t)) mk |= bit;
    }
    mask[half] = mk;
  }
  (void)rows;
  m->bg3_cols[r][0] = mask[0], m->bg3_cols[r][1] = mask[1];
  return mask[0] || mask[1];
}
void MdPlanes_Bg3Rows(MdPlanes *m, Ppu *ppu, MdHybrid *h) {
  const BgLayer *bg = &ppu->bgLayer[2];
  const uint8 *v = (const uint8 *)ppu->vram;
  uint32 tiles = bg->tileAdr & 0x7fff, map = bg->tilemapAdr & 0x7fff;
  uint32 size = bg->tilemapWider | bg->tilemapHigher << 1;
  bool all = tiles != m->bg3_tiles || map != m->bg3_map || size != m->bg3_size;
  m->bg3_tiles = (uint16)tiles, m->bg3_map = (uint16)map, m->bg3_size = (uint8)size;
  uint32 ntiles = tiles + 512 * 8 <= 0x8000 ? 512 : (0x8000 - tiles) / 8;
  bool flipped = all, tile_changed = all;
  for (uint32 b = tiles >> 4, e = (tiles + ntiles * 8) >> 4; b < e; b++) {
    if (!all && !g_vram_dirty[b >> 3]) { b |= 7; continue; }
    if (!all && !BlockDirty(b)) continue;
    tile_changed = true;                        // (a digit of the status bar...: every row's lines)
    if (b >= kBlk_End) BlockClean(b);           // (BG3 over the Genesis's regions: theirs to clean)
    for (uint32 t = ((b << 4) - tiles) >> 3, k = 0; k < 2 && t < ntiles; k++, t++) {
      const uint8 *w = v + (tiles + t * 8) * 2;
      uint32 any = 0;
      for (int i = 0; i < 16; i++) any |= w[i];
      // empty -> not: the rows that hold it must look again (all of them). Not -> empty is not
      // followed (until the BG3 setup changes): those cells are just painted with nothing - a
      // tile that blinks would make every row be looked at every frame
      uint8 bit = kBit8[t & 7];
      if (all) {
        if (any) m->bg3_empty[t >> 3] &= (uint8)~bit; else m->bg3_empty[t >> 3] |= bit;
      } else if (any && (m->bg3_empty[t >> 3] & bit)) {
        m->bg3_empty[t >> 3] &= (uint8)~bit;
        flipped = true;
      }
    }
  }
  if (all) for (uint32 t = ntiles; t < 512; t++) m->bg3_empty[t >> 3] &= (uint8)~kBit8[t & 7];
#if defined(Z32X) && defined(Z32_MD_PROF)
  uint16 pq_ = z32_frt();
#endif
  uint32 rows = bg->tilemapHigher ? 64 : 32, cols = bg->tilemapWider ? 64 : 32;
  if (flipped) memset(m->bg3_rows, 0, sizeof m->bg3_rows), memset(m->bg3_cols, 0, sizeof m->bg3_cols);
  for (uint32 r = 0; r < rows; r++) {
    // the row's two (or four) 16-cell blocks
    uint32 base = map + (r & 31) * 32 + (r >= 32 ? (cols == 64 ? 0x800 : 0x400) : 0);
    bool dirty = flipped;
    for (uint32 half = 0; half < (cols == 64 ? 4u : 2u); half++) {
      uint32 k = ((base + (half & 1) * 16 + (half >> 1) * 0x400) >> 4) & 0x7ff;
      if (BlockDirty(k)) {
        dirty = true;
        if (k >= kBlk_End) BlockClean(k);
      }
    }
    if (tile_changed) m->bg3_row_gen[r]++;
    if (!dirty) continue;
    uint32 hh = 2166136261u;                    // written again the same: nothing changed
    for (uint32 c = 0; c < cols; c++) hh = (hh ^ Word(v, base + (c & 31) + (c >= 32 ? 0x400 : 0))) * 16777619u;
    if (!flipped && hh == m->bg3_row_hash[r]) continue;
    m->bg3_row_hash[r] = hh;
    if (!tile_changed) m->bg3_row_gen[r]++;
    uint8 bit = kBit8[r & 7];
    if (Bg3Row(m, v, map, r, cols, rows)) m->bg3_rows[r >> 3] |= bit; else m->bg3_rows[r >> 3] &= (uint8)~bit;
  }
  h->bg3_rows = m->bg3_rows, h->bg3_cols = m->bg3_cols, h->bg3_row_gen = m->bg3_row_gen;
#if defined(Z32X) && defined(Z32_MD_PROF)
  g_md_prof[7] += (uint16)(z32_frt() - pq_);
#endif
}

uint16 MdPlanes_Colour(uint16 c, uint8 brightness) {
  uint32 r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
  r = r * brightness / 15, g = g * brightness / 15, b = b * brightness / 15;
  r = (r * 7 + 15) / 31, g = (g * 7 + 15) / 31, b = (b * 7 + 15) / 31;   // 5 bits -> 3, rounded
  return (uint16)(b << 9 | g << 5 | r << 1);
}

// ---- ops into the sink; a write that continues the last one (the next address) joins it ----
typedef struct Ops {
  MdSink *s;
  bool full;                   // the commit is full: what is left waits for the next frame
  int last;                    // the last op's header in s->buf (-1: none since the flush)
  uint32 last_end;             // the Genesis address after it
} Ops;
static inline uint32 VramWrite(uint32 a) { return 0x40000000 | (a & 0x3fff) << 16 | a >> 14; }
static inline uint32 CramWrite(uint32 a) { return 0xC0000000 | (a & 0x7f) << 16; }
static inline uint32 VsramWrite(uint32 a) { return 0x40000010 | (a & 0x7f) << 16; }
static void Flush(Ops *o) {
  if (o->s->n) o->s->flush(o->s);
  o->s->n = 0;
  o->last = -1;
}
// room for n data words of a VRAM write at Genesis address a (or another op, a = ~0), or NULL
static uint16 *OpBegin(Ops *o, uint32 ctrl, uint32 a, uint32 n) {
  MdSink *s = o->s;
  if (o->full || s->total + 3 + (int)n > kMd_CommitMax) { o->full = true; return NULL; }
  if (a != ~0u && o->last >= 0 && o->last_end == a && s->n + (int)n <= (int)countof(s->buf)) {
    s->buf[o->last + 2] += (uint16)n;          // continues the last write
    uint16 *d = s->buf + s->n;
    s->n += n, s->total += n, o->last_end += n * 2;
    return d;
  }
  if (s->n + 3 + (int)n > (int)countof(s->buf)) Flush(o);
  uint16 *d = s->buf + s->n;
  d[0] = (uint16)(ctrl >> 16), d[1] = (uint16)ctrl, d[2] = (uint16)n;
  o->last = a != ~0u ? s->n : -1, o->last_end = a + n * 2;
  s->n += 3 + n, s->total += 3 + n;
  return d + 3;
}
// take back the n words just begun (they were the same as what the Genesis has)
static void OpCancel(Ops *o, uint16 *d, uint32 n) {
  MdSink *s = o->s;
  if (o->last >= 0 && d == s->buf + o->last + 3) {   // it was an op of its own
    s->n -= 3 + n, s->total -= 3 + n, o->last = -1;
  } else {                                           // it joined the last one
    s->buf[o->last + 2] -= (uint16)n, s->n -= n, s->total -= n, o->last_end -= n * 2;
  }
}
static void OpReg(Ops *o, uint32 reg, uint32 val) {
  OpBegin(o, (uint32)(0x8000 | reg << 8 | val) << 16, ~0u, 0);
}

static uint32 Hash(const uint16 *d, int n) {
  uint32 h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
  return h ? h : 1;
}

// a SNES tile row (planes 0-3 at w[0], w[1], w[16], w[17]) as a Genesis row: pixel 0 in the top
// nibble - ppu.c's H-flip table (bit i -> nibble i) does it (ppu_init made it)
extern uint32 g_ppuExpand[2][256];
#define g_md_expand g_ppuExpand[1]
static inline void InitExpand(void) { }
static void ConvertTile(MdPlanes *m, Ppu *ppu, uint32 t, uint16 *d) {
  const uint8 *w = (const uint8 *)&ppu->vram[kMd_TileBase + t * 16];
  uint32 any = 0;
  for (int r = 0; r < 8; r++) {
    const uint8 *b = w + r * 2;
    uint32 row = g_md_expand[b[0]] | g_md_expand[b[1]] << 1 | g_md_expand[b[16]] << 2 | g_md_expand[b[17]] << 3;
    d[r * 2] = (uint16)(row >> 16), d[r * 2 + 1] = (uint16)row;
    any |= row;
  }
  uint8 bit = kBit8[t & 7], old = m->tile_empty[t >> 3] & bit;
  if (any) m->tile_empty[t >> 3] &= (uint8)~bit;
  else m->tile_empty[t >> 3] |= bit;
  if ((m->tile_empty[t >> 3] & bit) != old) m->gen++;   // the cells the screen shows may change
}

// a SNES map word as a plane cell: its palette's line, or the blank tile (the 32X draws it)
static inline uint16 MdCell(const MdPlanes *m, uint32 e) {
  uint32 l = m->line_of_pal[e >> 10 & 7];
  if (l == 0xff) return kMd_BlankTile;
  return (uint16)((e >> 13 & 1) << 15 | l << 13 | (e >> 15 & 1) << 12 | (e >> 14 & 1) << 11 | (e & 0x3ff));
}
// SNES map word offset (0..0xfff in a 64x64 map) -> the plane cell's byte offset
static inline uint32 MdCellOffset(uint32 o) {
  uint32 q = o >> 10, r = o >> 5 & 31, c = o & 31;
  return ((r + (q >> 1) * 32) * 64 + c + (q & 1) * 32) * 2;
}

// the dirty blocks of the copied regions as ops, what changed (as many as fit; the rest stay dirty)
static void SendDirty(MdPlanes *m, Ppu *ppu, Ops *o) {
  for (uint32 b = kBlk_MapB; b < kBlk_End && !o->full; b++) {
    if (!g_vram_dirty[b >> 3]) { b |= 7; continue; }   // a clean byte: its 8 blocks
    if (!BlockDirty(b)) continue;
    if (b >= kBlk_Tiles) {
      uint32 t = b - kBlk_Tiles;
      uint16 *d = OpBegin(o, VramWrite(t * 32), t * 32, 16);
      if (!d) break;
      ConvertTile(m, ppu, t, d);
      uint32 h = Hash(d, 16);
      if (h == m->tile_hash[t]) OpCancel(o, d, 16);
      else m->tile_hash[t] = h;
    } else {
      uint32 base = b < kBlk_MapA ? 0 : 0x1000, plane = b < kBlk_MapA ? kMd_PlaneB : kMd_PlaneA;
      uint32 o0 = (b - (base >> 4)) * 16;      // word offset in the map: 16 cells of one row
      uint32 a = plane + MdCellOffset(o0);
      uint16 *d = OpBegin(o, VramWrite(a), a, 16);
      if (!d) break;
      const uint8 *v = (const uint8 *)ppu->vram + (base + o0) * 2;
      for (uint32 k = 0; k < 16; k++) d[k] = MdCell(m, v[k * 2] | v[k * 2 + 1] << 8);
    }
    BlockClean(b);
  }
}

// before the frame is drawn, without sending: the emptiness of the tiles written since (the
// screen's palettes depend on it), gen bumped for a map written; the words the sending will take
static uint32 Peek(MdPlanes *m, Ppu *ppu, bool look) {
  uint32 n = 0;
  for (uint32 b = kBlk_MapB; b < kBlk_End; b++) {
    if (!g_vram_dirty[b >> 3]) { b |= 7; continue; }
    if (!BlockDirty(b)) continue;
    n += 19;
    if (!look) continue;
    if (b < kBlk_Tiles) { m->gen++; continue; }
    uint32 t = b - kBlk_Tiles;
    const uint32 *w = (const uint32 *)&ppu->vram[kMd_TileBase + t * 16];
    uint32 any = 0;
    for (int i = 0; i < 8; i++) any |= w[i];
    uint8 bit = kBit8[t & 7], old = m->tile_empty[t >> 3] & bit;
    if (any) m->tile_empty[t >> 3] &= (uint8)~bit; else m->tile_empty[t >> 3] |= bit;
    if ((m->tile_empty[t >> 3] & bit) != old) m->gen++;
  }
  return n;
}

// mark the map blocks that hold a cell of palette p (its Genesis line changed)
static void MarkPalette(Ppu *ppu, uint32 p) {
  const uint8 *v = (const uint8 *)ppu->vram + 1;          // the words' high bytes: vhopppcc
  for (uint32 b = kBlk_MapB; b < kBlk_Tiles; b++, v += 32) {
    for (int k = 0; k < 32; k += 2)
      if (((v[k] >> 2) & 7) == p) { BlockMark(b); break; }
  }
}

// the cells a layer shows: per palette, how many (non-empty tiles); -1: a tile past 511 shows
static bool CountPalettes(MdPlanes *m, Ppu *ppu, int layer, uint32 *count) {
  const BgLayer *bg = &ppu->bgLayer[layer];
  uint32 base = layer == 0 ? 0x1000 : 0x0000;
  uint32 y0 = (bg->vScroll + 1) & 0x1ff, x0 = bg->hScroll & 0x1ff;
  uint32 nrows = ((y0 & 7) + 224 + 7) >> 3, ncols = ((x0 & 7) + 256 + 7) >> 3;
  const uint8 *v = (const uint8 *)ppu->vram;
  for (uint32 ry = 0; ry < nrows; ry++) {
    uint32 cy = ((y0 >> 3) + ry) & 63;
    const uint8 *row = v + (base + (cy >> 5) * 0x800 + (cy & 31) * 32) * 2;
    for (uint32 rx = 0, cx = x0 >> 3; rx < ncols; rx++, cx = (cx + 1) & 63) {
      const uint8 *c = row + ((cx >> 5) * 0x400 + (cx & 31)) * 2;
      uint32 t = c[0] | (c[1] & 3) << 8;
      if (t >= 512) return false;
      if (!Bit(m->tile_empty, t))
        count[c[1] >> 2 & 7]++;
    }
  }
  return true;
}

// the cells of palettes off the Genesis a layer shows, by map row (MdHybrid), or false: too many
static bool ListOffCells(MdPlanes *m, Ppu *ppu, int layer, MdHybrid *h) {
  const BgLayer *bg = &ppu->bgLayer[layer];
  uint32 base = layer == 0 ? 0x1000 : 0x0000;
  uint32 y0 = (bg->vScroll + 1) & 0x1ff, x0 = bg->hScroll & 0x1ff;
  uint32 nrows = ((y0 & 7) + 224 + 7) >> 3, ncols = ((x0 & 7) + 256 + 7) >> 3;
  int li = layer;
  h->row0[li] = (uint8)((y0 >> 3) & 63), h->nrows[li] = (uint8)nrows;
  uint32 n = 0;
  const uint8 *v = (const uint8 *)ppu->vram;
  for (uint32 ry = 0; ry < nrows; ry++) {
    h->start[li][ry] = (uint16)n;
    uint32 cy = ((y0 >> 3) + ry) & 63;
    const uint8 *row = v + (base + (cy >> 5) * 0x800 + (cy & 31) * 32) * 2;
    for (uint32 rx = 0, cx = x0 >> 3; rx < ncols; rx++, cx = (cx + 1) & 63) {
      const uint8 *c = row + ((cx >> 5) * 0x400 + (cx & 31)) * 2;
      uint32 e = c[0] | c[1] << 8;
      uint32 t = e & 0x3ff;
      if ((h->md_pal & kBit8[e >> 10 & 7]) || Bit(m->tile_empty, t))
        continue;
      if (n == kMd_OffMax) return false;
      h->cx[li][n] = (uint8)cx, h->cell[li][n] = (uint16)e, n++;
    }
  }
  h->start[li][nrows] = (uint16)n;
  return true;
}

// the colour math / colour window can change no pixel of the frame (as set at its start; the
// line painter checks every line - kMdWhy_LineCheck)
static bool MathMayShow(Ppu *ppu) {
  if (ppu->clipMode == 3) return true;                  // the main screen is always clipped
  if (ppu->preventMathMode == 3 || !ppu->mathEnabled) return false;
  uint32 fixed = ppu->fixedColorR | ppu->fixedColorG << 5 | ppu->fixedColorB << 10;
  return fixed != 0 || ppu->halfColor || (ppu->addSubscreen && ppu->screenEnabled[1] != 0);
}

void MdPlanes_Prepare(MdPlanes *m, Ppu *ppu, MdHybrid *h, int room) {
  MD_T0;
  h->why = 0, h->md_pal = 0;
  memset(h->off_base, 0, sizeof h->off_base);
  memset(h->spr_slot, 0xff, sizeof h->spr_slot);
  uint32 words = Peek(m, ppu, true);
  MD_T(0);
  // the decision
  uint8 why = 0;
  const BgLayer *b1 = &ppu->bgLayer[0], *b2 = &ppu->bgLayer[1];
  uint32 layers = ppu->screenEnabled[0] & 3;
  h->layers = (uint8)layers;
  if (ppu->forcedBlank || ppu->brightness == 0) why = kMdWhy_Blank;
  else if (ppu->mode != 1) why = kMdWhy_Mode;
  else if (b1->tilemapAdr != 0x1000 || b2->tilemapAdr != 0x0000 || !b1->tilemapWider || !b1->tilemapHigher ||
           !b2->tilemapWider || !b2->tilemapHigher || b1->tileAdr != kMd_TileBase || b2->tileAdr != kMd_TileBase)
    why = kMdWhy_Layout;
  else if (ppu->mosaicEnabled & 7) why = kMdWhy_Mosaic;   // (BG3 too: the 32X draws it unmosaicked)
  else if ((ppu->screenWindowed[0] & 3) && (ppu->windowsel & 0xaa)) why = kMdWhy_Window;
  else if (MathMayShow(ppu)) why = kMdWhy_Math;
  else if ((int)words + 160 > room) why = kMdWhy_NotSynced;   // (+ the colours, scroll, planes)
  uint32 count[8] = { 0 };
  uint16 key[5] = { b1->hScroll, b1->vScroll, b2->hScroll, b2->vScroll, (uint16)layers };
  if (!why) {
    if (m->cvalid && m->cgen == m->gen && !memcmp(key, m->ckey, sizeof key)) {
      memcpy(count, m->ccount, sizeof count);  // the screen's cells as last time
      if (m->chigh) why = kMdWhy_HighTiles;
    } else {
      bool high = false;
      for (int l = 0; l < 2 && !high; l++)
        if ((layers >> l & 1) && !CountPalettes(m, ppu, l, count)) high = true;
      memcpy(m->ccount, count, sizeof count), memcpy(m->ckey, key, sizeof key);
      m->cgen = m->gen, m->cvalid = 1, m->chigh = high;
      if (high) why = kMdWhy_HighTiles;
    }
  }
  MD_T(1);
  int groups = 0;
  if (!why) {                                  // the sprites behind the planes: their palettes
    for (int i = 0; i < ppu->objCount; i++) {
      uint32 k = ppu->objList[i];
      uint32 attr = (uint32)ppu->oam[k * 2 + 1] >> 8;
      if ((attr >> 4 & 3) >= 2) continue;
      uint32 p = attr >> 1 & 7;
      if (h->spr_slot[p] == 0xff) {
        if (groups == 4) { why = kMdWhy_SpritePalettes; break; }
        h->spr_slot[p] = (uint8)groups++;
      }
    }
  }
  if (!why) {
    // the Genesis's 4 lines: the palettes with the most cells (a palette that has its line
    // wins a tie - no resend)
    uint32 md = 0;
    for (int k = 0; k < 4; k++) {
      int best = -1;
      for (int p = 0; p < 8; p++) {
        if ((md >> p & 1) || !count[p]) continue;
        bool owns = m->line_of_pal[p] != 0xff && m->pal_of_line[m->line_of_pal[p]] == p;
        bool best_owns = best >= 0 && m->line_of_pal[best] != 0xff && m->pal_of_line[m->line_of_pal[best]] == best;
        if (best < 0 || count[p] > count[best] || (count[p] == count[best] && owns && !best_owns)) best = p;
      }
      if (best < 0) break;
      md |= 1 << best;
    }
    for (int p = 0; p < 8; p++) {
      if (!(md >> p & 1)) continue;
      uint32 l = m->line_of_pal[p];
      if (l != 0xff && m->pal_of_line[l] == p) continue;    // it has its line
      int free_l = -1;
      for (int k = 0; k < 4 && free_l < 0; k++)
        if (m->pal_of_line[k] == 0xff || !(md >> m->pal_of_line[k] & 1)) free_l = k;
      if (l != 0xff && (m->pal_of_line[l] == 0xff || !(md >> m->pal_of_line[l] & 1))) free_l = (int)l;   // its old line: no resend
      m->pal_of_line[free_l] = (uint8)p;
      if (l != (uint32)free_l) {
        m->line_of_pal[p] = (uint8)free_l;
        MarkPalette(ppu, p);                   // its cells name the new line
      }
    }
    for (int p = 0; p < 8; p++) {              // shown, but off the Genesis: blank cells there
      if (!count[p] || (md >> p & 1)) continue;
      uint32 l = m->line_of_pal[p];
      if (l != 0xff) {
        if (m->pal_of_line[l] == p) m->pal_of_line[l] = 0xff;
        m->line_of_pal[p] = 0xff;
        MarkPalette(ppu, p);
      }
      if (groups == 4) { why = kMdWhy_Palettes; break; }
      h->off_base[p] = (uint8)(64 + 16 * groups++);
    }
    h->md_pal = (uint8)md;
  }
  MD_T(2);
  if (!why && (int)Peek(m, ppu, false) + 160 > room)   // with the cells that changed line
    why = kMdWhy_NotSynced;
  MD_T(3);
  uint32 used = 0;
  for (int p = 0; p < 8; p++) used |= (count[p] != 0) << p;
  if (!why && (used & ~(uint32)h->md_pal)) {     // cells the 32X draws: their lists
    if (m->lvalid && m->lgen == m->gen && m->lmd_pal == h->md_pal && !memcmp(key, m->lkey, sizeof key)) {
      if (!m->lok) why = kMdWhy_Palettes;      // as last time
    } else {
      h->nrows[0] = h->nrows[1] = 0;
      bool ok = true;
      for (int l = 0; l < 2 && ok; l++)
        if ((layers >> l & 1) && !ListOffCells(m, ppu, l, h)) ok = false;
      memcpy(m->lkey, key, sizeof key), m->lgen = m->gen, m->lmd_pal = h->md_pal, m->lvalid = 1, m->lok = ok;
      if (!ok) why = kMdWhy_Palettes;
    }
  } else {
    h->nrows[0] = h->nrows[1] = 0, m->lvalid = 0;
  }
  MD_T(4);
  if (!why) {
    MdPlanes_Bg3Rows(m, ppu, h);
    MD_T(6);
    h->sig_hs[0] = b1->hScroll, h->sig_hs[1] = b2->hScroll;
    h->sig_vs[0] = b1->vScroll, h->sig_vs[1] = b2->vScroll;
  }
  h->why = why;
}

void MdPlanes_Send(MdPlanes *m, Ppu *ppu, MdHybrid *h, MdSink *sink) {
  MD_T0;
  InitExpand();
  Ops o = { sink, false, -1, 0 };
  SendDirty(m, ppu, &o);
  const BgLayer *b1 = &ppu->bgLayer[0], *b2 = &ppu->bgLayer[1];
  if (!h->why && !o.full) {
    // the colours of the 4 lines (entry 0 of line 0: the backdrop)
    uint16 src[64], cram[64];                  // the SNES colours they come from: as last time?
    for (int l = 0; l < 4; l++) {
      uint32 p = m->pal_of_line[l];
      src[l * 16] = l == 0 ? ppu->cgram[0] : 0;
      for (int j = 1; j < 16; j++) src[l * 16 + j] = p == 0xff ? 0 : ppu->cgram[p * 16 + j];
    }
    bool same = m->cram_valid && m->cram_bright == ppu->brightness && !memcmp(src, m->cram_src, sizeof src);
    if (!same) {
      for (int i = 0; i < 64; i++) cram[i] = (i & 15) || i == 0 ? MdPlanes_Colour(src[i], ppu->brightness) : 0;
      memcpy(m->cram_src, src, sizeof src), m->cram_bright = ppu->brightness;
    }
    if (!same && (!m->cram_valid || memcmp(cram, m->cram, sizeof cram))) {
      uint16 *d = OpBegin(&o, CramWrite(0), ~0u, 64);
      if (d) { memcpy(d, cram, sizeof cram); memcpy(m->cram, cram, sizeof cram); m->cram_valid = 1; }
    }
    // scroll: the SNES's line 1 shows map row vScroll + 1; the picture is 32 pixels in
    uint16 vs[2] = { (uint16)((b1->vScroll + 1) & 0x3ff), (uint16)((b2->vScroll + 1) & 0x3ff) };
    uint16 hs[2] = { (uint16)((32 - b1->hScroll) & 0x3ff), (uint16)((32 - b2->hScroll) & 0x3ff) };
    if (!m->scroll_valid || memcmp(vs, m->vsram, 4) || memcmp(hs, m->hscroll, 4)) {
      uint16 *d = OpBegin(&o, VsramWrite(0), ~0u, 2);
      if (d) { d[0] = vs[0], d[1] = vs[1]; }
      d = OpBegin(&o, VramWrite(kMd_HScroll), ~0u, 2);
      if (d) { d[0] = hs[0], d[1] = hs[1]; }
      memcpy(m->vsram, vs, 4), memcpy(m->hscroll, hs, 4), m->scroll_valid = 1;
    }
  }
  if (o.full) {                                // more than Prepare reckoned: not shown so
    m->cram_valid = m->scroll_valid = 0;
    if (!h->why) h->why = kMdWhy_NotSynced;
  }
  MD_T(5);
}

void MdPlanes_Show(MdPlanes *m, uint32 layers, MdSink *sink) {
  Ops o = { sink, false, -1, 0 };
  if (layers != m->plane_on) {                 // always room: the commit limit leaves it
    sink->total -= 6;
    OpReg(&o, 2, (layers & 1 ? kMd_PlaneA : kMd_PlaneOff) >> 10);
    OpReg(&o, 4, (layers & 2 ? kMd_PlaneB : kMd_PlaneOff) >> 13);
    sink->total += 6;
    m->plane_on = (uint8)layers;
  }
  Flush(&o);
}

static inline uint16 Front(uint16 c, const uint8 *m) {
  uint16 v = (uint16)(m[c & 31] | m[c >> 5 & 31] << 5 | m[c >> 10 & 31] << 10);
  return v ? v : 0x0400;                       // never 0x0000 in front (see sh2/z32_main.cpp)
}
void MdPlanes_Palette32x(const MdHybrid *h, const uint16 *cgram, uint8 brightness, uint16 *out) {
  uint8 lv[32];
  for (int i = 0; i < 32; i++) lv[i] = (uint8)(i * brightness / 15);
  out[0] = (uint16)(Front(cgram[0], lv) | 0x8000);     // behind: the Genesis shows where it has a pixel
  for (int i = 1; i < 32; i++) out[i] = Front(cgram[i], lv), out[32 + i] = (uint16)(out[i] | 0x8000);
  out[32] = 0x0400;                                     // the side borders: black, in front
  for (int i = 64; i < 128; i++) out[i] = 0x8400;
  for (int p = 0; p < 8; p++) {
    if (h->spr_slot[p] != 0xff)                         // sprites 0 / 1: behind the planes
      for (int j = 0; j < 16; j++) out[64 + h->spr_slot[p] * 16 + j] = (uint16)(Front(cgram[128 + p * 16 + j], lv) | 0x8000);
    if (h->off_base[p])                                 // BG1 / BG2 cells the 32X draws: in front
      for (int j = 1; j < 16; j++) out[h->off_base[p] + j] = Front(cgram[p * 16 + j], lv);
  }
  for (int i = 128; i < 256; i++) out[i] = Front(cgram[i], lv);
}
