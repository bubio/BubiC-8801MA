# build_windows.ps1 — BubiC-8801MA ローカルビルド (Windows / MSVC)
# 使い方: .\scripts\build_windows.ps1 [-BuildType Debug|Release] [-Arch x64|ARM64]
#
# 前提: Visual Studio 2019/2022 (C++ ワークロード) がインストール済みであること
param(
    [string]$BuildType = "Release",
    [string]$Arch      = "x64"
)

$ErrorActionPreference = "Stop"
$BuildDir = "build_$Arch"

Write-Host "=== BubiC-8801MA Windows Build ===" -ForegroundColor Cyan
Write-Host "  Build type : $BuildType"
Write-Host "  Architecture: $Arch"
Write-Host ""

# ==========================================
# SDL3 Policy (Windows)
# Use official SDL release 3.4.2 (no source build)
# Expect extracted SDL next to repository:
#   third_party/SDL3-3.4.2/
# ==========================================

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$SdlRoot = Join-Path $ProjectRoot "third_party\SDL3-3.4.2"

if (-not (Test-Path $SdlRoot)) {
    Write-Host "[INFO] SDL3-3.4.2 not found. Downloading official release..." -ForegroundColor Yellow

    $version = "3.4.2"
    $baseUrl = "https://github.com/libsdl-org/SDL/releases/download/release-$version"
    $zipName = "SDL3-devel-$version-VC.zip"
    $zipPath = Join-Path $ProjectRoot $zipName

    Invoke-WebRequest -Uri "$baseUrl/$zipName" -OutFile $zipPath

    Expand-Archive $zipPath -DestinationPath $ProjectRoot -Force

    $extracted = Get-ChildItem -Path $ProjectRoot -Directory -Filter "SDL3-$version*" | Select-Object -First 1
    if ($null -eq $extracted) {
        throw "Failed to extract SDL3 archive."
    }

    New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot "third_party") | Out-Null
    Move-Item $extracted.FullName $SdlRoot

    Remove-Item $zipPath -Force

    Write-Host "[INFO] SDL3-3.4.2 downloaded and prepared." -ForegroundColor Green
}

if (-not (Test-Path $SdlRoot)) {
    throw "SDL3-3.4.2 setup failed."
}

$SdlCMakePath = Join-Path $SdlRoot "cmake"
if (-not (Test-Path $SdlCMakePath)) {
    throw "SDL3 cmake config not found in $SdlRoot"
}

$CMakeArch = if ($Arch -eq "ARM64") { "ARM64" } else { "x64" }

cmake -S . -B $BuildDir `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -A $CMakeArch `
    -DCMAKE_PREFIX_PATH="$SdlCMakePath"

cmake --build $BuildDir --config $BuildType -j $env:NUMBER_OF_PROCESSORS

Write-Host ""
Write-Host "=== Build complete ===" -ForegroundColor Green
Write-Host "  EXE: $BuildDir\$BuildType\BubiC-8801MA.exe"
