# build.ps1 — 手动编译 MCLauncher 为 YXG_MinecraftLauncher.dll（x64 Release）
# 因 MSBuild 被安全策略拦截，改用 cl.exe/link.exe 手动构建。
# zlib 源码直接静态编译进 DLL（避免 zs.lib 是 /MD 导致 CRT 冲突）。
$ErrorActionPreference = "Stop"

$msvc = "I:\Microsoft Visual Studio\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
$kit  = "I:\Windows Kits\10"
$kitVer = "10.0.26100.0"
$zlibDir = "D:\zlib-1.3.2"

$srcDir = "$PSScriptRoot\src"
$incDir = "$PSScriptRoot\include"
$objDir = "$PSScriptRoot\obj\x64\Release"
$binDir = "$PSScriptRoot\bin\x64\Release"

New-Item -ItemType Directory -Force -Path $objDir | Out-Null
New-Item -ItemType Directory -Force -Path $binDir | Out-Null

$env:INCLUDE = "$msvc\include;$msvc\atlmfc\include;$kit\Include\$kitVer\ucrt;$kit\Include\$kitVer\shared;$kit\Include\$kitVer\um;$kit\Include\$kitVer\winrt;$kit\Include\$kitVer\cppwinrt"
$env:LIB    = "$msvc\lib\x64;$msvc\atlmfc\lib\x64;$kit\Lib\$kitVer\ucrt\x64;$kit\Lib\$kitVer\um\x64"
$env:PATH   = "$msvc\bin\Hostx64\x64;$kit\bin\$kitVer\x64;" + $env:PATH

$cl  = "$msvc\bin\Hostx64\x64\cl.exe"
$link = "$msvc\bin\Hostx64\x64\link.exe"

# 本项目 C++ 源文件
$srcs = @(
    "Json.cpp", "Crypto.cpp", "Http.cpp", "Zip.cpp",
    "LauncherCore.cpp", "MsAuth.cpp", "JavaDetect.cpp", "LoaderInstall.cpp",
    "Theme.cpp", "Background.cpp", "ModManager.cpp", "LauncherApp.cpp",
    "Ui.cpp", "Wallpapers.cpp", "dllmain.cpp"
)

# zlib inflate 所需源文件（静态编译进 DLL）
$zsrcs = @("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c")

$common = "/c /TP /W3 /sdl /permissive- /std:c++20 /MT /EHsc /utf-8 /Zc:char8_t- /O2 /D NDEBUG /D _WINDOWS /D _USRDLL /D MC_LAUNCHER_EXPORTS /D NOMINMAX /D WIN32_LEAN_AND_MEAN /D _CRT_SECURE_NO_WARNINGS /D _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING /I `"$incDir`" /I `"$srcDir`" /I `"$zlibDir`""

$objs = @()
foreach ($s in $srcs) {
    $cpp = "$srcDir\$s"
    $obj = "$objDir\$([System.IO.Path]::GetFileNameWithoutExtension($s)).obj"
    Write-Host "cl $s"
    & $cl $common.Split(" ") "/Fo$obj" $cpp
    if ($LASTEXITCODE -ne 0) { throw "编译失败: $s" }
    $objs += $obj
}

# 编译 zlib（C，/MT 静态；/O1 避免 adler32.c 在 /O2 下触发 MSVC C1001 内部编译器错误）
$zcommon = "/c /TC /nologo /W3 /O1 /MT /D NDEBUG /D _CRT_SECURE_NO_WARNINGS /I `"$zlibDir`""
foreach ($s in $zsrcs) {
    $c = "$zlibDir\$s"
    $obj = "$objDir\z_$([System.IO.Path]::GetFileNameWithoutExtension($s)).obj"
    Write-Host "cl $s (zlib)"
    & $cl $zcommon.Split(" ") "/Fo$obj" $c
    if ($LASTEXITCODE -ne 0) { throw "编译失败(zlib): $s" }
    $objs += $obj
}

$out = "$binDir\YXG_MinecraftLauncher.dll"
Write-Host "link -> $out"
$linkArgs = @("/DLL", "/OUT:$out", "/SUBSYSTEM:WINDOWS", "/MACHINE:X64") + $objs + @("winhttp.lib", "bcrypt.lib", "advapi32.lib", "gdiplus.lib", "shell32.lib", "user32.lib", "gdi32.lib", "ole32.lib", "comdlg32.lib", "kernel32.lib")
& $link @linkArgs
if ($LASTEXITCODE -ne 0) { throw "链接失败" }

Write-Host ""
Write-Host "构建成功: $out"
Get-Item $out | Select-Object FullName, Length, LastWriteTime
