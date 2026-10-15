# Package the Windows x86 build for release.
#
#   powershell -ExecutionPolicy Bypass -File package-windows.ps1 [-Version <v>] [-BuildDir <dir>]
#
# Writes dist\PowerSlide-<version>-windows-x86.zip (PowerSlide.exe, license
# files, README.txt) and its .sha256. The executable must stand alone: the CRT,
# SDL3 and libchdr are linked statically (checked by check_standalone.py).

param(
    [string]$Version = "",
    [string]$BuildDir = "build-msvc-x86\Release",
    [string]$DistDir = "dist"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not $Version) {
    $match = Select-String -Path CMakeLists.txt -Pattern '^set\(CTR_NATIVE_VERSION "(.*)"\)$'
    $Version = $match.Matches[0].Groups[1].Value
}

$AppName = "PowerSlide"
$Exe = Join-Path $BuildDir "ctr_native.exe"
$PackageName = "$AppName-$Version-windows-x86"
$StageDir = Join-Path $DistDir $PackageName
$ZipPath = Join-Path $DistDir "$PackageName.zip"

if (-not (Test-Path $Exe)) {
    Write-Error "missing executable: $Exe. Run build-msvc.bat first."
}

$Python = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } else { "py" }
& $Python tools/release/check_standalone.py $Exe
if ($LASTEXITCODE -ne 0) {
    Write-Error "ctr_native.exe is not standalone"
}

if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
New-Item -ItemType Directory -Path $StageDir | Out-Null

# The build target keeps the upstream name; players see the product name.
Copy-Item $Exe (Join-Path $StageDir "$AppName.exe")
Copy-Item LICENSE, THIRD_PARTY_NOTICES.md $StageDir

$Readme = @"
$AppName $Version for Windows

Game data is not included. You need your own copy of Crash Team Racing for
the PlayStation, North American release (SCUS-94426), as a disc image.

Install:
- Unzip this folder anywhere and open $AppName.exe.
- The first time, a setup dialog asks for your disc image: a .cue file (with
  its .bin next to it), a .bin file, or a .chd file.
- $AppName copies the disc into its data folder, so you only do this once.

The disc image must be a raw single-track MODE2/2352 dump (what .cue/.bin
and .chd dumps of this disc are). A 2048-byte .iso is missing the audio and
video data and is not accepted.

Change the disc image: hold Shift while opening $AppName, or use
"Change disc image..." in the menu's About tab (F1).

Data folder (disc image, settings, memory cards, logs):
  %APPDATA%\$AppName

Portable use: put the disc image at assets\ctr-u.bin next to $AppName.exe.
$AppName then keeps everything in its own folder and skips setup.

If Windows SmartScreen says it protected your PC: click "More info", then
"Run anyway".

Requirements: Windows 10 or later, a GPU with OpenGL 3.3.

If the game stops with an error, the message offers a Show Log button. Please
include ctr_native-console.log and "Crash Team Racing.log" from the data folder
when reporting a problem.
"@
Set-Content -Path (Join-Path $StageDir "README.txt") -Value ($Readme -replace "`r?`n", "`r`n") -Encoding ascii -NoNewline

Compress-Archive -Path $StageDir -DestinationPath $ZipPath
Remove-Item -Recurse -Force $StageDir

$Hash = (Get-FileHash -Algorithm SHA256 $ZipPath).Hash.ToLowerInvariant()
Set-Content -Path "$ZipPath.sha256" -Value "$Hash  $PackageName.zip`n" -Encoding ascii -NoNewline

Write-Host "Wrote $ZipPath"
Write-Host "Wrote $ZipPath.sha256"
