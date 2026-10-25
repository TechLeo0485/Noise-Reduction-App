<#
.SYNOPSIS
  Builds a Release, installs it into build\install\ninja-release (windeployqt runs as part of
  `cmake --install`), and generates an installer with Inno Setup 6 if it is available.
#>
param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$preset = "ninja-release"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

Push-Location $root
try {
    $installDir = Join-Path $root "build\install\$preset"
    if (Test-Path $installDir) { Remove-Item -Recurse -Force $installDir }
    # windeployqt prints harmless warnings (e.g. dxcompiler.dll not found) on stderr, which Windows
    # PowerShell would turn into a terminating error: judge native tools by their exit code only.
    $ErrorActionPreference = "Continue"
    cmake --install "build\$preset" --prefix $installDir 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = "Stop"
    if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

    $iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if (-not $iscc) {
        foreach ($candidate in @((Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
                                 (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"))) {
            if (Test-Path $candidate) { $iscc = Get-Command $candidate; break }
        }
    }

    # The Microsoft Visual C++ runtime the binaries were built against; setup installs it when missing.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $redist = Get-ChildItem (Join-Path $vsPath "VC\Redist\MSVC") -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "vc_redist.x64.exe" } | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $redist) { throw "vc_redist.x64.exe not found under $vsPath\VC\Redist\MSVC" }
    Write-Host "Bundling $redist"

    if ($iscc) {
        $version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'VERSION (\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
        & $iscc.Source "/DAppVersion=$version" "/DSourceDir=$installDir" "/DVCRedist=$redist" "/O$(Join-Path $root 'dist')" (Join-Path $root "installer\VoiceStudio.iss")
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
        Write-Host "Installer written to dist\"
    } else {
        Write-Host "Inno Setup not found; portable build is in $installDir"
    }
} finally {
    Pop-Location
}
