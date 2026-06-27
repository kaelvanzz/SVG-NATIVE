@echo off
reg add "HKLM\SOFTWARE\Classes\CLSID\{E018945B-AA86-4008-9FBD-7BF0B3AEB5D5}" /ve /d "Fake PNG" /f
reg add "HKLM\SOFTWARE\Classes\CLSID\{E018945B-AA86-4008-9FBD-7BF0B3AEB5D5}\InprocServer32" /ve /d "C:\NONEXIST.DLL" /f
reg add "HKLM\SOFTWARE\Classes\CLSID\{E018945B-AA86-4008-9FBD-7BF0B3AEB5D5}\InprocServer32" /v ThreadingModel /d Both /f
exit
