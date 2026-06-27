@echo off
setlocal enabledelayedexpansion

set WIN_KITS="C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
set MAKEPRI=%WIN_KITS:"=%\makepri.exe
set MAKEAPPX=%WIN_KITS:"=%\makeappx.exe
set SIGNTOOL=%WIN_KITS:"=%\signtool.exe

set PROJ_ROOT=%~dp0..
set MSIX_DIR=%~dp0
set PKG_DIR=%MSIX_DIR%package
set CERT_DIR=%MSIX_DIR%cert
set DLL_SRC=%PROJ_ROOT%\build\x64\wicdecoder.dll

if not exist "%DLL_SRC%" (
    echo ERROR: wicdecoder.dll not found at %DLL_SRC%
    echo Run "build\build.bat" first.
    exit /b 1
)

:: Clean previous package
if exist "%PKG_DIR%" rmdir /s /q "%PKG_DIR%"
mkdir "%PKG_DIR%"
if not exist "%CERT_DIR%" mkdir "%CERT_DIR%"

:: 1. Create self-signed certificate
echo [1/6] Creating self-signed certificate...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=SVGCodec' -KeyUsage DigitalSignature -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3') -CertStoreLocation 'Cert:\CurrentUser\My'; " ^
    "$pwd = ConvertTo-SecureString -String 'svgcodec' -Force -AsPlainText; " ^
    "Export-PfxCertificate -Cert $cert -FilePath '%CERT_DIR%\SVGCodec.pfx' -Password $pwd | Out-Null; " ^
    "Export-Certificate -Cert $cert -FilePath '%CERT_DIR%\SVGCodec.cer' -Type CERT | Out-Null; " ^
    "Write-Host '  Certificate created: ' $cert.Thumbprint"
if errorlevel 1 (
    echo FAILED: Could not create certificate.
    echo Make sure you run this script as Administrator.
    pause
    exit /b 1
)

:: 2. Copy files to package directory
echo [2/6] Copying package files...
copy /y "%DLL_SRC%" "%PKG_DIR%\wicdecoder.dll" >nul
xcopy /y /e "%MSIX_DIR%Assets" "%PKG_DIR%\Assets\" >nul
copy /y "%MSIX_DIR%AppxManifest.xml" "%PKG_DIR%\" >nul
echo   OK

:: 3. Create resources.pri
echo [3/6] Creating resources.pri...
pushd "%PKG_DIR%"
"%MAKEPRI%" createconfig /cf priconfig.xml /dq lang-neutral /o 2>nul
"%MAKEPRI%" new /pr . /cf priconfig.xml /o 2>nul
if exist resources.pri (echo   OK) else (echo   WARN: resources.pri not created)
popd

:: 4. Determine hash algorithm from certificate
echo [4/6] Determining certificate hash...
for /f "tokens=*" %%a in ('
    powershell -NoProfile -Command ^
        "$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=SVGCodec' } | Select-Object -First 1; " ^
        "if ($cert.SignatureAlgorithm.FriendlyName -match 'sha256') { echo SHA256 } else { echo SHA1 }"
') do set HASH_ALGO=%%a
if not defined HASH_ALGO set HASH_ALGO=SHA256
echo   Using: %HASH_ALGO%

:: 5. Build MSIX package
echo [5/6] Building MSIX package...
"%MAKEAPPX%" pack /d "%PKG_DIR%" /p "%MSIX_DIR%SVGCodec.msix" /o /v 2>&1
if errorlevel 1 (
    echo FAILED: MakeAppx.exe failed.
    echo Try running from a Developer Command Prompt, or check the error above.
    pause
    exit /b 1
)
echo   OK: SVGCodec.msix created

:: 6. Sign the package
echo [6/6] Signing MSIX package...
"%SIGNTOOL%" sign /fd %HASH_ALGO% /a /f "%CERT_DIR%\SVGCodec.pfx" /p svgcodec "%MSIX_DIR%SVGCodec.msix"
if errorlevel 1 (
    echo FAILED: SignTool.exe failed.
    echo Try running from a Developer Command Prompt.
    pause
    exit /b 1
)
echo   OK: SVGCodec.msix signed

:: Verify
echo.
echo === MSIX Package created ===
dir /b "%MSIX_DIR%SVGCodec.msix"
echo.
echo To install, run as Administrator:
echo   Add-AppxPackage -Path "%MSIX_DIR%SVGCodec.msix"
echo.
echo To install with dependency on the certificate (required for first install):
echo   powershell -NoProfile -Command "Add-AppxPackage -Path '%MSIX_DIR%SVGCodec.msix'"
echo.

endlocal