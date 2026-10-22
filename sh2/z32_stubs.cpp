/* Zelda 3 32X - stage 1 stubs: the SNES sound chip (APU / SPC player / MSU) and the PC build's
 * "run the original 65816 code beside" checker are not on the 32X. The game only writes music
 * and effect numbers to the APU ports (zelda_apu_write); stage 1 drops them. */
#include "../zelda3/src/types.h"
#include "../zelda3/src/zelda_rtl.h"
#include "../zelda3/src/audio.h"
#include "../zelda3/src/zelda_cpu_infra.h"
#include "../zelda3/src/variables.h"
#include "../zelda3/src/features.h"
#include "../zelda3/src/assets.h"
#include "../zelda3/src/spc_player.h"
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
/* what the game sends to the sound chip: stage 1 drops it (stage 2: CD audio + PWM effects) */
uint8 z32_apu_ports[4];
void zelda_apu_write(uint32_t adr, uint8_t val) { z32_apu_ports[adr & 3] = val; }
void ZeldaPushApuState() {}
void LoadSongBank(const uint8 *p) { (void)p; }
void ZeldaRestoreMusicAfterLoad_Locked(bool is_reset) { (void)is_reset; }
void ZeldaSaveMusicStateToRam_Locked() {}
void ZeldaPlayMsuAudioTrack(uint8 track) { (void)track; }
void ZeldaEnableMsu(uint8 enable) { (void)enable; }
struct SpcPlayer *SpcPlayer_Create() { return 0; }
void SpcPlayer_Initialize(struct SpcPlayer *p) { (void)p; }
