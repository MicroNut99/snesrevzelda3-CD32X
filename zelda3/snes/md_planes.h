// Z32X "rip and tear": the SNES's BG1 and BG2 on the Genesis video chip's two tile planes (the way
// Sonic CD and SuperTux REV draw their levels). The Genesis scrolls them in hardware; the 32X draws
// only the sprites and BG3 (the status bar, text) over them - a 32X pixel is either in front of
// the Genesis picture or behind it (the CRAM's through bit), and the painter in ppu.c makes each
// one the right way (kPpuRenderFlags_Hybrid).
//
// MdPlanes keeps a copy of BG1/BG2 in the Genesis's video RAM in step with the SNES VRAM: once per
// drawn frame (MdPlanes_Prepare, MdPlanes_Send) it turns what the game wrote since (g_vram_dirty) into Genesis
// tiles (4 bits a pixel, packed) and plane cells, and emits "ops" - writes to the Genesis video
// chip - that the 68000 applies in the V-blank the 32X shows the frame in (cart/cart_md.s).
// Tiles go only when they really changed (a hash of each as last sent: the animated tiles are
// rewritten every frame); cells go when the game wrote them.
//
// Palettes: the SNES has 8 background palettes, the Genesis 4 colour lines. The 4 palettes with
// the most cells on screen get the lines; cells of the others (usually a few dozen) show the
// blank tile on the Genesis and are drawn by the 32X instead, in their own colours (MdHybrid).
//
// An op: [control long: 2 words] [n] [n data words]; n = 0: the control's high word is a VDP
// register write (0x8000 | reg << 8 | value).
//
// Genesis video RAM (byte addresses):
//   0x0000-0x3FFF  tiles 0-511 = SNES words 0x2000-0x3FFF (the background tiles: BG12NBA 0x2000)
//   0x8000         plane A (BG1), 64 x 64 cells      0xA000  plane B (BG2), 64 x 64
//   0xC000         H scroll table                    0xC400  sprite table (empty)
//   0xC800         a blank tile (0x640)              0xD000  window (off)
//   0xE000         an empty plane (every cell the blank tile): a layer off on the SNES main
//                  screen points its plane here
#ifndef ZELDA3_SNES_MD_PLANES_H_
#define ZELDA3_SNES_MD_PLANES_H_
#include "ppu.h"

enum {
  kMd_TileBase = 0x2000,       // the SNES word address of Genesis tile 0
  kMd_PlaneA = 0x8000, kMd_PlaneB = 0xA000, kMd_HScroll = 0xC000, kMd_Sprites = 0xC400, kMd_Window = 0xD000,
  kMd_PlaneOff = 0xE000, kMd_BlankTile = 0x640,
  kMd_CommitMax = 0x2600,      // words of ops one commit may hold (the 68000's queue: 0x2800)
  kMd_OffMax = 192,            // BG1 / BG2 cells a frame the 32X draws (palette off the Genesis), per
                               // layer (the 13 recordings: at most 163, 99% of frames 75 or fewer)
};

// Why a frame is not shown with the Genesis planes (MdHybrid.why); 0: it is
enum {
  kMdWhy_None = 0, kMdWhy_Blank, kMdWhy_Mode, kMdWhy_Layout, kMdWhy_Mosaic, kMdWhy_Window,
  kMdWhy_Math, kMdWhy_Palettes, kMdWhy_HighTiles, kMdWhy_SpritePalettes, kMdWhy_NotSynced,
  kMdWhy_LineCheck, kMdWhy_Count
};

// Where ops go: a buffer that is flushed (to the 68000, or the PC's model) when it fills
typedef struct MdSink {
  uint16 buf[512];
  int n;                       // words in buf
  int total;                   // words since the last commit
  void (*flush)(struct MdSink *s);
} MdSink;

typedef struct MdPlanes {
  // what the Genesis holds (as sent)
  uint8 line_of_pal[8];        // SNES palette 0-7 -> Genesis line 0-3; 0xff: its cells are blank
  uint8 pal_of_line[4];        // Genesis line -> SNES palette (0xff: free)
  uint16 cram[64];             // the Genesis colours sent
  uint16 cram_src[64];         // the SNES colours they were made from, at this brightness
  uint8 cram_bright;
  uint16 vsram[2];             // A, B
  uint16 hscroll[2];           // A, B (full-screen mode)
  uint8 plane_on;              // bit 0 plane A shows BG1, bit 1 plane B BG2 (else the empty plane)
  uint8 cram_valid, scroll_valid;
  uint8 tile_empty[512 / 8];   // SNES tiles 0-511 (at kMd_TileBase): every pixel 0
  uint32 tile_hash[512];       // the Genesis's tiles as sent (0: not yet)
  // BG3 (the 32X draws it): which tiles are empty, which map rows hold any other
  uint8 bg3_empty[512 / 8];
  uint8 bg3_rows[8];
  uint32 bg3_cols[64][2];
  uint16 bg3_row_gen[64];
  uint32 bg3_row_hash[64];     // the row's map words as last looked at (the status bar is
                               // written again every frame, the same)
  uint16 bg3_map, bg3_tiles;   // the addresses they were found for (0xffff: none yet)
  uint8 bg3_size;
  // the screen's cells as last looked at: unchanged scroll, layers and maps (gen: bumped when a
  // cell or a tile's emptiness changes) - the counts and lists of last time still hold
  uint32 gen;
  uint16 ckey[5]; uint32 cgen; uint32 ccount[8]; uint8 cvalid, chigh;
  uint16 lkey[5]; uint32 lgen; uint8 lmd_pal, lvalid, lok;
} MdPlanes;

// One frame's picture split: what the 32X layer is drawn with (ppu->hybrid, both CPUs read it)
typedef struct MdHybrid {
  uint8 why;                   // kMdWhy_*: 0 = the Genesis planes show BG1/BG2
  uint8 md_pal;                // bit p: palette p's cells are on the Genesis
  uint8 off_base[8];           // palette p drawn by the 32X: the 32X CRAM index of its colour 0
  uint8 spr_slot[8];           // sprite palette 0-7 -> its behind-the-planes copy in CRAM 64 + 16 * slot (0xff none)
  uint16 sig_hs[2], sig_vs[2]; // BG1 / BG2 scroll as sent: every line must still have it
  uint8 layers;                // BG1 / BG2 on the main screen (bits 0 / 1): the planes shown
  uint8 fb;                    // the 32X frame buffer drawn (0 / 1): its lines' spans (ppu.c)
  // BG3 (MdPlanes' own, unchanged while the frame is drawn):
  const uint8 *bg3_rows;       // the map rows (bit per row 0-63) with a tile that is not empty
  const uint32 (*bg3_cols)[2]; // per map row: its columns (bits 0-63) with a non-empty tile
  const uint16 *bg3_row_gen;   // bumped when the row (or any BG3 tile) changes: a line drawn from
                               // the same row of the same generation is the same (ppu.c)
  // the cells the 32X draws, per layer (0 BG1, 1 BG2), by map row: rows row0 .. row0 + nrows - 1
  uint8 row0[2], nrows[2];
  uint16 start[2][31];         // cells of row r: [start[r], start[r + 1])
  uint8 cx[2][kMd_OffMax];     // the cell's column 0-63
  uint16 cell[2][kMd_OffMax];  // the SNES map word
} MdHybrid;

void MdPlanes_Reset(MdPlanes *m);
// Once per drawn frame, before drawing: the frame's split (h). room: words the commit has left.
void MdPlanes_Prepare(MdPlanes *m, Ppu *ppu, MdHybrid *h, int room);
// Then (the slave may be drawing already: the VRAM and the frame's registers do not change) the
// ops for the Genesis, into the sink. If they did not fit after all, h->why is set: the frame
// cannot be shown with the planes (its lines were drawn for them: draw it again, or drop it)
void MdPlanes_Send(MdPlanes *m, Ppu *ppu, MdHybrid *h, MdSink *sink);
// After drawing: which planes show (bits 0 / 1: BG1 / BG2; 0 for a frame the 32X drew whole);
// then flush the sink - the frame's ops are complete, ready to commit
void MdPlanes_Show(MdPlanes *m, uint32 layers, MdSink *sink);
// BG3's map rows that hold a non-empty tile (h->bg3_rows): the 32X skips the others' lines
void MdPlanes_Bg3Rows(MdPlanes *m, Ppu *ppu, MdHybrid *h);
// The 32X's 256 colours for a Genesis-planes frame, 15-bit + through bit
void MdPlanes_Palette32x(const MdHybrid *h, const uint16 *cgram, uint8 brightness, uint16 *out);
// A 15-bit SNES colour at a brightness as a Genesis colour (0000 BBB0 GGG0 RRR0)
uint16 MdPlanes_Colour(uint16 c, uint8 brightness);

#endif  // ZELDA3_SNES_MD_PLANES_H_
