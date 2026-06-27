@echo off
reg add "HKLM\SOFTWARE\Classes\.svg\shell\Open with SVG Viewer" /ve /d "Open with SVG Viewer" /f
reg add "HKLM\SOFTWARE\Classes\.svg\shell\Open with SVG Viewer\command" /ve /t REG_SZ /d "\"C:\Program Files\SVG-NATIVE\SvgViewer.exe\" \"%%1\"" /f
exit
