<#
.SYNOPSIS
  Builds and test-signs the VoiceStudio Virtual Microphone driver (driver\sysvad) into build\driver-package.

.DESCRIPTION
  1. Restores the Windows Driver Kit and SDK as NuGet packages into driver\packages (no WDK installation).
  2. Builds the driver with MSBuild (Visual Studio 2022 Build Tools).
  3. Signs vsvirtualmic.sys and the catalog with a self-signed test certificate, created once in your
     CurrentUser\My store ("CN=VoiceStudio Virtual Microphone Test Signing", private key not exportable).
  4. Writes vsvirtualmic.sys/.inf/.cat and the public certificate (vsvirtualmic-test.cer) to build\driver-package,
     which the CMake install step and the installer pick up.

  Test-signed drivers only load while Windows Test Mode is on (see driver\README.md). This script does not change
  any Windows setting and does not trust the certificate on this PC.

.EXAMPLE
  scripts\build-driver.ps1
  scripts\build-driver.ps1 -PackageSource C:\vs_ext\drv\nupkg2   # restore from downloaded .nupkg files
#>
param(
    [string]$NuGet = "",
    [string]$PackageSource = "",
    [string]$CertSubject = "CN=VoiceStudio Virtual Microphone Test Signing"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$driver = Join-Path $root "driver"
$out = Join-Path $root "build\driver-package"

# ---- Tools ---------------------------------------------------------------------------------------------
if (-not $NuGet) {
    $cmd = Get-Command nuget.exe -ErrorAction SilentlyContinue
    if ($cmd) { $NuGet = $cmd.Source }
}
if (-not $NuGet -or -not (Test-Path $NuGet)) {
    throw "nuget.exe not found. Install it (winget install Microsoft.NuGet) or pass -NuGet <path>."
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild (Visual Studio 2022 Build Tools) not found." }

# ---- 1. WDK + SDK from NuGet ---------------------------------------------------------------------------
$restore = @("restore", (Join-Path $driver "packages.config"), "-PackagesDirectory", (Join-Path $driver "packages"), "-NonInteractive")
if ($PackageSource) { $restore += @("-Source", $PackageSource) }
& $NuGet @restore
if ($LASTEXITCODE -ne 0) { throw "NuGet restore failed" }

# The WDK build steps (stampinf, inf2cat) are run by name: put the package's tool folders on PATH.
$wdkTools = Get-ChildItem (Join-Path $driver "packages") -Directory -Filter "Microsoft.Windows.WDK.x64.*" | Select-Object -First 1
$wdkBinDirs = Get-ChildItem (Join-Path $wdkTools.FullName "c\bin") -Directory | ForEach-Object { Join-Path $_.FullName "x64"; Join-Path $_.FullName "x86" }
$env:PATH = (($wdkBinDirs | Where-Object { Test-Path $_ }) -join ";") + ";" + $env:PATH

# ---- 2. Build ------------------------------------------------------------------------------------------
foreach ($proj in @("sysvad\EndpointsCommon\EndpointsCommon.vcxproj", "sysvad\TabletAudioSample\TabletAudioSample.vcxproj")) {
    # The in-build InfVerif / ApiValidator steps look for x86 tools that the NuGet WDK does not ship; both run
    # below with the x64 tools instead.
    & $msbuild (Join-Path $driver $proj) /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off `
        /p:SkipPackageVerification=true /p:ApiValidator_Enable=false
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $proj" }
}
$sys = Get-ChildItem (Join-Path $driver "sysvad\TabletAudioSample\x64\Release") -Recurse -Filter vsvirtualmic.sys | Select-Object -First 1
$inf = Get-ChildItem (Join-Path $driver "sysvad\TabletAudioSample\x64\Release") -Recurse -Filter VsVirtualMic.inf | Select-Object -First 1
if (-not $sys -or -not $inf) { throw "Build output (vsvirtualmic.sys / VsVirtualMic.inf) not found" }

# Validation: only universal / Windows-driver DDIs, and an INF that Windows accepts.
$wdkC = Join-Path $wdkTools.FullName "c"
$apiVer = Get-ChildItem (Join-Path $wdkC "build") -Directory | Where-Object { $_.Name -match '^10\.' } | Select-Object -First 1
$apiBin = Join-Path $wdkC "bin\$($apiVer.Name)\x64"
& (Join-Path $apiBin "apivalidator.exe") "-DriverPackagePath:$($sys.FullName)" `
    "-SupportedApiXmlFiles:$(Join-Path $apiVer.FullName 'universalDDIs\x64\UniversalDDIs.xml')" `
    "-ModuleWhiteListXmlFiles:$(Join-Path $apiVer.FullName 'universalDDIs\x64\ModuleWhiteList.xml')" "-ApiExtractorExePath:$apiBin"
if ($LASTEXITCODE -ne 0) { throw "ApiValidator: the driver uses APIs outside the universal DDI set (exit $LASTEXITCODE)" }
$infverif = Get-ChildItem (Join-Path $wdkC "tools") -Recurse -Filter infverif.exe | Where-Object { $_.FullName -match "\\x64\\" } | Select-Object -First 1
& $infverif.FullName /w /v $inf.FullName
if ($LASTEXITCODE -ne 0) { throw "InfVerif failed for $($inf.Name)" }

New-Item -ItemType Directory -Force $out | Out-Null
Get-ChildItem $out -File | Remove-Item
Copy-Item $sys.FullName (Join-Path $out "vsvirtualmic.sys")
Copy-Item $inf.FullName (Join-Path $out "vsvirtualmic.inf")

# ---- 3. Test signing -----------------------------------------------------------------------------------
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $CertSubject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if (-not $cert) {
    Write-Host "Creating the test-signing certificate $CertSubject (CurrentUser\My, key not exportable)"
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertSubject -CertStoreLocation Cert:\CurrentUser\My `
        -KeyExportPolicy NonExportable -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(10)
}
Export-Certificate -Cert $cert -FilePath (Join-Path $out "vsvirtualmic-test.cer") | Out-Null

$wdkBin = Get-ChildItem (Join-Path $driver "packages") -Recurse -Filter signtool.exe | Where-Object { $_.FullName -match "\\x64\\" } | Select-Object -First 1
$inf2cat = Get-ChildItem (Join-Path $driver "packages") -Recurse -Filter inf2cat.exe | Select-Object -First 1
if (-not $wdkBin) { $wdkBin = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe | Where-Object { $_.FullName -match "\\x64\\" } | Select-Object -Last 1 }
if (-not $wdkBin -or -not $inf2cat) { throw "signtool.exe / inf2cat.exe not found in the WDK packages" }
$signtool = $wdkBin.FullName

& $signtool sign /q /fd SHA256 /sha1 $cert.Thumbprint /s My (Join-Path $out "vsvirtualmic.sys")
if ($LASTEXITCODE -ne 0) { throw "Signing vsvirtualmic.sys failed" }
& $inf2cat.FullName /driver:$out /os:10_X64 /uselocaltime
if ($LASTEXITCODE -ne 0) { throw "inf2cat failed" }
& $signtool sign /q /fd SHA256 /sha1 $cert.Thumbprint /s My (Join-Path $out "vsvirtualmic.cat")
if ($LASTEXITCODE -ne 0) { throw "Signing vsvirtualmic.cat failed" }

Write-Host "Driver package: $out (signed by $($cert.Subject), thumbprint $($cert.Thumbprint))"
Get-ChildItem $out | Format-Table Name, Length -AutoSize
