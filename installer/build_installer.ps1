# Builds Cosmic Platformer in Release, runs the tests, stages the installation and
# compiles the Windows installer with Inno Setup.
#
# Requirements: Visual Studio 2022 (C++ workload) or Build Tools, CMake >= 3.24, Git,
# Inno Setup 6 (https://jrsoftware.org/isinfo.php). Internet is only needed the first
# time to download the dependencies (SDL3, SDL3_image, SDL3_ttf, SDL3_mixer, json, miniz).
#
# Usage (PowerShell):  .\installer\build_installer.ps1

param(
    [string]$Config = "Release",
    [string]$Generator = "Visual Studio 17 2022",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build"
$Stage = Join-Path $Build "stage"

Write-Host "== Configuration" -ForegroundColor Cyan
cmake -S $Root -B $Build -G $Generator -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }

Write-Host "== Compilation ($Config)" -ForegroundColor Cyan
cmake --build $Build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

if (-not $SkipTests) {
    Write-Host "== Tests" -ForegroundColor Cyan
    ctest --test-dir $Build -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
}

Write-Host "== Installation (staging)" -ForegroundColor Cyan
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
cmake --install $Build --config $Config --prefix $Stage
if ($LASTEXITCODE -ne 0) { throw "Staging failed" }

Write-Host "== Installateur" -ForegroundColor Cyan
$Iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
if (-not $Iscc) { $Iscc = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe" }
if (-not (Test-Path $Iscc)) { throw "Inno Setup 6 (ISCC.exe) introuvable. Installez-le : https://jrsoftware.org/isinfo.php" }
& $Iscc "/DStageDir=$Stage" (Join-Path $PSScriptRoot "CosmicPlatformer.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

Write-Host "Installateur créé dans installer\Output\" -ForegroundColor Green
