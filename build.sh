#!/usr/bin/env bash
# Mac / Linux: needs Docker running and an internet connection (first run downloads ~1 GB).
set -euo pipefail
cd "$(dirname "$0")"

docker run --rm -v "$PWD":/project -w /project devkitpro/devkitppc \
  bash -c 'export DEVKITPPC=/opt/devkitpro/devkitPPC DEVKITPRO=/opt/devkitpro && make clean && make'

mkdir -p apps/Alley_Showdown
cp Alley_Showdown.dol apps/Alley_Showdown/boot.dol
ls -l Alley_Showdown.dol
echo
echo "DONE."
echo " - Dolphin:      File > Open > Alley_Showdown.dol"
echo " - Real Wii/USB Loader GX/Homebrew Channel: copy the 'apps' folder to the SD card root."
