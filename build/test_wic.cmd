@echo off
setlocal
set CSC="C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set SRC="C:\Users\user\Documents\SVG-NATIVE\test_decoder.cs"
set OUT="C:\Users\user\Documents\SVG-NATIVE\test_decoder.exe"

%CSC% /nologo /platform:x64 /target:exe /out:%OUT% %SRC%
if errorlevel 1 (
    echo COMPILE FAILED
    exit /b 1
)
echo COMPILE OK
%OUT%
echo EXIT CODE: %ERRORLEVEL%
