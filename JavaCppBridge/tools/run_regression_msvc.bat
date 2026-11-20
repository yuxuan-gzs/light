@echo off
setlocal enabledelayedexpansion
rem run_regression_msvc.bat - LangBridge end-to-end regression on MSVC.
rem Builds CLI with cl, translates both examples, compiles & runs generated
rem C++ (cl) and generated Java (javac when available), diffs against
rem tests/expect\, prints PASS/FAIL summary.

set "ROOT=%~dp0.."
cd /d "%ROOT%"
if not exist build mkdir build
if not exist build\gen mkdir build\gen
if not exist build\obj mkdir build\obj

set "VCTools=I:\Microsoft Visual Studio\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDKInc=I:\Windows Kits\10\Include\10.0.26100.0"
set "SDKLib=I:\Windows Kits\10\Lib\10.0.26100.0"
set "INCLUDE=%VCTools%\include;%SDKInc%\ucrt;%SDKInc%\um;%SDKInc%\shared"
set "LIB=%VCTools%\lib\x64;%SDKLib%\ucrt\x64;%SDKLib%\um\x64"
set "PATH=%VCTools%\bin\Hostx64\x64;%SystemRoot%\system32;%SystemRoot%;%PATH%"

set PASS=0
set FAIL=0

echo [regression] building langbridge CLI...
cl /nologo /std:c++17 /EHsc /W3 /O2 /I"%ROOT%\include" /Fo:"%ROOT%\build\obj\\" /Fe:"%ROOT%\build\langbridge.exe" ^
   src\preprocess.cpp src\lexer.cpp src\parser.cpp src\analyze.cpp src\gen_common.cpp ^
   src\gen_j2c.cpp src\gen_c2j.cpp src\main.cpp
if errorlevel 1 (
  echo   FAIL: CLI build
  exit /b 1
)
echo   CLI built

set "LBEXE=%ROOT%\build\langbridge.exe"
set "GEN=%ROOT%\build\gen"

rem ------------------------- Java -> C++ ---------------
for %%f in ("%ROOT%\examples\java\*.java") do (
  call :j2c "%%~nf"
)
rem ------------------------- C++ -> Java ---------------
for %%f in ("%ROOT%\examples\cpp\*.cpp") do (
  call :c2j "%%~nf"
)

echo.
echo regression result: %PASS% passed, %FAIL% failed
if %FAIL%==0 (exit /b 0) else (exit /b 1)

:j2c
set "STEM=%~1"
"%LBEXE%" --mode java2cpp -i "%ROOT%\examples\java\%STEM%.java" -o "%GEN%\%STEM%.cpp" >nul 2>&1
cl /nologo /std:c++17 /EHsc /W3 /O2 /Fo:"%GEN%\\" /Fe:"%GEN%\%STEM%_cpp.exe" "%GEN%\%STEM%.cpp" >"%GEN%\%STEM%_cc.log" 2>&1
if errorlevel 1 (
  echo   FAIL: %STEM% generated C++ did not compile
  set /a FAIL+=1
  exit /b
)
"%GEN%\%STEM%_cpp.exe" >"%GEN%\%STEM%_j2c.out" 2>nul
rem Canonicalise line endings / trailing newline before the golden diff.
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\normalize.ps1" -In "%GEN%\%STEM%_j2c.out" -Out "%GEN%\%STEM%_j2c.norm" >nul 2>&1
if not exist "%GEN%\%STEM%_j2c.norm" copy /y "%GEN%\%STEM%_j2c.out" "%GEN%\%STEM%_j2c.norm" >nul
fc "%ROOT%\tests\expect\%STEM%_j2c.txt" "%GEN%\%STEM%_j2c.norm" >nul 2>&1
if errorlevel 1 (
  echo   FAIL: %STEM% C++ output mismatch
  set /a FAIL+=1
) else (
  echo   PASS: %STEM% ^(C++ run matches^)
  set /a PASS+=1
)
exit /b

:c2j
set "STEM=%~1"
"%LBEXE%" --mode cpp2java -i "%ROOT%\examples\cpp\%STEM%.cpp" -o "%GEN%\%STEM%.java" >nul 2>&1
if not exist "%GEN%\%STEM%_j2j" mkdir "%GEN%\%STEM%_j2j"
copy /y "%GEN%\%STEM%.java" "%GEN%\%STEM%_j2j\Main.java" >nul
where javac >nul 2>&1
if errorlevel 1 (
  if exist "%GEN%\%STEM%.java" (
    echo   PASS: %STEM% ^(javac absent; file generated^)
    set /a PASS+=1
  ) else (
    echo   FAIL: %STEM% empty Java output
    set /a FAIL+=1
  )
  exit /b
)
pushd "%GEN%\%STEM%_j2j"
javac -encoding UTF-8 Main.java >compile.log 2>&1
if errorlevel 1 (
  echo   FAIL: %STEM% javac failed
  set /a FAIL+=1
  popd
  exit /b
)
java Main >"%GEN%\%STEM%_c2j.out" 2>run.log
if errorlevel 1 (
  echo   FAIL: %STEM% java run failed
  set /a FAIL+=1
  popd
  exit /b
)
popd
rem Java println writes CRLF on Windows; canonicalise before the golden diff.
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\normalize.ps1" -In "%GEN%\%STEM%_c2j.out" -Out "%GEN%\%STEM%_c2j.norm" >nul 2>&1
if not exist "%GEN%\%STEM%_c2j.norm" copy /y "%GEN%\%STEM%_c2j.out" "%GEN%\%STEM%_c2j.norm" >nul
fc "%ROOT%\tests\expect\%STEM%_c2j.txt" "%GEN%\%STEM%_c2j.norm" >nul 2>&1
if errorlevel 1 (
  echo   FAIL: %STEM% Java output mismatch
  set /a FAIL+=1
) else (
  echo   PASS: %STEM% ^(Java run matches^)
  set /a PASS+=1
)
exit /b