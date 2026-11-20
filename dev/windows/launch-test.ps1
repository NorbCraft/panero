# SPDX-License-Identifier: GPL-2.0-or-later
# Launch check for the portable Windows ZIP, run on a fresh GitHub runner.
#  1. Unzip, hide MSYS2 (C:\msys64) and strip PATH to Windows folders only,
#     so nothing outside the ZIP can satisfy a missing DLL.
#  2. Start panero.exe as a first-time user would, wait for the main window,
#     take a screenshot and check that every loaded module (also in the
#     kioslave5.exe workers) comes from the ZIP folder or from Windows.
#  3. Run the Total Commander style GUI checks (gui-test.py) on disposable files.
param(
    [Parameter(Mandatory)] [string] $ZipDir,
    [Parameter(Mandatory)] [string] $OutDir
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$zip = Get-ChildItem $ZipDir -Filter 'panero-*-windows-x64.zip' | Select-Object -First 1
if (-not $zip) { throw "No ZIP found in $ZipDir" }
$root = 'C:\pt'
Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
Expand-Archive $zip.FullName -DestinationPath $root
$app = Join-Path $root 'Panero'
$exe = Join-Path $app 'panero.exe'
"ZIP: $($zip.Name) ($([math]::Round($zip.Length / 1MB, 1)) MB)" | Tee-Object "$OutDir\summary.txt"

# --- 1. clean environment -------------------------------------------------
if (Test-Path 'C:\msys64') {
    Rename-Item 'C:\msys64' 'C:\msys64-hidden-by-test'
    'MSYS2 hidden (C:\msys64 renamed)' | Tee-Object -Append "$OutDir\summary.txt"
}
$cleanPath = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem;$env:SystemRoot\System32\WindowsPowerShell\v1.0"

function Save-Screen([string] $name) {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $bmp.Save((Join-Path $OutDir "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
}

function Start-Panero([string[]] $arguments) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo $exe
    foreach ($a in $arguments) { $psi.ArgumentList.Add($a) }
    $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $env:USERPROFILE
    $psi.Environment['PATH'] = $cleanPath
    $psi.Environment['QT_LOGGING_RULES'] = 'kf.kio.*=true'
    $psi.Environment['QT_FORCE_STDERR_LOGGING'] = '1'
    $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $script:stderrTask = $p.StandardError.ReadToEndAsync()
    return $p
}

function Wait-Window($p, [int] $seconds) {
    for ($i = 0; $i -lt $seconds * 2; $i++) {
        Start-Sleep -Milliseconds 500
        $p.Refresh()
        if ($p.HasExited) { return $false }
        if ($p.MainWindowHandle -ne 0 -and $p.MainWindowTitle) { return $true }
    }
    return $false
}

function Test-Modules($procs) {
    $bad = @()
    $all = @()
    foreach ($proc in $procs) {
        foreach ($m in $proc.Modules) {
            $all += "$($proc.ProcessName)`t$($m.FileName)"
            $f = $m.FileName
            if ($f.StartsWith($app, 'OrdinalIgnoreCase')) { continue }
            if ($f.StartsWith($env:SystemRoot, 'OrdinalIgnoreCase')) { continue }
            $bad += "$($proc.ProcessName): $f"
        }
    }
    $all | Sort-Object -Unique | Set-Content "$OutDir\loaded-modules.txt"
    return $bad
}

# --- 2. first launch ------------------------------------------------------
$p = Start-Panero @()
$up = Wait-Window $p 60
Start-Sleep -Seconds 4
Save-Screen '01-first-launch'
if (-not $up) {
    $code = if ($p.HasExited) { "exit code $($p.ExitCode)" } else { 'no window' }
    "FAIL: Panero did not show a window ($code)" | Tee-Object -Append "$OutDir\summary.txt"
    if (-not $p.HasExited) { $p.Kill() }
    $script:stderrTask.Result | Set-Content "$OutDir\stderr-first-launch.txt"
    exit 1
}
$p.Refresh()
"PASS: main window '$($p.MainWindowTitle)' after start" | Tee-Object -Append "$OutDir\summary.txt"

$procs = @($p) + @(Get-Process kioslave5, kiod5 -ErrorAction SilentlyContinue)
$bad = Test-Modules $procs
"Modules checked in: $(($procs | ForEach-Object ProcessName) -join ', ')" | Tee-Object -Append "$OutDir\summary.txt"
if ($bad.Count) {
    'FAIL: modules loaded from outside the ZIP folder and Windows:' | Tee-Object -Append "$OutDir\summary.txt"
    $bad | Tee-Object -Append "$OutDir\summary.txt"
} else {
    'PASS: every loaded module comes from the ZIP folder or Windows' | Tee-Object -Append "$OutDir\summary.txt"
}

# Close it like a user (Alt+F4 equivalent: WM_CLOSE) and collect the log
$null = $p.CloseMainWindow()
if (-not $p.WaitForExit(15000)) { $p.Kill(); 'WARN: had to kill Panero after close' | Tee-Object -Append "$OutDir\summary.txt" }
$script:stderrTask.Result | Set-Content "$OutDir\stderr-first-launch.txt"
Get-Process kioslave5, kiod5 -ErrorAction SilentlyContinue | Stop-Process -Force

# --- 3. GUI checks --------------------------------------------------------
$gui = Join-Path $PSScriptRoot 'gui-test.py'
if (Test-Path $gui) {
    python -m pip install --quiet pywinauto==0.6.9 pillow==11.3.0
    $env:PANERO_EXE = $exe
    $env:PANERO_CLEAN_PATH = $cleanPath
    python $gui --out $OutDir
    $guiExit = $LASTEXITCODE
} else { $guiExit = 0 }

if ($bad.Count -or $guiExit -ne 0) { exit 1 }
