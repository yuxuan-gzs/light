# build_and_test.ps1 - 链接 app、编译运行自检、编译 DLL 验证
$ErrorActionPreference = 'Continue'
$msvc = 'I:\Microsoft Visual Studio\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
$kit  = 'I:\Windows Kits\10'
$sdk  = '10.0.26100.0'
$env:INCLUDE = "$msvc\include;$msvc\atlmfc\include;$kit\Include\$sdk\ucrt;$kit\Include\$sdk\shared;$kit\Include\$sdk\um;$kit\Include\$sdk\winrt;$kit\Include\$sdk\cppwinrt"
$env:LIB     = "$msvc\lib\x64;$msvc\atlmfc\lib\x64;$kit\Lib\$sdk\ucrt\x64;$kit\Lib\$sdk\um\x64"
$env:PATH    = "$msvc\bin\Hostx64\x64;$kit\bin\$sdk\x64;$env:PATH"

$cl   = "$msvc\bin\Hostx64\x64\cl.exe"
$link = "$msvc\bin\Hostx64\x64\link.exe"
$rc   = "$kit\bin\$sdk\x64\rc.exe"
$src  = 'D:\YX\VCBlocks\VCBlocks\src'
$proj = 'D:\YX\VCBlocks\VCBlocks'
$build = 'D:\YX\VCBlocks\build'

# ---- 0. 编译资源（TCC bundle 嵌入）----
Write-Output "== compile resource =="
& $rc /nologo /fo "$build\tcc_res.res" "$proj\tcc_res.rc" 2>&1 | Out-Null
if (Test-Path "$build\tcc_res.res") { Write-Output "res OK" } else { Write-Output "res FAIL" }

# ---- 1. 链接 app ----
Write-Output "== link app =="
$appObjs = @('util','Json','BlockModel','BlockLibrary','Translator','ProjectStore','CodeGenerator','Decompiler','LibraryScanner','TccCompiler','JdkCompiler','MsvcCompiler','Http','LibraryStore','AppSettings','DllBuilder','Theme','MainWindow','LibraryStorePanel','StartupDialog','PluginEntry')
$objs = ($appObjs | ForEach-Object { "`"$build\$_`".obj" }) -join ' '
$cmd = "/OUT:`"$build\YXG_VCBlocks.dll`" /DLL /NOLOGO $objs `"$build\tcc_res.res`" gdiplus.lib comdlg32.lib user32.lib gdi32.lib winhttp.lib comctl32.lib"
$out = & $link $cmd.Split(' ') 2>&1
Write-Output ($out -join "`n")
if ($LASTEXITCODE -ne 0) { Write-Output "DLL LINK FAILED" } else { Write-Output "DLL LINK OK -> $build\YXG_VCBlocks.dll" }

# ---- 2. 编译自检程序 ----
Write-Output "== compile selftest =="
& $cl /c /nologo /utf-8 /EHsc /MT /W3 /std:c++20 /permissive- /D NOMINMAX /D NDEBUG "/I$src" "/ID:\YX\VCBlocks\VCBlocks\include" "/Fo$build\selftest.obj" "D:\YX\VCBlocks\selftest\selftest.cpp" 2>&1 | ForEach-Object { Write-Output $_ }
if ($LASTEXITCODE -ne 0) { Write-Output "SELFTEST COMPILE FAILED" }

# ---- 3. 链接自检程序 ----
Write-Output "== link selftest =="
$coreObjs = @('util','Json','BlockModel','BlockLibrary','Translator','CodeGenerator','Decompiler','LibraryScanner','DllBuilder')
$so = ($coreObjs | ForEach-Object { "`"$build\$_`".obj" }) -join ' '
$cmd2 = "/OUT:`"$build\selftest.exe`" /SUBSYSTEM:CONSOLE /NOLOGO `"$build\selftest.obj`" $so"
$out2 = & $link $cmd2.Split(' ') 2>&1
Write-Output ($out2 -join "`n")
if ($LASTEXITCODE -ne 0) { Write-Output "SELFTEST LINK FAILED" }

# ---- 4. 运行自检 ----
Write-Output "== run selftest =="
& "$build\selftest.exe"
Write-Output "selftest exit code: $LASTEXITCODE"

# ---- 5. 编译生成的 DLL 源码（验证 DLL 可编译） ----
Write-Output "== compile generated DLL source =="
& $cl /nologo /TC /utf-8 /EHsc /MT /W3 /LD "/Fe:$build\test_plugin.dll" "$build\test_plugin.c" /link /DLL user32.lib gdi32.lib 2>&1 | ForEach-Object { Write-Output $_ }
if ($LASTEXITCODE -eq 0) { Write-Output "GENERATED DLL COMPILE OK -> $build\test_plugin.dll" } else { Write-Output "GENERATED DLL COMPILE FAILED" }

# ---- 6. 编译生成的 exe 源码并运行 ----
Write-Output "== compile generated exe source =="
& $cl /nologo /TC /utf-8 /EHsc /MT /W3 "/Fe:$build\test_program.exe" "$build\test_program.c" 2>&1 | ForEach-Object { Write-Output $_ }
if ($LASTEXITCODE -eq 0) {
    Write-Output "== run generated exe =="
    & "$build\test_program.exe"
}
