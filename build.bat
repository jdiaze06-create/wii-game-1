@echo off
rem Windows: needs Docker Desktop running and an internet connection (first run downloads ~1 GB).
cd /d "%~dp0"

docker run --rm -v "%cd%":/project -w /project devkitpro/devkitppc bash -c "export DEVKITPPC=/opt/devkitpro/devkitPPC DEVKITPRO=/opt/devkitpro && make clean && make"
if errorlevel 1 (
  echo.
  echo BUILD FAILED - copy the messages above and send them to get help.
  pause
  exit /b 1
)

if not exist apps\Dead_Light mkdir apps\Dead_Light
copy /y Dead_Light.dol apps\Dead_Light\boot.dol >nul
dir Dead_Light.dol
echo.
echo DONE.
echo  - Dolphin: File ^> Open ^> Dead_Light.dol
echo  - Real Wii / Homebrew Channel / USB Loader GX: copy the "apps" folder to the SD card root.
pause
