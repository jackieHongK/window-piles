# Usage: powershell -ExecutionPolicy Bypass -File build.ps1 [-Config Release|Debug] [-Clean]
# Builds with the CMake bundled in VS 2022 (Build Tools is enough) using the Visual Studio generator.
# Outputs: build\bin\<Config>\*.exe
#
# Note: the Ninja generator + vcvars environment crashes CMake's compiler detection (0xC0000409)
# on this machine when the user profile path contains non-ASCII characters, so we use the
# Visual Studio generator, which needs no vcvars environment at all.
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio 2022 Build Tools (C++ workload).' }
$vsRoot = & $vswhere -latest -products * -property installationPath
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake' }

if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

& $cmake -S $root -B $buildDir -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }
& $cmake --build $buildDir --config $Config -- /m /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Write-Host "[build] OK: $buildDir\bin\$Config"
