# Builds songrender: the game's sound engine and songs on a PC (32-bit, as
# the port is), for psp/make_art.sh to render the PSP menu's music.
# Run from the repository's root after `make psp`, which makes the songs and
# samples: make -f psp/songrender/Makefile
# Needs a compiler that builds 32-bit programs (gcc -m32: gcc-multilib on
# Debian and Ubuntu).

CC ?= gcc
AS := as
OUT := build/songrender
CFLAGS := -m32 -O2 -std=gnu99 -iquote include -DPORTABLE -DNONMATCHING -DUBFIX -DMODERN=1 \
          -DFIRERED -DREVISION=0 -DENGLISH -fno-strict-aliasing -w
ASFLAGS := --32 -I sound --defsym VER_64BIT=0 --defsym FIRERED=1 --defsym REVISION=0 --defsym ENGLISH=1 \
           --defsym MODERN=1 --defsym PORTABLE=1 --defsym UBFIX=1

C_SRCS := src/m4a.c src/music_player.c src/sound_mixer.c src/platform/cgb_audio.c src/platform/bios.c src/m4a_tables.c \
          psp/songrender/main.c
SONGS := $(wildcard sound/songs/midi/*.s)
C_OBJS := $(C_SRCS:%.c=$(OUT)/%.o)
SONG_OBJS := $(SONGS:%.s=$(OUT)/%.o)

$(OUT)/songrender: $(C_OBJS) $(SONG_OBJS) $(OUT)/data/sound_data.o
	$(CC) -m32 -no-pie -o $@ $^ -lm

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/sound/%.o: sound/%.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# As the game's Makefile does data files: macros with pret's preproc, then the
# C preprocessor
$(OUT)/data/sound_data.o: data/sound_data.s
	@mkdir -p $(dir $@)
	tools/preproc/preproc $< charmap.txt | cpp -I include - | tools/preproc/preproc -ie $< charmap.txt \
		| $(AS) $(ASFLAGS) -o $@
