# Pokémon FireRed PSP

*Imagine a 2005 where Nintendo, just this once, let Kanto travel to Sony's shiny new
handheld. Game Freak widened the world to fill the PSP's long screen, swapped every A and B
for ✕ and ○, and gave Red three save files for the bus ride home. This is that game, the one
that never was.*

Pokémon FireRed running natively on the Sony PSP. It isn't an emulator: the game itself is
compiled for the PSP from [pret's decompilation](https://github.com/pret/pokefirered), so it
opens and plays like any other PSP game. It sits in the PSP's Game menu with its own icon,
background and music, starts with ✕, quits from the HOME button, and keeps its saves in
its own folder. There's nothing to set up and no emulator menus in the way.

It's played and tested on a PSP 3001 with custom firmware (6.61 PRO-C), and in PPSSPP.
It's a fan project, not made or approved by Nintendo, Game Freak, The Pokémon Company or Sony.

**Built with AI, openly.** Every change in this repository was made with Claude, Anthropic's
AI assistant, working in Claude Code alongside the owner of this repository, who chose the
features and tested each build on a real PSP. [How this was made](#how-this-was-made) says
more. AI-written code can contain mistakes, so review it before relying on it.

**No game files here.** There's no ROM and no ready-made game to download. The builder asks
for your own Pokémon FireRed ROM and checks it, so you build the game you own. What you
build contains the game, so keep it to yourself.

Also by me: [Pokémon Emerald PSP](https://github.com/JustKidding23/Pokemon-Emerald-PSP),
[SM64 PSP](https://github.com/JustKidding23/SM64-PSP) and
[Ocarina of Time PSP](https://github.com/JustKidding23/Ocarina-of-Time-PSP)

## What's new

- **A wider Kanto.** The picture fills the PSP's screen without stretching. You see more
  of the world to the left and right, and battles show more of their backdrop. Menus,
  the bag and the Pokédex keep their GBA size in the middle, with their backgrounds carried
  out to the edges.
- **PSP buttons.** Wherever the game names or shows a button, it shows the PSP's: ✕, ○,
  △, □, START and SELECT, drawn in the game's own style.
- **Three save files.** FILE SELECT, in the start menu and on the title's menu, shows them
  like the CONTINUE screen. You can play, copy or delete each one.
- **Turbo.** Hold □ to play two or three times as fast. The music keeps its normal tempo
  and the sound effects keep up.
- **Settings** on the PSP's ♪ button: screen size, smoothing, which button is A, frame skip,
  turbo speed and a speed display.
- **A proper PSP menu entry.** Charizard from the box art, the FireRed logo and the title
  music when the game is selected, if you make them (see [The menu's art and music](#the-menus-art-and-music)).
- **The Cable Club is closed.** The PSP can't use a GBA link cable, so its attendant
  says so. Wireless features say the Wireless Adapter isn't connected, as on a GBA without one.

The whole game is here: Kanto, the Sevii Islands and the postgame.

## Playing

| PSP | Game |
|---|---|
| ✕, ○ | A, B (the settings can swap them) |
| D-pad or analog stick | D-pad |
| L, R, START, SELECT | L, R, Start, Select |
| □ (hold) | Turbo |
| ♪ button | Settings |
| HOME | Quit to the PSP's menu |

SELECT and △ together also open the settings, for where the ♪ button can't be read
(like PPSSPP).

### Save files

The saves are `saves/slot1.sav` to `saves/slot3.sav` in the game's folder. Each is an
ordinary 128 KB FireRed save, the same kind an emulator or a cartridge dumper makes, so you
can bring a game over by copying it there with one of those names. The previous version of
each is kept as `slotN.sav.bak`. A `pokefirered.sav` next to the EBOOT becomes file 1 if
there are no saves yet.

Playing another file starts the game over with it, so FILE SELECT asks first: anything you
haven't saved is lost.

## Building

You'll need a PC running Linux, or Windows with WSL (the Ubuntu app works), and your own
Pokémon FireRed (USA) ROM, dumped from your cartridge. The game is built on the PC, then
copied to the PSP.

1. **Install the build tools.** On Ubuntu or Debian:

   ```
   sudo apt install build-essential git libpng-dev python3 python3-venv curl
   ```

   For the PSP menu's music too, add `cmake ffmpeg libsndfile1-dev gcc-multilib`.

2. **Get this repository and build it,** giving it your ROM:

   ```
   git clone https://github.com/JustKidding23/Pokemon-FireRed-PSP.git
   cd Pokemon-FireRed-PSP
   ./make_psp.sh path/to/your/FireRed.gba
   ```

   It checks that the ROM is Pokémon FireRed, downloads the PSP toolchain into the
   folder (nothing is installed on your system), builds the game, and makes the PSP
   menu's icon, background and music. The first time takes a while. If something is
   missing, it says what to install.

3. **Copy it to the PSP.** Connect the PSP over USB and copy the `PSP` folder from `out/`
   to the root of the memory stick. The PSP needs custom firmware to run homebrew.

The game then appears in the PSP's Game menu as **Pokemon FireRed PSP**.

### On Windows

**FireRed-PSP-Builder.exe** (from the [Releases](https://github.com/JustKidding23/Pokemon-FireRed-PSP/releases)
page) does all of this in a small window: choose your ROM, and it builds the game and
copies it to your PSP if it's plugged in. It uses Windows' own Linux part (WSL), which it
offers to install the first time; that needs permission and a restart. It was tested on a
fresh Windows 11, but not yet on a real PC, so please report how it goes. Its source is in
`windows/`.

### Building by hand

`make_psp.sh` runs these steps, for anyone who'd rather do them:

- Install the [pspdev](https://github.com/pspdev/pspdev/releases) toolchain in `~/pspdev`
  (or set `PSPDEV`), then run `make psp -j$(nproc)`. The game is in
  `build/psp32/PSP/GAME/FIRERED_PSP/`: `EBOOT.PBP` and `kbuttons.prx`, a small helper
  that lets the game read the ♪ button.
- `psp/make_art.sh` makes the menu's art and music from the US box art, as its first lines
  explain. The music is the title screen's theme, made by the game's own sound engine
  (`psp/songrender`). Run `make psp` again afterwards to put them in the EBOOT. They come
  from the box art, so they stay out of the repository.

## How this was made

This port was made in October 2026 by Claude (Opus 5.5) in Claude Code, directed by the
owner of this repository, who chose how it should look and feel and tested every build on
their PSP. It builds on the owner's [Pokémon Emerald PSP](https://github.com/JustKidding23/Pokemon-Emerald-PSP)
port, which brought pret's decompilation to the PSP by way of the PC and PlayStation 2
ports before it.

Claude wrote the wider view and the PSP buttons, sped up the drawing until the overworld,
battles and menus ran smoothly on real hardware, and then went looking for trouble. It
played every move's battle animation and every corner of the map in an emulator set to
catch what a PSP won't forgive, and fixed what it found, like slot machines that would
crash on the way out.

[docs/psp.md](docs/psp.md) has the details for anyone making PSP homebrew: what differs
between a real PSP and PPSSPP, how the wider view and the faster drawing work, and the
test tools. The commit messages say what each change does and how it was tested.

## Credits

[CREDITS.md](CREDITS.md) names everyone whose work this is built on: the pret team who
decompiled the game, and the PC, PS2 and PSP ports before this one.

Want to help, or found a problem? [CONTRIBUTING.md](CONTRIBUTING.md) says how. Reports from
playing on your PSP are as welcome as code.
