# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Launch check for the portable Windows ZIP, run on a fresh GitHub runner.
#  1. Unzip, hide MSYS2 (C:\msys64) and give Panero a PATH with Windows folders
#     only, so nothing outside the ZIP can satisfy a missing DLL.
#  2. gui-test.py starts panero.exe as a first-time user would, goes through the
#     first-run dialogs, checks that every module loaded by Panero and its KIO
#     workers comes from the ZIP folder or Windows, then runs the Total
#     Commander style checks on disposable files.
param(
    [Parameter(Mandatory)] [string] $ZipDir,
    [Parameter(Mandatory)] [string] $OutDir
)
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$zip = Get-ChildItem $ZipDir -Filter 'panero-*-windows-x64.zip' | Select-Object -First 1
if (-not $zip) { throw "No ZIP found in $ZipDir" }
$root = 'C:\pt'
Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
Expand-Archive $zip.FullName -DestinationPath $root
"ZIP: $($zip.Name) ($([math]::Round($zip.Length / 1MB, 1)) MB, " +
    "$((Get-ChildItem -Recurse -File "$root\Panero").Count) files)" | Tee-Object "$OutDir\summary.txt"

if (Test-Path 'C:\msys64') {
    Rename-Item 'C:\msys64' 'C:\msys64-hidden-by-test'
    'MSYS2 hidden (C:\msys64 renamed)' | Tee-Object -Append "$OutDir\summary.txt"
}
(Get-CimInstance Win32_OperatingSystem).Caption | Tee-Object -Append "$OutDir\summary.txt"

python -m pip install --quiet pywinauto==0.6.9 pillow==11.3.0 psutil==7.0.0
$env:PANERO_EXE = "$root\Panero\panero.exe"
$env:PANERO_CLEAN_PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
python "$PSScriptRoot\gui-test.py" --out $OutDir
$code = $LASTEXITCODE

# Preview 13's focused checks on the same fresh runner: Quick View and the file
# operations (progress windows, jobs button, Undo, marks). They start past the
# first-start assistant, which gui-test.py has already checked.
$env:PANERO_NO_FIRST_START_ASSISTANT = '1'
foreach ($t in @(@{ py = 'quickview-test.py'; dir = 'quickview' }, @{ py = 'jobs-gui-test.py'; dir = 'jobs' })) {
    $out = Join-Path $OutDir $t.dir
    New-Item -ItemType Directory -Force $out | Out-Null
    python "$PSScriptRoot\$($t.py)" --work "C:\pt\$($t.dir)" --out $out
    "$($t.py): exit code $LASTEXITCODE" | Tee-Object -Append "$OutDir\summary.txt"
    if ($LASTEXITCODE -ne 0 -and $code -eq 0) { $code = $LASTEXITCODE }
}
exit $code
