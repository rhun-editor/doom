ifeq ($(origin CC),default)
CC = clang
endif
HOST_SYSTEM := $(shell uname -s)
ifeq ($(HOST_SYSTEM),Darwin)
TARGET = macos-arm64
PLATFORM_CFLAGS = -D__MACOSX__ -mmacosx-version-min=12.0
PLATFORM_LIBS = -mmacosx-version-min=12.0 -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework AudioUnit -lpthread
else ifneq ($(filter MINGW% MSYS%,$(HOST_SYSTEM)),)
TARGET = windows-x64
EXE = .exe
RESOURCES = build/$(TARGET)/utf8.o
PLATFORM_LIBS = -static -lwinmm -lole32 -luuid -lksuser
else
TARGET = linux-x86_64
PLATFORM_LIBS = -ldl -lpthread -lm
endif
BINARY = bin/doom-$(TARGET)$(EXE)
ENGINE = vendor/doomgeneric
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable -Wno-missing-field-initializers -Wno-deprecated-declarations -D_DEFAULT_SOURCE $(PLATFORM_CFLAGS) -DFEATURE_SOUND -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -I$(ENGINE) -Ivendor/rhun -Ivendor/zlib -Ivendor/miniaudio -Ivendor/tinysoundfont -Isrc
LDFLAGS = $(PLATFORM_LIBS)
ENGINE_NAMES = dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric
ZLIB_NAMES = adler32 compress crc32 deflate trees zutil
OBJECTS = $(addprefix build/$(TARGET)/zlib/,$(addsuffix .o,$(ZLIB_NAMES))) $(addprefix build/$(TARGET)/engine/,$(addsuffix .o,$(ENGINE_NAMES))) build/$(TARGET)/main.o build/$(TARGET)/png.o build/$(TARGET)/audio.o $(RESOURCES)

.PHONY: all clean test
all: $(BINARY)
$(BINARY): $(OBJECTS)
	@mkdir -p bin
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
build/$(TARGET)/zlib/%.o: vendor/zlib/%.c Makefile
	@mkdir -p build/$(TARGET)/zlib
	$(CC) $(CFLAGS) -w -c $< -o $@
build/$(TARGET)/engine/%.o: $(ENGINE)/%.c Makefile
	@mkdir -p build/$(TARGET)/engine
	$(CC) $(CFLAGS) -w -c $< -o $@
build/$(TARGET)/%.o: src/%.c src/platform.h src/os.h vendor/rhun/rhunext.h Makefile
	@mkdir -p build/$(TARGET)
	$(CC) $(CFLAGS) -c $< -o $@
build/$(TARGET)/utf8.o: src/windows.rc src/doom.manifest
	@mkdir -p build/$(TARGET)
	windres $< -o $@
print-target:
	@echo $(TARGET)
clean:
	rm -rf build bin
test: all
	python3 tests/unit.py
