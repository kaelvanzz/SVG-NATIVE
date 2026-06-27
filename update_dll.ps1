Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Copy-Item "C:\Users\user\Documents\SVG-NATIVE\bin\x64\SvgThumbProvider.dll" "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll" -Force
regsvr32 /s "C:\Program Files\SVG-NATIVE\SvgThumbProvider.dll"
Start-Process explorer
Write-Host "Done"
Start-Sleep -Seconds 2
