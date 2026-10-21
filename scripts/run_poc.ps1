# Runs every P0 PoC and writes raw results to docs\poc-raw\auto-*.md (no machine name, so results can be shared)
# Usage (from the repo root):
#   powershell -ExecutionPolicy Bypass -File scripts\run_poc.ps1 [-SkipBuild] [-SkipDrag]
#
# -SkipDrag : PoC-05 self-test moves the real mouse for ~6 s with SendInput; skip it if the PC is in use.
# Only TestWindows.exe windows are hidden/moved; no other app is touched.
param([switch]$SkipBuild, [switch]$SkipDrag)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $SkipBuild) { & "$root\build.ps1" -Config Release }
$bin = "$root\build\bin\Release"
$rawDir = "$root\docs\poc-raw"
New-Item -ItemType Directory -Force $rawDir | Out-Null
$tag = "auto"

function Start-Harness([string[]]$harnessArgs) {
    $p = Start-Process -FilePath "$bin\TestWindows.exe" -ArgumentList ($harnessArgs + @('--seconds', '180')) -PassThru -WindowStyle Minimized
    Start-Sleep -Milliseconds 1500
    return $p
}
function Stop-Harness($p) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -ErrorAction SilentlyContinue }
}

# Machine profile (for the low-end reference PC comparison)
$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
$ramGb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
$gpu = (Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name) -join ', '
$disk = (Get-PhysicalDisk | Select-Object -ExpandProperty MediaType) -join ', '
$profile = "# PoC run on $tag`n`n- CPU: $cpu`n- RAM: $ramGb GB`n- GPU: $gpu`n- Disk: $disk`n- Date: $(Get-Date -Format s)`n"
Set-Content -Encoding utf8 "$rawDir\$tag-00-machine.md" $profile

$h = Start-Harness @('--normal', '3')
& "$bin\poc_dwm.exe" --out "$rawDir\$tag-01-dwm.md" | Out-Null
Stop-Harness $h

$h = Start-Harness @('--all')
& "$bin\poc_wgc.exe" --out "$rawDir\$tag-03-wgc.md" | Out-Null
Stop-Harness $h

$h = Start-Harness @('--normal', '3')
& "$bin\poc_watchdog.exe" selftest --out "$rawDir\$tag-04-watchdog.md" | Out-Null
Stop-Harness $h

if (-not $SkipDrag) {
    $h = Start-Harness @('--normal', '3')
    & "$bin\poc_drag.exe" --selftest --out "$rawDir\$tag-05-drag.md" | Out-Null
    Stop-Harness $h
}
Write-Host "Results written to $rawDir\$tag-*.md"
