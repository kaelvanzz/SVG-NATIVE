@echo off
REM SVG Native Handler Uninstaller
REM Must run as Administrator

setlocal

set "DLL_DST=%ProgramFiles%\SVG-NATIVE\SvgThumbProvider.dll"

echo Uninstalling SVG Thumbnail Provider...

:: 1. Unregister COM server
if exist "%DLL_DST%" regsvr32 /s /u "%DLL_DST%"
echo [OK] COM server unregistered

:: 2. Remove shell extension approval
reg delete "HKLM\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" ^
    /v "{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}" /f >nul 2>nul

:: 3. Remove CLSID
reg delete "HKCR\CLSID\{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}" /f >nul 2>nul

:: 4. Remove thumbnail handler registration
reg delete "HKCR\.svg\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" /f >nul 2>nul
reg delete "HKCR\SystemFileAssociations\.svg\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" /f >nul 2>nul

:: 5. Remove Photos association
reg delete "HKLM\Software\Classes\Applications\Microsoft.Windows.Photos_8wekyb3d8bbwe\SupportedTypes" ^
    /v ".svg" /f >nul 2>nul

:: 6. Delete DLL
if exist "%DLL_DST%" (
    del "%DLL_DST%"
    rmdir "%ProgramFiles%\SVG-NATIVE%" 2>nul
)
echo [OK] Files removed

:: 7. Restart Explorer
echo.
echo Uninstall complete.
echo Restarting Explorer...
taskkill /f /im explorer.exe >nul 2>nul
start explorer.exe

echo Done.
endlocal
