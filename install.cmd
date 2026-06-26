@echo off
REM SVG Native Handler Installer
REM Must run as Administrator

setlocal enabledelayedexpansion

:: Check admin
net session >nul 2>nul
if errorlevel 1 (
    echo ERROR: This script must run as Administrator.
    echo Right-click install.cmd -^> Run as administrator.
    pause
    exit /b 1
)

:: Detect architecture
if "%PROCESSOR_ARCHITECTURE%"=="AMD64" set ARCH=x64
if "%PROCESSOR_ARCHITECTURE%"=="x86" set ARCH=x86
if "%PROCESSOR_ARCHITECTURE%"=="ARM64" set ARCH=arm64
if not defined ARCH (
    echo Unsupported architecture: %PROCESSOR_ARCHITECTURE%
    pause
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
set "DLL_SRC=%SCRIPT_DIR%bin\%ARCH%\SvgThumbProvider.dll"
set "DLL_DST=%ProgramFiles%\SVG-NATIVE\SvgThumbProvider.dll"

if not exist "%DLL_SRC%" (
    echo ERROR: %DLL_SRC% not found.
    echo Run build\build.bat first, or download pre-built binaries.
    pause
    exit /b 1
)

echo.
echo === SVG Thumbnail Provider Installer (%ARCH%) ===
echo.

:: 1. Copy DLL
echo [1/5] Copying DLL...
if not exist "%ProgramFiles%\SVG-NATIVE\" mkdir "%ProgramFiles%\SVG-NATIVE\"
copy /y "%DLL_SRC%" "%DLL_DST%" >nul
if errorlevel 1 (
    echo FAILED: Cannot write to C:\Program Files\SVG-NATIVE\
    pause
    exit /b 1
)
echo   OK: %DLL_DST%

:: 2. Unblock if downloaded from internet
powershell -NoProfile -Command "Unblock-File '%DLL_DST%'" >nul 2>nul

:: 3. Register COM server (this creates CLSID + InProcServer32 + Approved + ShellEx)
echo [2/5] Registering COM server...
regsvr32 /s "%DLL_DST%"
if errorlevel 1 (
    echo FAILED: regsvr32 failed.
    pause
    exit /b 1
)
echo   OK: COM server registered

:: 4. Ensure thumbnail handler key exists under HKLM (regsvr32 may write to HKCU)
echo [3/5] Registering thumbnail handler...
reg add "HKLM\SOFTWARE\Classes\.svg\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" ^
    /ve /d "{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}" /f >nul
reg add "HKLM\SOFTWARE\Classes\SystemFileAssociations\.svg\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" ^
    /ve /d "{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}" /f >nul
echo   OK: Thumbnail handler registered

:: 5. Add Photos to OpenWith
echo [4/5] Adding Photos to .svg OpenWith...
reg add "HKLM\SOFTWARE\Classes\.svg\OpenWithProgids\Microsoft.Windows.Photos_8wekyb3d8bbwe" /f >nul 2>nul
reg add "HKLM\SOFTWARE\Classes\Applications\Microsoft.Windows.Photos_8wekyb3d8bbwe\SupportedTypes" ^
    /v ".svg" /t REG_SZ /f >nul 2>nul
echo   OK: Photos added

:: 6. Verify
echo [5/5] Verifying installation...
echo.
echo   DLL:    C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll
if exist "%DLL_DST%" (echo   Status: PRESENT) else (echo   Status: MISSING)
echo.
echo   Registry:
reg query "HKLM\SOFTWARE\Classes\CLSID\{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}\InProcServer32" ^
    /v ThreadingModel 2>nul | findstr "Apartment" >nul && echo   CLSID:        OK || echo   CLSID:        MISSING
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" ^
    /v "{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}" 2>nul | findstr "SVG" >nul && echo   Approved:     OK || echo   Approved:     MISSING
reg query "HKLM\SOFTWARE\Classes\.svg\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" ^
    /ve 2>nul | findstr "{9B2A1C9A" >nul && echo   .svg ShellEx: OK || echo   .svg ShellEx: MISSING
reg query "HKLM\SOFTWARE\Classes\SystemFileAssociations\.svg\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" ^
    /ve 2>nul | findstr "{9B2A1C9A" >nul && echo   SysAssoc:     OK || echo   SysAssoc:     MISSING
echo.
echo === Installation complete ===
echo.
echo Next steps:
echo   1. Press any key to:
echo      - Clear thumbnail cache
echo      - Restart Windows Explorer
echo   2. Then open a folder with .svg files in "Large icons" view
echo.
pause

echo.
echo Clearing thumbnail cache...
del /f /s /q "%LocalAppData%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>nul
del /f /s /q "%LocalAppData%\Microsoft\Windows\Explorer\thumbcache_idx.db" >nul 2>nul
del /f /s /q "%LocalAppData%\Microsoft\Windows\Explorer\iconcache_*.db" >nul 2>nul
echo   OK: Cache cleared

echo Restarting Explorer...
taskkill /f /im explorer.exe >nul 2>nul
start explorer.exe
echo Done.
endlocal
