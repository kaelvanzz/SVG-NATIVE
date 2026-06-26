@echo off
REM Create self-signed certificate and sign the INF file
REM Run as Administrator

setlocal

set PROJECT_ROOT=%~dp0..
set SRC_DIR=%PROJECT_ROOT%\src
set OUT_DIR=%PROJECT_ROOT%\build

echo Creating self-signed certificate...
powershell -NoProfile -Command ^
    "New-SelfSignedCertificate -Type Custom -Subject 'CN=SvgNative Dev' -KeyUsage DigitalSignature -FriendlyName 'SvgNative Test Cert' -CertStoreLocation 'Cert:\CurrentUser\My' -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')"

echo.
echo Certificate created. Now signing...
echo You may need to install the certificate to Trusted Root CA store.
echo.

echo To install the certificate permanently:
echo   1. Run: certmgr.msc
echo   2. Find 'SvgNative Test Cert' under Personal
echo   3. Right-click - Export...
echo   4. Install to Trusted Root Certification Authorities
echo.
echo Or run this PowerShell as Admin:
echo   Import-PfxCertificate -FilePath 'cert.pfx' -CertStoreLocation Cert:\LocalMachine\TrustedRoot -Password (ConvertTo-SecureString -String 'password' -Force -AsPlainText)
echo.

pause