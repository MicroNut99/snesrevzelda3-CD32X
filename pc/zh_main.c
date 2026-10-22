/* zelda3 headless reference run (PC): no window, no sound. Loads zelda3_assets.dat, runs N
 * frames with a scripted pad and writes chosen frames as PPM. Used to check the CD32X port
 * against the original C code frame by frame.
 *   zh_main <assets.dat> <frames> [pad script "frame:bits,..."] [dump frames "a,b,c"] [outdir]
 * pad bits: SNES joypad as zelda3 takes it (see kButton_* below). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../zelda3/src/types.h"
#include "../zelda3/src/zelda_rtl.h"
#include "../zelda3/src/assets.h"
#include "../zelda3/src/config.h"
#include "../zelda3/snes/ppu.h"
#include "../zelda3/src/util.h"
#include "../zelda3/src/zelda_cpu_infra.h"
#include "../zelda3/src/variables.h"

Config g_config;
const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];
void NORETURN Die(const char *error) { fprintf(stderr, "Error: %s\n", error); exit(1); }
void ZeldaApuLock(void) {}
void ZeldaApuUnlock(void) {}

static uint8 *read_file(const char *n, size_t *len) {
  FILE *f = fopen(n, "rb"); if (!f) return 0;
  fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
  uint8 *d = malloc(*len); if (fread(d, 1, *len, f) != *len) { fclose(f); return 0; } fclose(f); return d;
}
static void load_assets(const char *n) {
  size_t length; uint8 *data = read_file(n, &length);
  if (!data) Die("no assets");
  static const char kAssetsSig[] = { kAssets_Sig };
  if (memcmp(data, kAssetsSig, 48) || *(uint32 *)(data + 80) != kNumberOfAssets) Die("bad assets");
  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);
  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    g_asset_sizes[i] = size; g_asset_ptrs[i] = data + offset; offset += size;
  }
}
static uint32 pix[256 * 240];
#ifdef ZH_MDSTAT                         /* pc/zhx_md: the Genesis-plane analysis (pc/zh_mdstat.c) */
void zh_mdstat_before_logic(void);
void zh_mdstat_draw(FILE *out, int f, uint8 *buf, size_t pitch, uint32 flags);
#endif
/* ZH_MDCHECK / ZH_VRAMCHECK: the Genesis-planes picture (pc/zh_md.c) */
void zh_md_reset(void);
void zh_md_frame(int f, uint8 *ref, uint32 dflags);
void zh_md_report(void);
void zh_vram_check(int f);
int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage\n"); return 2; }
  load_assets(argv[1]);
  const int frames = atoi(argv[2]);
  const char *script = argc > 3 ? argv[3] : "";
  const char *dumps = argc > 4 ? argv[4] : "";
  const char *out = argc > 5 ? argv[5] : ".";
  ZeldaInitialize();
  ZeldaSetLanguage(NULL);                /* the game's text (as main.c) */
  g_zenv.ppu->extraLeftRight = 0;        /* no widescreen margin (main.c: extended_aspect_ratio 0) */
  /* ZH_REPLAY=n: replay reference recording n (saves/ref, run from the zelda3 folder);
   * ZH_HASH=file: the SNES RAM's hash after every frame (to compare two builds) */
  if (getenv("ZH_REPLAY")) SaveLoadSlot(kSaveLoad_Replay, 256 + atoi(getenv("ZH_REPLAY")));
  FILE *hf = getenv("ZH_HASH") ? fopen(getenv("ZH_HASH"), "w") : 0;
  { extern FILE *g_apu_log;          /* ZH_APULOG=file: "bank port value" for each new sound command */
    if (getenv("ZH_APULOG")) g_apu_log = fopen(getenv("ZH_APULOG"), "w"); }
  FILE *pxf = getenv("ZH_PIXHASH") ? fopen(getenv("ZH_PIXHASH"), "w") : 0;
  int input = 0;
  const bool mdcheck = getenv("ZH_MDCHECK") != NULL;
  if (mdcheck) zh_md_reset();
  for (int f = 0; f < frames; f++) {
    const char *p = script;               /* "frame:bits" pairs: the pad from that frame on */
    while (*p) { int fr = atoi(p); const char *c = strchr(p, ':'); if (!c) break; if (fr == f) input = (int)strtol(c + 1, 0, 0);
                 const char *n = strchr(c, ','); if (!n) break; p = n + 1; }
#ifdef ZH_MDSTAT
    static FILE *mdf;
    if (!mdf && getenv("ZH_MDSTAT")) mdf = fopen(getenv("ZH_MDSTAT"), "w");
    if (mdf) zh_mdstat_before_logic();
#endif
    ZeldaRunFrame(input);
    if (getenv("ZH_MODLOG")) {          /* ZH_MODLOG: the module / submodule when it changes */
      static int last = -1; int m = g_ram[0x10] << 24 | g_ram[0x11] << 16 | g_ram[0x22] << 8 | g_ram[0x23];
      if (m != last) fprintf(stderr, "frame %d module %08x\n", f, m), last = m;
      if (getenv("ZH_TEXTLOG") && g_ram[0x10] == 0x14 && f % 20 == 0)
        fprintf(stderr, "frame %d msgmod=%d trs=%d pos=%d idx=%x pfc=%d dark=%d buf=%02x %02x %02x %02x\n", f, g_ram[0x1cd8], g_ram[0x1cd4],
                g_ram[0x1cd9] | g_ram[0x1cda] << 8, g_ram[0x1cf0] | g_ram[0x1cf1] << 8, g_ram[0xc007], g_ram[0xc009], g_ram[0x11200], g_ram[0x11201], g_ram[0x11202], g_ram[0x11203]);
    }
    if (hf) {
      uint32 h = 2166136261u;
      for (int i = 0; i < 0x20000; i++) h = (h ^ g_ram[i]) * 16777619u;
      fprintf(hf, "%d %08x\n", f, h);
    }
    if (getenv("ZH_RAMAT") && atoi(getenv("ZH_RAMAT")) == f) {   /* the SNES RAM after frame f */
      FILE *rf = fopen(getenv("ZH_RAMFILE") ? getenv("ZH_RAMFILE") : "ram.bin", "wb");
      if (rf) { fwrite(g_ram, 1, 0x20000, rf); fclose(rf); }
    }
    if (getenv("ZH_PPUDBG") && atoi(getenv("ZH_PPUDBG")) == f) {
      Ppu *q = g_zenv.ppu;
      fprintf(stderr, "fb=%d bright=%d mode=%d scr=%02x/%02x win=%02x/%02x w1=%d-%d w2=%d-%d wsel=%08x\n", q->forcedBlank, q->brightness, q->mode,
        q->screenEnabled[0], q->screenEnabled[1], q->screenWindowed[0], q->screenWindowed[1], q->window1left, q->window1right, q->window2left, q->window2right, q->windowsel);
      for (int l = 0; l < 3; l++) fprintf(stderr, "bg%d h=%d v=%d map=%04x tiles=%04x\n", l, q->bgLayer[l].hScroll, q->bgLayer[l].vScroll, q->bgLayer[l].tilemapAdr, q->bgLayer[l].tileAdr);
      uint32 vh = 2166136261u; for (int i = 0; i < 0x10000; i++) vh = (vh ^ ((uint8 *)q->vram)[i]) * 16777619u;
      fprintf(stderr, "vramhash=%08x\n", vh);
      { FILE *vf = fopen("vram.bin", "wb"); if (vf) { fwrite(q->vram, 1, 0x10000, vf); fwrite(q->cgram, 1, 512, vf); fclose(vf); } }
      fprintf(stderr, "math: enabled=%02x add_sub=%d half=%d subtract=%d fixed=%d,%d,%d clip=%d prevent=%d bright=%d\n", q->mathEnabled, q->addSubscreen,
        q->halfColor, q->subtractColor, q->fixedColorR, q->fixedColorG, q->fixedColorB, q->clipMode, q->preventMathMode, q->brightness);
    }
    char key[16]; snprintf(key, sizeof key, ",%d,", f);
    char hay[4096]; snprintf(hay, sizeof hay, ",%s,", dumps);
    static uint8 idx[256 * 2 * 240];
    bool drawn = false;
    /* ZH_RGB15: the 32X's 15-bit direct colour picture (colour math, brightness) instead of the
     * 8-bit CGRAM indices; ZH_AUTO: as the 32X - direct colour only for the frames that need it
     * (PpuNeedsDirectColour); ZH_REF: zelda3's depth-buffer line renderer only (the reference) */
    const bool rgb15 = getenv("ZH_AUTO") ? PpuNeedsDirectColour(g_zenv.ppu) : getenv("ZH_RGB15") != NULL;
    const uint32 dflags = kPpuRenderFlags_NewRenderer | (rgb15 ? kPpuRenderFlags_Rgb15 : kPpuRenderFlags_Index8) |
                          (getenv("ZH_REF") ? kPpuRenderFlags_NoPaint : 0);
    const int dpitch = rgb15 ? 512 : 256;
#ifdef ZH_MDSTAT
    if (mdf) {                     /* ZH_MDSTAT=file: every frame drawn (as ZH_DRAWALL), analysed */
      zh_mdstat_draw(mdf, f, idx, dpitch, dflags);
      drawn = true;
    } else
#endif
    if (mdcheck) {                 /* ZH_MDCHECK: every frame, both pictures, compared (8-bit) */
      if (getenv("ZH_VRAMCHECK")) zh_vram_check(f);
      zh_md_frame(f, idx, kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Index8);
      drawn = true;
    } else
    if (getenv("ZH_SKIP") && f % atoi(getenv("ZH_SKIP")) != 0) {
      ZeldaSkipPpuFrame();         /* frame skip as the 32X: the RAM must stay the same */
    } else if (getenv("ZH_DRAWALL")) {    /* draw every frame as the game (and the 32X) does: the draw
                                    * changes g_ram (irq_flag, zelda_rtl.c), so hashes for the 32X
                                    * test (expect.bin) must be made with this on */
      if (getenv("ZH_SPLIT")) {    /* the 32X's two-CPU drawing, one part after the other:
                                    * lines 0..n-1 in the game's context, n..224 in a copy */
        static Ppu other;
        ZeldaDrawCtx ctx, ctx2;
        int n = atoi(getenv("ZH_SPLIT"));
        ZeldaDrawPpuFrameBegin(&ctx, idx, dpitch, dflags);
        ZeldaDrawCtxCopy(&ctx2, &other, &ctx);
        ZeldaDrawPpuLines(&ctx2, n, ctx.height, ctx.height);
        ZeldaDrawPpuLines(&ctx, 0, n - 1, ctx.height);
        ZeldaDrawPpuFrameEnd(&ctx);
      } else {
        ZeldaDrawPpuFrame(idx, dpitch, dflags);
      }
      drawn = true;
      if (pxf && rgb15) {           /* ZH_PIXHASH, direct colour: the 15-bit picture's values */
        uint32 h = 2166136261u;
        const uint16 *p16 = (const uint16 *)idx;
        for (int i = 0; i < 256 * 224; i++) h = (h ^ (p16[i] & 0xff)) * 16777619u, h = (h ^ (p16[i] >> 8)) * 16777619u;
        fprintf(pxf, "%d %08x\n", f, h);
      } else if (pxf) {             /* ZH_PIXHASH: the 8-bit picture + CGRAM, per frame */
        uint32 h = 2166136261u;
        for (int i = 0; i < 256 * 224; i++) h = (h ^ idx[i]) * 16777619u;
        for (int i = 0; i < 256; i++) h = (h ^ (g_zenv.ppu->cgram[i] & 0xff)) * 16777619u, h = (h ^ (g_zenv.ppu->cgram[i] >> 8)) * 16777619u;
        fprintf(pxf, "%d %08x\n", f, h);
      }
    }
    if (strstr(hay, key) && getenv("ZH_INDEX8")) {    /* the 32X's path: 8-bit CGRAM indices */
      if (!drawn) ZeldaDrawPpuFrame(idx, dpitch, dflags);
      char fn[512]; snprintf(fn, sizeof fn, "%s/%c%05d.ppm", out, rgb15 ? 'd' : 'i', f);
      FILE *o = fopen(fn, "wb"); fprintf(o, "P6\n256 224\n255\n");
      for (int i = 0; i < 256 * 224; i++) { uint16 c = rgb15 ? ((const uint16 *)idx)[i] : g_zenv.ppu->cgram[idx[i]];
        uint8 rgb[3] = { (uint8)((c & 31) << 3), (uint8)((c >> 5 & 31) << 3), (uint8)((c >> 10 & 31) << 3) }; fwrite(rgb, 1, 3, o); }
      fclose(o);
    } else if (strstr(hay, key)) {
      memset(pix, 0, sizeof pix);
      ZeldaDrawPpuFrame((uint8 *)pix, 256 * 4, 0);
      char fn[512]; snprintf(fn, sizeof fn, "%s/z%05d.ppm", out, f);
      FILE *o = fopen(fn, "wb"); fprintf(o, "P6\n256 224\n255\n");
      for (int i = 0; i < 256 * 224; i++) { uint8 rgb[3] = { pix[i] >> 16, pix[i] >> 8, pix[i] }; fwrite(rgb, 1, 3, o); }
      fclose(o);
    }
  }
  if (mdcheck) zh_md_report();
  return 0;
}
MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
