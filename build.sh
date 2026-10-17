#!/usr/bin/env bash
# Mac / Linux: needs Docker running and an internet connection (first run downloads ~1 GB).
set -euo pipefail
cd "$(dirname "$0")"

docker run --rm -v "$PWD":/project -w /project devkitpro/devkitppc \
  bash -c 'export DEVKITPPC=/opt/devkitpro/devkitPPC DEVKITPRO=/opt/devkitpro && make clean && make'

mkdir -p apps/Dead_Light
cp Dead_Light.dol apps/Dead_Light/boot.dol
ls -l Dead_Light.dol
echo
echo "DONE."
echo " - Dolphin:      File > Open > Dead_Light.dol"
echo " - Real Wii/Homebrew Channel/USB Loader GX: copy the 'apps' folder to the SD card root."
