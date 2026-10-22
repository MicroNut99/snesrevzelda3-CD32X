# zelda3 headless reference (PC): make -C pc  ->  pc/zh
Z := ../zelda3
SRCS := $(filter-out $(Z)/src/main.c $(Z)/src/opengl.c $(Z)/src/glsl_shader.c $(Z)/src/config.c,$(wildcard $(Z)/src/*.c)) $(wildcard $(Z)/snes/*.c) \
        $(Z)/third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c zh_main.c
zh: $(SRCS)
	$(CC) -O2 -w -I$(Z) -Istub -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 $(SRCS) -o $@ -lm
