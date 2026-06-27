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
set "EXE_SRC=%SCRIPT_DIR%bin\%ARCH%\SvgViewer.exe"
set "EXE_DST=%ProgramFiles%\SVG-NATIVE\SvgViewer.exe"

if not exist "%DLL_SRC%" (
    echo ERROR: %DLL_SRC% not found.
    echo Run build\build.bat first, or download pre-built binaries.
    pause
    exit /b 1
)
if not exist "%EXE_SRC%" (
    echo ERROR: %EXE_SRC% not found.
    echo Run build\build.bat first.
    pause
    exit /b 1
)

echo.
echo === SVG Thumbnail Provider Installer (%ARCH%) ===
echo.

:: 1. Copy files
echo [1/5] Copying files...
if not exist "%ProgramFiles%\SVG-NATIVE\" mkdir "%ProgramFiles%\SVG-NATIVE\"
copy /y "%DLL_SRC%" "%DLL_DST%" >nul
if errorlevel 1 (
    echo FAILED: Cannot write to C:\Program Files\SVG-NATIVE\
    pause
    exit /b 1
)
copy /y "%EXE_SRC%" "%EXE_DST%" >nul
if errorlevel 1 (
    echo FAILED: Cannot copy SvgViewer.exe
    pause
    exit /b 1
)
echo   OK: %DLL_DST%
echo   OK: %EXE_DST%

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

:: 4. PerceivedType + WIC decoder + file association
echo [4/5] Registering SVG image type + file association...
reg add "HKLM\SOFTWARE\Classes\.svg" /v PerceivedType /t REG_SZ /d "image" /f >nul
reg add "HKLM\SOFTWARE\Classes\.svg" /v "Content Type" /t REG_SZ /d "image/svg+xml" /f >nul
rem Register SvgViewer.svg ProgId
reg add "HKLM\SOFTWARE\Classes\SvgViewer.svg" /ve /d "SVG Image" /f >nul
reg add "HKLM\SOFTWARE\Classes\SvgViewer.svg\shell\open\command" /ve /t REG_SZ /d "\"%EXE_DST%\" \"%%1\"" /f >nul
reg add "HKLM\SOFTWARE\Classes\.svg\Shell\Open\command" /ve /t REG_SZ /d "\"%EXE_DST%\" \"%%1\"" /f >nul
rem Set SvgViewer.svg as default ProgId for .svg
reg add "HKLM\SOFTWARE\Classes\.svg" /ve /d SvgViewer.svg /f >nul
rem Register in OpenWithProgids
reg add "HKLM\SOFTWARE\Classes\.svg\OpenWithProgids" /v SvgViewer.svg /t REG_SZ /d "" /f >nul
rem Register context menu entry
reg add "HKLM\SOFTWARE\Classes\.svg\shell\Open with SVG Viewer" /ve /d "Open with SVG Viewer" /f >nul
reg add "HKLM\SOFTWARE\Classes\.svg\shell\Open with SVG Viewer\command" /ve /t REG_SZ /d "\"%EXE_DST%\" \"%%1\"" /f >nul
rem Register as Windows Application for Default Apps settings
reg add "HKLM\SOFTWARE\RegisteredApplications" /v "SVG Viewer" /t REG_SZ /d "Software\SVG-Viewer\Capabilities" /f >nul
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities" /v "ApplicationName" /t REG_SZ /d "SVG Viewer" /f >nul
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities" /v "ApplicationIcon" /t REG_SZ /d "%DLL_DST%,0" /f >nul
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities" /v "ApplicationDescription" /t REG_SZ /d "Native SVG viewer using Direct2D" /f >nul
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities\FileAssociations" /v ".svg" /t REG_SZ /d "SvgViewer.svg" /f >nul
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities\MIMEAssociations" /v "image/svg+xml" /t REG_SZ /d "SvgViewer.svg" /f >nul
echo   OK: SVG registered as image type

:: 5. Verify
echo [5/5] Verifying installation...
echo.
echo   Files:
if exist "%DLL_DST%" (echo   DLL:    PRESENT) else (echo   DLL:    MISSING)
if exist "%EXE_DST%" (echo   Viewer: PRESENT) else (echo   Viewer: MISSING)
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
reg query "HKLM\SOFTWARE\Classes\.svg\Shell\Open\command" /ve 2>nul | findstr "SvgViewer" >nul && echo   OpenCmd:      OK || echo   OpenCmd:      MISSING
reg query "HKLM\SOFTWARE\Classes\.svg\shell\Open with SVG Viewer" /ve 2>nul | findstr "SVG" >nul && echo   CtxMenu:      OK || echo   CtxMenu:      MISSING
reg query "HKLM\SOFTWARE\RegisteredApplications" /v "SVG Viewer" 2>nul | findstr "SVG" >nul && echo   RegApp:       OK || echo   RegApp:       MISSING
echo.
echo === Installation complete ===
echo.
echo Next steps:
echo   1. Press any key to clear thumbnail cache and restart Explorer.
echo   2. Right-click any .svg file -^> "Open with SVG Viewer" to render in Photos.
echo   3. To set SVG Viewer as the default open app:
echo      - Right-click a .svg -^> Open with -^> Choose another app
echo      - Select "SVG Viewer" and check "Always use this app"
echo      - Or go to Settings -^> Apps -^> Default apps -^> ".svg" -^> SVG Viewer
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
