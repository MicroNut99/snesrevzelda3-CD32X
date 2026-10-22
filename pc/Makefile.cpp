# zelda3 headless reference, everything compiled as C++ (the Z32X build's language): make -C pc -f Makefile.cpp -> pc/zhx
Z := ../zelda3
SRCS := $(filter-out $(Z)/src/main.c $(Z)/src/opengl.c $(Z)/src/glsl_shader.c $(Z)/src/config.c,$(wildcard $(Z)/src/*.c)) $(wildcard $(Z)/snes/*.c) zh_main.c
OBJS := $(patsubst %.c,obj/%.o,$(notdir $(SRCS)))
CXXFLAGS := -O2 -w -fpermissive -x c++ -I$(Z) -Istub -DSYSTEM_VOLUME_MIXER_AVAILABLE=0
vpath %.c $(Z)/src $(Z)/snes .
zhx: $(OBJS) obj/opus.o
	g++ $^ -o $@ -lm
obj/opus.o: $(Z)/third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c
	@mkdir -p obj; gcc -O2 -w -c $< -o $@
obj/%.o: %.c
	@mkdir -p obj; g++ $(CXXFLAGS) -c $< -o $@
