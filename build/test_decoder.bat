@echo off
cd /d "C:\Users\user\Documents\SVG-NATIVE"
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /platform:x64 test_decoder.cs
if errorlevel 1 (
    echo COMPILE FAILED
    exit /b 1
)
test_decoder.exe
