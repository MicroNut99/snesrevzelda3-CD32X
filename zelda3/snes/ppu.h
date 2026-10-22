
#ifndef ZELDA3_SNES_PPU_H_
#define ZELDA3_SNES_PPU_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "snes/saveload.h"
typedef struct Ppu Ppu;
struct MdHybrid;

#include "src/types.h"

typedef struct BgLayer {
  uint16_t hScroll;
  uint16_t vScroll;
  // -- snapshot starts here
  bool tilemapWider;
  bool tilemapHigher;
  uint16_t tilemapAdr;
  // -- snapshot ends here
  uint16_t tileAdr;
} BgLayer;

enum {
  kPpuXPixels = 256 + kPpuExtraLeftRight * 2,
};

typedef uint16_t PpuZbufType;

// Z32X: the high-priority tiles of a layer's line, met while painting its low ones (ppu.c)
typedef struct PpuPaintItem {
  uint32_t row;                         // the tile row's pixels, nibble 0 first
  uint16_t x;                           // where on the line
  uint8_t base, n;                      // the palette offset, the pixel count
} PpuPaintItem;
typedef struct PpuPaintList {
  int n;
  PpuPaintItem item[48];                // 33 tiles a line, a few more where windows split it
} PpuPaintList;

typedef struct PpuPixelPrioBufs {
  // This holds the prio in the upper 8 bits and the color in the lower 8 bits.
  PpuZbufType data[kPpuXPixels];
} PpuPixelPrioBufs;

enum {
  kPpuRenderFlags_NewRenderer = 1,
  // Render mode7 upsampled by 4x4
  kPpuRenderFlags_4x4Mode7 = 2,
  // Use 240 height instead of 224
  kPpuRenderFlags_Height240 = 4,
  // Disable sprite render limits
  kPpuRenderFlags_NoSpriteLimits = 8,
  // Z32X (CD32X port): each pixel is one byte, the CGRAM index (the 32X's palette is the
  // SNES CGRAM, same 15-bit format). Colour math is not applied in this mode.
  kPpuRenderFlags_Index8 = 16,
  // Z32X: each pixel is 16 bits, the SNES colour after colour math and brightness as 15-bit BGR
  // (the 32X's direct colour mode); brightness as level * b / 15 on the 5-bit levels
  kPpuRenderFlags_Rgb15 = 32,
  // Z32X test: never the painted mode-1 line (the depth-buffer path is the reference)
  kPpuRenderFlags_NoPaint = 64,
  // Z32X "rip and tear" (md_planes.h): BG1 / BG2 are on the Genesis planes; the 8-bit line holds
  // the sprites and BG3 only, as indices into the 32X palette MdPlanes_Palette32x makes (0: see
  // through to the Genesis, its through bit set; 33-63 BG3 low, 64-127 sprites 0 / 1: behind
  // the planes; sprites 2 over a high BG1 / BG2 pixel: 0)
  kPpuRenderFlags_Hybrid = 128,
};


struct Ppu {
  bool lineHasSprites;
  uint8_t lastBrightnessMult;
  uint8_t lastMosaicModulo;
  uint8_t renderFlags;
  uint32_t renderPitch;
  uint8_t *renderBuffer;
  uint8_t extraLeftCur, extraRightCur, extraLeftRight, extraBottomCur;
  float mode7PerspectiveLow, mode7PerspectiveHigh;

  // TMW / TSW etc
  uint8 screenEnabled[2];
  uint8 screenWindowed[2];
  uint8 mosaicEnabled;
  uint8 mosaicSize;
  // object/sprites
  uint16_t objTileAdr1;
  uint16_t objTileAdr2;
  uint8_t objSize;
  // Window
  uint8_t window1left;
  uint8_t window1right;
  uint8_t window2left;
  uint8_t window2right;
  uint32_t windowsel;

  // color math
  uint8_t clipMode;
  uint8_t preventMathMode;
  bool addSubscreen;
  bool subtractColor;
  bool halfColor;
  uint8 mathEnabled;
  uint8_t fixedColorR, fixedColorG, fixedColorB;
  // settings
  bool forcedBlank;
  uint8_t brightness;
  uint8_t mode;

  // vram access
  uint16_t vramPointer;
  uint16_t vramIncrement;
  bool vramIncrementOnHigh;
  // cgram access
  uint8_t cgramPointer;
  bool cgramSecondWrite;
  uint8_t cgramBuffer;
  // oam access
  uint16_t oamAdr;
  bool oamSecondWrite;
  uint8_t oamBuffer;

  // background layers
  BgLayer bgLayer[4];
  uint8_t scrollPrev;
  uint8_t scrollPrev2;
  
  // mode 7
  int16_t m7matrix[8]; // a, b, c, d, x, y, h, v
  uint8_t m7prev;
  bool m7largeField;
  bool m7charFill;
  bool m7xFlip;
  bool m7yFlip;
  bool m7extBg_always_zero;
  // mode 7 internal
  int32_t m7startX;
  int32_t m7startY;

  uint16_t oam[0x110];
  // Z32X: the frame's sprites that are not parked at Y 0xF0, in OAM order (PpuBeginDrawing):
  // index, and the two high-table bits (size, X bit 8)
  uint8_t objCount;
  bool objBufferDirty;
  // Z32X: what ppu_evaluateSprites put on this line: the priorities used (bit p), the x span
  uint8_t objLevels;
  int16_t objMinX, objMaxX;
  // Z32X: PpuPaintLine's line: [0..255] the main screen's CGRAM indices, [256..511] the main
  // screen's layer per pixel (colour-math lines only), [512..767] the subscreen's indices
  uint32_t lineBuf[64 * 3];
  PpuPaintList paintHi[3];              // Z32X: BG1..3's high-priority tiles of the line
  // Z32X Hybrid: the frame's split (md_planes.h), and set by a line the Genesis cannot show as drawn
  const struct MdHybrid *hybrid;
  uint8 hybridFail;
  uint32 hybridKey[6];                  // PpuHybridCheck: the registers of the last line checked
  uint8 regsChanged;                    // Z32X: a register was written (ppu_write) since the last look
  uint8_t objList[128];
  uint8_t objHigh[128];
  uint8 objLines[32];                   // Z32X: the lines (0-255) a sprite of objList is on (bits)
  
  // store 31 extra entries to remove the need for clamp
  uint8_t brightnessMult[32 + 31];
  uint8_t brightnessMultHalf[32 * 2];
  // Z32X: the same for 5-bit levels (kPpuRenderFlags_Rgb15), and the CGRAM through them
  uint8_t bright5[32 + 31];
  uint8_t bright5Half[32 * 2];
  uint16_t palette15[256];
  uint16_t tint15[256];        // Z32X: the colours the fixed-colour math makes of them (PpuPaintLine)
  uint32_t tintKey;            //   the settings they were made for (0: none yet this frame)
  uint16_t cgram[0x100];
  uint8_t mosaicModulo[kPpuXPixels];
#ifndef Z32X                            // (the 4x mode-7 picture only: not on the 32X - 1 KB of SDRAM)
  uint32_t colorMapRgb[256];
#endif
  PpuPixelPrioBufs bgBuffers[2];
  PpuPixelPrioBufs objBuffer;
  // Z32X: the VRAM is shared by the frame's drawing contexts (a copy of this struct per SH2,
  // ZeldaDrawPpuFrameBegin), so it is a separate block
#ifdef __cplusplus
  le16 *vram;                           // Z32X: little-endian, as all SNES memory (types.h)
#else
  uint16_t *vram;
#endif
};

Ppu* ppu_init();
void ppu_free(Ppu* ppu);
void ppu_reset(Ppu* ppu);
void ppu_handleVblank(Ppu* ppu);
void ppu_runLine(Ppu* ppu, int line);
uint8_t ppu_read(Ppu* ppu, uint8_t adr);
void ppu_write(Ppu* ppu, uint8_t adr, uint8_t val);
void ppu_saveload(Ppu *ppu, SaveLoadFunc *func, void *ctx);
void PpuBeginDrawing(Ppu *ppu, uint8_t *buffer, size_t pitch, uint32_t render_flags);
void PpuHybridSpansFull(int fb);        // Z32X: frame buffer fb holds another picture (md_planes.h)

// Returns the current render scale, 1x = 256px, 2x=512px, 4x=1024px
int PpuGetCurrentRenderScale(Ppu *ppu, uint32_t render_flags);
bool PpuNeedsDirectColour(Ppu *ppu);    // Z32X

void PpuSetMode7PerspectiveCorrection(Ppu *ppu, int low, int high);
void PpuSetExtraSideSpace(Ppu *ppu, int left, int right, int bottom);

#endif  // ZELDA3_SNES_PPU_H_
