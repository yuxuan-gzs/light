# 编译单个 GUI 源文件（与 vcxproj Release|x64 参数对齐）
# 关键：必须加 /GL /O2（WholeProgramOptimization），否则 obj 无 LTCG IR，
#       与其余 /GL obj 混合链接 /LTCG 会生成损坏代码 → 栈 cookie 崩溃(0xc0000409)。
param(
    [Parameter(Mandatory=$true)][string]$Src,
    [string]$ObjDir = "D:\YX\light\light\x64\Release"
)
$ErrorActionPreference = "Stop"
$vc = "I:\Microsoft Visual Studio\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
$kit = "I:\Windows Kits\10"
$vcpkg = "G:\VCPKG-2026.07.29\installed\x64-windows\include"
$env:INCLUDE = "$vc\include;$vc\atlmfc\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um;$kit\Include\10.0.26100.0\winrt;$kit\Include\10.0.26100.0\cppwinrt;$vcpkg"
$env:LIB = "$vc\lib\x64;$vc\atlmfc\lib\x64;$kit\Lib\10.0.26100.0\ucrt\x64;$kit\Lib\10.0.26100.0\um\x64"
$env:PATH = "$vc\bin\Hostx64\x64;$kit\bin\10.0.26100.0\x64;$env:PATH"
$cl = "$vc\bin\Hostx64\x64\cl.exe"
$name = [System.IO.Path]::GetFileNameWithoutExtension($Src)
$obj = Join-Path $ObjDir "$name.obj"
& $cl /c /nologo /W3 /WX- /sdl /O2 /Oi /GL /Gm- /EHsc /MT /GS /Gy `
  /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /std:c++20 /permissive- `
  /I D:\zlib-1.3.2 /I "D:\zlib-1.3.2\contrib\minizip" /I D:\lzma\C `
  /I "$vcpkg" /TP `
  "/Fo:$obj" "/Fd:$ObjDir\$name.pdb" "$Src"
if ($LASTEXITCODE -ne 0) { Write-Output "COMPILE_FAILED($Src) exit=$LASTEXITCODE"; exit 1 }
Write-Output "COMPILE_OK $Src -> $obj"
