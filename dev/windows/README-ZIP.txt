Panero for Windows (preview)
============================

Panero is a twin-panel file manager, an independent fork of Krusader.
This is a preview of the Windows version, built from the same source code as
the Linux version. It has known gaps (see below).

Run
---
Unzip the folder anywhere (for example to your Desktop) and start panero.exe.
Nothing is installed and nothing outside the folder is changed, except Panero's
own settings:

  %LOCALAPPDATA%\panerorc and %LOCALAPPDATA%\panero   (delete them to reset)

Windows may show a SmartScreen warning because the file is not signed.
Choose "More info" > "Run anyway".

Not available on Windows yet
----------------------------
- Cloud drives (rclone) and Connect to Server
- Single-instance mode and remote control (these use D-Bus)
- Owners, groups and Unix permissions (Windows does not have them)
- ISO image browsing; archive browsing needs 7-Zip on PATH

License and source code
-----------------------
Panero is free software under the GNU General Public License, version 2 or
later. The license texts are in the "licenses" folder.

Source code of Panero:      https://github.com/NorbCraft/panero
The included libraries (Qt 5, KDE Frameworks 5, the MinGW-w64/GCC runtime and
others) are the MSYS2 UCRT64 builds; their source packages are published at
https://repo.msys2.org/mingw/sources/ and the build recipes at
https://github.com/msys2/MINGW-packages .
