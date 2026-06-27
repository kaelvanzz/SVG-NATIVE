$ErrorActionPreference = "Stop"
$WinKits = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
$MakePri = Join-Path $WinKits "makepri.exe"
$MakeAppx = Join-Path $WinKits "makeappx.exe"
$SignTool = Join-Path $WinKits "signtool.exe"

$ProjRoot = Resolve-Path "$PSScriptRoot\.."
$PkgDir = "$PSScriptRoot\package"
$CertDir = "$PSScriptRoot\cert"
$DllSrc = "$ProjRoot\build\x64\wicdecoder.dll"

if (-not (Test-Path $DllSrc)) {
    Write-Error "wicdecoder.dll not found at $DllSrc`nRun build\build.bat x64 first."
    exit 1
}

# Clean previous
if (Test-Path $PkgDir) { Remove-Item -Recurse -Force $PkgDir }
New-Item -ItemType Directory -Force -Path $PkgDir | Out-Null
New-Item -ItemType Directory -Force -Path $CertDir | Out-Null

Write-Host "[1/6] Creating self-signed certificate..."
$cert = New-SelfSignedCertificate -Type Custom -Subject "CN=SVGCodec" `
    -KeyUsage DigitalSignature -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3") `
    -CertStoreLocation "Cert:\CurrentUser\My"
$pwd = ConvertTo-SecureString -String "svgcodec" -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath "$CertDir\SVGCodec.pfx" -Password $pwd | Out-Null
Export-Certificate -Cert $cert -FilePath "$CertDir\SVGCodec.cer" -Type CERT | Out-Null
Write-Host "  Certificate thumbprint: $($cert.Thumbprint)"

Write-Host "[2/6] Copying package files..."
Copy-Item $DllSrc "$PkgDir\wicdecoder.dll"
if (Test-Path "$PSScriptRoot\Assets") {
    Copy-Item -Recurse "$PSScriptRoot\Assets" "$PkgDir\"
}
Copy-Item "$PSScriptRoot\AppxManifest.xml" "$PkgDir\"

Write-Host "[3/6] Creating resources.pri..."
Push-Location $PkgDir
& $MakePri createconfig /cf priconfig.xml /dq en-US /o 2>&1
& $MakePri new /pr . /cf priconfig.xml /o 2>&1
if (Test-Path resources.pri) { Write-Host "  OK" } else { Write-Host "  WARN: no resources.pri" }
Pop-Location

Write-Host "[4/6] Determining hash algorithm..."
$algo = if ($cert.SignatureAlgorithm.FriendlyName -match "sha256") { "SHA256" } else { "SHA1" }
Write-Host "  Using: $algo"

Write-Host "[5/6] Building MSIX package..."
& $MakeAppx pack /d $PkgDir /p "$PSScriptRoot\SVGCodec.msix" /o /v 2>&1
if (-not $?) { Write-Error "MakeAppx.exe failed"; exit 1 }
Write-Host "  OK: SVGCodec.msix created"

Write-Host "[6/6] Signing MSIX package..."
& $SignTool sign /fd $algo /a /f "$CertDir\SVGCodec.pfx" /p svgcodec "$PSScriptRoot\SVGCodec.msix"
if (-not $?) { Write-Error "SignTool.exe failed"; exit 1 }
Write-Host "  OK: SVGCodec.msix signed"

Write-Host "`n=== MSIX Package created ==="
Get-Item "$PSScriptRoot\SVGCodec.msix" | Select-Object Name, Length
Write-Host "`nTo install, run as Administrator:"
Write-Host "  Add-AppxPackage -Path '$PSScriptRoot\SVGCodec.msix'"