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

set LIBS=d2d1.lib d3d11.lib dxguid.lib windowscodecs.lib ole32.lib shlwapi.lib user32.lib gdi32.lib advapi32.lib

REM ?��??��? wicdecoder.dll (standalone WIC decoder for MSIX packaging) ?��??��?
set WIC_DLL_NAME=wicdecoder.dll
echo Building %WIC_DLL_NAME% for %ARCH%...
cl.exe /nologo /W3 /LD /EHsc /I"%INC_DIR%" ^
    /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
    /Fe"%OUT_DIR%\%WIC_DLL_NAME%" ^
    "%SRC_DIR%\wicdecoder.cpp" ^
    /link /DEF:"%SRC_DIR%\wicdecoder.def" /DLL /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 ( echo Build FAILED & exit /b 1 )
echo Build succeeded: %OUT_DIR%\%WIC_DLL_NAME%

REM ?��??��? SvgThumbProvider.dll (thumbnail provider + WIC decoder) ?��??��?
set DLL_NAME=SvgThumbProvider.dll
echo Building %DLL_NAME% for %ARCH%...
cl.exe /nologo /W3 /LD /EHsc /I"%INC_DIR%" ^
    /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
    /Fe"%OUT_DIR%\%DLL_NAME%" ^
    "%SRC_DIR%\thumbprovider.cpp" ^
    /link /DEF:"%SRC_DIR%\thumbprovider.def" /DLL /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 ( echo Build FAILED & exit /b 1 )
echo Build succeeded: %OUT_DIR%\%DLL_NAME%

REM ?��??��? SvgViewer.exe (standalone bridge) ?��??��?
set EXE_NAME=SvgViewer.exe
echo Building %EXE_NAME% for %ARCH%...
cl.exe /nologo /EHsc /I"%INC_DIR%" ^
    /Fe"%OUT_DIR%\%EXE_NAME%" ^
    "%SRC_DIR%\svgviewer.cpp" ^
    /link d2d1.lib d3d11.lib dxguid.lib shlwapi.lib ole32.lib user32.lib shell32.lib
if errorlevel 1 ( echo SVG Viewer build FAILED & exit /b 1 )
echo Build succeeded: %OUT_DIR%\%EXE_NAME%

REM ?��??��? svghook.dll (WIC hook DLL, for injection into Photos) ?��??��?
set HOOK_DLL_NAME=svghook.dll
echo Building %HOOK_DLL_NAME% for %ARCH%...
cl.exe /nologo /W3 /LD /EHsc /I"%SRC_DIR%" /I"%SRC_DIR%\minhook" ^
    /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
    /Fe"%OUT_DIR%\%HOOK_DLL_NAME%" ^
    "%SRC_DIR%\svghook.cpp" ^
    "%SRC_DIR%\minhook\hook.c" ^
    "%SRC_DIR%\minhook\buffer.c" ^
    "%SRC_DIR%\minhook\trampoline.c" ^
    "%SRC_DIR%\minhook\hde\hde64.c" ^
    /link d2d1.lib d3d11.lib dxgi.lib ole32.lib windowscodecs.lib user32.lib
if errorlevel 1 ( echo Hook DLL build FAILED & exit /b 1 )
echo Build succeeded: %OUT_DIR%\%HOOK_DLL_NAME%

REM Build test_render.exe (rendering test harness)
set TEST_NAME=test_render.exe
echo Building %TEST_NAME% for %ARCH%...
cl.exe /nologo /EHsc /I"%INC_DIR%" /I"%SRC_DIR%" ^
    /Fe"%OUT_DIR%\%TEST_NAME%" ^
    "%SRC_DIR%\test_render.cpp" ^
    /link d2d1.lib d3d11.lib dxguid.lib windowscodecs.lib shlwapi.lib ole32.lib
if errorlevel 1 ( echo Test build FAILED & exit /b 1 )
echo Build succeeded: %OUT_DIR%\%TEST_NAME%

REM ?��??��? inject_svg.exe (SetWindowsHookEx injector) ?��??��?
set INJ_NAME=inject_svg.exe
echo Building %INJ_NAME% for %ARCH%...
cl.exe /nologo /EHsc /I"%SRC_DIR%" ^
    /Fe"%OUT_DIR%\%INJ_NAME%" ^
    "%SRC_DIR%\inject_svg.cpp" ^
    /link user32.lib ole32.lib
if errorlevel 1 ( echo Injector build FAILED & exit /b 1 )
echo Build succeeded: %OUT_DIR%\%INJ_NAME%

REM Copy to bin\ for pre-built distribution
set BIN_DIR=%PROJECT_ROOT%\bin\%ARCH%
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
copy /y "%OUT_DIR%\%DLL_NAME%" "%BIN_DIR%\" >nul
copy /y "%OUT_DIR%\%EXE_NAME%" "%BIN_DIR%\" >nul
copy /y "%OUT_DIR%\%HOOK_DLL_NAME%" "%BIN_DIR%\" >nul
copy /y "%OUT_DIR%\%INJ_NAME%" "%BIN_DIR%\" >nul
copy /y "%OUT_DIR%\%WIC_DLL_NAME%" "%BIN_DIR%\" >nul
echo Copied to %BIN_DIR%
echo.
echo To install, run install.cmd as Administrator

endlocal