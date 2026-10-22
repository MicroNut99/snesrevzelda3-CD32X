// This file defines various things related to the runtime environment
// of the code
#ifndef ZELDA3_ZELDA_RTL_H_
#define ZELDA3_ZELDA_RTL_H_

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "types.h"
#include "features.h"

struct Snes;
struct Dsp;

typedef struct ZeldaEnv {
  uint8 *ram;
  uint8 *sram;
  le16 *vram;
  struct Ppu *ppu;
  struct SpcPlayer *player;
  struct Dma *dma;
  
  MemBlk dialogue_blk;
  MemBlk dialogue_font_blk;
  uint8 dialogue_flags;
} ZeldaEnv;
extern ZeldaEnv g_zenv;
extern int frame_ctr_dbg;

// Z32X: which VRAM words the game wrote since the last look, a bit per 16 words (one 4bpp tile,
// half a tilemap row) - the 32X keeps the Genesis's copy of the backgrounds in step with them.
// Every writer of g_zenv.vram marks what it writes (VramW / ZeldaVramDirty); pc/zh_main.c
// (ZH_VRAMCHECK) checks that against a copy of the VRAM after every frame.
// The two BG maps (words 0x0000-0x1FFF) also have a bit per word (g_vram_fine): a column the
// game writes (every 32nd word) is sent to the Genesis as that, not as the 64 blocks it touches.
extern uint8 g_vram_dirty[0x8000 / 16 / 8];
extern uint8 g_vram_fine[0x2000 / 8];
void ZeldaVramDirty(uint32 addr, uint32 nwords);
void ZeldaVramDirtyStride(uint32 addr, uint32 nwords, uint32 step);
static inline le16 *VramW(uint32 addr, uint32 nwords) {   // &g_zenv.vram[addr], marked
  ZeldaVramDirty(addr, nwords);
  return &g_zenv.vram[addr];
}
static inline le16 *VramWS(uint32 addr, uint32 nwords, uint32 step) {   // nwords, every step-th
  ZeldaVramDirtyStride(addr, nwords, step);
  return &g_zenv.vram[addr];
}

typedef void PlayerHandlerFunc();
typedef void HandlerFuncK(int k);

static inline void zelda_snes_dummy_write(uint32 adr, uint8 val) {}

void zelda_apu_write(uint32_t adr, uint8_t val);
uint8_t zelda_read_apui00();
uint8_t zelda_apu_read(uint32_t adr);
void zelda_ppu_write(uint32_t adr, uint8_t val);
void zelda_ppu_write_word(uint32_t adr, uint16_t val);


// 512x480 32-bit pixels. Returns true if we instead draw 1024x960
void HdmaSetup(uint32 addr6, uint32 addr7, uint8 transfer_unit, uint8 reg6, uint8 reg7, uint8 indirect_bank);

void ZeldaInitialize();
void ZeldaReset(bool preserve_sram);
bool ZeldaStartReplayFromMemory(const uint8 *sav, size_t len);   // Z32X
void ZeldaDrawPpuFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags);
/* Z32X: the frame's drawing in parts (zelda_rtl.c), for drawing on two CPUs */
typedef struct ZeldaDrawHdma {
  const uint8 *table;
  const uint8 *indir_ptr;
  uint8 rep_count;
  uint8 mode;
  uint8 ppu_addr;
  uint8 indir_bank;
} ZeldaDrawHdma;
typedef struct ZeldaDrawCtx {
  struct Ppu *ppu;
  ZeldaDrawHdma hdma[2];
  int height;
  uint8 irq_flag_copy;    /* irq_flag (g_ram 0x128) at the frame start */
  uint16 bg3_hofs;
} ZeldaDrawCtx;
void ZeldaDrawPpuFrameBegin(ZeldaDrawCtx *ctx, uint8 *pixel_buffer, size_t pitch, uint32 render_flags);
void ZeldaDrawCtxCopy(ZeldaDrawCtx *dst, struct Ppu *dst_ppu, const ZeldaDrawCtx *ctx);
void ZeldaDrawPpuLines(ZeldaDrawCtx *ctx, int from, int to, int last);
void ZeldaSkipPpuFrame(void);
void ZeldaDrawPpuFrameEnd(ZeldaDrawCtx *ctx);
void ZeldaRunFrameInternal(uint16 input, int run_what);
bool ZeldaRunFrame(int input_state);
void LoadSongBank(const uint8 *p);
void ZeldaApuLock();
void ZeldaApuUnlock();
bool ZeldaIsPlayingMusicTrack(uint8 track);
uint8 ZeldaGetEntranceMusicTrack(int track);
void ZeldaSetLanguage(const char *language);
void PatchCommand(char cmd);

// Things for state management

enum {
  kSaveLoad_Save = 0,
  kSaveLoad_Load = 1,
  kSaveLoad_Replay = 2,
};

void SaveLoadSlot(int cmd, int which);
void ZeldaWriteSram();
void ZeldaReadSram();

typedef void ZeldaRunFrameFunc(uint16 input, int run_what);
typedef void ZeldaSyncAllFunc();

void ZeldaSetupEmuCallbacks(uint8 *emu_ram, ZeldaRunFrameFunc *func, ZeldaSyncAllFunc *sync_all);

// Button definitions, zelda splits them in separate 8-bit high/low
enum {
  kJoypadL_A = 0x80,
  kJoypadL_X = 0x40,
  kJoypadL_L = 0x20,
  kJoypadL_R = 0x10,

  kJoypadH_B = 0x80,
  kJoypadH_Y = 0x40,
  kJoypadH_Select = 0x20,
  kJoypadH_Start = 0x10,

  kJoypadH_Up = 0x8,
  kJoypadH_Down = 0x4,
  kJoypadH_Left = 0x2,
  kJoypadH_Right = 0x1,

  kJoypadH_AnyDir = 0xf,
};

#endif  // ZELDA3_ZELDA_RTL_H_
