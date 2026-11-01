@echo off
REM Replace the VCBlocks plugin DLL in light's Cache\gui\dll
REM Run this AFTER closing light.exe

set SRC=D:\YX\VCBlocks\build\YXG_VCBlocks.dll
set DST=D:\YX\light\x64\Release\Cache\gui\dll\YXG_VCBlocks.dll

copy /Y "%SRC%" "%DST%" >nul 2>&1
if errorlevel 1 (
    echo.
    echo [FAILED] Cannot overwrite the DLL. light.exe is probably still running.
    echo Please fully close light (and the light service) then run this again.
) else (
    echo.
    echo [OK] DLL replaced successfully.
    echo Now restart light. A new sidebar tab "VCBlocks demo" should appear.
)
echo.
pause
