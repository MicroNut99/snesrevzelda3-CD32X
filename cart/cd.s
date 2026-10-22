| ===========================================================================================
| U-R-R v18: CD audio from a Sega CD in Mode 1 (U-R-R in the cartridge slot, a Sega CD
| underneath, an audio CD in the drive).  Taken from the Sonic32XPlus1 cartridge (MicroNut99,
| tails/cd.asm + tails/cd_sub.asm), which is Chilly Willy's InitCD and Sub-CPU program from Vic's
| YATSSD (src-md/main.c, cd.s) - the method D32XR uses.  Converted here from asm68k to GNU as;
| the Sega CD BIOS is unpacked with the Kosinski decoder in cart/kos.s.
|
| In Mode 1 the Sega CD sits at 0x400000: BIOS ROM 0x400000, program RAM window 0x420000,
| Word RAM 0x600000.  CD_Init finds the BIOS, unpacks the Sub-CPU BIOS into program RAM, copies
| the Sub-CPU program (CD_Sub below) to 0x6000 and starts it; from then on every V-blank raises
| the Sub-CPU's level 2 interrupt (cart_md.s vblank_handler).
| State in CD_State: 0 not tried, 1 no Sega CD (no BIOS), 2 no answer from the Sub-CPU, 3 ready.
| ===========================================================================================

CD_Init:
        clr.b   CD_Ok
        clr.b   CD_Lvl2
        .if HW_BUILD
| v40 HW: the BIOS name at 0x6D(a0) is read BYTE BY BYTE.  0x6D is odd: a long read there is an
| ADDRESS ERROR on a real 68000 (the v18w red screen with a Sega CD attached); emulators allow it.
| The same fix as the Sonic32XPlus1 real-hardware build (mars/cd.asm, CD_IdIs).
        move.l  #0x53454741,d2                  | "SEGA"
        lea     0x415800,a0                     | where the Sega CD BIOS can be (by model)
        bsr.b   CD_IdIs
        beq.b   CD_BiosFound
        lea     0x416000,a0
        bsr.b   CD_IdIs
        beq.b   CD_BiosFound
        move.l  #0x574F4E44,d2                  | "WOND": WonderMega / X'Eye
        bsr.b   CD_IdIs
        beq.b   CD_BiosFound
        lea     0x41AD00,a0                     | LaserActive
        move.l  #0x53454741,d2
        bsr.b   CD_IdIs
        beq.b   CD_BiosFound
        move.b  #1,CD_State                     | no Sega CD
        rts
CD_IdIs:                                        | Z set if the 4 bytes at 0x6D(a0) are d2
        move.b  0x6D(a0),d1
        lsl.l   #8,d1
        move.b  0x6E(a0),d1
        lsl.l   #8,d1
        move.b  0x6F(a0),d1
        lsl.l   #8,d1
        move.b  0x70(a0),d1
        cmp.l   d2,d1
        rts
        .else
        lea     0x415800,a0                     | where the Sega CD BIOS can be (by model)
        cmpi.l  #0x53454741,0x6D(a0)            | "SEGA"
        beq.b   CD_BiosFound
        lea     0x416000,a0
        cmpi.l  #0x53454741,0x6D(a0)
        beq.b   CD_BiosFound
        cmpi.l  #0x574F4E44,0x6D(a0)            | "WOND": WonderMega / X'Eye
        beq.b   CD_BiosFound
        lea     0x41AD00,a0                     | LaserActive
        cmpi.l  #0x53454741,0x6D(a0)
        beq.b   CD_BiosFound
        move.b  #1,CD_State                     | no Sega CD
        rts
        .endif
CD_BiosFound:
        move.l  a0,-(sp)
        move.w  #0xFF00,0xA12002                | reset the gate array (this exact order)
        move.b  #3,0xA12001
        move.b  #2,0xA12001
        move.b  #0,0xA12001
1:      move.b  #2,0xA12001                     | reset the Sub-CPU, request its bus
        btst    #1,0xA12001
        beq.b   1b
        move.w  #2,0xA12002                     | no write protection, bank 0, 2M
        lea     0x420000,a1                     | clear program RAM bank 0
        move.w  #0x20000/4-1,d1
2:      clr.l   (a1)+
        dbra    d1,2b
        movea.l (sp)+,a0                        | unpack the Sub-CPU BIOS to 0x0000
        move.l  #0x420000,-(sp)
        move.l  a0,-(sp)
        jsr     Kos_Decomp
        addq.l  #8,sp
        lea     CD_Sub,a0                       | our Sub-CPU program to 0x6000
        lea     0x426000,a1
        move.w  cd_sub_words,d1
3:      move.w  (a0)+,(a1)+
        dbra    d1,3b
        move.b  #0,0xA1200E                     | main comm port clear
        move.b  #0x2A,0xA12002                  | write-protect up to 0x5400
4:      move.b  #1,0xA12001                     | let the Sub-CPU run
        btst    #0,0xA12001
        beq.b   4b
        move.b  #1,CD_Lvl2                      | the BIOS needs level 2 interrupts
        move.l  #2000000,d1
5:      cmpi.b  #0x49,0xA1200F                  | 'I': our program is running
        beq.b   6f
        subq.l  #1,d1
        bne.b   5b
        clr.b   CD_Lvl2                         | no answer
        move.b  #2,CD_State
        rts
6:      move.l  #4000000,d1
7:      tst.b   0xA1200F                        | ... and ready for commands
        beq.b   8f
        subq.l  #1,d1
        bne.b   7b
        move.b  #2,CD_State
        rts
8:      move.b  #1,CD_Ok
        move.b  #3,CD_State
        rts
cd_sub_words:
        .word   (CD_SubEnd-CD_Sub)/2-1

| d0.b = command (D T P S Z C) -> d0.b = 'D' done, 'E' error, 0 no CD / no answer
CD_Cmd:
        tst.b   CD_Ok
        beq.b   3f
        move.l  #300000,d1
1:      tst.b   0xA1200F                        | the Sub-CPU is ready
        beq.b   2f
        subq.l  #1,d1
        bne.b   1b
        bra.b   3f
2:      move.b  d0,0xA1200E
        move.l  #3000000,d1
4:      move.b  0xA1200F,d0                     | its answer
        bne.b   5f
        subq.l  #1,d1
        bne.b   4b
        move.b  #0,0xA1200E
3:      moveq   #0,d0
        rts
5:      move.b  #0,0xA1200E                     | seen
        rts

CD_TrackCount:                                  | d0 = the last track's number (0 = no CD)
        moveq   #0x44,d0                        | 'D'
        bsr.b   CD_Cmd
        cmpi.b  #0x44,d0
        beq.b   1f
        moveq   #0,d0
        rts
1:      moveq   #0,d0
        move.b  0xA12023,d0                     | last track (sub status word 0x8022: first, last)
        rts

CD_PlayTrack:                                   | d0.w = track (1 ...), d1.b = -1 from it on, 0 once, else repeat
        tst.b   CD_Ok
        beq.b   1f
        move.w  d0,0xA12010
        move.b  d1,0xA12012
        moveq   #0x50,d0                        | 'P'
        bra.b   CD_Cmd
1:      rts

CD_Stop:
        moveq   #0x53,d0                        | 'S'
        bra.b   CD_Cmd

CD_Volume:                                      | Zelda 3 32X: d0.w = volume 0 .. 0x400
        tst.b   CD_Ok
        beq.b   1f
        move.w  d0,0xA12010
        moveq   #0x56,d0                        | 'V'
        bra.w   CD_Cmd
1:      rts

CD_PauseResume:
        moveq   #0x5A,d0                        | 'Z'
        bra.w   CD_Cmd

| -------------------------------------------------------------------------------------------
| The Sub-CPU program (copied to its program RAM at 0x6000).  Commands in the main comm port
| (main 0xA1200E = sub 0x800E): D disc info, T track info, P play (track in 0x8010, flag byte
| 0x8012: -1 from it on, 0 once, else repeat), S stop, Z pause / go on, C check disc,
| V volume (0x8010: 0 .. 0x400 - Zelda 3 32X).  Result in
| the sub comm port (0x800F): D done, E error; the main CPU then clears its port.
| Short addresses 0x8000-0x80FF are the sign-extended 0xFFFF80xx.
| -------------------------------------------------------------------------------------------
        .align  2
CD_Sub:
SPHeader:
        .ascii  "MAIN-SUBCPU"
        .byte   0
        .word   0x0001, 0x0000
        .long   0x00000000
        .long   0x00000000
        .long   SPHeaderOffsets-SPHeader
        .long   0x00000000
SPHeaderOffsets:
        .word   SPInit-SPHeaderOffsets
        .word   SPMain-SPHeaderOffsets
        .word   SPInt2-SPHeaderOffsets
        .word   SPNull-SPHeaderOffsets
        .word   0x0000

SPInit:
        move.b  #0x49,0x800F.w                  | sub comm port = 'I' (initializing)
        andi.b  #0xE2,0x8003.w                  | priority mode off, 2M, Sub-CPU has Word RAM
        rts

SPMain:
        move.w  #0x0081,d0                      | CDBSTAT
        jsr     0x5F22.w
        move.w  (a0),d0                         | BIOS status word
        bmi.b   1f                              | not ready
        lsr.w   #8,d0
        cmpi.b  #0x40,d0
        beq.b   2f                              | tray open
        cmpi.b  #0x10,d0
        beq.b   2f                              | no disc
1:      lea     drive_init_parms(pc),a0
        move.w  #0x0010,d0                      | DRVINIT
        jsr     0x5F22.w
        move.w  #0x0089,d0                      | CDCSTOP
        jsr     0x5F22.w
2:      move.b  #0,0x800F.w                     | sub comm port = READY

WaitCmd:
        tst.b   0x800E.w
        beq.b   WaitCmd
        cmpi.b  #0x44,0x800E.w                  | D
        beq.w   GetDiscInfo
        cmpi.b  #0x54,0x800E.w                  | T
        beq.w   GetTrackInfo
        cmpi.b  #0x50,0x800E.w                  | P
        beq.w   PlayTrack
        cmpi.b  #0x53,0x800E.w                  | S
        beq.w   StopPlaying
        cmpi.b  #0x5A,0x800E.w                  | Z
        beq.w   PauseResume
        cmpi.b  #0x43,0x800E.w                  | C
        beq.w   CheckDisc
        cmpi.b  #0x56,0x800E.w                  | V (Zelda 3 32X: the music's volume)
        beq.w   SetVolume
        move.b  #0x45,0x800F.w                  | 'E' error
WaitAck:
        tst.b   0x800E.w
        bne.b   WaitAck                         | until the main CPU has seen the result
        move.b  #0,0x800F.w                     | READY
        bra.b   WaitCmd

GetDiscInfo:
        move.w  #0x0081,d0                      | CDBSTAT
        jsr     0x5F22.w
        move.w  (a0),0x8020.w                   | BIOS status word
        move.w  16(a0),0x8022.w                 | first track, last track
        move.w  18(a0),0x8024.w                 | drive version, flag
        move.b  #0x44,0x800F.w
        bra.w   WaitAck

GetTrackInfo:
        move.w  0x8010.w,d1                     | track number
        move.w  #0x0083,d0                      | CDBTOCREAD
        jsr     0x5F22.w
        move.l  d0,0x8020.w                     | MMSSFFTN
        move.b  d1,0x8024.w                     | track type
        move.b  #0x44,0x800F.w
        bra.w   WaitAck

PlayTrack:
        move.w  #0x0002,d0                      | MSCSTOP
        jsr     0x5F22.w
        move.w  0x8010.w,d1                     | track number
        move.w  #0x0011,d0                      | MSCPLAY - from this track on
        move.b  0x8012.w,d2                     | flag
        bmi.b   2f
        beq.b   1f
        move.w  #0x0013,d0                      | MSCPLAYR - repeat
        bra.b   2f
1:      move.w  #0x0012,d0                      | MSCPLAY1 - once
2:      lea     track_number(pc),a0
        move.w  d1,(a0)
        jsr     0x5F22.w
        move.b  #0x44,0x800F.w
        bra.w   WaitAck

StopPlaying:
        move.w  #0x0002,d0                      | MSCSTOP
        jsr     0x5F22.w
        move.b  #0x44,0x800F.w
        bra.w   WaitAck

PauseResume:
        move.w  #0x0081,d0                      | CDBSTAT
        jsr     0x5F22.w
        move.b  (a0),d0
        cmpi.b  #1,d0
        beq.b   1f                              | playing: pause
        cmpi.b  #5,d0
        beq.b   2f                              | paused: go on
        move.b  #0x45,0x800F.w
        bra.w   WaitAck
1:      move.w  #0x0003,d0                      | MSCPAUSEON
        jsr     0x5F22.w
        move.b  #0x44,0x800F.w
        bra.w   WaitAck
2:      move.w  #0x0004,d0                      | MSCPAUSEOFF
        jsr     0x5F22.w
        move.b  #0x44,0x800F.w
        bra.w   WaitAck

SetVolume:                                      | Zelda 3 32X: 0x8010 = volume 0 .. 0x400
        move.w  0x8010.w,d1
        move.w  #0x0085,d0                      | FDRSET: the CD audio fader
        jsr     0x5F22.w
        move.b  #0x44,0x800F.w
        bra.w   WaitAck

CheckDisc:
        lea     drive_init_parms(pc),a0
        move.w  #0x0010,d0                      | DRVINIT
        jsr     0x5F22.w
        move.w  #0x0089,d0                      | CDCSTOP
        jsr     0x5F22.w
        move.b  #0x44,0x800F.w
        bra.w   WaitAck

SPInt2:
        rts
SPNull:
        rts

        .align  2
drive_init_parms:
        .byte   0x01, 0xFF                      | first track 1, last track: all
track_number:
        .word   0
        .align  2
CD_SubEnd:
