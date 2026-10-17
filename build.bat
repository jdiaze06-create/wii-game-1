@echo off
rem Windows: needs Docker Desktop running and an internet connection (first run downloads ~1 GB).
rem Everything is saved to build_log.txt so you can send it for help.
cd /d "%~dp0"

docker run --rm -v "%cd%":/project -w /project devkitpro/devkitppc bash -c "export DEVKITPPC=/opt/devkitpro/devkitPPC DEVKITPRO=/opt/devkitpro; echo == toolchain check ==; for f in $DEVKITPPC/bin/powerpc-eabi-gcc $DEVKITPRO/libogc/include/gccore.h $DEVKITPRO/libogc/include/asndlib.h $DEVKITPRO/libogc/include/wiiuse/wpad.h $DEVKITPRO/libogc/lib/wii/libogc.a $DEVKITPRO/libogc/lib/wii/libasnd.a $DEVKITPRO/libogc/lib/wii/libwiiuse.a $DEVKITPRO/libogc/lib/wii/libbte.a $DEVKITPRO/tools/bin/elf2dol; do if [ -e $f ]; then echo ok       $f; else echo MISSING  $f; fi; done; echo == building ==; make clean && make" > build_log.txt 2>&1
set BUILD_STATUS=%errorlevel%
type build_log.txt

if not "%BUILD_STATUS%"=="0" goto failed
if not exist Dead_Light.dol goto failed

if not exist apps\Dead_Light mkdir apps\Dead_Light
copy /y Dead_Light.dol apps\Dead_Light\boot.dol >nul
dir Dead_Light.dol
echo.
echo DONE.
echo  - Dolphin: File ^> Open ^> Dead_Light.dol
echo  - Real Wii / Homebrew Channel / USB Loader GX: copy the "apps" folder to the SD card root.
pause
exit /b 0

:failed
echo.
echo BUILD FAILED. The full text is saved in build_log.txt - send it for a fix.
pause
exit /b 1
