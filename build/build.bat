@echo off
setlocal enabledelayedexpansion

set ARCH=%~1
if "%ARCH%"=="" set ARCH=x64
if /i not "%ARCH%"=="x64" if /i not "%ARCH%"=="x86" if /i not "%ARCH%"=="arm64" (
    echo Usage: %~nx0 [x64^|x86^|arm64]
    echo Default: x64
    exit /b 1
)

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo cl.exe not found. Locating Visual Studio...
    set "VCTOOLS="
    for %%p in (
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community"
        "C:\Program Files\Microsoft Visual Studio\2022\Community"
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional"
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
        "C:\Program Files\Microsoft Visual Studio\2019\Community"
    ) do if exist "%%~p\VC\Auxiliary\Build\vcvarsall.bat" set "VCTOOLS=%%~p"
    if "!VCTOOLS!"=="" (
        echo Visual Studio 2019/2022 not found.
        echo Please run from a VS Developer Command Prompt.
        exit /b 1
    )
    call "!VCTOOLS!\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    if errorlevel 1 (
        echo Failed to initialize VS environment for %ARCH%.
        exit /b 1
    )
)

set PROJECT_ROOT=%~dp0..
set SRC_DIR=%PROJECT_ROOT%\src
set INC_DIR=%PROJECT_ROOT%\inc
set OUT_DIR=%PROJECT_ROOT%\build\%ARCH%

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

set DLL_NAME=SvgThumbProvider.dll
set DEF_FILE=%SRC_DIR%\thumbprovider.def

echo Building %DLL_NAME% for %ARCH%...

set LIBS=d2d1.lib d3d11.lib dxguid.lib windowscodecs.lib ole32.lib shlwapi.lib user32.lib gdi32.lib advapi32.lib

cl.exe /nologo /W3 /LD /EHsc /I"%INC_DIR%" ^
    /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
    /Fe"%OUT_DIR%\%DLL_NAME%" ^
    "%SRC_DIR%\thumbprovider.cpp" ^
    /link /DEF:"%DEF_FILE%" /DLL /SUBSYSTEM:WINDOWS %LIBS%

if errorlevel 1 (
    echo Build FAILED
    exit /b 1
)

echo Build succeeded: %OUT_DIR%\%DLL_NAME%

REM Copy to bin\ for pre-built distribution
set BIN_DIR=%PROJECT_ROOT%\bin\%ARCH%
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
copy /y "%OUT_DIR%\%DLL_NAME%" "%BIN_DIR%\%DLL_NAME%" >nul
echo Copied to %BIN_DIR%\%DLL_NAME%
echo.
echo To install, right-click src\SvgThumbProvider.inf -^> Install
echo (Admin required)

endlocal
