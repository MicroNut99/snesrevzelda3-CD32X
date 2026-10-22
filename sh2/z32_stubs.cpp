/* Zelda 3 32X - in place of zelda3's audio.c: the SNES sound chip (APU / SPC player / MSU) and the
 * PC build's "run the original 65816 code beside" checker are not on the 32X. The game writes
 * music and effect numbers to the APU ports (zelda_apu_write, ZeldaPlayMsuAudioTrack): they go
 * to z32_sound.cpp (PWM effects, Sega CD music). */
#include "../zelda3/src/types.h"
#include "../zelda3/src/zelda_rtl.h"
#include "../zelda3/src/audio.h"
#include "../zelda3/src/zelda_cpu_infra.h"
#include "../zelda3/src/variables.h"
#include "../zelda3/src/features.h"
#include "../zelda3/src/assets.h"
#include "../zelda3/src/spc_player.h"
#include "z32_sound.h"
void ZeldaApuLock() {}
void ZeldaApuUnlock() {}
/* Music state the game logic reads: as audio.c with the MSU player idle (it is not used) -
 * from the SNES RAM and the tables, so the 32X matches the PC frame for frame. */
bool ZeldaIsPlayingMusicTrack(uint8 track) { return track == music_unk1; }
bool ZeldaIsPlayingMusicTrackWithBug(uint8 track) {
  return track == (enhanced_features0 & kFeatures0_MiscBugFixes ? music_unk1 : last_music_control);
}
uint8 ZeldaGetEntranceMusicTrack(int i) { return kEntranceData_musicTrack[i]; }
uint8_t zelda_read_apui00() { return g_ram[kRam_APUI00]; }
/* the SPC's reply ports: the PC reference never runs the sound chip (no audio thread), so they
 * stay at their power-on 0 there - and here */
uint8_t zelda_apu_read(uint32_t adr) { (void)adr; return 0; }
bool ZeldaIsMusicPlaying() { return false; }
/* what the game sends to the sound chip: effects to the PWM mixer, music to the Sega CD
 * (z32_sound.cpp) */
uint8 z32_apu_ports[4];
void zelda_apu_write(uint32_t adr, uint8_t val) {
  z32_apu_ports[adr & 3] = val;
  if (adr & 3)
    z32_sound_port(adr & 3, val);
}
void ZeldaPushApuState() {}
void LoadSongBank(const uint8 *p) { (void)p; }
void ZeldaRestoreMusicAfterLoad_Locked(bool is_reset) { (void)is_reset; }
void ZeldaSaveMusicStateToRam_Locked() {}
void ZeldaPlayMsuAudioTrack(uint8 music_ctrl) {   /* as audio.c without MSU: port 0, and the CD */
  z32_apu_ports[0] = music_ctrl;
  z32_music(music_ctrl);
}
void ZeldaEnableMsu(uint8 enable) { (void)enable; }
struct SpcPlayer *SpcPlayer_Create() { return 0; }
void SpcPlayer_Initialize(struct SpcPlayer *p) { (void)p; }
