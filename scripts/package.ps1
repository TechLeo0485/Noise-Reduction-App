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
    cmake --install "build\$preset" --prefix $installDir
    if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

    $iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if (-not $iscc) {
        $candidate = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"
        if (Test-Path $candidate) { $iscc = Get-Item $candidate }
    }
    if ($iscc) {
        $version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'VERSION (\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
        & $iscc.Source "/DAppVersion=$version" "/DSourceDir=$installDir" "/O$(Join-Path $root 'dist')" (Join-Path $root "installer\VoiceStudio.iss")
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
        Write-Host "Installer written to dist\"
    } else {
        Write-Host "Inno Setup not found; portable build is in $installDir"
    }
} finally {
    Pop-Location
}
