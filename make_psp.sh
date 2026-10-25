#!/usr/bin/env bash
# Builds Pokémon FireRed PSP in one step:
#
#   ./make_psp.sh PATH/TO/YOUR/FIRERED.gba [--no-art]
#
# It checks that the ROM is Pokémon FireRed (USA), fetches the PSP toolchain
# into .psp-build/ (nothing is installed on the system), builds the game, and
# makes the PSP menu's icon, background and music. The result is out/PSP,
# ready to copy to the root of the memory stick.
#
# The game itself is built from pret's decompilation in this repository; the
# ROM is only checked, to make sure you own the game. --no-art skips the menu's
# art and music (they need a few more packages).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
cd "$here"
work="$here/.psp-build"
PSPDEV_RELEASE=v20261001   # the pspdev release this port was built and tested with
FIRERED_SHA1="41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc dd5945db9b930750cb39d00c84da8571feebf417"
BOXART_URL='https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Game_Boy_Advance/master/Named_Boxarts/Pokemon%20-%20FireRed%20Version%20(USA).png'
KEYART_URL='https://static0.gamerantimages.com/wordpress/wp-content/uploads/2026/03/pokemon-firered-version.jpg'

step() { printf '\n== %s\n' "$*"; }
fail() { printf '\n%s\n' "$*" >&2; exit 1; }
have() { command -v "$1" > /dev/null 2>&1; }

# The command that installs packages on this system, for the messages
install_hint() {
    if have apt-get; then echo "sudo apt install $1"
    elif have dnf; then echo "sudo dnf install $2"
    elif have pacman; then echo "sudo pacman -S --needed $3"
    elif have brew; then echo "brew install $4"
    else echo "install: $1"
    fi
}

rom=""
art=1
for arg in "$@"; do
    case "$arg" in
        --no-art) art=0 ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) rom=$arg ;;
    esac
done
[ -n "$rom" ] || fail "Usage: ./make_psp.sh PATH/TO/YOUR/FIRERED.gba [--no-art]"

step "Checking your ROM"
[ -f "$rom" ] || fail "There's no file at $rom."
if have sha1sum; then sum=$(sha1sum "$rom" | cut -d' ' -f1); else sum=$(shasum "$rom" | cut -d' ' -f1); fi
case " $FIRERED_SHA1 " in
    *" $sum "*) echo "Pokémon FireRed (USA): OK" ;;
    *) fail "That isn't Pokémon FireRed (USA), version 1.0 or 1.1 (its SHA-1 is $sum).
Use a copy of your own cartridge, dumped unchanged." ;;
esac

step "Checking the build tools"
missing=""
for tool in cc c++ make python3 curl tar; do have "$tool" || missing="$missing $tool"; done
if ! printf '#include <png.h>\n' | cc -E -x c - > /dev/null 2>&1; then missing="$missing libpng"; fi
if [ -n "$missing" ]; then
    fail "Missing:$missing. Install them with:
  $(install_hint 'build-essential libpng-dev python3 python3-venv curl' 'gcc gcc-c++ make libpng-devel python3 curl' 'base-devel libpng python curl' 'libpng python')
then run this again."
fi
echo "OK"

step "Getting the PSP toolchain"
if [ -n "${PSPDEV:-}" ] && [ -x "$PSPDEV/bin/psp-gcc" ]; then
    echo "Using the one in $PSPDEV"
elif [ -x "$work/pspdev/bin/psp-gcc" ]; then
    export PSPDEV="$work/pspdev"
    echo "Already downloaded"
else
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64)
            distro=ubuntu-latest-x86_64
            if [ -r /etc/os-release ]; then
                . /etc/os-release
                case "${ID:-}" in debian) distro=debian-latest ;; fedora) distro=fedora-latest ;; esac
            fi ;;
        Darwin-arm64) distro=macos-latest-arm64 ;;
        Darwin-x86_64) distro=macos-15-intel-x86_64 ;;
        *) fail "There's no ready-made PSP toolchain for $(uname -s) on $(uname -m). See https://github.com/pspdev/pspdev" ;;
    esac
    mkdir -p "$work"
    url="https://github.com/pspdev/pspdev/releases/download/$PSPDEV_RELEASE/pspdev-$distro.tar.gz"
    echo "Downloading pspdev $PSPDEV_RELEASE ($distro), a few hundred MB"
    curl -fL --progress-bar -o "$work/pspdev.tar.gz" "$url"
    tar -xzf "$work/pspdev.tar.gz" -C "$work"
    rm "$work/pspdev.tar.gz"
    [ -x "$work/pspdev/bin/psp-gcc" ] || fail "The toolchain didn't unpack as expected."
    export PSPDEV="$work/pspdev"
fi
export PATH="$PSPDEV/bin:$PATH"

jobs=$( (have nproc && nproc) || sysctl -n hw.ncpu 2> /dev/null || echo 2)

step "Building the game (the first time takes a while)"
make psp -j"$jobs" > "$work/build.log" 2>&1 || { tail -20 "$work/build.log"; fail "The build failed. The whole log is in .psp-build/build.log."; }
echo "OK"

if [ "$art" = 1 ]; then
    step "Making the PSP menu's icon, background and music"
    art_ok=1
    if python3 -m venv "$work/venv" > /dev/null 2>&1 \
            && "$work/venv/bin/pip" install -q pillow numpy opencv-python-headless > "$work/pip.log" 2>&1; then
        mkdir -p "$work/art"
        [ -f "$work/art/box.png" ] || curl -fsSL -o "$work/art/box.png" "$BOXART_URL" || art_ok=0
        [ -f "$work/art/key.jpg" ] || curl -fsSL -o "$work/art/key.jpg" "$KEYART_URL" || rm -f "$work/art/key.jpg"
    else
        echo "Python's venv module or pip isn't there: $(install_hint python3-venv python3 python python)"
        art_ok=0
    fi

    # The PSP menu only played music from atracdenc release 0.2.3
    atracdenc=""
    if [ -x "$work/atracdenc/build/src/atracdenc" ]; then
        atracdenc="$work/atracdenc/build/src/atracdenc"
    elif have cmake && have ffmpeg; then
        echo "Building atracdenc 0.2.3 (the PSP menu's music encoder)"
        rm -rf "$work/atracdenc" && mkdir -p "$work/atracdenc"
        if curl -fsSL https://github.com/dcherednik/atracdenc/archive/refs/tags/0.2.3.tar.gz \
                | tar -xz -C "$work/atracdenc" --strip-components=1 \
                && cmake -S "$work/atracdenc" -B "$work/atracdenc/build" -DCMAKE_BUILD_TYPE=Release \
                    -DCMAKE_CXX_FLAGS="-include cstdint" > "$work/atracdenc.log" 2>&1 \
                && cmake --build "$work/atracdenc/build" -j"$jobs" >> "$work/atracdenc.log" 2>&1; then
            atracdenc="$work/atracdenc/build/src/atracdenc"
        else
            echo "atracdenc didn't build (it needs libsndfile: $(install_hint libsndfile1-dev libsndfile-devel libsndfile libsndfile))"
        fi
    fi
    if [ -z "$atracdenc" ]; then
        echo "No music for the menu: it needs cmake, ffmpeg and libsndfile, and a compiler for 32-bit programs:"
        echo "  $(install_hint 'cmake ffmpeg libsndfile1-dev gcc-multilib' 'cmake ffmpeg libsndfile-devel glibc-devel.i686 libgcc.i686' 'cmake ffmpeg libsndfile lib32-gcc-libs' 'cmake ffmpeg libsndfile')"
    fi

    if [ "$art_ok" = 1 ]; then
        # The venv's Python comes first, so the art script's python3 has Pillow
        if PATH="$work/venv/bin:$PATH" PYTHON="$work/venv/bin/python" KEYART="$work/art/key.jpg" \
                bash psp/make_art.sh "$work/art/box.png" $atracdenc > "$work/art.log" 2>&1; then
            make psp -j"$jobs" >> "$work/build.log" 2>&1 || fail "Packing the menu art failed. See .psp-build/build.log."
            echo "OK"
        else
            tail -5 "$work/art.log"
            echo "The menu's art couldn't be made, so the game has the PSP's plain icon."
        fi
    fi
fi

step "Done"
rm -rf out
mkdir -p out/PSP/GAME
cp -R build/psp32/PSP/GAME/FIRERED_PSP out/PSP/GAME/
ls out/PSP/GAME/FIRERED_PSP
cat <<EOF

Copy the PSP folder from out/ to the root of your PSP's memory stick (it
merges with the PSP folder already there). The game then appears in the
PSP's Game menu as Pokemon FireRed PSP. Your PSP needs custom firmware.
EOF
