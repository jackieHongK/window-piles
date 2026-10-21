# Builds Release and packs build\dist\WindowPiles-<Version>-win-x64.zip (WindowPiles.exe + release readme)
# Usage (from the repo root):
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Version 0.1.1 [-SkipBuild]
# Publish (separately, after checking the zip):
#   gh release create v<Version> build\dist\WindowPiles-<Version>-win-x64.zip --title "..." --notes "..."
param([Parameter(Mandatory)] [string]$Version, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $SkipBuild) { & "$root\build.ps1" -Config Release }

$name = "WindowPiles-$Version-win-x64"
$stage = "$root\build\dist\$name"
$zip = "$root\build\dist\$name.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "$root\build\bin\Release\WindowPiles.exe" $stage

# The readme's file name is Korean; looked up by glob so this script stays ASCII (PS 5.1 reads BOM-less scripts as ANSI).
$readme = Get-ChildItem "$PSScriptRoot\release\*.txt" | Select-Object -First 1
$text = [IO.File]::ReadAllText($readme.FullName, [Text.Encoding]::UTF8).Replace('{VERSION}', $Version)
[IO.File]::WriteAllText((Join-Path $stage $readme.Name), $text, (New-Object Text.UTF8Encoding $true))

if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "[package] OK: $zip"
