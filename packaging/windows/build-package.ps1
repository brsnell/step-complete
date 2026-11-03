<#
.SYNOPSIS
  Builds the Windows installer and portable zip from an existing CMake build tree.

.DESCRIPTION
  packaging\windows\build-package.ps1 -BuildDir build\vcpkg -OutputDir dist

  Run from a shell where the MSVC environment is set up (VCToolsRedistDir) and windeployqt
  (Qt bin dir) and ISCC (Inno Setup 6, preinstalled on GitHub's windows-2022 runner) can be found.

  Optional code signing: set WINDOWS_CERT_PFX_BASE64 and WINDOWS_CERT_PASSWORD. Without them
  the exe and installer are left unsigned (Windows SmartScreen will warn).

  Visual C++ runtime: the few runtime DLLs (msvcp140*, vcruntime140*; ~1 MB) are copied app-local
  from the Visual Studio redist folder. Compared to `windeployqt --compiler-runtime` (which ships the
  ~25 MB vc_redist installer and needs admin rights) this keeps the install small, works with the
  per-user (no admin) installer and the portable zip, and needs nothing run at install time.
#>
param(
  [Parameter(Mandatory = $true)][string]$BuildDir,
  [Parameter(Mandatory = $true)][string]$OutputDir,
  # Folder with OCCT/FreeType DLLs: <vcpkg_installed>\<triplet>\bin (auto-detected if omitted)
  [string]$VcpkgBinDir = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$BuildDir = (Resolve-Path $BuildDir).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

# Version from the CMake cache.
$versionLine = Select-String -Path (Join-Path $BuildDir "CMakeCache.txt") -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$' | Select-Object -First 1
if (-not $versionLine) { throw "Cannot determine version from CMakeCache.txt" }
$version = $versionLine.Matches[0].Groups[1].Value

function Invoke-Checked {
  param([string]$Exe, [string[]]$Arguments)
  & $Exe @Arguments
  if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

# --- Stage: cmake --install ---------------------------------------------------
$stageRoot = Join-Path $OutputDir "stage"
$stage = Join-Path $stageRoot "StepComplete"
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
Invoke-Checked cmake @("--install", $BuildDir, "--prefix", $stage, "--config", "Release")

# --- Qt ------------------------------------------------------------------------
$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $windeployqt) { throw "windeployqt not found in PATH (add the Qt bin directory)" }
# Keep it small: no translations/D3D compiler/software GL/compiler runtime (handled app-local below),
# and skip plugin types the viewer never loads. The windows platform plugin, the Windows styles and
# imageformats (JPEG export) are kept.
Invoke-Checked $windeployqt.Source @(
  "--release", "--no-translations", "--no-system-d3d-compiler", "--no-system-dxc-compiler",
  "--no-opengl-sw", "--no-compiler-runtime",
  "--skip-plugin-types", "iconengines,networkinformation,tls,generic,qmltooling,position,multimedia",
  (Join-Path $stage "StepComplete.exe"))
# Qt SVG is not used by the app; drop what the image-format plugins may pull in.
Get-ChildItem -Path $stage -Filter "Qt6Svg*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem -Path (Join-Path $stage "imageformats") -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -notmatch '^qjpeg' } | Remove-Item -Force

# --- OCCT / FreeType from vcpkg -------------------------------------------------
if (-not $VcpkgBinDir) {
  $candidate = Get-ChildItem -Path (Join-Path $BuildDir "vcpkg_installed") -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName "bin") } | Select-Object -First 1
  if (-not $candidate) { throw "vcpkg_installed not found under $BuildDir; pass -VcpkgBinDir" }
  $VcpkgBinDir = Join-Path $candidate.FullName "bin"
}
Write-Host "Copying DLLs from $VcpkgBinDir"
Copy-Item -Path (Join-Path $VcpkgBinDir "*.dll") -Destination $stage -Force

# --- Visual C++ runtime, app-local ----------------------------------------------
$redist = $env:VCToolsRedistDir
if (-not $redist) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  $ver = (Get-Content (Join-Path $vs "VC\Auxiliary\Build\Microsoft.VCRedistVersion.default.txt")).Trim()
  $redist = Join-Path $vs "VC\Redist\MSVC\$ver"
}
$crt = Get-ChildItem -Path (Join-Path $redist "x64") -Directory -Filter "Microsoft.VC*.CRT" |
  Where-Object { $_.Name -notmatch 'Debug' } | Select-Object -First 1
if (-not $crt) { throw "VC++ runtime redist folder not found under $redist" }
Write-Host "Copying VC++ runtime from $($crt.FullName)"
Copy-Item -Path (Join-Path $crt.FullName "*.dll") -Destination $stage -Force

# --- Optional signing -------------------------------------------------------------
$signing = $env:WINDOWS_CERT_PFX_BASE64 -and $env:WINDOWS_CERT_PASSWORD
$pfx = Join-Path $OutputDir "codesign.pfx"
function Sign-File {
  param([string]$Path)
  if (-not $signing) { return }
  $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
    Sort-Object FullName -Descending | Select-Object -First 1
  if (-not $signtool) { throw "signtool.exe not found" }
  Invoke-Checked $signtool.FullName @("sign", "/f", $pfx, "/p", $env:WINDOWS_CERT_PASSWORD,
    "/fd", "sha256", "/td", "sha256", "/tr", "http://timestamp.digicert.com", "/d", "STEP Complete", $Path)
}
if ($signing) {
  [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:WINDOWS_CERT_PFX_BASE64))
  Write-Host "Code signing enabled"
} else {
  Write-Host "WINDOWS_CERT_PFX_BASE64 / WINDOWS_CERT_PASSWORD not set: leaving binaries unsigned"
}
try {
  Sign-File (Join-Path $stage "StepComplete.exe")

  # --- Portable zip ----------------------------------------------------------------
  $zip = Join-Path $OutputDir "StepComplete-$version-windows-x64-portable.zip"
  if (Test-Path $zip) { Remove-Item -Force $zip }
  Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

  # --- Installer (Inno Setup) ---------------------------------------------------------
  $iscc = Get-Command ISCC -ErrorAction SilentlyContinue
  $isccPath = if ($iscc) { $iscc.Source } else { "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
  if (-not (Test-Path $isccPath)) { throw "Inno Setup (ISCC.exe) not found" }
  Invoke-Checked $isccPath @("/Qp", "/DAppVersion=$version", "/DSourceDir=$stage", "/DOutputDir=$OutputDir",
    (Join-Path $here "stepcomplete.iss"))
  Sign-File (Join-Path $OutputDir "StepComplete-$version-windows-x64-setup.exe")
} finally {
  if (Test-Path $pfx) { Remove-Item -Force $pfx }
}

Remove-Item -Recurse -Force $stageRoot
Get-ChildItem $OutputDir -File | Format-Table Name, Length
