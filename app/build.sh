#!/bin/sh
# Builds the app with devkitARM (in the devkitPro container unless this is already running inside it), then
# packages the .cia with the tools in tools/bin.
#
#   ./build.sh            development build: theme-plaza.3dsx, theme-plaza.cia (with the test channel and the timing log)
#   ./build.sh release    the build to hand out: theme-plaza-release.3dsx, theme-plaza-release.cia (without them)
#   ./build.sh clean      removes the development build's objects
#   ./build.sh version    prints the version number
set -e
cd "$(dirname "$0")"
TOOLS="$PWD/../tools/bin"
NAME=theme-plaza
# The CIA's title version. The HOME Menu caches each title's name and icon by title id AND version
# (3dbrew: Home Menu, Cache.dat), so raise it whenever the icon, name or tagline changes, or consoles keep the old ones.
VERSION_MAJOR=0; VERSION_MINOR=4; VERSION_MICRO=0
VERSION=$VERSION_MAJOR.$VERSION_MINOR.$VERSION_MICRO
if [ "$1" = "version" ]; then echo "$VERSION"; exit 0; fi
if [ -n "$DEVKITPRO" ] && command -v make >/dev/null; then
  build() { make "$@"; }
else
  build() { podman run --rm -v "$PWD":/work:Z -w /work docker.io/devkitpro/devkitarm:latest make "$@"; }
fi
# the icon file given to badge extdata when the app has to create it (read from romfs by core/extdata.cpp)
"$TOOLS/bannertool-1.2.3-linux/bannertool" makesmdh -s "HOME Menu badges" -l "Badge data for the HOME Menu" -p "Theme Plaza" -i meta/icon.png -o romfs/extdata.smdh >/dev/null
if [ "$1" = "release" ]; then
  NAME=theme-plaza-release
  build -j"$(nproc)" BUILD=build-release TARGET=$NAME APP_DEFINES= APP_VERSION=$VERSION
else
  build -j"$(nproc)" APP_VERSION=$VERSION "$@"
  [ "$1" = "clean" ] && exit 0
fi
mkdir -p build
# the HOME Menu banner: a 3D scene (meta/banner.cgfx, made by tools/make_banner.py) with the sound meta/audio.wav
# (tools/make_music.py: stereo, under 3 seconds, as the HOME Menu wants)
"$TOOLS/bannertool-1.2.3-linux/bannertool" makebanner -ci meta/banner.cgfx -a meta/audio.wav -o build/banner.bnr >/dev/null
"$TOOLS/bannertool-1.2.3-linux/bannertool" makesmdh -s "Theme Plaza" -l "Theme Plaza" -p "Customize your 3DS!" -i meta/icon.png -o build/icon.icn >/dev/null
"$TOOLS/makerom" -f cia -o $NAME.cia -elf $NAME.elf -rsf meta/app.rsf -icon build/icon.icn -banner build/banner.bnr -logo meta/logo.bcma.lz -exefslogo -target t \
  -DAPP_TITLE="Theme Plaza" -DAPP_PRODUCT_CODE="CTR-P-TPLZ" -DAPP_UNIQUE_ID=0xA4E30 -DAPP_ROMFS="$PWD/romfs" \
  -DAPP_CATEGORY=Application -DAPP_USE_ON_SD=true -DAPP_ENCRYPTED=false -DAPP_MEMORY_TYPE=Application \
  -DAPP_SYSTEM_MODE=64MB -DAPP_SYSTEM_MODE_EXT=Legacy -DAPP_CPU_SPEED=804MHz -DAPP_ENABLE_L2_CACHE=true -DAPP_VERSION_MAJOR=$VERSION_MAJOR
# makerom ignores its version options for a CIA built from an ELF, so the version is written in afterwards
python3 ../tools/set_cia_version.py $NAME.cia $VERSION
ls -la $NAME.3dsx $NAME.cia
