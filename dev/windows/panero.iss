; SPDX-FileCopyrightText: 2026 Panero contributors
; SPDX-License-Identifier: GPL-2.0-or-later
;
; Panero Windows installer (D-020): Inno Setup 6, per user without
; administrator rights (%LOCALAPPDATA%\Programs\Panero), Start menu entry,
; optional desktop icon, uninstaller. The AppId never changes, so a newer
; installer upgrades the same installation in place. Settings live in the
; user profile (%LOCALAPPDATA%\panerorc, ...), not in the program folder, so
; upgrades keep them; uninstalling leaves them too.
;
; Built by .github/workflows/windows.yml from the folder that becomes the
; portable ZIP:
;   ISCC.exe /DSourceDir=<dist>\panero-<ver>-windows-x64\Panero /DAppVersion=<ver>
;            /DNumericVersion=<x.y.z> /DOutputDir=<dist> dev\windows\panero.iss

#ifndef SourceDir
  #error SourceDir (the unpacked portable folder) must be given with /DSourceDir=...
#endif
#ifndef AppVersion
  #error AppVersion must be given with /DAppVersion=...
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; fixed for every Panero release: upgrades find the existing installation
AppId={{57A1DB5D-E2D2-41F6-804E-6A302AA64E0D}
AppName=Panero
AppVersion={#AppVersion}
AppVerName=Panero {#AppVersion}
AppPublisher=Panero contributors
AppPublisherURL=https://github.com/NorbCraft/panero
AppSupportURL=https://github.com/NorbCraft/panero/issues
AppUpdatesURL=https://github.com/NorbCraft/panero/releases
AppComments=Twin-panel file manager, an independent fork of Krusader
VersionInfoVersion={#NumericVersion}
VersionInfoProductVersion={#NumericVersion}
VersionInfoProductTextVersion={#AppVersion}
; per user, no administrator rights: {autopf} = %LOCALAPPDATA%\Programs
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
DefaultDirName={autopf}\Panero
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=panero-{#AppVersion}-windows-x64-setup
SetupIconFile=..\..\app\windows\panero.ico
UninstallDisplayIcon={app}\panero.exe
UninstallDisplayName=Panero
LicenseFile=..\..\LICENSES\GPL-2.0-or-later.txt
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; a running Panero is closed for the upgrade (Restart Manager)
CloseApplications=yes
RestartApplications=no
ChangesAssociations=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; an upgrade replaces the program files completely (no stale plugins or DLLs);
; the settings are elsewhere and stay
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\data"
Type: files; Name: "{app}\*.dll"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Panero"; Filename: "{app}\panero.exe"; WorkingDir: "{userdocs}"; Comment: "Twin-panel file manager"
Name: "{autodesktop}\Panero"; Filename: "{app}\panero.exe"; WorkingDir: "{userdocs}"; Tasks: desktopicon

[Run]
Filename: "{app}\panero.exe"; Description: "{cm:LaunchProgram,Panero}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}"
