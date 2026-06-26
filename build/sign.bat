@echo off
REM Sign INF file with self-signed certificate
REM Must run as Administrator

setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set SRC_DIR=%PROJECT_ROOT%\src
set OUT_DIR=%PROJECT_ROOT%\build

REM Find signtool.exe
set SIGNTOOL=
for %%A in ("C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" "C:\Program Files\Windows Kits\10\bin\*\x64\signtool.exe") do (
    for /f "delims=" %%B in ('dir /b /s "%%~A" 2^>nul') do (
        if not defined SIGNTOOL set "SIGNTOOL=%%B"
    )
)

if not defined SIGNTOOL (
    echo signtool.exe not found. Please install Windows SDK.
    echo Download from: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/
    exit /b 1
)

echo Using signtool: !SIGNTOOL!

REM Create certificate if not exists
set CERT_NAME="SvgNative Test Cert"
set PFX_FILE=%OUT_DIR%\cert.pfx
set PASSWORD=TestPassword123

powershell -NoProfile -Command ^
    "$cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=SvgNative Dev' -KeyUsage DigitalSignature -FriendlyName 'SvgNative Test Cert' -CertStoreLocation 'Cert:\CurrentUser\My' -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}'); "^
    "$pwd = ConvertTo-SecureString -String '%PASSWORD%' -Force -AsPlainText; "^
    "$cert | Export-PfxCertificate -FilePath '%PFX_FILE%' -Password $pwd; "^
    "Write-Host 'Thumbprint:' $cert.Thumbprint"

if not exist "%PFX_FILE%" (
    echo Failed to create certificate
    exit /b 1
)

echo.
echo Importing certificate to Trusted Root...

powershell -NoProfile -Command ^
    "$pwd = ConvertTo-SecureString -String '%PASSWORD%' -AsPlainText -Force; "^
    "Import-PfxCertificate -FilePath '%PFX_FILE%' -CertStoreLocation Cert:\LocalMachine\Root -Password $pwd | Out-Null; "^
    "Write-Host 'Certificate imported to Trusted Root CA'"

echo.
echo Signing INF file...

"%SIGNTOOL%" sign /f "%PFX_FILE%" /p "%PASSWORD%" /fd SHA256 /td SHA256 "%SRC_DIR%\SvgThumbProvider.inf"

if errorlevel 1 (
    echo.
    echo Signing failed. Try running as Administrator.
    exit /b 1
)

echo.
echo SUCCESS! INF file is now signed.
echo You can now install: right-click src\SvgThumbProvider.inf ^> Install

pause