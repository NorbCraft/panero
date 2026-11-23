# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Installer test (D-020) on a fresh GitHub runner, after launch-test.ps1:
#  1. silent per-user install (/VERYSILENT /CURRENTUSER): files in
#     %LOCALAPPDATA%\Programs\Panero, Start menu entry, uninstall entry in HKCU,
#     no administrator rights needed (nothing in Program Files or HKLM);
#  2. Panero starts from the installed folder (every loaded module from there or
#     Windows) and shows its main window;
#  3. "upgrade": the installer runs again over the installation; the settings
#     (%LOCALAPPDATA%\panerorc, with a marker added) are kept;
#  4. uninstall: program folder, shortcut and uninstall entry are gone, the
#     settings stay.
# Results: <OutDir>\installer-results.md (+ step summary); exit 1 on a failure.
param(
    [Parameter(Mandatory)] [string] $SetupDir,
    [Parameter(Mandatory)] [string] $OutDir
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$setup = Get-ChildItem $SetupDir -Filter 'panero-*-windows-x64-setup.exe' | Select-Object -First 1
if (-not $setup) { throw "No installer found in $SetupDir" }

$appId = '{57A1DB5D-E2D2-41F6-804E-6A302AA64E0D}_is1'
$uninstKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId"
$machineKey = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId"
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\Panero'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Panero.lnk'
$settings = Join-Path $env:LOCALAPPDATA 'panerorc'
$results = [System.Collections.Generic.List[object]]::new()
$python = (Get-Command python).Source
# Panero gets the Windows folders only on PATH (as a user without dev tools has)
$cleanPath = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
$shot = 80

function Record($check, $ok, $note = '') {
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    $results.Add([pscustomobject]@{ Check = $check; Status = $status; Note = $note })
    Write-Host "${status}: $check $(if ($note) { "- $note" })"
}

function Shot($name) {
    $script:shot++
    $file = Join-Path $OutDir ('{0:D2}-{1}.png' -f $script:shot, $name)
    & $python -c "from PIL import ImageGrab; ImageGrab.grab().save(r'$file')"
}

function Install($label) {
    $log = Join-Path $OutDir "setup-$label.log"
    $p = Start-Process -FilePath $setup.FullName -PassThru -Wait `
        -ArgumentList '/VERYSILENT', '/CURRENTUSER', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`""
    return $p.ExitCode
}

function StartInstalled($label) {
    $exe = Join-Path $installDir 'panero.exe'
    $savedPath = $env:Path
    $env:Path = $cleanPath
    $p = Start-Process -FilePath $exe -PassThru -WorkingDirectory $env:USERPROFILE
    $env:Path = $savedPath
    $title = ''
    for ($i = 0; $i -lt 60 -and -not $p.HasExited; $i++) {
        Start-Sleep -Seconds 1
        $p.Refresh()
        if ($p.MainWindowHandle -ne 0 -and $p.MainWindowTitle) { $title = $p.MainWindowTitle; break }
    }
    Start-Sleep -Seconds 3
    Shot "installed-panero-$label"
    $foreign = @()
    if (-not $p.HasExited) {
        $foreign = @($p.Modules | Where-Object {
                $_.FileName -notlike "$installDir\*" -and $_.FileName -notlike "$env:SystemRoot\*" } |
            ForEach-Object FileName)
    }
    Record "Panero starts from the installed folder ($label)" ($title -ne '' -and -not $p.HasExited) "window title '$title'"
    Record "Every loaded module comes from the installed folder or Windows ($label)" ($title -ne '' -and $foreign.Count -eq 0) `
        ($(if ($foreign.Count) { $foreign -join '; ' } else { "$(@($p.Modules).Count) modules" }))
    if (-not $p.HasExited) {
        $null = $p.CloseMainWindow()
        if (-not $p.WaitForExit(15000)) { $p.Kill() }
    }
    Get-Process kioslave5, kiod5 -ErrorAction SilentlyContinue | Stop-Process -Force
}

"Installer: $($setup.Name) ($([math]::Round($setup.Length / 1MB, 1)) MB)" | Tee-Object (Join-Path $OutDir 'installer-summary.txt')
$version = ($setup.Name -replace '^panero-(.*)-windows-x64-setup\.exe$', '$1')

# --- 1. install ---------------------------------------------------------------
$code = Install 'install'
Record 'Silent per-user install (/VERYSILENT /CURRENTUSER) succeeds' ($code -eq 0) "exit code $code"
Record 'Program installed to %LOCALAPPDATA%\Programs\Panero' (Test-Path (Join-Path $installDir 'panero.exe')) $installDir
foreach ($f in 'kioslave5.exe', 'qt.conf', 'plugins\platforms\qwindows.dll', 'data\icontheme.rcc', 'unins000.exe') {
    Record "Installed file $f" (Test-Path (Join-Path $installDir $f))
}
Record 'Start menu entry' (Test-Path $startMenu) $startMenu
Record 'No desktop icon unless chosen' (-not (Test-Path (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Panero.lnk')))
$reg = Get-ItemProperty $uninstKey -ErrorAction SilentlyContinue
Record 'Uninstall entry in HKCU (Apps & features)' ($null -ne $reg -and $reg.DisplayName -eq 'Panero' -and $reg.DisplayVersion -eq $version) `
    "DisplayName '$($reg.DisplayName)', DisplayVersion '$($reg.DisplayVersion)', InstallLocation '$($reg.InstallLocation)'"
Record 'Nothing machine-wide (no HKLM entry, nothing in Program Files)' (-not (Test-Path $machineKey) -and -not (Test-Path "$env:ProgramFiles\Panero"))

# --- 2. start from the installed location ---------------------------------------
StartInstalled 'after-install'

# --- 3. upgrade: install again over it, settings kept ------------------------------
$marker = "PaneroInstallerTest=$(Get-Random)"
if (Test-Path $settings) {
    Add-Content -Path $settings -Value "`n[Panero Installer Test]`n$marker"
} else {
    Set-Content -Path $settings -Value "[Panero Installer Test]`n$marker"
}
$before = (Get-Item (Join-Path $installDir 'panero.exe')).LastWriteTimeUtc
$code = Install 'upgrade'
$reg = Get-ItemProperty $uninstKey -ErrorAction SilentlyContinue
$entries = @(Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' |
    Where-Object { (Get-ItemProperty $_.PSPath).DisplayName -eq 'Panero' })
Record 'Upgrade (running the installer again) succeeds in place' ($code -eq 0 -and (Test-Path (Join-Path $installDir 'panero.exe'))) `
    "exit code $code; panero.exe written $before -> $((Get-Item (Join-Path $installDir 'panero.exe')).LastWriteTimeUtc)"
Record 'One uninstall entry after the upgrade (same AppId)' ($entries.Count -eq 1) "$($entries.Count) entries"
Record 'Settings kept by the upgrade' ((Test-Path $settings) -and (Select-String -Path $settings -SimpleMatch $marker -Quiet)) $settings
StartInstalled 'after-upgrade'
Record 'Settings kept after starting the upgraded Panero' ((Test-Path $settings) -and (Select-String -Path $settings -SimpleMatch $marker -Quiet))

# --- 4. uninstall --------------------------------------------------------------------
$uninstaller = Join-Path $installDir 'unins000.exe'
$p = Start-Process -FilePath $uninstaller -PassThru -Wait -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$(Join-Path $OutDir 'uninstall.log')`""
for ($i = 0; $i -lt 30 -and (Test-Path (Join-Path $installDir 'panero.exe')); $i++) { Start-Sleep -Seconds 1 } # it finishes in a copy of itself
Start-Sleep -Seconds 2
Record 'Silent uninstall succeeds' ($p.ExitCode -eq 0) "exit code $($p.ExitCode)"
Record 'Program folder removed' (-not (Test-Path $installDir)) $installDir
Record 'Start menu entry removed' (-not (Test-Path $startMenu))
Record 'Uninstall entry removed' (-not (Test-Path $uninstKey))
Record 'Settings stay after uninstalling (user data)' ((Test-Path $settings) -and (Select-String -Path $settings -SimpleMatch $marker -Quiet))

$md = "| Check | Result | Note |`n|---|---|---|`n" + (($results | ForEach-Object { "| $($_.Check) | $($_.Status) | $($_.Note) |" }) -join "`n") + "`n"
Set-Content -Path (Join-Path $OutDir 'installer-results.md') -Value $md -Encoding utf8
if ($env:GITHUB_STEP_SUMMARY) { Add-Content -Path $env:GITHUB_STEP_SUMMARY -Value "## Panero Windows installer checks`n`n$md" }
if ($results | Where-Object Status -eq 'FAIL') { exit 1 }
exit 0
