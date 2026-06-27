@echo off
regsvr32 /u "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll" >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\CLSID\{11E7785D-7BFE-411C-AD88-48849C9EE8B1}" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\CLSID\{7ED96837-96F0-4812-B211-F13C24117ED3}\Instance\{11E7785D-7BFE-411C-AD88-48849C9EE8B1}" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\MIME\Database\Content Type\image/svg+xml" /f >nul 2>&1
if exist "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll" rename "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll" "SvgThumbProvider.dll.old"
copy /y "C:\Users\user\Documents\SVG-NATIVE\bin\x64\SvgThumbProvider.dll" "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll" >nul
regsvr32 /s "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll"
del "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll.old" >nul 2>&1
