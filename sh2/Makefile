# Zelda 3 32X - SH2 program (stage 1: the speed / correctness test).  make -C sh2 Z32_DATA=<folder>
# with zelda3_assets.dat (from your ROM), replay.bin and expect.bin (pc/zh) in Z32_DATA.
# DEFS: the build's switches (sh2/z32_main.cpp: Z32_NO_CHECK, Z32_PLAY, ...); ../build.sh sets them.
ROOTDIR ?= $(if $(GENDEV),$(GENDEV),/opt/toolchains/sega)
PREFIX  := $(ROOTDIR)/sh-elf/bin/sh-elf-
Z       := ../zelda3
Z32_DATA ?= ../build
ARCH    := -m2 -mb
CXXFLAGS := $(ARCH) -O2 -fpermissive -Wno-narrowing -w -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables \
            -ffunction-sections -fdata-sections -x c++ -I$(Z) -I../pc/stub -DZ32X $(DEFS)
GAME := $(filter-out main config audio spc_player opengl glsl_shader zelda_cpu_infra,$(basename $(notdir $(wildcard $(Z)/src/*.c))))
SNES := ppu dma md_planes
OBJS := $(addprefix obj/,$(addsuffix .o,$(GAME) $(SNES))) obj/z32_main.o obj/z32_sound.o obj/z32_save.o obj/z32_stubs.o obj/z32_sys.o obj/z32_data.o
vpath %.c $(Z)/src $(Z)/snes
all: z32.bin
z32.bin: z32.elf
	$(PREFIX)objcopy -O binary $< $@
z32.elf: obj/z32_crt0.o $(OBJS) zelda.ld
	$(PREFIX)g++ $(ARCH) -nostdlib -T zelda.ld -Wl,--gc-sections -Wl,-Map=z32.map obj/z32_crt0.o $(OBJS) -Wl,--start-group -lc -lgcc -Wl,--end-group -o $@
obj/z32_crt0.o: crt0.s
	@mkdir -p obj; $(PREFIX)gcc $(ARCH) -c $< -o $@
obj/z32_data.o: z32_data.s
	@mkdir -p obj; $(PREFIX)gcc $(ARCH) -Wa,-I$(Z32_DATA) -c $< -o $@
obj/z32_sys.o: z32_sys.c
	@mkdir -p obj; $(PREFIX)gcc $(ARCH) -O2 -c $< -o $@
obj/%.o: %.cpp
	@mkdir -p obj; $(PREFIX)g++ $(CXXFLAGS) -c $< -o $@
obj/%.o: %.c
	@mkdir -p obj; $(PREFIX)g++ $(CXXFLAGS) -c $< -o $@
clean:
	rm -rf obj z32.elf z32.bin z32.map
