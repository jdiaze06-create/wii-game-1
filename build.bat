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

if not exist apps\Alley_Showdown mkdir apps\Alley_Showdown
copy /y Alley_Showdown.dol apps\Alley_Showdown\boot.dol >nul
dir Alley_Showdown.dol
echo.
echo DONE.
echo  - Dolphin: File ^> Open ^> Alley_Showdown.dol
echo  - Real Wii / USB Loader GX / Homebrew Channel: copy the "apps" folder to the SD card root.
pause
