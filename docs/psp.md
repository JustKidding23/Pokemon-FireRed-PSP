# Notes on the PSP port

How the PSP version works, what was found on the way, and how it was tested. It's for anyone
working on this port or making their own PSP homebrew. The README covers playing and building.

## How it fits together

The game code is pret's, shared with the GBA build. The PSP version adds:

| File | What it does |
|---|---|
| `src/platform/psp.c` | The frame loop, the buttons, the save files, FILE SELECT, the settings and the test inputs |
| `src/platform/psp_hw.c` | Everything that talks to the PSP: the GU, sound, buttons, files, clock, watchdog and trace |
| `include/platform/psp_hw.h` | The line between them. Only standard C types cross it, so pspsdk's headers and the game's never meet |
| `src/platform/gba_easy_draw.c` | The renderer: the GBA's backgrounds, sprites, windows and blending, drawn in software |
| `src/platform/cgb_audio.c` | The Game Boy sound channels |
| `src/platform/test_*.c` | Test-only shortcuts (see [Testing](#testing)) |
| `psp/kbuttons/` | A kernel module that reads the ♪ button for the game |
| `psp/make_art.sh` | Makes the PSP menu's icon, background and music |
| `psp/tools/` | `lwlr_hazards.py` and `trace.py` (see below) |

`make psp` builds the game as a PRX, packs it into `EBOOT.PBP` and copies `kbuttons.prx` next
to it. Changes to pret's files are inside `#ifdef PORTABLE` (or `UBFIX` for undefined behavior
fixed the way the GBA behaves), so the GBA build is unchanged.

### A frame

The game runs one frame for every frame the PSP shows. The picture is drawn into a 282x160
image and the GU scales it 1.7 times to fill the screen's height. When drawing falls behind,
frames run without being drawn (each line's interrupts and HBlank DMAs still run), so the
game keeps its speed. Turbo runs two or three game frames per frame shown. The sequencer runs
once, so the music keeps its tempo, and sound effects run every game frame so they keep up.

### The wider view

The GBA shows 240 columns. The PSP shows 282, 21 more on each side. What goes in those margins
depends on the screen, and `include/bg.h` has flags for it, reset whenever a screen sets up
its backgrounds:

- On the overworld, maps are wider than the screen, so the margins just show more map.
  Windows whose edges sit on the screen's edge (the overworld's WIN0) carry on to the line's
  edge.
- On screens made for 240 columns, a background narrower than the view has its edge carried
  on (`ExtendEdges`), which suits gradients, stripes and solid colors.
- `gWideViewRepeatingBgs`, `gWideViewClampBgs`, `gWideViewNoWrapBgs`, `gWideViewLeftOnlyBgs`,
  `gWideViewEndsAtEdgeBgs` and `gWideViewNarrowBgs` say how a background should fill the
  margins where neither works, for example a pattern that repeats or a picture that should
  stop at the GBA's edge.

Sprites keep their GBA coordinates. A sprite parked just past the GBA's edge to hide it shows
in the margin, so the few places that do that were checked one by one.

### Drawing fast enough

The renderer started as a straight translation of the GBA's hardware. Most of the speed came
from not doing per-pixel work where a whole run of pixels is the same:

- Backgrounds that nothing blends are drawn straight onto the line, a tile row at a time.
- Blending keeps, for each pixel of the line, which layer drew it. A blended layer then only
  looks at the pixel below and mixes the two, with the three color channels in one word.
  Semi-transparent sprites mark their pixels and are blended the same way as the line is put
  together. Haze went from 63 ms a frame to 25 ms on a PSP 3001.
- Fades over the whole screen are done by the GU after drawing.

Test builds can draw every line both the old way and the new way and count the pixels that
differ (`RENDERAB`), which is how these were checked to change nothing.

### Saves

The game's flash save is a 128 KB array. It's read from `saves/slotN.sav` at startup and
written whole every time the game saves: to `slotN.sav.tmp` first, then the old file becomes
`slotN.sav.bak` and the new one takes its place, since `sceIoRename` doesn't overwrite. The
saved structures keep the GBA's layout, so saves move freely between this port, emulators and
cartridges.

### Link play

There's no link cable or Wireless Adapter on a PSP. `IsWirelessAdapterConnected` returns
`FALSE`, so the game's own messages handle Mystery Gift, the Union Room and Two Island's
minigames. The Cable Club would save and then wait for a partner forever, so on the PSP its
attendant says it's closed (`data/scripts/cable_club.inc`).

## Things found on real hardware

### The PSP's CPU and lwl/lwr

An instruction that uses the result of an `lwl`/`lwr` pair straight away gets the aligned word
`lwr` loaded, not the merged value. GCC makes these pairs out of byte-by-byte reads, which
pret's script interpreters do all the time. The build avoids it with
`-fdisable-tree-bswap -fno-store-merging -fno-tree-vectorize`, and
`psp/tools/lwlr_hazards.py pokefirered32.elf` lists any pair whose result the next instruction
uses. PPSSPP doesn't reproduce this.

### Division by zero

For MIPS, GCC follows every division with a trap for a zero divisor. A PSP stops the game
there (PSPLink shows `Exception - Breakpoint`, `break 0x7`). The GBA gives 0 and carries on,
and the game does divide by zero: Spark's animation froze the PSP. The build uses
`-mno-check-zero-division`, and the known cases (`SAFE_DIV`, `SAFE_MOD`) give the GBA's result.
PPSSPP only logs `BREAK!` and carries on, so a test build with `ZERO_DIV=-mcheck-zero-division`
and a look through PPSSPP's log finds any others.

### Reading and writing near address 0

On a GBA, address 0 is the BIOS, so code that reads through a NULL pointer gets harmless
bytes and writes do nothing. On a PSP they can crash. PPSSPP's JIT ignores them too, but with
`-l` it logs `Bad memory access`, and its interpreter (`-i`) stops at the first one. The ones
found and fixed:

- battle scripts pass no argument (NULL) to `playanimation` for held items, weather and the
  Silph Scope, and the command read it anyway
- leaving the slot machines, the reel task ran once more after the slot machine's state was
  freed and set to NULL, reading and writing through it
- the move animations that shake the battle background rebuilt a pointer from two `s16`
  halves, which sign-extended into an address near the top of memory

### Struct padding

agbcc rounds a struct with a halfword or larger member up to a multiple of 4 bytes. GCC doesn't,
so such a struct whose size isn't a multiple of 4 is smaller on the PSP. That matters wherever
the GBA's size is assumed:

- **Saves.** The quest log's object events (18 bytes on the PSP, 20 on the GBA) and the Fame
  Checker's entries (2 and 4) moved everything saved after them, so saves didn't move between
  the PSP and a GBA intact. Both are `ALIGNED(4)` now, and `src/save.c` checks at build time
  every save field whose GBA offset pret notes. Saves written before the fix are moved to the
  GBA's layout when they're read (`MigrateQuestLogLayout` in `psp.c`).
- **Code that steps through memory by a fixed size.** `union AffineAnimCmd` is 8 bytes on the
  GBA and 6 on the PSP, so code that stepped through animation commands by 8 bytes read garbage,
  and Thrash's animation never ended. Look for offsets written as `<< 3` or `* 8` on arrays of
  structs.

`pahole` on an ELF built with `CFLAGS=-g` lists every struct's size and its members' offsets.

### Other differences

- When a homebrew program crashes, the PSP freezes and turns itself off about 20 seconds
  later, with no message. PSPLink catches the crash.
- PPSSPP doesn't model the CPU's cache, so programs run slower on a PSP than its timing
  suggests. Speed has to be measured on the PSP.

## Debugging on a PSP

### PSPLink

[PSPLink](https://github.com/pspdev/psplinkusb) starts a program on the PSP over USB and shows
a crash's registers. pspdev includes the PC side (`usbhostfs_pc` and `pspsh`).

1. Unzip PSPLink into `PSP/GAME/` and start it on the PSP with the USB cable in. On Linux,
   install pspdev's `share/psplinkusb/50-psplink.rules` into `/etc/udev/rules.d/` once.
2. Copy `build/psp32/pokefirered.prx` into a folder and run `usbhostfs_pc` there. That folder
   is `host0:/` on the PSP, and the game keeps its saves and settings there.
3. Run `pspsh` and type `./pokefirered.prx`. `reset` stops it and `scrshot x.bmp` saves the
   screen.

A crash prints the exception and the registers. `modinfo` with the game's UID gives
`TextAddr`: take that from an address and look it up with
`psp-addr2line -f -e pokefirered32.elf <address>`. Keep the ELF of every build you test, since
a later build moves everything.

### In the game

- **The watchdog.** If the game stops responding for 5 seconds, a blue screen shows the step
  of the frame it was in, the main callbacks and the running tasks.
- **The trace.** With `trace = 1` in `settings.ini`, the game writes the same to `trace.bin`
  before every step. After a freeze it says what the game was doing last:
  `psp/tools/trace.py trace.bin pokefirered32.elf`.

## Testing

`make psp PSP_TEST=1 BUILD_DIR=build-test` builds a version that plays buttons from
`test_input.txt` next to the EBOOT, one `<frames> <buttons>` line each (`30 CROSS+UP`), and
writes a log to `speed.log` when the input ends. Run it in `PPSSPPHeadless`, or on a PSP over
PSPLink. Add `PSP_TEST_FLAGS=-DPSP_PROFILE` for per-stage timings.

Besides button names, a line can hold one of these, done once the overworld is up:

| Input | What it does |
|---|---|
| `SHOT` | Saves the screen as `shot_NNN.rgba` and logs the map, the player's position, the display registers and any sprite in the margins |
| `WARP:group:num:x:y` | Warps the player |
| `SETFLAG:hex`, `SETVAR:hex:value` | Sets a story flag or variable |
| `COMPLETE` | Turns the save into a finished one: every badge, the National Pokédex, one of each Pokémon boxed, every item |
| `WILDBATTLE`, `ANIMS:first:last` | Starts a wild battle, then plays every move's animation from `first` to `last` |
| `EVOLVE`, `HATCH`, `TRADE` | Evolves the first Pokémon, hatches an egg, plays an in-game trade |
| `COINS`, `DAYCAREEGG`, `CURRENTS` | Coins and the Coin Case, an egg waiting at the Day Care, Seafoam's currents running |
| `HOF`, `HOFPC`, `MYSTERYGIFT`, `HELPWIDTHS` | The Hall of Fame, its PC, Mystery Gift on the title menu, the help texts' widths |
| `RENDERAB` | Draws every line the old way too and logs differing pixels at each `SHOT` |

Every move's animation, the places the story reaches late and the screens behind them were
played this way, in PPSSPP with the division trap on and bad memory accesses logged, and timed
on a PSP 3001 over PSPLink.
