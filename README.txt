ZIP301 / UNZIP301  -  CS 301 Final Project

BUILD
  1. Open Zip301.sln in Visual Studio 2022 (or newer).
  2. Build > Build Solution (Ctrl+Shift+B).
     zip301.exe and unzip301.exe are created in this folder (next to Zip301.sln).
  Both Debug and Release configurations are fully optimized (/O2, x64).

RUN (Developer Command Prompt or cmd, in this folder)
  zip301.exe gulliver.txt        -> gulliver.zip301
  unzip301.exe gulliver.zip301   -> gulliver2.txt

RUN FROM INSIDE VISUAL STUDIO
  Right-click Zip301 or Unzip301 > Set as Startup Project, then Ctrl+F5.
  The command-line argument and working directory are preset
  (Zip301: gulliver.txt, Unzip301: gulliver.zip301, working dir = this folder).
  Change them in Project > Properties > Debugging > Command Arguments.

TEST
  Put the sample .txt files and the reference executables (renamed to
  ref_zip301.exe and ref_unzip301.exe) in this folder and run run_tests.bat.
