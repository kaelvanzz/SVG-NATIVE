@echo off
cd /d "C:\Users\user\Documents\SVG-NATIVE"
set VCTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VCTOOLS%\VC\Auxiliary\Build\vcvarsall.bat" x64
cl.exe /nologo /Fe"C:\Users\user\Documents\SVG-NATIVE\test_direct2.exe" test_direct2.cpp ole32.lib windowscodecs.lib
if errorlevel 1 (echo COMPILE FAILED & exit /b 1)
echo COMPILE OK
test_direct2.exe
