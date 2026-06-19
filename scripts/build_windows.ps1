# build_windows.ps1 — BubiC-8801MA ローカルビルド (Windows / MSVC)
# 使い方: .\scripts\build_windows.ps1 [-BuildType Debug|Release] [-Arch x64|ARM64]
#
# 前提: Visual Studio 2026 (C++ ワークロード) がインストール済みであること
param(
    [string]$BuildType = "Release",
    [string]$Arch      = "x64"
)

$ErrorActionPreference = "Stop"
$BuildDir = "build_$Arch"
$SdlVersion = "3.4.10"

Write-Host "=== BubiC-8801MA Windows Build ===" -ForegroundColor Cyan
Write-Host "  Build type : $BuildType"
Write-Host "  Architecture: $Arch"
Write-Host ""

# ==========================================
# SDL3 Policy (Windows)
# Use official SDL release (no source build)
# Expect extracted SDL next to repository:
#   third_party/SDL3-$SdlVersion/
# ==========================================

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$SdlRoot = Join-Path $ProjectRoot "third_party\SDL3-$SdlVersion"

if (-not (Test-Path $SdlRoot)) {
    Write-Host "[INFO] SDL3-$SdlVersion not found. Downloading official release..." -ForegroundColor Yellow

    $baseUrl = "https://github.com/libsdl-org/SDL/releases/download/release-$SdlVersion"
    $zipName = "SDL3-devel-$SdlVersion-VC.zip"
    $zipPath = Join-Path $ProjectRoot $zipName

    Invoke-WebRequest -Uri "$baseUrl/$zipName" -OutFile $zipPath

    Expand-Archive $zipPath -DestinationPath $ProjectRoot -Force

    $extracted = Get-ChildItem -Path $ProjectRoot -Directory -Filter "SDL3-$SdlVersion*" | Select-Object -First 1
    if ($null -eq $extracted) {
        throw "Failed to extract SDL3 archive."
    }

    New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot "third_party") | Out-Null
    Move-Item $extracted.FullName $SdlRoot

    Remove-Item $zipPath -Force

    Write-Host "[INFO] SDL3-$SdlVersion downloaded and prepared." -ForegroundColor Green
}

if (-not (Test-Path $SdlRoot)) {
    throw "SDL3-$SdlVersion setup failed."
}

$SdlCMakePath = Join-Path $SdlRoot "cmake"
if (-not (Test-Path $SdlCMakePath)) {
    throw "SDL3 cmake config not found in $SdlRoot"
}

$CMakeArch = if ($Arch -eq "ARM64") { "ARM64" } else { "x64" }
$CMakeGenerator = "Visual Studio 18 2026"

function Get-CMakeCacheValue {
    param(
        [string[]]$Cache,
        [string]$Key
    )

    $match = $Cache | Select-String -Pattern "^$([regex]::Escape($Key)):INTERNAL=(.*)$" | Select-Object -First 1
    if ($null -eq $match) {
        return ""
    }

    return $match.Matches.Groups[1].Value
}

function Clear-CMakeConfigureCache {
    param([string]$CachePath)

    Write-Host "[INFO] Removing stale CMake cache: $CachePath" -ForegroundColor Yellow
    Remove-Item -LiteralPath $CachePath -Force

    $cmakeFilesDir = Join-Path (Split-Path -Parent $CachePath) "CMakeFiles"
    if (Test-Path $cmakeFilesDir) {
        Remove-Item -LiteralPath $cmakeFilesDir -Recurse -Force
    }
}

if (Test-Path $BuildDir) {
    $cmakeCaches = Get-ChildItem -LiteralPath $BuildDir -Recurse -Filter "CMakeCache.txt" -File

    foreach ($cmakeCache in $cmakeCaches) {
        $cache = Get-Content -LiteralPath $cmakeCache.FullName
        $cachedGenerator = Get-CMakeCacheValue -Cache $cache -Key "CMAKE_GENERATOR"
        $cachedPlatform = Get-CMakeCacheValue -Cache $cache -Key "CMAKE_GENERATOR_PLATFORM"
        $isTopLevelCache = ($cmakeCache.FullName -eq (Join-Path (Resolve-Path $BuildDir) "CMakeCache.txt"))

        if (($cachedGenerator -ne $CMakeGenerator) -or ($isTopLevelCache -and ($cachedPlatform -ne $CMakeArch))) {
            Clear-CMakeConfigureCache -CachePath $cmakeCache.FullName
        }
    }
}

cmake -S . -B $BuildDir `
    -G $CMakeGenerator `
    -A $CMakeArch `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DCMAKE_PREFIX_PATH="$SdlCMakePath"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }

cmake --build $BuildDir --config $BuildType -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) { throw "CMake build failed (exit $LASTEXITCODE)" }

Write-Host ""
Write-Host "=== Build complete ===" -ForegroundColor Green
Write-Host "  EXE: $BuildDir\$BuildType\BubiC-8801MA.exe"
