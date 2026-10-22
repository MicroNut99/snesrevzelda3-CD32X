! Zelda 3 32X - data in the cartridge ROM, made from YOUR ROM at build time (never committed):
! the game's assets (zelda3_assets.dat), and for the stage-1 test the chapter-1 recording and the
! PC's RAM hash per frame. Z32_DATA is the folder that holds them (sh2/Makefile).
        .section .romdata,"a"
        .align  4
        .global _z32_assets, _z32_assets_end
_z32_assets:     .incbin "zelda3_assets.dat"
_z32_assets_end:
        .align  4
        .global _z32_replay, _z32_replay_end
_z32_replay:     .incbin "replay.bin"
_z32_replay_end:
        .align  4
        .global _z32_expect, _z32_expect_end
_z32_expect:     .incbin "expect.bin"
_z32_expect_end:
        .align  4
        .global _z32_expect_px, _z32_expect_px_end
_z32_expect_px:  .incbin "expect_px.bin"       ! the PC's 8-bit picture + CGRAM hash per frame
_z32_expect_px_end:
        .align  4
        .global _z32_sfx
_z32_sfx:        .incbin "sfx.bin"             ! the sound effects + song flags (tools/make_sound.py)
