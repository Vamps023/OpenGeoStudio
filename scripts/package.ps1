# OpenGeoStudio Packaging Script
# Builds the application and creates a portable zip archive
#
# Usage:
#   .\scripts\package.ps1 [-OutputDir <path>]
#
# Requirements:
#   - Visual Studio 2022 Build Tools
#   - CMake, Ninja
#   - Qt 6.8.0 (msvc2022_64)
#   - vcpkg
#   - MapLibre Native Qt (installed at D:\git\maplibre-native-qt\install)

param(
    [string]$OutputDir = "D:\git\OpenGeoStudio-Qt\dist"
)

$ErrorActionPreference = "Stop"

$RepoRoot = "D:\git\OpenGeoStudio-Qt"
$BuildDir = "$RepoRoot\build"
$DeployDir = "$BuildDir\deploy"
$VcpkgToolchain = "C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake"

Write-Host "=== OpenGeoStudio Packaging ===" -ForegroundColor Cyan

# Step 1: Configure and build
Write-Host "`n[1/4] Building application..." -ForegroundColor Yellow

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$cmd = "`"$vcvars`" >nul 2>&1 && "
$cmd += "cmake -S `"$RepoRoot`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=`"$VcpkgToolchain`" 2>&1 && "
$cmd += "cmake --build `"$BuildDir`" --config Release 2>&1"

cmd /c $cmd
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

# Step 2: Verify deploy directory
Write-Host "`n[2/4] Verifying deploy directory..." -ForegroundColor Yellow

if (-not (Test-Path "$DeployDir\OpenGeoStudio.exe")) {
    Write-Host "OpenGeoStudio.exe not found in deploy directory!" -ForegroundColor Red
    exit 1
}

$exeSize = (Get-Item "$DeployDir\OpenGeoStudio.exe").Length
Write-Host "  OpenGeoStudio.exe: $([math]::Round($exeSize / 1KB, 1)) KB"

$dllCount = (Get-ChildItem "$DeployDir\*.dll").Count
Write-Host "  DLLs: $dllCount"

# Step 3: Copy README
Write-Host "`n[3/4] Copying README..." -ForegroundColor Yellow
Copy-Item "$RepoRoot\PORTABLE_README.txt" "$DeployDir\README.txt" -Force

# Step 4: Create zip archive
Write-Host "`n[4/4] Creating zip archive..." -ForegroundColor Yellow

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$zipPath = "$OutputDir\OpenGeoStudio-$timestamp.zip"

if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

Compress-Archive -Path "$DeployDir\*" -DestinationPath $zipPath -CompressionLevel Optimal

$zipSize = (Get-Item $zipPath).Length
Write-Host "  Archive: $zipPath" -ForegroundColor Green
Write-Host "  Size: $([math]::Round($zipSize / 1MB, 1)) MB" -ForegroundColor Green

Write-Host "`n=== Packaging Complete ===" -ForegroundColor Cyan
Write-Host "Portable application: $DeployDir" -ForegroundColor Green
Write-Host "Zip archive: $zipPath" -ForegroundColor Green
