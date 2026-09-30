@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_2025.ps1"
if %ERRORLEVEL% NEQ 0 (
    echo Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
echo Build completed successfully.
