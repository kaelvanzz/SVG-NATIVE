@echo off
taskkill /f /im explorer.exe >nul 2>nul
copy /y "C:\Users\user\Documents\SVG-NATIVE\bin\x64\SvgThumbProvider.dll" "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll"
regsvr32 /s "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll"
start explorer.exe
echo Done.
