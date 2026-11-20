@echo off
rem build_local_msvc.bat - Local development build for LangBridge using MSVC.
rem Uses explicit vctools/SDK paths (no vcvars64.bat, which may hang in
rem non-interactive shells). Edit the three paths below if your VS install
rem differs. The portable build entry is tools/build.sh (probes g++/clang++).
setlocal

set "VCTools=I:\Microsoft Visual Studio\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDKInc=I:\Windows Kits\10\Include\10.0.26100.0"
set "SDKLib=I:\Windows Kits\10\Lib\10.0.26100.0"

if not exist "%VCTools%\bin\Hostx64\x64\cl.exe" (
  echo [build_local_msvc] cl.exe not found under "%VCTools%".
  echo [build_local_msvc] Edit tools\build_local_msvc.bat with your MSVC Tools path.
  exit /b 3
)

set "INCLUDE=%VCTools%\include;%SDKInc%\ucrt;%SDKInc%\um;%SDKInc%\shared"
set "LIB=%VCTools%\lib\x64;%SDKLib%\ucrt\x64;%SDKLib%\um\x64"
set "PATH=%VCTools%\bin\Hostx64\x64;%SystemRoot%\system32;%SystemRoot%;%PATH%"

if not exist "%~dp0..\build" mkdir "%~dp0..\build"
rem Object files go to build\obj so the repository root stays clean.
if not exist "%~dp0..\build\obj" mkdir "%~dp0..\build\obj"

cl /nologo /std:c++17 /EHsc /W3 /O2 /I"%~dp0..\include" /Fo:"%~dp0..\build\obj\\" /Fe:"%~dp0..\build\langbridge.exe" ^
   "%~dp0..\src\preprocess.cpp" "%~dp0..\src\lexer.cpp" "%~dp0..\src\parser.cpp" "%~dp0..\src\analyze.cpp" ^
   "%~dp0..\src\gen_common.cpp" "%~dp0..\src\gen_j2c.cpp" "%~dp0..\src\gen_c2j.cpp" "%~dp0..\src\main.cpp"

echo [build_local_msvc] exit code = %errorlevel%
exit /b %errorlevel%