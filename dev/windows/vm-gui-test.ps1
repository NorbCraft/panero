# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# GUI test on the developer's Windows VM (D-022), the VM variant of
# launch-test.ps1. Run on the VM desktop from the repository clone:
#   dev/windows/vm.sh stage <name>
#   dev/windows/vm.sh gui <name> dev/windows/vm-gui-test.ps1 [-Script <py>] [-OutDir <dir>]
# The VM is a real machine, so unlike the CI runner nothing outside the clone
# is replaced or renamed:
#  - Panero runs from the staged portable folder (dist\panero-*-windows-x64\Panero);
#    MSYS2 stays where it is, PATH holds only the Windows folders;
#  - PANERO_TEST_PROFILE=1: Panero's settings and data go to Qt's test location
#    (%LOCALAPPDATA%\qttest), never to the user's %LOCALAPPDATA%\panerorc;
#  - the disposable files live in <clone>\vm-work; gui-test.py --vm registers no
#    Cloud Files test provider and removes from the Recycle Bin only the items
#    it deleted itself (original path inside the work folder);
#  - psutil (not part of the VM's Python) is installed into <VM root>\vm-pydeps.
param(
    [string] $OutDir = 'results\vm-gui',
    [string] $Script = 'gui-test.py',
    [string[]] $Extra = @()
)
$ErrorActionPreference = 'Stop'
$clone = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $clone

$dir = Get-ChildItem "$clone\dist" -Directory -Filter 'panero-*-windows-x64' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $dir -or -not (Test-Path "$($dir.FullName)\Panero\panero.exe")) {
    throw "No staged Panero in $clone\dist (run: dev/windows/vm.sh stage <name>)"
}
$exe = "$($dir.FullName)\Panero\panero.exe"
if (-not [IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $clone $OutDir }
Remove-Item -Recurse -Force $OutDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $OutDir | Out-Null
$work = Join-Path $clone 'vm-work'

$python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
if (-not (Test-Path $python)) { throw "Python 3.12 not found at $python" }
$deps = Join-Path (Split-Path $clone) 'vm-pydeps'   # shared by the clones in the VM root
if (-not (Test-Path "$deps\psutil")) {
    & $python -m pip install --quiet --disable-pip-version-check --target $deps psutil==7.0.0
}
$env:PYTHONPATH = $deps

# A fresh test profile, so the first-run checks see a first run. Only Panero's
# own files in Qt's test location are removed.
$qttest = "$env:LOCALAPPDATA\qttest"
foreach ($p in 'panerorc', 'panero', 'kxmlgui5\panero', 'panero-cloud', 'paneroui.rc') {
    Remove-Item -Recurse -Force (Join-Path $qttest $p) -ErrorAction SilentlyContinue
}

"Panero: $exe" | Tee-Object "$OutDir\summary.txt"
(Get-CimInstance Win32_OperatingSystem).Caption | Tee-Object -Append "$OutDir\summary.txt"
$env:PANERO_EXE = $exe
$env:PANERO_TEST_PROFILE = '1'
$env:PANERO_CLEAN_PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
& $python "$PSScriptRoot\$Script" --vm --work $work --out $OutDir @Extra
$code = $LASTEXITCODE
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
"exit code $code" | Tee-Object -Append "$OutDir\summary.txt"
exit $code
