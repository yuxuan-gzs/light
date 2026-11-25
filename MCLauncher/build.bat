@echo off
setlocal
chcp 65001 >nul

set "vcvars=I:\Microsoft Visual Studio\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
call "%vcvars%" x64
if errorlevel 1 ( echo vcvarsall ʧ�� & exit /b 1 )

set "zlibDir=D:\zlib-1.3.2"
set "srcDir=%~dp0src"
set "incDir=%~dp0include"
set "objDir=%~dp0obj\x64\Release"
set "binDir=%~dp0bin\x64\Release"
if not exist "%objDir%" mkdir "%objDir%"
if not exist "%binDir%" mkdir "%binDir%"

set "CPPFLAGS=/c /TP /W3 /std:c++20 /MT /EHsc /utf-8 /Zc:char8_t- /O2 /D NDEBUG /D _WINDOWS /D _USRDLL /D NOMINMAX /D WIN32_LEAN_AND_MEAN /D _CRT_SECURE_NO_WARNINGS /I "%incDir%" /I "%srcDir%" /I "%zlibDir%""
set "CFLAGS=/c /TC /nologo /W3 /O1 /MT /D NDEBUG /D _CRT_SECURE_NO_WARNINGS /I "%zlibDir%""

echo === ���� C++ Դ�ļ� ===
cl %CPPFLAGS% /Fo"%objDir%\Json.obj" "%srcDir%\Json.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\Crypto.obj" "%srcDir%\Crypto.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\Http.obj" "%srcDir%\Http.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\Zip.obj" "%srcDir%\Zip.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\LauncherCore.obj" "%srcDir%\LauncherCore.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\MsAuth.obj" "%srcDir%\MsAuth.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\JavaDetect.obj" "%srcDir%\JavaDetect.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\LoaderInstall.obj" "%srcDir%\LoaderInstall.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\Theme.obj" "%srcDir%\Theme.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\Background.obj" "%srcDir%\Background.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\ModManager.obj" "%srcDir%\ModManager.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\MlcData.obj" "%srcDir%\MlcData.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\ContentLib.obj" "%srcDir%\ContentLib.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\LauncherApp.obj" "%srcDir%\LauncherApp.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\Ui.obj" "%srcDir%\Ui.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\UiPages.obj" "%srcDir%\UiPages.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\UiPaint.obj" "%srcDir%\UiPaint.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\UiActions.obj" "%srcDir%\UiActions.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\Wallpapers.obj" "%srcDir%\Wallpapers.cpp" || goto :fail
cl %CPPFLAGS% /Fo"%objDir%\dllmain.obj" "%srcDir%\dllmain.cpp" || goto :fail

echo === ���� zlib ===
cl %CFLAGS% /Fo"%objDir%\z_adler32.obj" "%zlibDir%\adler32.c" || goto :fail
cl %CFLAGS% /Fo"%objDir%\z_crc32.obj" "%zlibDir%\crc32.c" || goto :fail
cl %CFLAGS% /Fo"%objDir%\z_inffast.obj" "%zlibDir%\inffast.c" || goto :fail
cl %CFLAGS% /Fo"%objDir%\z_inflate.obj" "%zlibDir%\inflate.c" || goto :fail
cl %CFLAGS% /Fo"%objDir%\z_inftrees.obj" "%zlibDir%\inftrees.c" || goto :fail
cl %CFLAGS% /Fo"%objDir%\z_zutil.obj" "%zlibDir%\zutil.c" || goto :fail

echo === ���� ===
set "out=%binDir%\YXG_MinecraftLauncher.dll"
link /DLL /OUT:"%out%" /SUBSYSTEM:WINDOWS /MACHINE:X64 ^
    "%objDir%\Json.obj" "%objDir%\Crypto.obj" "%objDir%\Http.obj" "%objDir%\Zip.obj" ^
    "%objDir%\LauncherCore.obj" "%objDir%\MsAuth.obj" "%objDir%\JavaDetect.obj" "%objDir%\LoaderInstall.obj" ^
    "%objDir%\Theme.obj" "%objDir%\Background.obj" "%objDir%\ModManager.obj" "%objDir%\LauncherApp.obj" ^
    "%objDir%\MlcData.obj" "%objDir%\ContentLib.obj" ^
    "%objDir%\Ui.obj" "%objDir%\UiPages.obj" "%objDir%\UiPaint.obj" "%objDir%\UiActions.obj" ^
    "%objDir%\Wallpapers.obj" "%objDir%\dllmain.obj" ^
    "%objDir%\z_adler32.obj" "%objDir%\z_crc32.obj" "%objDir%\z_inffast.obj" "%objDir%\z_inflate.obj" "%objDir%\z_inftrees.obj" "%objDir%\z_zutil.obj" ^
    winhttp.lib bcrypt.lib advapi32.lib gdiplus.lib shell32.lib user32.lib gdi32.lib ole32.lib comdlg32.lib kernel32.lib
if errorlevel 1 goto :fail

echo.
echo �����ɹ�: %out%
exit /b 0

:fail
echo.
echo ���������ʧ�ܣ���鿴����Ĵ�����Ϣ��
exit /b 1