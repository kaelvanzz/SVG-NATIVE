@echo off
cd /d "C:\Users\user\Documents\SVG-NATIVE\build\x64"
inject_svg.exe "%1" > "%TEMP%\inject_result.txt" 2>&1
type "%TEMP%\inject_result.txt"
exit
