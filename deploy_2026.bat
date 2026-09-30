@echo off
set "SOURCE=C:\Users\user\Desktop\cpp\C4D_DollyZoom\sdk_2026\build\bin\Release\plugins\C4D_ShrinkWrap"
set "DEST=\\vmware-host\Shared Folders\plugins\C4D_ShrinkWrap"

echo.
echo [DEPLOYMENT] Copying C4D_ShrinkWrap via PowerShell...
echo [SOURCE] %SOURCE%
echo [DEST]   %DEST%
echo.

powershell -Command "$ErrorActionPreference = 'Stop'; try { if (-not (Test-Path '%DEST%')) { New-Item -ItemType Directory -Path '%DEST%' -Force }; Get-ChildItem '%DEST%\*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue; if (Test-Path '%DEST%\C4D_ShrinkWrap.xdl64') { try { Remove-Item '%DEST%\C4D_ShrinkWrap.xdl64' -Force } catch { Move-Item '%DEST%\C4D_ShrinkWrap.xdl64' '%DEST%\C4D_ShrinkWrap.xdl64.old' -Force } }; Copy-Item '%SOURCE%\C4D_ShrinkWrap.xdl64' '%DEST%\' -Force; Copy-Item 'C:\Users\user\Desktop\cpp\C4D_ShrinkWrap\res' '%DEST%\' -Recurse -Force } catch { Write-Error $_; exit 1 }"

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Copy failed! Exit Code: %ERRORLEVEL%
    echo [ERROR] Please close Cinema 4D and try again.
    echo.
    exit /b %ERRORLEVEL%
)

echo [SUCCESS] Plugin files successfully copied/updated to Cinema 4D plugins!
echo.
