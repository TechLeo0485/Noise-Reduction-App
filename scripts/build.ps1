<#
.SYNOPSIS
  Configures, builds and tests VoiceStudio with Ninja + MSVC, importing the Visual Studio
  developer environment automatically.

.EXAMPLE
  scripts\build.ps1                 # Release build + unit tests
  scripts\build.ps1 -Config Debug
  scripts\build.ps1 -NoTests
  scripts\build.ps1 -Hardware       # also run tests that open real audio devices
#>
param(
    [ValidateSet("Release", "Debug")] [string]$Config = "Release",
    [switch]$NoTests,
    [switch]$Hardware
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$preset = if ($Config -eq "Release") { "ninja-release" } else { "ninja-debug" }

# Resolve Qt: current environment, then the persisted user/machine variable (terminals started
# before `setx` - including all VS Code terminals until VS Code restarts - don't see it), then
# the newest C:\Qt\<version>\msvc2022_64 kit.
if (-not $env:QTDIR) {
    $env:QTDIR = [Environment]::GetEnvironmentVariable("QTDIR", "User")
    if (-not $env:QTDIR) { $env:QTDIR = [Environment]::GetEnvironmentVariable("QTDIR", "Machine") }
}
if (-not $env:QTDIR) {
    $kit = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^6\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "msvc2022_64" } |
        Where-Object { Test-Path (Join-Path $_ "lib\cmake\Qt6\Qt6Config.cmake") } |
        Select-Object -First 1
    if ($kit) { $env:QTDIR = $kit }
}
if (-not $env:QTDIR -or -not (Test-Path (Join-Path $env:QTDIR "lib\cmake\Qt6\Qt6Config.cmake"))) {
    throw "Qt 6 (MSVC 2022 64-bit) not found. Set QTDIR to e.g. C:\Qt\6.8.3\msvc2022_64 (see README)."
}
Write-Host "Using Qt: $env:QTDIR"

# A running VoiceStudio.exe is locked by Windows and makes the link step fail (LNK1104).
$running = Get-Process VoiceStudio -ErrorAction SilentlyContinue
if ($running) {
    throw "VoiceStudio is running (PID $($running.Id -join ', ')). Close it, then run the build again."
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Visual Studio 2022 not found (vswhere.exe missing)." }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw "No Visual Studio installation with the C++ toolset found." }
    $devShell = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
}

Push-Location $root
try {
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    if (-not $NoTests) {
        ctest --test-dir "build\$preset" --output-on-failure --label-exclude "hardware"
        if ($LASTEXITCODE -ne 0) { throw "Unit tests failed" }
        if ($Hardware) {
            ctest --test-dir "build\$preset" --output-on-failure --label-regex "hardware"
            if ($LASTEXITCODE -ne 0) { throw "Hardware tests failed" }
        }
    }
    Write-Host "Done. Binaries: build\$preset\bin"
} finally {
    Pop-Location
}
