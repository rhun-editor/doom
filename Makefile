CC = clang
ENGINE = vendor/doomgeneric
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable -Wno-missing-field-initializers -Wno-deprecated-declarations -D_DEFAULT_SOURCE -D__MACOSX__ -DFEATURE_SOUND -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -I$(ENGINE) -Ivendor/rhun -Isrc
LDFLAGS = -framework CoreFoundation -framework AudioToolbox -framework AudioUnit -lz -lpthread
ENGINE_NAMES = dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric
OBJECTS = $(addprefix build/engine/,$(addsuffix .o,$(ENGINE_NAMES))) build/main.o build/png.o build/audio.o

.PHONY: all clean test
all: bin/doom-macos-arm64
bin/doom-macos-arm64: $(OBJECTS)
	@mkdir -p bin
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
build/engine/%.o: $(ENGINE)/%.c
	@mkdir -p build/engine
	$(CC) $(CFLAGS) -w -c $< -o $@
build/%.o: src/%.c src/platform.h vendor/rhun/rhunext.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -rf build bin
test: all
	python3 tests/unit.py
