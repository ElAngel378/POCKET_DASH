# GBDK path - use environment variable if set, otherwise default to C:/gbdk
GBDK ?= C:/gbdk
GBCC = $(GBDK)/bin/lcc

PROJECT_NAME = POCKETDASH
SRCDIR = src
INCDIR = include
TEMPDIR = temp

# make PROFILE=1: profiling ROM bin/POCKETDASH_prof.gb (own temp dir) with god mode and section
# markers for tools/profile.py, which also picks the level. Never overwrites the normal ROM.
ifdef PROFILE
PROJECT_NAME = POCKETDASH_prof
TEMPDIR = temp_prof
PROF_FLAGS = -DDEBUG_PROFILE -DDEBUG_GODMODE $(PROF_EXTRA)
endif
BINDIR = bin
LIBDIR = lib

SRCS = $(wildcard $(SRCDIR)/*.c) $(wildcard $(SRCDIR)/*/*.c)
ASMS = $(wildcard $(SRCDIR)/*/*.s)
OBJS = $(foreach src, $(SRCS), $(TEMPDIR)/$(notdir $(src:.c=.o))) $(foreach src, $(ASMS), $(TEMPDIR)/$(notdir $(src:.s=.o)))

vpath %.c $(SRCDIR) $(SRCDIR)/graphics $(SRCDIR)/music $(SRCDIR)/sprites $(SRCDIR)/levels $(SRCDIR)/sfx $(SRCDIR)/furnace
vpath %.s $(SRCDIR)/furnace

# SDCC optimizes almost nothing by default, and lcc has no -O option
# (unrecognized options go to the linker!). Compiler flags must be forwarded
# via -Wf. Without these the game cannot hold 60fps: all OAM/metatile work
# runs at naive -O0 speed.
# -Wl-yp0x143=0x80 enables GBC support in the ROM header; -Wm-ys the Super Game Boy one (0x146 = 3)
LCCFLAGS = $(PROF_FLAGS) -I$(INCDIR) -Isrc/graphics -Wf--opt-code-speed -Wf--max-allocs-per-node50000 -Wa-I. -Wl-j -Wl-yt0x1B -Wl-yo256 -Wl-ya1 -Wl-yp0x143=0x80 -Wm-ys
LIBS = $(LIBDIR)/hUGEDriver.lib

all: prepare $(BINDIR)/$(PROJECT_NAME).gb

prepare:
	@mkdir -p $(TEMPDIR)
	@mkdir -p $(BINDIR)

$(TEMPDIR)/%.o: %.c
	$(GBCC) $(LCCFLAGS) -c -o $@ $<

# the Furnace player's per-tick routine (tools/convert_furnace.py copies it from fur2gb)
$(TEMPDIR)/%.o: %.s
	$(GBCC) $(LCCFLAGS) -c -o $@ $<

# No automatic header tracking: list generated headers / INCBIN data explicitly
$(TEMPDIR)/mt_renderer.o: src/graphics/bg_level_tables.h
$(TEMPDIR)/bg_extra_tiles.o: levels/chr_data/bg_extra_tiles.bin
$(TEMPDIR)/tileset.o: levels/chr_data/bg_base_tiles.bin levels/chr_data/bg_base_tiles_flipped.bin
$(TEMPDIR)/saw_anim_data.o: levels/chr_data/saw_anim_tiles.bin
$(TEMPDIR)/assets.o: include/bg_tiles.h
$(TEMPDIR)/famidash_sprite_tiles.o: src/sprites/sprite_tile_tables.h src/sprites/dmg_object_icons.h src/sprites/coin_tiles.h levels/chr_data/sprite_tiles.bin
$(TEMPDIR)/pause_button_tiles.o: src/sprites/sprite_blob_offsets.h src/graphics/practice_button_cgb.inc src/graphics/pause_buttons_cgb.inc
# icon counts and frame banks (tools/build_icon_select_gfx.py, tools/make_cube_icon_frames.py: a
# stale object kept the icon select menu at the old icon count)
$(TEMPDIR)/state_icon_select.o $(TEMPDIR)/save_manager.o $(TEMPDIR)/gameplay.o $(TEMPDIR)/icon_pages.o $(TEMPDIR)/icon_previews.o $(TEMPDIR)/icon_catalog.o: include/icon_catalog.h include/cube_icon_frames.h
# the metatile and collision tables (a stale object kept the old square saw hitboxes in the ROM)
$(TEMPDIR)/famidash_metatiles.o: include/famidash_metatiles_dmg.c include/famidash_metatiles.h include/collision.h
$(TEMPDIR)/menu_bg.o: levels/chr_data/menu_ground_tiles.bin levels/chr_data/menu_ground_map.bin
# title menu big buttons (tools/gen_title_buttons.py): tile / palette counts in the header
$(TEMPDIR)/state_menu.o $(TEMPDIR)/title_buttons.o: include/title_buttons.h
# Furnace songs: song ids and the setting
$(TEMPDIR)/death_effect.o $(TEMPDIR)/sample_player.o $(TEMPDIR)/gameplay.o $(TEMPDIR)/state_play_level.o $(TEMPDIR)/state_level_complete.o $(TEMPDIR)/state_settings.o $(TEMPDIR)/furplayer.o $(TEMPDIR)/furplayer_core.o $(TEMPDIR)/music_select.o $(TEMPDIR)/state_music_test.o $(TEMPDIR)/furnace_songs.o $(TEMPDIR)/collision.o $(TEMPDIR)/sp_draw.o $(TEMPDIR)/main.o: include/furnace_songs.h include/music.h include/furplayer.h
$(TEMPDIR)/save_manager.o $(TEMPDIR)/state_settings.o: include/settings.h
$(TEMPDIR)/state_menu.o $(TEMPDIR)/state_level_complete.o $(TEMPDIR)/icon_pages.o $(TEMPDIR)/scratch.o: include/scratch.h
# level maps (INCBIN): any changed map rebuilds the small level wrappers
$(TEMPDIR)/level_%.o: $(wildcard levels/level_data/*.bin)

$(BINDIR)/$(PROJECT_NAME).gb: $(OBJS)
	-rm -f $@
	$(GBCC) $(LCCFLAGS) -o $@ $(OBJS) $(LIBS)
	@python tools/check_banks.py $(BINDIR)/$(PROJECT_NAME).map || (rm -f $@ && exit 1)

clean:
	-rm -rf $(TEMPDIR) $(BINDIR)
