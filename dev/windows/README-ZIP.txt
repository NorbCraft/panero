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
- ISO image browsing

Archives
--------
Pack (Alt+F5), Unpack (Alt+F9) and Enter on an archive use the 7-Zip in the
"7-Zip" folder (zip, 7z, rar to unpack, gzip, bzip2, xz) and Windows' own
tar.exe (.tar, .tar.gz, .tar.bz2, .tar.xz).

License and source code
-----------------------
Panero is free software under the GNU General Public License, version 2 or
later. The license texts are in the "licenses" folder.

Source code of Panero:      https://github.com/NorbCraft/panero
The included libraries (Qt 5, KDE Frameworks 5, the MinGW-w64/GCC runtime and
others) are the MSYS2 UCRT64 builds; their source packages are published at
https://repo.msys2.org/mingw/sources/ and the build recipes at
https://github.com/msys2/MINGW-packages .

7-Zip (7z.exe, 7z.dll in the "7-Zip" folder) is the official release from
https://www.7-zip.org/ by Igor Pavlov, under the GNU LGPL with the unRAR
restriction; see licenses/7-Zip-License.txt. Its source code is at
https://www.7-zip.org/download.html .
