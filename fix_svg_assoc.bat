@echo off
reg add "HKLM\SOFTWARE\Classes\SvgViewer.svg" /ve /d "SVG Image" /f
reg add "HKLM\SOFTWARE\Classes\SvgViewer.svg\shell\open\command" /ve /t REG_SZ /d "\"C:\Program Files\SVG-NATIVE\SvgViewer.exe\" \"%%1\"" /f
reg add "HKLM\SOFTWARE\Classes\.svg" /ve /d SvgViewer.svg /f
reg delete "HKLM\SOFTWARE\Classes\.svg\OpenWithProgids" /v ChromeHTML /f
reg delete "HKLM\SOFTWARE\Classes\.svg\OpenWithProgids" /v MSEdgeHTM /f
reg delete "HKLM\SOFTWARE\Classes\.svg\OpenWithProgids" /v svgfile /f
exit
