/* Zelda 3 32X - renders the game's sound at build time with zelda3's own SNES sound driver
 * (src/spc_player.c) and DSP (snes/dsp.c): 32000 Hz stereo, 534 samples a 60 Hz frame.
 *
 *   zh_sound <zelda3_assets.dat> <out folder>
 *
 * Writes into <out folder>:
 *   song_NN.raw   song NN (hex, 01..22) for a CD audio track: one pass of the song - up to its
 *                 endless loop (the CD player repeats the track) or its end; 16-bit stereo,
 *                 32000 Hz, little-endian
 *   songs.txt     NN bank frames loops(1 = repeats, 0 = plays once) for each song that plays
 *   sfx_P_II.raw  sound effect II (hex) on port P (1 ambience, 2 / 3 effects), centred: 16-bit
 *                 mono, 32000 Hz
 *   sfx.txt       P II frames loops(1 = still playing after the longest render: an ambience
 *                 that repeats) for each effect that makes a sound
 * tools/make_sound.py turns them into the CD tracks and the cartridge's effect bank. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../zelda3/src/types.h"
#include "../zelda3/src/assets.h"
#include "../zelda3/src/spc_player.h"
#include "../zelda3/snes/dsp.h"

extern int g_spc_song_loops;
const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];
static uint8 *g_asset_data;

static void load_assets(const char *fn) {
  FILE *f = fopen(fn, "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", fn); exit(1); }
  fseek(f, 0, SEEK_END); size_t n = ftell(f); fseek(f, 0, SEEK_SET);
  g_asset_data = (uint8 *)malloc(n);
  if (fread(g_asset_data, 1, n, f) != n) exit(1);
  fclose(f);
  const uint8 *data = g_asset_data;
  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);
  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }
}
MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
void NORETURN Die(const char *error) { fprintf(stderr, "%s\n", error); exit(1); }

/* the driver as the game leaves it: the intro bank, then the indoor or ending bank over it */
static SpcPlayer *new_player(int bank) {
  SpcPlayer *p = SpcPlayer_Create();
  SpcPlayer_Initialize(p);
  SpcPlayer_Upload(p, kSoundBank_intro);
  if (bank == 1) SpcPlayer_Upload(p, kSoundBank_indoor);
  if (bank == 2) SpcPlayer_Upload(p, kSoundBank_ending);
  return p;
}

static int16 g_frame[534 * 2];
static void frame(SpcPlayer *p) {                /* one 60 Hz frame of sound into g_frame */
  SpcPlayer_GenerateSamples(p);
  dsp_getSamples(p->dsp, g_frame, 534, 2);
}

static int peak(void) {
  int m = 0;
  for (int i = 0; i < 534 * 2; i++) { int v = abs(g_frame[i]); if (v > m) m = v; }
  return m;
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: zh_sound <zelda3_assets.dat> <out folder>\n"); return 1; }
  load_assets(argv[1]);
  char fn[1024];

  /* songs: each id in each bank; the bank whose song starts soonest and plays longest wins */
  snprintf(fn, sizeof fn, "%s/songs.txt", argv[2]);
  FILE *list = fopen(fn, "w");
  for (int id = 1; id <= 0x22; id++) {
    int best_bank = -1, best_frames = 0, best_loops = 0;
    for (int bank = 0; bank < 3; bank++) {
      SpcPlayer *p = new_player(bank);
      for (int i = 0; i < 10; i++) frame(p);
      g_spc_song_loops = 0;
      p->input_ports[0] = (uint8)id;
      int frames = 0, loud = 0, started = 0, loops = 0;
      for (; frames < 60 * 60 * 5; frames++) {   /* at most 5 minutes */
        frame(p);
        if (peak() > 64) loud++;
        if (p->port_to_snes[0] == id) started = 1;
        if (g_spc_song_loops) { loops = 1; frames++; break; }
        if (started && p->port_to_snes[0] == 0) { frames++; break; }   /* the song ended */
      }
      if (started && loud > 30 && frames > best_frames) best_bank = bank, best_frames = frames, best_loops = loops;
      free(p);
    }
    if (best_bank < 0) continue;
    /* render it again in that bank, into the file */
    SpcPlayer *p = new_player(best_bank);
    for (int i = 0; i < 10; i++) frame(p);
    g_spc_song_loops = 0;
    p->input_ports[0] = (uint8)id;
    snprintf(fn, sizeof fn, "%s/song_%02x.raw", argv[2], id);
    FILE *o = fopen(fn, "wb");
    for (int i = 0; i < best_frames; i++) { frame(p); fwrite(g_frame, 2, 534 * 2, o); }
    fclose(o);
    free(p);
    fprintf(list, "%02x %d %d %d\n", id, best_bank, best_frames, best_loops);
    fprintf(stderr, "song %02x: bank %d, %.1f s%s\n", id, best_bank, best_frames / 60.0, best_loops ? ", repeats" : "");
  }
  fclose(list);

  /* sound effects: each id on each port, centred, until its channels are free again */
  snprintf(fn, sizeof fn, "%s/sfx.txt", argv[2]);
  list = fopen(fn, "w");
  static int16 mono[60 * 8 * 534];
  for (int port = 1; port <= 3; port++) {
    for (int id = 1; id <= (port == 1 ? 0x1f : 0x3f); id++) {
      SpcPlayer *p = new_player(0);
      for (int i = 0; i < 10; i++) frame(p);
      p->input_ports[port] = (uint8)id;
      int n = 0, last_loud = -1, loops = 0;
      const int kMax = 60 * 8;                    /* at most 8 seconds */
      for (int f = 0; f < kMax; f++) {
        frame(p);
        if (f == 1) p->input_ports[port] = 0;    /* the game sends it for one frame */
        for (int i = 0; i < 534; i++) mono[n + i] = (int16)((g_frame[i * 2] + g_frame[i * 2 + 1]) >> 1);
        n += 534;
        if (peak() > 32) last_loud = f;
        uint8 active = port == 1 ? p->port1_active : port == 2 ? p->port2_active : p->port3_active;
        if (f > 2 && !active && f - last_loud > 2) break;
        if (f == kMax - 1) loops = 1;
      }
      free(p);
      if (last_loud < 0) continue;               /* no sound */
      int frames = loops ? kMax : last_loud + 1;
      snprintf(fn, sizeof fn, "%s/sfx_%d_%02x.raw", argv[2], port, id);
      FILE *o = fopen(fn, "wb");
      fwrite(mono, 2, (size_t)frames * 534, o);
      fclose(o);
      fprintf(list, "%d %02x %d %d\n", port, id, frames, loops);
    }
  }
  fclose(list);
  return 0;
}
