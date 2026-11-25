@echo off
echo %DATE% %TIME% restarter started >> D:\YX\whale-restart.log
timeout /t 25 /nobreak >nul
echo %DATE% %TIME% stopping app >> D:\YX\whale-restart.log
taskkill /IM "DSH Desktop.exe" /F >nul 2>&1
timeout /t 6 /nobreak >nul
echo %DATE% %TIME% starting app >> D:\YX\whale-restart.log
start "" "D:\yuxuangzs\DSH Desktop\DSH Desktop.exe"
