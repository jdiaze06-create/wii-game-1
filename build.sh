#!/usr/bin/env bash
# Mac / Linux: needs Docker running and an internet connection (first run downloads ~1 GB).
# Everything is also saved to build_log.txt so you can send it for help.
set -o pipefail
cd "$(dirname "$0")"

docker run --rm -v "$PWD":/project -w /project devkitpro/devkitppc bash -c '
export DEVKITPPC=/opt/devkitpro/devkitPPC DEVKITPRO=/opt/devkitpro
echo "== toolchain check =="
for f in $DEVKITPPC/bin/powerpc-eabi-gcc $DEVKITPRO/libogc/include/gccore.h \
         $DEVKITPRO/libogc/include/asndlib.h $DEVKITPRO/libogc/include/wiiuse/wpad.h \
         $DEVKITPRO/libogc/lib/wii/libogc.a $DEVKITPRO/libogc/lib/wii/libasnd.a \
         $DEVKITPRO/libogc/lib/wii/libwiiuse.a $DEVKITPRO/libogc/lib/wii/libbte.a \
         $DEVKITPRO/tools/bin/elf2dol; do
  if [ -e "$f" ]; then echo "ok       $f"; else echo "MISSING  $f"; fi
done
echo "== building =="
make clean && make
' 2>&1 | tee build_log.txt
STATUS=${PIPESTATUS[0]}

if [ "$STATUS" -ne 0 ] || [ ! -f Dead_Light.dol ]; then
  echo
  echo "BUILD FAILED. The full text is saved in build_log.txt - send it for a fix."
  exit 1
fi

mkdir -p apps/Dead_Light
cp Dead_Light.dol apps/Dead_Light/boot.dol
ls -l Dead_Light.dol
echo
echo "DONE."
echo " - Dolphin:      File > Open > Dead_Light.dol"
echo " - Real Wii/Homebrew Channel/USB Loader GX: copy the 'apps' folder to the SD card root."
