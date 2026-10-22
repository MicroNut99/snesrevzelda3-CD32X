! ===========================================================================================
! U-R-R (32X cartridge) - SH2 start-up for a 32X CARTRIDGE (both SH2s).        (sh2/crt0.s)
! Modelled on Chilly Willy's cartridge crt0 (MIT): the boot ROM copies the program to SDRAM,
! leaves "M_OK" in COMM0 and "S_OK" in COMM4; the 68000 (cart/cart_md.s) clears both when it
! is ready; only then do the SH2s run C code.  The master keeps interrupts MASKED (SR = 0xF0)
! and polls; the slave opens level 10 for the 32X H interrupt, which plays the sound (one PWM
! sample per line).  Any other CPU exception parks the CPU.
!
! The first 0x38 bytes are a boot table in the same layout as Kobo's CD build, so
! tools/make_cart.py can copy its values into the 32X cartridge header:
!   +0x18 destination  +0x1C size  +0x20 master entry  +0x24 slave entry
!   +0x28 master VBR   +0x2C slave VBR
! ===========================================================================================
        .section .header,"ax"
        .ascii  "ZELDA3 32X      "      /* +0x00 module name (16) */
        .long   0                       /* +0x10 version */
        .long   0                       /* +0x14 source (unused) */
        .long   0x06000000              /* +0x18 destination in SDRAM */
        .long   __code_size             /* +0x1C bytes to copy */
        .long   master_start            /* +0x20 */
        .long   slave_start             /* +0x24 */
        .long   master_vbr              /* +0x28 */
        .long   slave_vbr               /* +0x2C */
        .long   0, 0                    /* +0x30 .. +0x37 */

        .text
        .align  2
! ---- vector tables (VBR): power-on PC/SP, manual reset PC/SP, then 124 exception vectors --
master_vbr:
        .long   master_start, 0x0603F000, master_start, 0x0603F000
        .rept   124
        .long   cpu_exception
        .endr
slave_vbr:
        .long   slave_start, 0x06040000, slave_start, 0x06040000
        .rept   60                      /* vectors 4 .. 63 */
        .long   cpu_exception
        .endr
        .rept   5                       /* vectors 64 .. 68: IRL 1-9 (v18z: a stray one is acknowledged, not a crash) */
        .long   irq_ignore
        .endr
        .long   slave_hint              /* vector 69: 32X H interrupt (IRL level 10) = sound */
        .rept   2                       /* vectors 70, 71: IRL 12-15 (V, VRES) - acknowledged, ignored */
        .long   irq_ignore
        .endr
        .rept   56                      /* vectors 72 .. 127 */
        .long   cpu_exception
        .endr

cpu_exception:                          ! park; the game screen simply stops
        bra     cpu_exception
        nop

! ---- v18z: a stray interrupt (real hardware only) - acknowledge every 32X source, return ----
irq_ignore:
        mov.l   r0,@-r15
        mov.l   r1,@-r15
        mov.l   frt_tocr,r1             ! the 32X interrupt hand-shake (as Kobo / Chilly Willy):
        mov     #0xE0,r0                !   TOCR output 0 ...
        mov.b   r0,@r1
        mov.b   @r1,r0
        mov     #0xE2,r0                !   ... then output 1
        mov.b   r0,@r1
        mov.b   @r1,r0
        mov.l   int_clr,r1              ! clear VRES, V, H, CMD, PWM
        mov.w   r0,@r1
        mov.w   r0,@(2,r1)
        mov.w   r0,@(4,r1)
        mov.w   r0,@(6,r1)
        mov.w   r0,@(8,r1)
        mov.w   @r1,r0
        nop
        nop
        mov.l   @r15+,r1
        mov.l   @r15+,r0
        rte
        nop
        .align  2
frt_tocr: .long 0xFFFFFE17
int_clr:  .long 0x20004014

! ---- slave H interrupt: one PWM sample per video line (C does the work) ------------------
! v18z: on real hardware the 32X interrupt needs the SH2's FRT output (TOCR) toggled and the
! H interrupt cleared at once, with a few cycles before the return - without it the next
! interrupt came in at the wrong level (vector 70) and the slave crashed after one sample.
slave_hint:
        sts.l   pr,@-r15
        mov.l   r0,@-r15
        mov.l   r1,@-r15
        mov.l   frt_tocr2,r1
        mov     #0xE0,r0
        mov.b   r0,@r1
        mov.b   @r1,r0
        mov     #0xE2,r0
        mov.b   r0,@r1
        mov.b   @r1,r0
        mov.l   hint_clr,r1
        mov.w   r0,@r1                  ! clear H
        nop
        nop
        nop
        nop
        mov.l   r2,@-r15
        mov.l   r3,@-r15
        mov.l   r4,@-r15
        mov.l   r5,@-r15
        mov.l   r6,@-r15
        mov.l   r7,@-r15
        sts.l   mach,@-r15
        sts.l   macl,@-r15
        mov.l   hint_c,r0
        jsr     @r0
        nop
        lds.l   @r15+,macl
        lds.l   @r15+,mach
        mov.l   @r15+,r7
        mov.l   @r15+,r6
        mov.l   @r15+,r5
        mov.l   @r15+,r4
        mov.l   @r15+,r3
        mov.l   @r15+,r2
        mov.l   @r15+,r1
        mov.l   @r15+,r0
        lds.l   @r15+,pr
        rte
        nop
        .align  2
hint_c: .long   _slave_hint_c
frt_tocr2: .long 0xFFFFFE17
hint_clr:  .long 0x20004018

! ---- v18z: the FRT set up as the 32X needs it (Kobo / Chilly Willy crt0), both SH2s ------
frt_setup:
        mov.l   frt_base,r1
        mov     #0x00,r0
        mov.b   r0,@(0x00,r1)           ! TIER: no FRT interrupts
        mov     #0xE2,r0
        mov.b   r0,@(0x07,r1)           ! TOCR: OCRA, output 1 on compare match
        mov     #0x00,r0
        mov.b   r0,@(0x04,r1)           ! OCR high
        mov     #0x01,r0
        mov.b   r0,@(0x05,r1)           ! OCR low: OCRA = 1
        mov     #0,r0
        mov.b   r0,@(0x06,r1)           ! TCR
        mov     #1,r0
        mov.b   r0,@(0x01,r1)           ! TCSR: clear FRC on match
        mov     #0x00,r0
        mov.b   r0,@(0x03,r1)
        mov.b   r0,@(0x02,r1)           ! FRC = 0
        mov.l   int_clr2,r1             ! clear any 32X interrupt left from the boot ROM
        mov.w   r0,@r1
        mov.w   r0,@(2,r1)
        mov.w   r0,@(4,r1)
        mov.w   r0,@(6,r1)
        mov.w   r0,@(8,r1)
        rts
        nop
        .align  2
frt_base: .long 0xFFFFFE10
int_clr2: .long 0x20004014

! ---- master SH2 -----------------------------------------------------------------------
master_start:
        mov.l   m_sr,r0
        ldc     r0,sr                   /* all interrupts masked */
        mov.l   m_stk,r15
        mov.l   m_vbr,r0
        ldc     r0,vbr
        mov.l   p_frt,r0                /* v18z */
        jsr     @r0
        nop
        mov.l   cctl,r0
        mov     #0x10,r1                /* purge cache, cache off while clearing bss */
        mov.b   r1,@r0

        mov     #0,r0                   ! clear .bss (byte loop: any alignment)
        mov.l   bss_s,r1
        mov.l   bss_e,r2
        cmp/eq  r1,r2
        bt      2f
1:      mov.b   r0,@r1
        add     #1,r1
        cmp/eq  r1,r2
        bf      1b
2:
        mov.l   comm0,r0                ! wait until the 68000 has cleared "M_OK"
        mov.l   m_ok,r1
3:      mov.l   @r0,r2
        nop
        nop
        cmp/eq  r1,r2
        bt      3b

        mov     #0x80,r0                /* FM = 1: the SH2s own the 32X VDP */
        mov.l   adapter,r1
        mov.b   r0,@r1

        mov.l   cctl,r0
        mov     #0x11,r1                /* purge + cache on */
        mov.b   r1,@r0

        mov.l   go_slave,r0             /* bss is clear: the slave may start C code */
        mov     #1,r1
        mov.l   r1,@r0

        mov.l   m_main,r0
        jsr     @r0
        nop
4:      bra     4b
        nop

! ---- slave SH2 ------------------------------------------------------------------------
slave_start:
        mov.l   m_sr,r0
        ldc     r0,sr
        mov.l   s_stk,r15
        mov.l   s_vbr,r0
        ldc     r0,vbr
        mov.l   p_frt,r0                /* v18z */
        jsr     @r0
        nop
        mov.l   comm4,r0                ! wait until the 68000 has cleared "S_OK"
        mov.l   s_ok,r1
5:      mov.l   @r0,r2
        nop
        nop
        cmp/eq  r1,r2
        bt      5b
        mov.l   go_slave,r0             ! wait for the master's "bss clear" flag
6:      mov.l   @r0,r1
        tst     r1,r1
        bt      6b
        mov.l   cctl,r0
        mov     #0x11,r1
        mov.b   r1,@r0
        mov.l   s_main,r0
        jsr     @r0
        nop
7:      bra     7b
        nop

        .align  2
m_sr:   .long   0x000000F0
p_frt:  .long   frt_setup
m_stk:  .long   0x0603F000
s_stk:  .long   0x06040000
m_vbr:  .long   master_vbr
s_vbr:  .long   slave_vbr
cctl:   .long   0xFFFFFE92
bss_s:  .long   __bss_start
bss_e:  .long   __bss_end
comm0:  .long   0x20004020
comm4:  .long   0x20004024
m_ok:   .ascii  "M_OK"
s_ok:   .ascii  "S_OK"
adapter:.long   0x20004000
go_slave:.long  _slave_go_flag + 0x20000000     /* cache-through alias */
m_main: .long   _main
s_main: .long   _secondary

        .global _start
_start:
        bra     master_start
        nop
