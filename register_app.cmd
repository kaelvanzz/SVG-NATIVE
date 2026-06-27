@echo off
REM Register as a proper Windows application for Default Apps settings
reg add "HKLM\SOFTWARE\RegisteredApplications" /v "SVG Viewer" /t REG_SZ /d "Software\SVG-Viewer\Capabilities" /f
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities" /v "ApplicationName" /t REG_SZ /d "SVG Viewer" /f
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities" /v "ApplicationIcon" /t REG_SZ /d "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll,0" /f
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities" /v "ApplicationDescription" /t REG_SZ /d "Native SVG viewer using Direct2D" /f
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities\FileAssociations" /v ".svg" /t REG_SZ /d "SvgViewer.svg" /f
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities\MIMEAssociations" /v "image/svg+xml" /t REG_SZ /d "SvgViewer.svg" /f
reg add "HKLM\SOFTWARE\SVG-Viewer\Capabilities\shell\open\command" /ve /t REG_SZ /d "\"C:\Program Files\SVG-NATIVE\SvgViewer.exe\" \"%%1\"" /f
reg add "HKLM\SOFTWARE\Classes\.svg\OpenWithProgids" /v "SvgViewer.svg" /t REG_SZ /d "" /f
exit
