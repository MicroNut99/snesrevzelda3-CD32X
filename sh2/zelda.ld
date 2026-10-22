/* Zelda 3 32X - SH2 program layout (a 32X cartridge).
 *   file 0x0000  the 0x38-byte boot table (crt0.s .header; tools/make_cart.py copies it into the
 *                32X header)
 *   SDRAM        crt0 + the hot code (.sdram) + .data: copied to 0x06000000 by the 32X boot ROM;
 *                then .bss, the heap (malloc: the PPU, DMA, save RAM) and the stacks
 *   ROM          the rest of the code, the read-only data and the game's assets, run / read in
 *                place: file offset F is SH2 address 0x02007FC8 + F (make_cart.py puts file
 *                0x38 at ROM 0x8000; the SH2 sees the cartridge at 0x02000000, cached) */
OUTPUT_ARCH(sh)
ENTRY(_start)
SECTIONS {
    .header 0x00000000 : { KEEP(*(.header)) }
    .sdram 0x06000000 : AT (0x38) {
        KEEP(*z32_crt0.o(.text))
        *(.sdram) *(.sdram.*)
        . = ALIGN(4);
    }
    .data : AT (0x38 + SIZEOF(.sdram)) {
        *(.data) *(.data.*) *(.sdata) *(.sdata.*) . = ALIGN(4);
    }
    __code_size = SIZEOF(.sdram) + SIZEOF(.data);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss) *(.bss.*) *(.sbss) *(.sbss.*) *(COMMON) . = ALIGN(16);
        __bss_end = .;
    }
    __heap_start = .;
    __heap_end = 0x0603C000;                         /* master stack below 0x0603F000, 12 KB */
    ___heap_start = __heap_start;                    /* the C names (SH ELF: one more underscore) */
    ___heap_end = __heap_end;
    _end = __heap_start; _edata = __bss_start; _stack = 0x0603F000;   /* newlib's references */
    ASSERT(__heap_start <= __heap_end, "Zelda 3 32X: .data + .bss do not fit in SDRAM")
    .text (0x02008000 + SIZEOF(.sdram) + SIZEOF(.data)) : AT (0x38 + SIZEOF(.sdram) + SIZEOF(.data)) {
        *(.text) *(.text.*) *(.rodata) *(.rodata.*) *(.romdata) *(.romdata.*)
        . = ALIGN(4);
        __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .;
        . = ALIGN(4);
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gcc_except_table*) }
}
