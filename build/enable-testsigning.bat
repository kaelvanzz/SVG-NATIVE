@echo off
REM Disable Driver Signature Enforcement (Test Mode)
REM WARNING: This reduces system security. Use only for testing.
REM Must run as Administrator

echo WARNING: This will disable driver signature enforcement temporarily.
echo This reduces system security. Only use for testing unsigned drivers.
echo.
echo Press Ctrl+C to cancel, or enter to continue...
pause >nul

bcdedit /set testsigning on

echo.
echo Test Signing mode enabled.
echo You can now install unsigned drivers/INFs.
echo.
echo After testing, disable with:
echo   bcdedit /set testsigning off
echo   (then restart)
echo.
echo NOTE: You need to RESTART your computer for changes to take effect.

pause