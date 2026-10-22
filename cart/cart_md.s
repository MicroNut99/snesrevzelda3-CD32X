| ===========================================================================================
| U-R-R (from the Kobo Deluxe 32X cartridge) - Genesis (68000) side.   (cart/cart_md.s)
|
| ZELDA 3 32X: U-R-R's starfield is gone.  Both Genesis planes are cleared at start-up and the
|   backdrop is black, so only the 32X picture (the SNES screen) shows.  COMM12 is free for the
|   SH2's own status (it was the starfield's scroll velocity).
|
| Linked at 0x880800 = ROM offset 0x800, seen through the 32X's fixed cartridge window.  The
| 32X vector jump list at ROM 0x200 (part of the standard boot block) sends the 68000 to fixed
| spots here (the same layout as D32XR's crt0):
|     0x880800 reset / start    0x880840 exception    0x880880 H-blank
|     0x8808C0 V-blank          0x880900 external interrupt
|
| Its job is what the Sega CD Sub-CPU + 68000 listener did in the CD version: answer Kobo's
| commands in 32X COMM0 (written by the SH2, cleared here when done).
|     48  pad          COMM8 = pad bits, COMM10 = 68000 frame counter (Kobo's clock)
|     52  text on/off  COMM2 = 1 on / 0 off (Genesis display; Kobo keeps it off)
|     53  sound pack   COMM12 = 0: no sound effects        (C2: Sega CD PCM)
|     56/59 backup RAM COMM12 = 0: "NO BACKUP RAM"         (C2: Sega CD backup RAM)
|     57  BRAM word    COMM8 = 0
|     60  SRAM load    (v18) cartridge SRAM -> the 128-byte save block in 68000 RAM; COMM2 =
|                      1 the block is valid (magic + checksum), 2 only v17m's high score was
|                      found (COMM8:COMM10 = that score), 0 nothing saved yet
|     61  SRAM save    (v18) the save block -> SRAM (the checksum is made here)
|     62  music        (v18) COMM2 = 0 silence, 1 FM (the VGM stream, from the start),
|                      Zelda 3 32X: 3 = CD track COMM8 once, 4 = CD volume COMM8 (0 .. 0x400);
|                      2 CD audio track COMM8, repeating (needs a Sega CD + audio CD)
|     63  CD status    (v18) COMM2 = 0 not tried, 1 no Sega CD, 2 Sega CD does not answer,
|                      3 ready; COMM8 = last track number (0 = no audio CD)
|     64  CD player    (v18) COMM2 = 'P' play from track COMM8 on, 'S' stop, 'Z' pause/go on
|     65  block read   (v18) COMM8 = word COMM2 (0-63) of the save block
|     66  block write  (v18) word COMM2 of the save block = COMM8
| v18: FM MUSIC - the V-blank plays one frame of the music stream (tools/make_music.py, from
|   a VGM) on the YM2612 / PSG; the Z80 is kept off the bus.  CD AUDIO - Chilly Willy's Mode 1
|   code (from Vic's YATSSD, via the Sonic32XPlus1 cartridge): cart/cd.s.
|     45/54/58 and anything else: acknowledged             (C2: CD music)
| Rules kept from the hardware work: COMM0 is polled with a pause between reads (never
| hammered while the SH2 writes it), and read twice - acted on only if both reads match.
| ===========================================================================================

        .equ    VDP_CTRL,   0xC00004
        .equ    VDP_DATA,   0xC00000
        .equ    COMM0,      0xA15120
        .equ    COMM2,      0xA15122
        .equ    COMM4,      0xA15124
        .equ    COMM8,      0xA15128
        .equ    COMM10,     0xA1512A
        .equ    COMM12,     0xA1512C
        .equ    ticks,      0x00FF0000          | Genesis RAM: frame counter
        .equ    pad1,       0x00FF0002          | Genesis RAM: pad 1, active-high bits
        .equ    ax,         0x00FF0004          | starfield positions, 1/16 pixel:
        .equ    ay,         0x00FF0006          |   near layer (plane A) x, y
        .equ    bx,         0x00FF0008          |   far layer (plane B) x, y
        .equ    by,         0x00FF000A
        .equ    seed,       0x00FF000C          | star placement random number
        .equ    sram_ram,   0x00FF0100          | v17m: the SRAM routine runs from here (RAM, 512 bytes)
        .equ    save_buf,   0x00FF0400          | v18: the 128-byte save block (64 words)
        .equ    mus_mode,   0x00FF0010          | v18: 0 silent, 1 FM
        .equ    mus_pal,    0x00FF0011          | v18: 1 = 50 Hz console (an extra music frame every 5th)
        .equ    mus_pal5,   0x00FF0012          | v18: PAL frame counter 0-4
        .equ    mus_wait,   0x00FF0014          | v18: frames left before the next commands
        .equ    mus_pos,    0x00FF0018          | v18: next stream byte (address)
        .equ    CD_Ok,      0x00FF0020          | v18: 1 = CD ready
        .equ    CD_Lvl2,    0x00FF0021          | v18: 1 = raise the Sub-CPU's level 2 interrupt each V-blank
        .equ    CD_State,   0x00FF0022          | v18: 0 not tried, 1 no Sega CD, 2 no answer, 3 ready
        .equ    YM_A0,      0xA04000            | YM2612 port 0 address / status
        .equ    YM_A1,      0xA04002            | YM2612 port 1 address
        .equ    PSG,        0xC00011

        .text
        .global _start
_start:                                         | 0x880800
        move.w  #0x2700,sr
        lea     0x00FFFFE0,sp
        lea     0x00FF0000,a0                   | v18x: clear our RAM (0xFF0000-0xFF0FFF).  A real
        move.w  #1023,d0                        | console powers up with random RAM (emulators: zeros);
9:      clr.l   (a0)+                           | a random CD_Ok sent the game to a Sega CD that was
        dbra    d0,9b                           | never started, and it hung at the starfield
        jmp     main

        .org    0x40
exception:                                      | 0x880840: any 68000 exception
        move.w  #0x2700,sr
        move.l  #0xC0000000,VDP_CTRL            | backdrop RED and display on: a crash is visible
        move.w  #0x000E,VDP_DATA
        move.w  #0x8144,VDP_CTRL
1:      bra.b   1b

        .org    0x80
hblank:                                         | 0x880880
        rte

        .org    0xC0
vblank:                                         | 0x8808C0
        jmp     vblank_handler

        .org    0x100
extint:                                         | 0x880900
        rte

| -------------------------------------------------------------------------------------------
main:
        | ---- Genesis video: starfield planes, display ON, V-blank interrupt ON ----
        lea     VDP_CTRL,a0
        move.w  #0x8004,(a0)
        move.w  #0x8134,(a0)                    | display off while we load, V-int on, mode 5
        move.w  #0x8230,(a0)                    | plane A name table at 0xC000
        move.w  #0x8407,(a0)                    | plane B name table at 0xE000
        move.w  #0x857C,(a0)                    | sprite table at 0xF800 (unused, kept empty)
        move.w  #0x8B00,(a0)                    | full-screen H and V scroll
        move.w  #0x8C81,(a0)                    | H40
        move.w  #0x8D3F,(a0)                    | H scroll table at 0xFC00
        move.w  #0x8F02,(a0)                    | auto-increment 2
        move.w  #0x9001,(a0)                    | planes 64 x 32 cells
        move.w  #0x9100,(a0)                    | no window
        move.w  #0x9200,(a0)
        move.w  #0x8700,(a0)                    | backdrop = colour 0
        bsr     stars_init
        move.w  #0x8174,(a0)                    | display on, V-int on

        | ---- controller ports ----
        move.b  #0x40,0xA10009
        move.b  #0x40,0xA1000B
        move.b  #0x40,0xA10003
        move.b  #0x40,0xA10005

        clr.w   ticks
        move.w  #0,pad1

        | ---- v17m: copy the SRAM routine to RAM.  It sets RV, which moves the cartridge, so it
        |      must not run from the cartridge window it is switching. ----
        lea     sram_blob,a1
        lea     sram_ram,a2
        move.w  #255,d0                         | 512 bytes (the routine is under that; no forward-label maths)
6:      move.w  (a1)+,(a2)+
        dbra    d0,6b

        | ---- 32X: wait for both SH2s (the boot ROM leaves "M_OK" / "S_OK"), then clear them -
        |      Kobo's SH2 start-up waits until COMM0 is no longer "M_OK" ----
1:      cmpi.l  #0x4D5F4F4B,COMM0               | "M_OK"
        bne.b   1b
2:      cmpi.l  #0x535F4F4B,COMM4               | "S_OK"
        bne.b   2b
        move.b  #0,0xA15107                     | RV off
        move.w  #0,0xA15104                     | cartridge bank 0
        move.l  #0,COMM0
        move.l  #0,COMM4

        | ---- v18: the Z80 is not used: hold its bus, so the 68000 can write the YM2612.  Its reset
        |      line also resets the YM2612, so release reset first. ----
        move.w  #0x100,0xA11100                 | bus request
        move.w  #0x100,0xA11200                 | reset off (YM2612 running)
7:      btst    #0,0xA11100                     | wait for the bus
        bne.b   7b
        clr.b   mus_mode
        clr.b   mus_pal5
        moveq   #0,d0
        btst    #0,VDP_CTRL+1                   | VDP status bit 0: PAL
        beq.b   8f
        moveq   #1,d0
8:      move.b  d0,mus_pal
        bsr     fm_silence

        move.w  #0x2000,sr                      | V-blank interrupts on
        | v18w: the Sega CD start-up is OFF for now - on real hardware with a Sega CD attached it
        | crashed the 68000 at power-on (red screen).  CD_State 1 = "no Sega CD": MUSIC is OFF.
        | Put "bsr CD_Init" back here once it is fixed and tested on hardware.
        .if HW_BUILD
        bsr     CD_Init                         | v40 HW: back on - CD_Init's BIOS check no longer reads
                                                |   a long at an odd address (that was the crash)
        .else
        move.b  #1,CD_State
        .endif

        | ---- Kobo's start-up handshake: SH2 writes 0x0AAA to COMM10, we answer 0x0BBB ----
3:      bsr     pause
        cmpi.w  #0x0AAA,COMM10
        bne.b   3b
        move.w  #0x0BBB,COMM10

| ---- command loop ---------------------------------------------------------------------------
loop:
        bsr     pause
        move.w  COMM0,d0
        beq.b   loop
        cmp.w   COMM0,d0                        | read twice - a torn read never matches
        bne.b   loop
        moveq   #0,d1
        move.b  d0,d1                           | low byte = command (high byte: effect id)
        cmpi.b  #48,d1
        beq.w   c_pad
        cmpi.b  #52,d1
        beq.w   c_text
        cmpi.b  #53,d1
        beq.w   c_zero12
        cmpi.b  #56,d1
        beq.w   c_zero12
        cmpi.b  #59,d1
        beq.w   c_zero12
        cmpi.b  #57,d1
        beq.w   c_zero8
        cmpi.b  #60,d1
        beq.w   c_sram_rd
        cmpi.b  #61,d1
        beq.w   c_sram_wr
        cmpi.b  #62,d1
        beq.w   c_music
        cmpi.b  #63,d1
        beq.w   c_cdstat
        cmpi.b  #64,d1
        beq.w   c_cdplay
        cmpi.b  #65,d1
        beq.w   c_blk_rd
        cmpi.b  #66,d1
        beq.w   c_blk_wr
        bra.w   done                            | 45, 54, 58, anything else: acknowledge

c_pad:
        move.w  pad1,d2
        swap    d2
        move.w  ticks,d2
        move.l  d2,COMM8                        | COMM8 = pad, COMM10 = frame counter
        bra.w   done
c_text:
        move.w  #0x8134,d2                      | off
        tst.w   COMM2
        beq.b   4f
        move.w  #0x8174,d2                      | on
4:      move.w  d2,VDP_CTRL
        bra.w   done
c_zero12:
        move.w  #0,COMM12                       | 0 = not available (no Sega CD yet)
        bra.w   done
c_zero8:
        move.w  #0,COMM8
        bra.w   done
c_sram_rd:                                      | v18: SRAM -> save block, checked here
        moveq   #0,d4
        jsr     sram_ram                        | d6/d5: v17m's old score, if any
        bsr     blk_sum                         | d0 = checksum of words 0-62
        lea     save_buf,a0
        cmpi.w  #0x5533,(a0)                    | "U3"
        bne.b   1f
        cmp.w   126(a0),d0
        bne.b   1f
        move.w  #1,COMM2
        bra.w   done
1:      tst.w   d6                              | not valid: v17m's single high score?
        beq.b   2f
        move.l  d5,COMM8
        move.w  #2,COMM2
        bra.w   done
2:      move.w  #0,COMM2
        bra.w   done
c_sram_wr:                                      | v18: save block -> SRAM
        lea     save_buf,a0
        move.w  #0x5533,(a0)
        bsr     blk_sum
        move.w  d0,126(a0)
        moveq   #1,d4
        jsr     sram_ram
done:
        move.w  #0,COMM0                        | command finished
        bra     loop

c_music:                                        | v18: COMM2 = 0 silence, 1 FM, 2 CD track COMM8
        move.w  COMM2,d2
        clr.b   mus_mode                        | the V-blank stops playing FM now
        bsr     fm_silence
        cmpi.w  #1,d2
        bne.b   1f
        tst.b   CD_Ok                           | FM: the CD stops
        beq.b   3f
        bsr     CD_Stop
3:      lea     music_stream+4,a0
        move.l  a0,mus_pos
        move.w  #0,mus_wait
        move.b  #1,mus_mode
        bra.w   done
1:      cmpi.w  #2,d2
        bne.b   4f
        move.w  #0x400,d0                       | Zelda 3 32X: a new song at full volume
        bsr     CD_Volume
        move.w  COMM8,d0                        | CD: this track, repeating
        move.b  #1,d1
        bsr     CD_PlayTrack
        bra.w   done
4:      cmpi.w  #3,d2                           | Zelda 3 32X: 3 = CD track COMM8, once
        bne.b   5f
        move.w  #0x400,d0
        bsr     CD_Volume
        move.w  COMM8,d0
        move.b  #0,d1
        bsr     CD_PlayTrack
        bra.w   done
5:      cmpi.w  #4,d2                           | Zelda 3 32X: 4 = the CD music's volume COMM8
        bne.b   2f
        move.w  COMM8,d0
        bsr     CD_Volume
        bra.w   done
2:      tst.b   CD_Ok                           | silence: the CD too
        beq.w   done
        bsr     CD_Stop
        bra.w   done
c_cdstat:
        moveq   #0,d0
        tst.b   CD_Ok
        beq.b   1f
        bsr     CD_TrackCount
1:      move.w  d0,COMM8
        moveq   #0,d0
        move.b  CD_State,d0
        move.w  d0,COMM2
        bra.w   done
c_cdplay:
        move.w  COMM2,d2
        cmpi.w  #0x50,d2                        | 'P'
        bne.b   1f
        clr.b   mus_mode                        | the CD player takes over from FM
        bsr     fm_silence
        move.w  COMM8,d0
        move.b  #0xFF,d1                        | from this track on
        bsr     CD_PlayTrack
        bra.w   done
1:      cmpi.w  #0x53,d2                        | 'S'
        bne.b   2f
        bsr     CD_Stop
        bra.w   done
2:      cmpi.w  #0x5A,d2                        | 'Z'
        bne.w   done
        bsr     CD_PauseResume
        bra.w   done
c_blk_rd:
        move.w  COMM2,d0
        andi.w  #63,d0
        add.w   d0,d0
        lea     save_buf,a0
        move.w  0(a0,d0.w),COMM8
        bra.w   done
c_blk_wr:
        move.w  COMM2,d0
        andi.w  #63,d0
        add.w   d0,d0
        lea     save_buf,a0
        move.w  COMM8,0(a0,d0.w)
        bra.w   done
blk_sum:                                        | d0 = 0x5A5A + words 0..62 of the save block
        movem.l d1/a0,-(sp)
        lea     save_buf,a0
        move.w  #0x5A5A,d0
        moveq   #62,d1
1:      add.w   (a0)+,d0
        dbra    d1,1b
        movem.l (sp)+,d1/a0
        rts

| ---- v18: FM music ------------------------------------------------------------------------------
fm_write0:                                      | d0 = register, d1 = value (port 0)
        btst    #7,YM_A0                        | wait while the YM2612 is busy
        bne.b   fm_write0
        move.b  d0,YM_A0
        move.b  d1,YM_A0+1
        rts
fm_write1:                                      | port 1 (channels 4-6)
        btst    #7,YM_A0
        bne.b   fm_write1
        move.b  d0,YM_A1
        move.b  d1,YM_A1+1
        rts
fm_silence:                                     | every channel off: key off, no output, PSG off
        movem.l d0-d2/a0,-(sp)
        lea     fm_keyoff,a0
        moveq   #5,d2
1:      moveq   #0x28,d0
        move.b  (a0)+,d1
        bsr     fm_write0
        dbra    d2,1b
        moveq   #0,d1
        moveq   #2,d2
        move.b  #0xB4,d0
2:      bsr     fm_write0                       | L/R off: the sound stops at once
        bsr     fm_write1
        addq.b  #1,d0
        dbra    d2,2b
        move.b  #0x2B,d0                        | DAC off
        moveq   #0,d1
        bsr     fm_write0
        move.b  #0x9F,PSG                       | the four PSG voices silent
        move.b  #0xBF,PSG
        move.b  #0xDF,PSG
        move.b  #0xFF,PSG
        movem.l (sp)+,d0-d2/a0
        rts
fm_keyoff:
        .byte   0x00, 0x01, 0x02, 0x04, 0x05, 0x06
        .align  2
| one 60 Hz frame of the stream; called from the V-blank (d0-d2/a0-a1 saved there)
mus_tick:
        tst.b   mus_mode
        beq.b   9f
        tst.w   mus_wait
        beq.b   1f
        subq.w  #1,mus_wait
        bne.b   9f
1:      movea.l mus_pos,a1
2:      move.b  (a1)+,d2
        cmpi.b  #0x52,d2
        bne.b   3f
        move.b  (a1)+,d0
        move.b  (a1)+,d1
        bsr     fm_write0
        bra.b   2b
3:      cmpi.b  #0x53,d2
        bne.b   4f
        move.b  (a1)+,d0
        move.b  (a1)+,d1
        bsr     fm_write1
        bra.b   2b
4:      cmpi.b  #0x50,d2
        bne.b   5f
        move.b  (a1)+,PSG
        bra.b   2b
5:      cmpi.b  #0x61,d2
        bne.b   6f
        move.b  (a1)+,d0                        | frames to wait (big-endian)
        lsl.w   #8,d0
        move.b  (a1)+,d0
        move.w  d0,mus_wait
        move.l  a1,mus_pos
9:      rts
6:      lea     music_stream,a0                 | 0x66 (or anything else): back to the loop point
        movea.l a0,a1
        adda.l  (a0),a1
        addq.l  #4,a1
        bra.b   2b

pause:                                          | ~0.25 ms between COMM0 reads
        move.w  #200,d3
5:      dbra    d3,5b
        rts

| ---- V-blank: read pad 1 (3/6-button, same routine as the CD version), count frames ---------
vblank_handler:
        movem.l d0-d2/a0-a1,-(sp)
        tst.b   CD_Lvl2                         | v18: the Sega CD BIOS needs a level 2
        beq.b   1f                              |      interrupt every frame
        ori.w   #0x0100,0xA12000
1:      lea     0xA10003,a0
        bsr.b   read_pad
        move.w  d0,pad1
        addq.w  #1,ticks
        bsr     mus_tick                        | v18: FM music
        tst.b   mus_pal                         | 50 Hz: one extra music frame every 5th
        beq.b   2f
        addq.b  #1,mus_pal5
        cmpi.b  #5,mus_pal5
        bcs.b   2f
        clr.b   mus_pal5
        bsr     mus_tick
2:      movem.l (sp)+,d0-d2/a0-a1
        rte

read_pad:
        bsr.b   get_input
        move.w  d0,d1
        andi.w  #0x0C00,d0
        bne.b   rp_none
        bsr.b   get_input
        bsr.b   get_input
        move.w  d0,d2
        bsr.b   get_input
        andi.w  #0x0F00,d0
        cmpi.w  #0x0F00,d0
        beq.b   rp_common
        move.w  #0x010F,d2
rp_common:
        lsl.b   #4,d2
        lsl.w   #4,d2
        andi.w  #0x303F,d1
        move.b  d1,d2
        lsr.w   #6,d1
        or.w    d1,d2
        eori.w  #0x1FFF,d2
        move.w  d2,d0
        rts
rp_none:
        move.w  #0xF000,d0
        rts

get_input:
        move.b  #0x00,(a0)
        nop
        nop
        move.b  (a0),d0
        move.b  #0x40,(a0)
        lsl.w   #8,d0
        move.b  (a0),d0
        rts

| ---- starfield ------------------------------------------------------------------------------
| colours: palette 0 = far (dim), palette 1 = near (bright).  Format 0000 BBB0 GGG0 RRR0.
star_cram:
        .word   0x0000, 0x0444, 0x0666, 0x0864, 0,0,0,0, 0,0,0,0, 0,0,0,0
        .word   0x0000, 0x0AAA, 0x0EEE, 0x0EA8, 0,0,0,0, 0,0,0,0, 0,0,0,0
| tiles 1-4 (4 bits per pixel, 8 rows of 4 bytes): a few single stars and one twinkle cross
star_tiles:
        .long   0x00000000,0x00000000,0x00000000,0x00010000,0x00000000,0x00000000,0x00000000,0x00000000
        .long   0x00000000,0x00000000,0x00000200,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000
        .long   0x00000000,0x00000000,0x00030000,0x00323000,0x00030000,0x00000000,0x00000000,0x00000000
        .long   0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,0x02000000,0x00000000
star_tiles_end:

stars_init:                                     | a0 = VDP_CTRL
        movem.l d0-d4/a1-a2,-(sp)
        lea     VDP_DATA,a1
        move.l  #0xC0000000,(a0)                | CRAM 0: the two star palettes
        lea     star_cram,a2
        moveq   #31,d0
1:      move.w  (a2)+,(a1)
        dbra    d0,1b
        move.l  #0x40000000,(a0)                | VRAM 0: tile 0 = empty, tiles 1-4 = stars
        moveq   #7,d0
2:      move.l  #0,(a1)
        dbra    d0,2b
        lea     star_tiles,a2
        moveq   #31,d0
3:      move.l  (a2)+,(a1)
        dbra    d0,3b
        move.l  #0x40000003,(a0)                | Zelda: planes A (0xC000) and B (0xE000) empty
        bsr.b   clear_plane
        move.l  #0x60000003,(a0)
        bsr.b   clear_plane
        move.l  #0x40000010,(a0)                | V scroll A, B = 0
        move.l  #0,(a1)
        move.l  #0x7C000003,(a0)                | H scroll A, B = 0
        move.l  #0,(a1)
        clr.l   ax
        clr.l   bx
        movem.l (sp)+,d0-d4/a1-a2
        rts

clear_plane:                                    | 64 x 32 cells of tile 0 (empty)
        move.w  #64*32-1,d4
1:      move.w  #0,(a1)
        dbra    d4,1b
        rts

| ---- SRAM (header "RA", 8 KB on odd bytes at 0x200001) -----------------------------------------
| Copied to RAM at start-up and run from there (it sets RV, which moves the cartridge).
|   d4 = 0: load - the 128-byte save block (v18) from 0x200101.. into save_buf, and v17m's
|           old high-score block from 0x200001: d6 = 1 if that one is valid, d5 = its score
|   d4 = 1: save - save_buf -> 0x200101..
| v17m's block, one byte per odd address: 'U' 'R' s3 s2 s1 s0 sum ~sum.
| With RV = 1 the 68000 sees the cartridge at 0x000000-0x3FFFFF (SRAM at 0x200000); the SH2s
| must not read the ROM meanwhile - the master waits on COMM0, and only asks between games.
        .align  2
sram_blob:
        move.w  sr,-(sp)
        move.w  #0x2700,sr                      | no interrupts while the cartridge is moved
        move.b  #1,0xA15107                     | RV on
        move.b  #1,0xA130F1                     | SRAM on, writable
        lea     0x200101,a2                     | the v18 block
        lea     save_buf,a3
        move.w  #127,d3
        tst.w   d4
        bne.b   5f
1:      move.b  (a2),(a3)+                      | ---- load ----
        addq.l  #2,a2
        dbra    d3,1b
        lea     0x200001,a2                     | v17m's old block
        moveq   #0,d6
        cmpi.b  #0x55,(a2)                      | 'U'
        bne.b   9f
        cmpi.b  #0x52,2(a2)                     | 'R'
        bne.b   9f
        lea     4(a2),a3
        moveq   #0,d5
        moveq   #0,d7
        moveq   #3,d3
2:      lsl.l   #8,d5
        move.b  (a3),d2
        move.b  d2,d5
        add.b   d2,d7
        addq.l  #2,a3
        dbra    d3,2b
        cmp.b   (a3),d7
        bne.b   9f
        not.b   d7
        cmp.b   2(a3),d7
        bne.b   9f
        moveq   #1,d6
        bra.b   9f
5:      move.b  (a3)+,(a2)                      | ---- save ----
        addq.l  #2,a2
        dbra    d3,5b
9:      move.b  #0,0xA130F1                     | SRAM off (write-protected, ROM mapped)
        move.b  #0,0xA15107                     | RV off: the SH2s see the ROM again
        move.w  (sp)+,sr
        rts
        .align  2
sram_blob_end:                                  | must stay under 512 bytes

        .include "cd.s"
        .include "kos.s"
        .align  2
music_stream:                                   | tools/make_music.py output (from cart/music/*.vgm)
        .incbin "music_stream.bin"
        .align  2
