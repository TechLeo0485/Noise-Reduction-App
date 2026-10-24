<#
.SYNOPSIS
  Installs the VoiceStudio build prerequisites on Windows 10/11.

.DESCRIPTION
  - Visual Studio 2022 Build Tools with the C++ workload (MSVC v143 + Windows 11 SDK) via winget
  - CMake and Ninja via winget
  - Qt 6 (MSVC 2022 64-bit, with Qt Quick Controls) via aqtinstall (pip)
  - Sets the QTDIR user environment variable

  Run from an elevated PowerShell. Review before running: this installs several GB.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\setup-dev.ps1 -QtVersion 6.8.3 -QtRoot C:\Qt

.EXAMPLE
  # Re-run only the Qt step (e.g. after a mirror timeout), using a specific mirror
  powershell -ExecutionPolicy Bypass -File scripts\setup-dev.ps1 -QtOnly -QtMirror https://mirrors.ocf.berkeley.edu/qt/
#>
param(
    [string]$QtVersion = "6.8.3",
    [string]$QtRoot = "C:\Qt",
    [switch]$SkipVisualStudio,
    [switch]$QtOnly,                                   # skip winget/pip steps (e.g. re-running after a Qt download failure)
    [string]$QtMirror = "https://download.qt.io/",     # alternative: https://mirrors.ocf.berkeley.edu/qt/
    [int]$QtRetries = 3
)

$ErrorActionPreference = "Stop"

function Install-WingetPackage([string]$Id, [string]$ExtraArgs = "") {
    Write-Host "==> winget install $Id"
    $arguments = @("install", "--id", $Id, "--exact", "--accept-source-agreements", "--accept-package-agreements")
    if ($ExtraArgs) { $arguments += @("--override", $ExtraArgs) }
    & winget @arguments
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { # already installed
        throw "winget failed for $Id (exit $LASTEXITCODE)"
    }
}

if (-not $QtOnly) {
    if (-not $SkipVisualStudio) {
        Install-WingetPackage "Microsoft.VisualStudio.2022.BuildTools" `
            "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.Windows11SDK.22621"
    }
    Install-WingetPackage "Kitware.CMake"
    Install-WingetPackage "Ninja-build.Ninja"

    Write-Host "==> Installing aqtinstall"
    python -m pip install --upgrade aqtinstall
    if ($LASTEXITCODE -ne 0) { throw "pip install aqtinstall failed" }
}

# Qt mirrors occasionally time out on the large qtbase archive. Pin a base URL (aqt otherwise
# follows redirects to arbitrary mirrors) and retry.
Write-Host "==> Installing Qt $QtVersion (msvc2022_64) into $QtRoot from $QtMirror"
# aqt defaults to 4 parallel downloads with a 30 s read timeout, which fails on slow or unstable
# connections. Download one archive at a time and tolerate long stalls.
$aqtConfig = Join-Path $env:TEMP "voicestudio-aqt-settings.ini"
@"
[aqt]
concurrency: 1

[requests]
connection_timeout: 30
response_timeout: 600
max_retries_on_connection_error: 10
retry_backoff: 2
"@ | Set-Content -Path $aqtConfig -Encoding ascii

$qtInstalled = $false
for ($attempt = 1; $attempt -le $QtRetries; $attempt++) {
    python -m aqt -c $aqtConfig install-qt windows desktop $QtVersion win64_msvc2022_64 --outputdir $QtRoot --base $QtMirror
    if ($LASTEXITCODE -eq 0) { $qtInstalled = $true; break }
    Write-Warning "Qt download attempt $attempt of $QtRetries failed."
    if ($attempt -lt $QtRetries) { Start-Sleep -Seconds 10 }
}
if (-not $qtInstalled) {
    throw "aqt install-qt failed. Retry with another mirror, e.g. -QtOnly -QtMirror https://mirrors.ocf.berkeley.edu/qt/"
}

$qtDir = Join-Path $QtRoot "$QtVersion\msvc2022_64"
if (-not (Test-Path (Join-Path $qtDir "lib\cmake\Qt6"))) { throw "Qt not found at $qtDir" }
[Environment]::SetEnvironmentVariable("QTDIR", $qtDir, "User")
Write-Host "QTDIR set to $qtDir (open a new terminal to pick it up)."

Write-Host ""
Write-Host "Optional: Inno Setup 6 for installer generation:  winget install JRSoftware.InnoSetup"
Write-Host "Optional: VB-CABLE for running the app: https://vb-audio.com/Cable/ (manual install)"
