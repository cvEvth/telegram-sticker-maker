# Usage: powershell -ExecutionPolicy Bypass -File scripts\package.ps1 [-SkipBuild] [-SkipInstaller]
# Builds the release preset, assembles dist\TelegramStickerMaker-<version>-win64\ (runtime files,
# VC++ runtime DLLs, licenses, readme), zips it, and builds the Inno Setup installer when ISCC is found.
# NOTE: keep this file ASCII-only; Windows PowerShell 5.1 reads BOM-less files with the ANSI code page.
param(
    [switch]$SkipBuild,
    [switch]$SkipInstaller
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ---- version: TSM_VERSION (set by the release workflow from the tag), else the default in CMakeLists.txt
if ($env:TSM_VERSION) {
    $version = $env:TSM_VERSION
} else {
    $cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
    if ($cmake -notmatch 'set\(TSM_VERSION (\d+\.\d+\.\d+)\)') { throw 'Cannot find the default version in CMakeLists.txt' }
    $version = $Matches[1]
}
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid version '$version' (expected x.y.z)" }
$name = "TelegramStickerMaker-$version-win64"
Write-Host "[package] version $version"

# ---- release build
if (-not $SkipBuild) {
    & cmd /c "scripts\build.bat release"
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
}
$bin = Join-Path $root 'build\release\bin'
if (-not (Test-Path (Join-Path $bin 'TelegramStickerMaker.exe'))) { throw "Release build not found in $bin" }

# ---- stage files
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

$skip = @('*.pdb', '*.ilk', '*.lib', '*.exp', 'tst_*.exe')
Get-ChildItem $bin -File | Where-Object {
    $file = $_.Name
    -not ($skip | Where-Object { $file -like $_ })
} | Copy-Item -Destination $stage
Get-ChildItem $bin -Directory | ForEach-Object { Copy-Item $_.FullName -Destination $stage -Recurse }

# ---- Visual C++ runtime (app-local, so no separate redistributable install is needed)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $crt) { throw 'Microsoft.VC143.CRT not found under the Visual Studio redist folder' }
foreach ($dll in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll') {
    Copy-Item (Join-Path $crt $dll) -Destination $stage
}

# ---- licenses and readme
$licenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
$ffmpegRoot = if ($env:FFMPEG_ROOT) { $env:FFMPEG_ROOT } else { 'D:\deps\ffmpeg' }
Copy-Item (Join-Path $ffmpegRoot 'LICENSE.txt') (Join-Path $licenses 'LGPL-3.0.txt')
$share = Join-Path $root 'build\release\vcpkg_installed\x64-windows\share'
Copy-Item (Join-Path $share 'libwebp\copyright') (Join-Path $licenses 'libwebp.txt')
Copy-Item (Join-Path $share 'libzip\copyright') (Join-Path $licenses 'libzip.txt')
Copy-Item (Join-Path $share 'zlib\copyright') (Join-Path $licenses 'zlib.txt')
Get-ChildItem (Join-Path $root 'packaging') -Filter '*.txt' | Copy-Item -Destination $stage

# ---- portable zip
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
# Compress-Archive in PowerShell 5.1 writes non-ASCII names in the ANSI code page; use UTF-8 explicitly
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true, [System.Text.Encoding]::UTF8)
$stageMb = [math]::Round(((Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 1)
$zipMb = [math]::Round(((Get-Item $zip).Length / 1MB), 1)
Write-Host "[package] folder: $stage ($stageMb MB)"
Write-Host "[package] zip:    $zip ($zipMb MB)"

# ---- installer (Inno Setup)
if ($SkipInstaller) { return }
$iscc = @(
    $env:ISCC,
    'D:\apps\InnoSetup6\ISCC.exe',
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) {
    Write-Host '[package] Inno Setup (ISCC.exe) not found; skipping the installer. Set ISCC to its path to enable it.'
    return
}
& $iscc /Qp "/DAppVersion=$version" "/DSourceDir=$stage" "/DOutputDir=$dist" (Join-Path $root 'packaging\installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed' }
$setup = Join-Path $dist "$name-setup.exe"
Write-Host "[package] setup:  $setup ($([math]::Round(((Get-Item $setup).Length / 1MB), 1)) MB)"
