@echo off
REM SVG Native Handler Installer
REM Must run as Administrator

setlocal enabledelayedexpansion

:: Detect architecture
if "%PROCESSOR_ARCHITECTURE%"=="AMD64" set ARCH=x64
if "%PROCESSOR_ARCHITECTURE%"=="x86" set ARCH=x86
if not defined ARCH (
    echo Unsupported architecture: %PROCESSOR_ARCHITECTURE%
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
set "DLL_SRC=%SCRIPT_DIR%bin\%ARCH%\SvgThumbProvider.dll"
set "DLL_DST=%ProgramFiles%\SVG-NATIVE\SvgThumbProvider.dll"

if not exist "%DLL_SRC%" (
    echo ERROR: %DLL_SRC% not found.
    echo Run build\build.bat first.
    exit /b 1
)

echo Installing SVG Thumbnail Provider (%ARCH%)...

:: 1. Copy DLL
if not exist "%ProgramFiles%\SVG-NATIVE\" mkdir "%ProgramFiles%\SVG-NATIVE\"
copy /y "%DLL_SRC%" "%DLL_DST%" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy DLL. Are you running as Administrator?
    exit /b 1
)
echo [OK] Copied DLL to %DLL_DST%

:: 2. Register COM server
regsvr32 /s "%DLL_DST%"
if errorlevel 1 (
    echo ERROR: regsvr32 failed.
    exit /b 1
)
echo [OK] Registered COM server

:: 3. Approve shell extension
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" ^
    /v "{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}" ^
    /t REG_SZ /d "SVG Thumbnail Provider" /f >nul
echo [OK] Shell extension approved

:: 4. Add Photos to .svg OpenWith
reg add "HKLM\Software\Classes\Applications\Microsoft.Windows.Photos_8wekyb3d8bbwe\SupportedTypes" ^
    /v ".svg" /t REG_SZ /f >nul 2>nul
echo [OK] Photos added to .svg OpenWith

:: 5. Restart Explorer
echo.
echo Installation complete.
echo Restarting Explorer...
taskkill /f /im explorer.exe >nul 2>nul
start explorer.exe

echo Done. Open an .svg file to check thumbnails.
endlocal
