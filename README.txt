ALLEY SHOWDOWN  -  Wii homebrew (original 1930s-cartoon-style shooting gallery)
==============================================================================

IMPORTANT: this zip contains the SOURCE CODE. The compiled game file (.dol) is
made by running the build script ONCE on your computer. (The compiler can't be
included here; it is downloaded automatically by Docker.)

STEP 1 - Install Docker
  Docker Desktop: https://www.docker.com/products/docker-desktop/   (start it)

STEP 2 - Build
  Windows:     double-click  build.bat
  Mac/Linux:   open a terminal in this folder and run:  ./build.sh
  First run downloads about 1 GB. When it says DONE you will have:
      Alley_Showdown.dol
      apps/Alley_Showdown/boot.dol   (+ meta.xml)

STEP 3 - Play
  Dolphin:
      File > Open > Alley_Showdown.dol
      (Controllers > Wii Remote: "Emulated Wii Remote"; IR pointer follows the mouse.)
  Real Wii (Homebrew Channel) / USB Loader GX:
      Copy the "apps" folder to the root of your SD card (or USB drive).
      Launch "Alley Showdown" from the Homebrew Channel, or from USB Loader GX's
      homebrew/apps launcher if you have that enabled.

  NOTE: this is a homebrew .dol, not a Wii disc image. It does not need an
  ISO/WBFS and will not appear in the normal game list.

CONTROLS
  Aim with the Wiimote | B = shoot | A = reload | HOME = quit
  D-pad aims if the pointer isn't detected.
  Shoot cats and crows. Do NOT shoot the bunny. Baddies that stay up cost you a life.

IF SOMETHING GOES WRONG
  - Build errors: copy the text from the build window and send it for a fix.
  - Black screen in Dolphin: make sure you opened the freshly built Alley_Showdown.dol
    (it should be tens of KB or larger), then send Dolphin's log (View > Show Log).


NO-INSTALL OPTION: BUILD ON GITHUB (free account, all in your browser)
  1. Go to github.com, sign in, click + > New repository, name it anything, Create.
  2. Click "uploading an existing file" and drag in everything from this folder
     (Makefile, src, apps, build files). If the hidden ".github" folder doesn't
     upload, click Add file > Create new file, type  .github/workflows/build.yml
     as the name, paste in the contents of that file, and commit.
  3. Open the "Actions" tab. The "Build Wii game" run starts by itself (2-3 min).
     If not: pick it on the left and press "Run workflow".
  4. Open the finished run, scroll to "Artifacts", download "Alley_Showdown_Wii".
     Inside is Alley_Showdown.dol (for Dolphin) and an apps folder (for SD card).
  If the run shows a red X, open it and send me the error text from the "Compile" step.
