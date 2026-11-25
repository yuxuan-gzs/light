@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

set "REMOTE_URL=https://github.com/yuxuan-gzs/light.git"
set "BRANCH=main"

echo [INFO] Checking Git...
git --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Git not found.
    pause
    exit /b 1
)

if not exist ".git" (
    echo [INFO] git init
    git init
    git branch -M %BRANCH%
) else (
    echo [INFO] existing .git found
    git branch -M %BRANCH% 2>nul
)

git remote get-url origin >nul 2>&1
if %errorlevel% neq 0 (
    echo [INFO] add remote origin
    git remote add origin %REMOTE_URL%
) else (
    for /f "delims=" %%i in ('git remote get-url origin') do set "CURRENT_URL=%%i"
    if not "!CURRENT_URL!"=="%REMOTE_URL%" (
        echo [WARN] fixing remote url
        git remote set-url origin %REMOTE_URL%
    )
)

echo [INFO] staging all changes (respecting .gitignore)...
git add -A

echo [INFO] staged files:
git diff --cached --name-only

git diff --cached --quiet
if %errorlevel% equ 0 (
    echo [INFO] nothing to commit.
    echo --- untracked (not ignored) ---
    git ls-files --others --exclude-standard
    goto :END
)

echo [INFO] committing...
git commit -m "Auto commit: upload folders and source files"

echo [INFO] pushing...
git push -u origin %BRANCH%

if %errorlevel% equ 0 (
    echo [SUCCESS]
) else (
    echo [ERROR] push failed
)

:END
echo.
pause
endlocal